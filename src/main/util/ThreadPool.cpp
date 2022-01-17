#include <optional>
#include <mutex>
#include "Abort.h"
#include "ThreadPool.h"
#include "util.h"

#define FILE_LINE "ThreadPool:" LINE ": "


namespace dimgel {

	void ThreadPool::TaskGroup::compute() {
		for (auto& t : tasks)  {
			if (owner.stopping) {
				break;
			}
			t->compute();
		}
	}


	void ThreadPool::TaskGroup::merge() {
		for (auto& t : tasks) {
			if (owner.stopping) {
				break;
			}
			t->merge();
		}
	}


	//----------------------------------------------------------------------------------------------------------------------------------------


	ThreadPool::ThreadPool(int numThreads, std::function<void(const char* exceptionMessage)> onTaskException)
		: onTaskException(onTaskException)
	{
		if (numThreads <= 0) {
			int numCPUs = sysconf(_SC_NPROCESSORS_ONLN);
			numThreads = std::max(1, numCPUs - -numThreads);
		}
		numBusyThreads = numThreads;
		threads.reserve(numThreads);
		while (--numThreads >= 0) {
			threads.push_back(std::thread(&threadFunction, this));
		}
	}


	void ThreadPool::threadFunction(ThreadPool* self) {
		self->threadMethod();
	}


	void ThreadPool::threadMethod() {
		std::unique_ptr<Task> currentTask;
		bool computeThrew = false;

		while (true) {
			{
				std::unique_lock l(m);

				// Finish previous task under lock.
				if (computeThrew) {
					computeThrew = false;
				} else if (currentTask) {
					try {
						// Calling merge() only if compute() didn't throw, i.e. if currentTaskException_opt was empty.
						currentTask->merge();
					} catch (Abort& e) {
						processTaskException("", true);
					} catch (std::exception& e) {
						processTaskException(e.what(), false);
					}
				}
				currentTask.reset();
				numBusyThreads--;

				// Get new task, stop, or wait.
				while (true) {
					if (!queue.empty()) {
						numBusyThreads++;
						currentTask = std::move(queue.front());
						queue.pop();
						break;
					}
					if (state != State::ACTIVE) {
						if (numBusyThreads == 0) {
							cvMain.notify_one();
						}
						if (state == State::DESTRUCTING) {
							return;
						}
					}
					cvWorker.wait(l);
				}
			}

			// Execute task.
			// We are outside mutex now, so store exception for future processing.
			try {
				currentTask->compute();
			} catch (Abort& e) {
				computeThrew = true;
				processTaskException(nullptr, true);
			} catch (std::exception& e) {
				computeThrew = true;
				processTaskException(e.what(), false);
			}
		}
	}


	void ThreadPool::processTaskException(const char* exceptionMessage, bool isAbort) {
		stopping = true;
		waitAllResult = false;
		if (!isAbort) {
			try {
				onTaskException(exceptionMessage);
			} catch (...) {
				// Nothing we can do.
			}
		}
	}


