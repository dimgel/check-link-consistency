#pragma once

#include <string>


namespace dimgel {

	// Wrapper for std::string_view that guarantees view is null-terminated, i.e. `cout << view` would produce same result as `cout << view.data()`.
	// ATTENTION! Don't use std::string_view::data() anywhere unless you know what you're doing.
	class StringRef {
		std::string_view x;

		constexpr StringRef(std::string_view x) : x(x) {}

	public:
		static constexpr decltype(std::string_view::npos) npos {std::string_view::npos};

		// Caller must ensure he does the right thing.
		static StringRef createUnsafe(std::string_view x)            { return StringRef(x); }
		static StringRef createUnsafe(const char* cp, size_t length) { return StringRef({cp, length}); }

		constexpr StringRef(const char* s = "") : x(s) {}


		constexpr const char& operator[](size_t pos) const { return x[pos]; }

		constexpr auto empty() const noexcept { return x.empty(); }
		constexpr bool ends_with(char c) const noexcept { return x.ends_with(c); }
		constexpr size_t find(char c, size_t pos = 0) const noexcept { return x.find(c, pos); }
		constexpr size_t length() const noexcept { return x.length(); }
		constexpr size_t rfind(char c, size_t pos = npos) const { return x.rfind(c, pos); }
		constexpr size_t size() const noexcept { return x.size(); }
		constexpr bool starts_with(char c) const noexcept { return x.starts_with(c); }
		constexpr bool starts_with(const char* s) const { return x.starts_with(s); }
		constexpr bool starts_with(std::string_view s) const noexcept { return x.starts_with(s); }
		constexpr StringRef substr(size_t pos) const { return StringRef{x.substr(pos)}; }
		constexpr std::string_view substr(size_t pos, size_t count) const { return x.substr(pos, count); }

		// No remove_suffix() method because it violates class purpose.
		void remove_prefix(size_t n) {
			// Weird that std::string::remove_prefix() does not check n > length() and thus wraps around 0.
			x.remove_prefix(std::min(n, x.length()));
		}


		auto s() const { return std::string(x); }
		const std::string_view sv() const noexcept { return x; }
		const char* cp() const noexcept { return x.data(); }

//		operator const std::string_view() const noexcept { return x; }
		explicit operator const char*() const noexcept { return cp(); }


		// Type hint for cases when StringRef must point to mutable buffer.
		struct Mutable;
	};


	struct StringRef::Mutable {
		StringRef sr;
	};


	inline bool operator ==(const StringRef& a, const std::string& b) { return a.sv() == std::string_view(b); }
	inline bool operator ==(const std::string& b, const StringRef& a) { return a.sv() == std::string_view(b); }
}
