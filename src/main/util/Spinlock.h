#pragma once

#include <pthread.h>


namespace dimgel {

	// Use RAII like this: std::lock_guard<Spinlock> guard(lock);
	class Spinlock final {
		pthread_spinlock_t f;

	public:
		Spinlock(const Spinlock&) = delete;
		Spinlock(Spinlock&&) = delete;
		Spinlock& operator =(const Spinlock&) = delete;
		Spinlock& operator =(Spinlock&&) = delete;

		inline Spinlock() noexcept { pthread_spin_init(&f, PTHREAD_PROCESS_PRIVATE); }
		inline ~Spinlock() noexcept { pthread_spin_destroy(&f); }

		// As of glibc-2.24, pthread_spin_lock() always returns 0.
		inline void lock() noexcept { pthread_spin_lock(&f); }
		inline void unlock() noexcept { pthread_spin_unlock(&f); }
	};
}