	std::vector<std::unique_ptr<ThreadPool::Task>> ThreadPool::groupTasks(std::vector<std::unique_ptr<Task>> tasks, int numTasksPerGroup) const {
		if (numTasksPerGroup == 0) {
			// Just to be explicit.
			throw std::runtime_error(FILE_LINE "groupTasks(): numTasksPerGroup == 0");
		}

		std::vector<std::unique_ptr<ThreadPool::Task>> result;
		int numTasks = (int)tasks.size();
		if (numTasks == 0) {
			return result;
		}

		int numGroups = (numTasksPerGroup > 0)
				// numTasks > 0, numTasksPerGroup > 0 ---> numGroups > 0, numGroups * numTasksPerGroup <= numTasks.
				? (numTasks + numTasksPerGroup - 1) / numTasksPerGroup
				: getNumThreads() * -numTasksPerGroup;
		// Forget about old numTasksPerGroup value, it's already used. Split evenly, 0 <= remainder < numGroups.
		numTasksPerGroup = numTasks / numGroups;
		int remainder = numTasks % numGroups;

//		cout << "ThreadPool::groupTasks(): numTasks=" << numTasks
//			 << ", numGroups=" << numGroups
//			 << ", numTasksPerGroup=" << numTasksPerGroup
//			 << ", remainder=" << remainder << endl;

		result.reserve(numGroups);
		int begin = 0;
		for (int i = 0;  i < numGroups;  i++) {
			int end = begin + numTasksPerGroup;
			if (remainder-- > 0) {
				end++;
			}
			if (end > numTasks) {
				throw std::runtime_error(FILE_LINE "groupTasks(): end > numTasks");
			}

			// Cannot just construct vector from another vector's range because std::unique_ptr must be std::move()-d.
			std::vector<std::unique_ptr<Task>> groupTasks;
			groupTasks.reserve(end - begin);
			for (int j = begin;  j < end;  j++) {
				groupTasks.push_back(std::move(tasks[j]));
			}
			result.push_back(std::make_unique<TaskGroup>(*this, std::move(groupTasks)));

			begin = end;
		}

		return result;
	}


	void ThreadPool::addTasks(std::vector<std::unique_ptr<Task>> tasks) {
		if (tasks.empty()) {
			return;
		}
		bool notifyOne;
		{
			std::unique_lock l(m);
			if (state != State::ACTIVE) {
				throw std::runtime_error(FILE_LINE "addTasks(): state != ACTIVE");
			}
			for (auto& t : tasks) {
				queue.push(std::move(t));
			}
			notifyOne = queue.size() == 1;
		}
		// "the lock does not need to be held for notification" (c) https://en.cppreference.com/w/cpp/thread/condition_variable
		if (notifyOne) {
			cvWorker.notify_one();
		} else {
			cvWorker.notify_all();
		}
	}


	void ThreadPool::waitAll_impl(State newState) {
		std::unique_lock l(m);
		if (state != State::ACTIVE) {
			throw std::runtime_error(FILE_LINE "waitAll_impl(): state != ACTIVE");
		}
		state = newState;
		bool mustNotify = state == State::DESTRUCTING;
		while (true) {
			// RACE:
			// - All threads complete their trivial tasks and enter cvWorker.wait() before I get here.
			// - Now (queue.empty() && numBusyThreads == 0) but all threads are sleeping.
			// - If we're in destructor, join() will hang forever.
			//
			// Same RACE might happen if I call waitAll() right before destruction:
			// - First waitAll_impl() call ensures all tasks are done, then the second waitAll_impl() is called by destructor.
			// - Now (queue.empty() && numBusyThreads == 0) but all threads are sleeping.
			//
			// So in destructor, I must wake up them all BEFORE checking if (queue.empty() && numBusyThreads == 0) and returning.
			// It's enough to do it only once after state := DESTRUCTING, not on each iteration of while() loop; thus `mustNotify` flag.
			//
			// But I don't need to wake up threads if state := WAITING, because no action is required from them:
			// they may sleep until addTasks() or waitAll_imp(DESTRUCTING) awake them.
			// Conceptually, I must awake anyone only when some action is requred from them -- be that new task to execute or termination request.
			if (mustNotify) {
				mustNotify = false;
				cvWorker.notify_all();
			}

			if (queue.empty() && numBusyThreads == 0) {
				break;
			}
			cvMain.wait(l);
		}
		if (state != State::DESTRUCTING) {
			state = State::ACTIVE;
		}
	}

	void ThreadPool::waitAll() {
		waitAll_impl(State::WAITING);
		bool x = waitAllResult;
		waitAllResult = true;
		stopping = false;
		if (!x) {
			throw Abort();
		}
	}


	ThreadPool::~ThreadPool() {
		waitAll_impl(State::DESTRUCTING);
		for (auto& t : threads) {
			t.join();
		}
	}
}
