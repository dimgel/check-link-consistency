#pragma once

#include <algorithm>
#include <unistd.h>


namespace dimgel {

	class Closeable {
		int fd;

	public:
		Closeable(int fd = -1) noexcept : fd(fd) {}

		Closeable(const Closeable&) = delete;
		Closeable(Closeable&& c) noexcept {
			fd = c.fd;
			c.fd = -1;
		}
		Closeable& operator =(const Closeable&) = delete;
		Closeable& operator =(Closeable&& c) noexcept {
			// swap() is essential!
			std::swap(fd, c.fd);
			return *this;
		}

		operator int() const noexcept { return fd; }
		bool isOpen() const noexcept { return fd != -1; }

		void close() noexcept {
			if (fd >= 0) {
				::close(fd);
				fd = -1;
			}
		}

		~Closeable() noexcept {
			close();
		}
	};
}
