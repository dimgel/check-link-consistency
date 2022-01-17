#include <fcntl.h>
#include <sstream>
#include <string.h>
#include "Error.h"
#include "StdCapture.h"
#include "util.h"

#define FILE_LINE "StdCapture:" LINE ": "


namespace dimgel {

	StdCapture::StdCapture(FILE* f, int fileno, bool forForkExec) : f(f), fileno(fileno) {
		// `man 7 pipe`: pipe capacity is 65536 bytes. Pretty enough for me.
		int pipeFDs[2];
		if (::pipe(pipeFDs) == -1) {
			throw Error(FILE_LINE "pipe() failed: %s", ConstCharPtr{strerror(errno)});
		}
		read = pipeFDs[0];
		write = pipeFDs[1];

		// https://stackoverflow.com/a/1549344/4247442
		int flags = fcntl(read, F_GETFL, 0);
		if (flags == -1) {
			throw Error(FILE_LINE "fcntl(F_GETFL) failed: %s", ConstCharPtr{strerror(errno)});
		}
		flags |= O_NONBLOCK;
		if (fcntl(read, F_SETFL, flags) == -1) {
			throw Error(FILE_LINE "fcntl(F_SETFL) failed: %s", ConstCharPtr{strerror(errno)});
		}

		if (!forForkExec) {
			initStdWrite();
			state = State::InitedNormal;
		}
	}


	// "=default" implementation does not work correctly (unit tests throw "bad file descriptor"), I need to strictly swap() all fields.
	StdCapture& StdCapture::operator =(StdCapture&& o) {
		std::swap(f, o.f);
		std::swap(fileno, o.fileno);
		read = std::move(o.read);
		write = std::move(o.write);
		oldWrite = std::move(o.oldWrite);
		std::swap(state, o.state);
		return *this;
	}


	StdCapture::~StdCapture() {
		// TODO Do I need to read fileno out before destroying?
		if (oldWrite != -1) {
			dup2(oldWrite, fileno);
		}
	}


	void StdCapture::initStdWrite() {
		fflush(f);

		if ((oldWrite = dup(fileno)) == -1) {
			throw Error(FILE_LINE "dup() failed: %s", ConstCharPtr{strerror(errno)});
		}
		if (dup2(write, fileno) == -1) {
			throw Error(FILE_LINE "dup2() failed: %s", ConstCharPtr{strerror(errno)});
		}
		write.close();
	}


	void StdCapture::initParentProcess() {
		if (state != State::NotInited) {
			throw std::runtime_error(FILE_LINE "already inited");
		}
		write.close();
		state = State::InitedParentProcess;
	}


	void StdCapture::initChildProcess() {
		if (state != State::NotInited) {
			throw std::runtime_error(FILE_LINE "already inited");
		}
		initStdWrite();
		read.close();
		state = State::InitedChildProcess;
	}


	void StdCapture::get(std::ostream& os) {
		if (state != State::InitedNormal && state != State::InitedParentProcess) {
			throw std::runtime_error(FILE_LINE "not inited");
		}

		fflush(f);

		constexpr int bufSize = 4096;
		char buf[bufSize];
		ssize_t n;

		while ((n = ::read(this->read, buf, bufSize)) != 0) {
			if (n == -1) {
				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					// This may come at first loop iteration if there's no input at all.
					break;
				}
				throw Error(FILE_LINE "read() failed: %s", ConstCharPtr{strerror(errno)});
			}
			os << std::string_view(buf, n);
			if (n < bufSize) {
				// I believe we don't need to re-invoke read() to get EAGAIN | EWOULDBLOCK.
				break;
			}
		}
	}
}
