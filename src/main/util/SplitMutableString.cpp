#include "SplitMutableString.h"


namespace dimgel {

	void SplitMutableString::ConstIterator::update(size_t begin) {
		if (owner->mergeDelims) {
			begin = owner->s.find_first_not_of(owner->delims, begin);
		}
		static_assert(std::string::npos > 0);
		size_t len = owner->s.length();
		if (begin > len) {
			// We can get here either by s.find_first_not_of() returning `npos` above,
			// or by operator++() called already at the end of string, so position after '\0' is (len + 1).
			begin = len;
		}

		size_t end = owner->s.find_first_of(owner->delims, begin);
		if (end == std::string::npos) {
			end = len;
		} else {
			const_cast<char*>(owner->s.data())[end] = '\0';
		}
		v = StringRef::createUnsafe(owner->s.data() + begin, end - begin);
	}


	SplitMutableString::ConstIterator& SplitMutableString::ConstIterator::operator++() {
		size_t len = owner->s.length();
		size_t begin = v.cp() - owner->s.data();
		if (begin == len) {
			// Iterator is already at end().
			return *this;
		}

		// Even if we hit end() now, increment partNo so it denotes after-last part.
		partNo++;

		// Next char after '\0' which is after current string_view's end.
		update(begin + v.length() + 1);
		return *this;
	}
}
