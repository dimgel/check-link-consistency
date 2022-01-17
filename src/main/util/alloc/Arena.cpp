#include "../Log.h"
#include "../util.h"
#include "Arena.h"

#define FILE_LINE "Arena:" LINE ": "


namespace dimgel::alloc {

	Arena::Arena(size_t pageSize) {
		if (pageSize == 0) {
			throw std::runtime_error(FILE_LINE "Arena(): pageSize == 0");
		}
		this->pageSizeWithHeader = pageHeaderSize + pageSize;
		newPage();
	}


	Arena::~Arena() {
		auto freeChain = [&](PageHeader* h) {
			while (h != nullptr) {
				auto x = h;
				h = h->nextPage;
				::free(x);
			}
		};
		freeChain(curPage);
		freeChain(freePage);
	}


	void Arena::newPage() {
		PageHeader* x;
		if (freePage != nullptr) {
			x = freePage;
			freePage = freePage->nextPage;
			numFreePages--;
		} else {
			x = reinterpret_cast<PageHeader*>(::malloc(pageSizeWithHeader));
			if (x == nullptr) {
				throw std::runtime_error(FILE_LINE "newPage(): malloc() failed");
			}
			static_assert(sizeof(x) == 8);
			if ((reinterpret_cast<uint64_t>(x) % 8) != 0) {
				::free(x);
				// Sanity check.
				throw std::runtime_error(FILE_LINE "newPage(): malloc() returned unaligned memory");
			}
		}
		numCurPages++;
		x->nextPage = curPage;
		x->offset.store(pageHeaderSize);
		curPage.store(x);
	}


	void* Arena::allocate(size_t size, size_t align) {
		align = alignAdjust(size, align);
		auto* cp = curPage.load();
		size_t old = cp->offset.load(), aligned, next;
		while (true) {
			aligned = alignUp(old, align);
			next = aligned + size;
			if (next > pageSizeWithHeader) {
				if (pageHeaderSize + size > pageSizeWithHeader) {
					throw std::runtime_error(FILE_LINE "allocate(): size is too large");
				}
				{
					// do-while(compare-exchange) is no good here: we're allocating, should not try-allocate in loop.
					// See also comments at PageHeader::offset declaration.
					std::lock_guard g(spinlock);
					if (curPage.load() == cp) {
						newPage();
					}
				}
				cp = curPage.load();
				old = cp->offset.load();
				continue;
			}
			// Re-initializes `old` if fails.
			if (std::atomic_compare_exchange_strong(&cp->offset, &old, next)) {
				// `break` instead of do-while -- because `continue` above would execute condition in do-while.
				break;
			}
		}
		return reinterpret_cast<char*>(cp) + aligned;
	}


	// TODO Unit-test.
	void Arena::reset() {
		PageHeader* c = curPage;
		PageHeader* f = freePage;
		// We leave one allocated curPage, so check `c->nextPage` instead of `c`.
		while (c->nextPage != nullptr) {
			// Move `c` from curPage chain to freePage chain.
			auto c1 = c->nextPage;
			c->nextPage = f;
			f = c;
			c = c1;
			numCurPages--;
			numFreePages++;
		}
		curPage = c;
		freePage = f;
	}


	void Arena::debugOutputStats(Log& m, const char* instanceName) const {
		size_t pageSizeB = (pageSizeWithHeader - pageHeaderSize);
		size_t pageSizeK = pageSizeB / 1024;
		size_t lastB = (curPage.load()->offset - pageHeaderSize);
		size_t lastK = (lastB + 1023) / 1024;
		size_t totalB = lastB + (pageSizeWithHeader - pageHeaderSize) * (numCurPages - 1);
		size_t totalK = (totalB + 1023) / 1024;
		m.debug(
			FILE_LINE "stats: Arena(%s): pageSize %lu B (>=%lu KiB), numCurPages %d, numFreePages %d, used %lu B (<=%lu KiB) on last page / %lu B (<=%lu KiB) total",
			ConstCharPtr{instanceName}, ulong{pageSizeB}, ulong{pageSizeK}, int{numCurPages}, int{numFreePages}, ulong{lastB}, ulong{lastK}, ulong{totalB}, ulong{totalK}
		);
	}
}
