#pragma once

#include <atomic>
#include "../Spinlock.h"
#include "alloc.h"
#include "MemoryManager.h"


namespace dimgel::alloc {

	class Arena : public MemoryManager {
		struct PageHeader {
			// Previous filled up page if we're in curPage chain, or next free page if we're in freePage chain.
			PageHeader* nextPage;
			// Cannot atomically update both Arena::currentPage and Arena::offset, race is possible:
			// other thread can allocate small chunk on old page and update offset (without acquiring spinlock)
			// while I'm allocating new page and resetting offset for it. Thus, moving offset inside page:
			std::atomic<size_t> offset;
		};

		static constexpr size_t pageHeaderSize = alignUp(sizeof(PageHeader), 8);

		size_t pageSizeWithHeader;
		Spinlock spinlock;
		std::atomic<PageHeader*> curPage {nullptr};
		PageHeader* freePage = nullptr;
		// For debug & statistics:
		int numCurPages = 0;
		int numFreePages = 0;

		// Otherwise what's the point:
		static_assert(std::atomic<PageHeader*>::is_always_lock_free);
		static_assert(std::atomic<size_t>::is_always_lock_free);

		Arena(const Arena&) = delete;
		Arena& operator =(const Arena&) = delete;
		void newPage();

	public:
		Arena(size_t pageSize);
		~Arena();
		void* allocate(size_t size, size_t align) override;
		void free(void* /*ptr*/, size_t /*size*/) override {}
		// Does not ::free(), instead resets this->offset and moves curPage->prevPage to freePage:
		void reset() override;

		size_t debugGetNumPages() const noexcept { return numCurPages; }
		size_t debugGetOffset() const noexcept { return curPage.load()->offset; }
		void debugOutputStats(std::ostream& os, const char* instanceName) const;
		void debugOutputStats(Log& m, const char* instanceName) const override;
	};
}
