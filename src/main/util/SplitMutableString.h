#pragma once

#include <type_traits>
#include "StringRef.h"


namespace dimgel {

	// Usage: for (auto& line : SplitMutableString(file)) { ... }, or begin()/end() if you need getPartNo().
	//
	// ATTENTION!!! Mutates source string: replaces delimiter chars with '\0'.
	// Empty parts count if they end with delimiter. That is, both "111\n222" and "111\n222\n" have 2 lines, "" has no lines.
	// If mergeDelims == true, only non-empty parts will be returned. Otherwise: e.g. "\n\n\n" will return 3 lines.
	//
	// https://internalpointers.com/post/writing-custom-iterators-modern-cpp
	// TODO Should learn how to rewrite this using C++20 concepts. Or maybe should not.
	// NOTE Could use callback instead of iterator (would be less boilerplate), but will keep iterator as working example.
	class SplitMutableString final {
		std::string_view s;   // Mutable!
		std::string delims;
		bool mergeDelims;

	public:
		SplitMutableString(StringRef::Mutable s, std::string delims = "\n", bool mergeDelims = false)
			: s(s.sr.sv()), delims(delims), mergeDelims(mergeDelims) {}
		SplitMutableString(std::string& s, std::string delims = "\n", bool mergeDelims = false)
			: s(s), delims(delims), mergeDelims(mergeDelims) {}

		SplitMutableString(const SplitMutableString&) = delete;
		SplitMutableString(SplitMutableString&&) = default;
		SplitMutableString& operator =(const SplitMutableString&) = delete;
		SplitMutableString& operator =(SplitMutableString&&) = default;
		~SplitMutableString() = default;


		class ConstIterator {
			// Pointer instead of value or reference, to enable default copy constructor and copy assignment.
			const SplitMutableString* owner;
			int partNo;
			StringRef v;

			void update(size_t begin);

		public:
			ConstIterator(const SplitMutableString* owner, int partNo, size_t begin) : owner(owner), partNo(partNo) {
				update(begin);
			}

			const SplitMutableString& getOwner() const noexcept {
				return *owner;
			}

			// 1-based. If delims == "\n" and mergeDelims == false (both default), then this is lineNo.
			// SplitMutableString::end() return 0, but when non-end() iterator hits end() it returns correct partNo past last part.
			int getPartNo() const noexcept {
				return partNo;
			}


			// Read-only, scan input only once.
			using iterator_category = std::input_iterator_tag;
			// Does not matter for input iterator: https://stackoverflow.com/a/46695460
			using difference_type = std::ptrdiff_t;
			using value_type = const StringRef;
			using pointer = value_type*;
			using reference = value_type&;

			reference operator*() const { return v; }   // Can be mutable.
			pointer operator->() const { return &v; }   // Can be mutable.


			// Prefix increment.
			ConstIterator& operator++();

			// Postfix increment.
			// TODO Copying ConstIterator is less efficient than copying std::string_view.
			//      Also, range-based for() does not require it: https://stackoverflow.com/a/16259612
			//      Should I just comment it out and rewrite its usages a little more verbose but penny-wise?
			ConstIterator operator++(int) {
				ConstIterator tmp = *this;
				++(*this);
				return tmp;
			}


			bool operator ==(const ConstIterator& x) const {
				// Comparing pointers.
				// UPD: Unnecessary; it's enough to compare v.cp() pointers.
//				static_assert(std::is_pointer_v<decltype(s)>);
//				if (x.s != s) {
//					return false;
//				}
				// std::string_view equals by value, but I don't loop to break on the first empty line because it's equal to end().
				// So, comparing pointers too.
				return x.v.cp() == v.cp() && x.v.length() == v.length();
				// NOT comparing partNo: end() always has 0.
			}

			bool operator !=(const ConstIterator& x) const {
				return !(*this == x);
			}
		};


		ConstIterator begin() const { return ConstIterator(this, 1, 0); }
		ConstIterator end()   const { return ConstIterator(this, 0, s.size()); }
	};
}
