#pragma once

#include <algorithm>
#include <unistd.h>


namespace dimgel {

	class Closeable {
		int fd;
	public:
		Closeable(int fd = -1) : fd(fd) {}

		Closeable(const Closeable&) = delete;
		Closeable(Closeable&& c) {
			*this = std::move(c);
		}
		Closeable& operator =(const Closeable&) = delete;
		Closeable& operator =(Closeable&& c) {
			// swap() is essential, see StdCapture move constructor & move assignment.
			std::swap(fd, c.fd);
			return *this;
		}

		operator int() const noexcept { return fd; }

		void close() {
			if (fd >= 0) {
				::close(fd);
				fd = -1;
			}
		}

		~Closeable() {
			close();
		}
	};
}
