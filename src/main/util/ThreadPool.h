#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>


namespace dimgel {

	// API is not thread-safe. I assume that all API methods are called from single ("main") thread.
	class ThreadPool final {
	public:

		// ATTENTION!!!
		// If some Task throws, no reason to continue other tasks: all remaining tasks in the same TaskGroup won't be computed and/or merged anyway.
		// So, ThreadPool::stopping flag will be set which is checked by TaskGroup between calling tasks.
		// That flag is reset by waitAll() right before return.
		class Task {
		public:
			Task() = default;
			Task(const Task&) = delete;
			Task& operator =(const Task&) = delete;

			virtual ~Task() = default;

			// Called first, in parallel.
			virtual void compute() {};

			// Called second, under ThreadPool's mutex, so only one merge() is executing at any moment.
			// If main thread does not interfere beween addTasks() and waitAll() / ~ThreadPool() calls, there'll be no races.
			virtual void merge() {};
		};


		// ATTENTION!!!
		// First all compute() are called, then all merge(), then group and all its tasks are deleted. Mind memory usage of large groups.
		class TaskGroup final : public Task {
			const ThreadPool& owner;
			std::vector<std::unique_ptr<Task>> tasks;
		public:
			TaskGroup(const ThreadPool& owner, std::vector<std::unique_ptr<Task>> tasks) : owner(owner), tasks(std::move(tasks)) {}
			void compute() override;
			void merge() override;
		};


	private:
		enum class State {
			ACTIVE,
			WAITING,
			DESTRUCTING
		};

		std::function<void(const char* exceptionMessage)> onTaskException;
		State state = State::ACTIVE;
		int numBusyThreads;
		std::vector<std::thread> threads;
		// TODO Specify std:queue's underlying collection?
		std::queue<std::unique_ptr<Task>> queue;
		std::atomic_bool waitAllResult = true;
		std::atomic_bool stopping = false;

		// Waking up threads: https://stackoverflow.com/a/32234772
		// For worker threads to wake up when new tasks are available or destructor is called.
		std::condition_variable cvWorker;
		// For main thread's waitAll() to signal when all tasks are complete.
		std::condition_variable cvMain;
		// Both cv-s share same mutex: https://stackoverflow.com/q/4062126
		std::mutex m;

		static void threadFunction(ThreadPool* self);
		void threadMethod();
		int getNumThreads() const noexcept { return threads.size(); }
		void processTaskException(const char* exceptionMessage, bool isAbort);
		void waitAll_impl(State newState);

	public:
		ThreadPool(const ThreadPool&) = delete;
		ThreadPool(ThreadPool&&) = delete;
		ThreadPool& operator =(const ThreadPool&) = delete;
		ThreadPool& operator =(ThreadPool&&) = delete;

		// If numThreads <= 0, its how many CPUs to spare. E.g. #CPUs=16, numThreads=-2 ---> #threads=14.
		// But at least one thread is always created.      E.g. #CPUs= 2, numThreads=-3 ---> #threads= 1.
		//
		// Callback onTaskException is called by worker thread ASYNCHRONOUSLY, for all exceptions except Abort.
		//
		// ATTENTION: If you use groupTasks() which you should, then after some task throws exception all remaining tasks in group don't get executed.
		ThreadPool(int numWorkerThreads, std::function<void(const char* exceptionMessage)> onTaskException);

		// This is to avoid abusing mutex too much if tasks are small and many. Returns TaskGroup-s.
		// Parameter `tasks` must be passed with std::move(), because std::unique_ptr() cannot be copied.
		//
		// If numTasksPerGroup > 0, it's upper bound. Some groups may contain fewer tasks, to evenly split the remainder.
		// E.g. numTasks = 101, numTasksPerGroup = 30 ---> numGroups = 4, numTasksPerGroup = 25, remainder = 1 ---> group sizes are 26 (1 group) and 25 (3 groups).
		//
		// If numTasksPerGroup < 0, then tasks are split into getNumThreads() * (-numTasksPerGroup) groups of equal size.
		// Some groups may contain more tasks computed size -- to evenly split the remainder.
		std::vector<std::unique_ptr<Task>> groupTasks(std::vector<std::unique_ptr<Task>> tasks, int numTasksPerGroup = -1) const;

		// Parameter `tasks` must be passed with std::move(), because std::unique_ptr() cannot be copied.
		void addTasks(std::vector<std::unique_ptr<Task>> tasks);

		// Waits until ALL tasks (active and queued) are completed.
		// Throws Abort if some task threw exceptions since previous waitAll();
		// at that moment those exceptions are already processed by onTaskException callback.
		void waitAll();

		// Waits until ALL tasks (active and queued) are completed.
		~ThreadPool();
	};
}
