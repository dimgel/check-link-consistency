#pragma once

#include <memory>
#include <optional>
#include <sstream>
#include "Closeable.h"


namespace dimgel {

	// https://stackoverflow.com/a/5419409
	// https://www.linux.org.ru/forum/development/16662026?cid=16662086
	// Redone for RAII. Instantiate it once, and call get...() periodically to collect stdout available so far.
	class StdCapture final {
		enum class State {
			NotInited,
			InitedNormal,
			InitedParentProcess,
			InitedChildProcess
		};

		FILE* f;
		int fd;
		Closeable read;
		Closeable write;
		Closeable oldWrite;
		State state = State::NotInited;

		StdCapture(FILE* f, int fd, bool forForkExec);
		void initStdWrite();

	public:
		static StdCapture createStdOut(bool forForkExec = false) { return StdCapture(stdout, STDOUT_FILENO, forForkExec); }
		static StdCapture createStdErr(bool forForkExec = false) { return StdCapture(stderr, STDERR_FILENO, forForkExec); }

		StdCapture(const StdCapture&) = delete;
		StdCapture(StdCapture&& o) {
			f = nullptr;
			fd = -1;
			*this = std::move(o);
		}
		StdCapture& operator =(const StdCapture&) = delete;
		StdCapture& operator =(StdCapture&& o);
		~StdCapture();

		// Support for concurrent util::forkExecStdCapture() calls, along with forForkExec constructor argument.
		void initParentProcess();
		void initChildProcess();

		// Get content captured so far.
		void get(std::ostream& os);

		std::string get() {
			std::ostringstream os;
			get(os);
			return os.str();
		}

		// To wait on.
		int getReadFD() const noexcept { return read; }
	};
}
