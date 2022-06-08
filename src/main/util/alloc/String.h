#pragma once

#include <regex>
#include <string.h>
#include <unordered_map>
#include <unordered_set>
#include "../StringRef.h"


namespace dimgel::alloc {
	class MemoryManager;


	// Trying to enforce non-orphan string views via type system: constructors allocate memory.
	// Null-terminated (allocates 1 more byte than source.length()).
	//
	// Because String constructor requires mm argument, {map<String,T>|set<String>}.{find(x)|contains(x)}
	// won't create temporary String(x) and will automatically use heterogeneous overloads instead.
	//
	class String final {
		StringRef x;

		explicit String(std::string_view x) : x{StringRef::createUnsafe(x)} {}
		explicit String(StringRef x) : x{x} {}

	public:
		// Empty, but still null-terminated! :)
		String() : x("") {}

		explicit String(alloc::MemoryManager& mm, std::string_view source);
		explicit String(alloc::MemoryManager& mm, StringRef source) : String{mm, source.sv()} {}

		// Pointers must not be null.
		explicit String(alloc::MemoryManager& mm, const char* source) : String{mm, std::string_view(source)} {}
		explicit String(alloc::MemoryManager& mm, const char* source, size_t count) : String{mm, std::string_view(source, count)} {}
		explicit String(alloc::MemoryManager& mm, const char* first, const char* last) : String{mm, std::string_view(first, last)} {}

		explicit String(MemoryManager& mm, std::initializer_list<std::string_view> sources);

		String(const String&) = default;
		String(String&&) = default;
		String& operator =(const String&) = default;
		String& operator =(String&&) = default;
		~String() = default;


		const char& operator[](size_t pos) const { return x[pos]; }

		auto empty() const noexcept { return x.empty(); }
		auto length() const noexcept { return x.length(); }
		auto size() const noexcept { return x.size(); }
		bool starts_with(char c) const noexcept { return x.starts_with(c); }
		String substr(size_t pos) const { return String{x.substr(pos)}; }
		std::string_view substr(size_t pos, size_t count) const { return x.substr(pos, count); }


		auto sr() const noexcept { return x; }
		auto sv() const noexcept { return x.sv(); }
		auto cp() const noexcept { return x.cp(); }
		auto s() const { return x.s(); }
		operator StringRef() const noexcept { return x; }
		explicit operator const char*() const noexcept { return x.cp(); }
	};
}


namespace std {
	// https://en.cppreference.com/w/cpp/utility/hash
	// https://www.cppstories.com/2021/heterogeneous-access-cpp20/
	template<> struct hash<dimgel::alloc::String> {
		using is_transparent = void;

		// Produces hash different from others! I'll better uniformly convert all types to string_view...
//		size_t operator() (const char* x) const { return std::hash<const char*>{}(x); }

		size_t operator() (const char* x) const { return std::hash<std::string_view>{}(x); }
		size_t operator() (const std::string& x) const { return std::hash<std::string>{}(x); }
		size_t operator() (std::string_view x) const { return std::hash<std::string_view>{}(x); }
		size_t operator() (dimgel::StringRef x) const { return std::hash<std::string_view>{}(x.sv()); }
		size_t operator() (dimgel::alloc::String x) const { return std::hash<std::string_view>{}(x.sv()); }
	};
}


namespace dimgel::alloc {
	inline bool operator < (const String& a, const String& b) { return a.sv() < b.sv(); }
	inline bool operator ==(const String& a, const String& b) { return a.sv() == b.sv(); }

	inline bool operator ==(const String& a, const char* b) { return strcmp(a.cp(), b) == 0; }
	inline bool operator ==(const char* b, const String& a) { return strcmp(a.cp(), b) == 0; }
	inline bool operator ==(const String& a, const std::string_view& b) { return a.sv() == b; }
	inline bool operator ==(const std::string_view& b, const String& a) { return a.sv() == b; }
	inline bool operator ==(const String& a, const StringRef& b) { return a.sv() == b.sv(); }
	inline bool operator ==(const StringRef& b, const String& a) { return a.sv() == b.sv(); }
	inline bool operator ==(const String& a, const std::csub_match& b) { return a.sv() == std::string_view(b.first,  b.second); }
	inline bool operator ==(const std::csub_match& b, const String& a) { return a.sv() == std::string_view(b.first,  b.second); }

	inline std::ostream& operator <<(std::ostream& os, const String& x) { return os << x.sv(); }


	using StringHashSet = std::unordered_set<String, std::hash<String>, std::equal_to<>>;
	template<class V> using StringHashMap = std::unordered_map<String, V, std::hash<String>, std::equal_to<>>;
}
