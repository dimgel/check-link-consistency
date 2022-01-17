#pragma once

#include <functional>
#include "StringRef.h"


namespace dimgel {

	// The only reasonable alternative I found is libinih, and it's already installed, but it's pulled in by GUI stuff:
	//     $ p -Qo /usr/include/INIReader.h
	//     $ pactree -rd5 libinih
	// My code is shorter anyway.
	class IniParser final {
	public:
		class Line final {
		public:
			class Value final {
				// Both lineNo and key are inside Value class so its method can use them to produce nice error messages, without referencing `Line& owner`.
				friend class IniParser;
				friend class Line;
				StringRef k;
				StringRef v;
				int l;
				Value(StringRef key, StringRef value, int lineNo) : k(key), v(value), l(lineNo) {}

			public:
				std::string s() const { return v.s(); }
				std::string_view sv() const noexcept { return v.sv(); }
				StringRef sr() const noexcept { return v; }
				StringRef::Mutable srMutable() const noexcept { return StringRef::Mutable{v}; }
				const char* cp() const noexcept { return v.cp(); }
				operator StringRef() const noexcept { return v; }
				explicit operator const char*() const noexcept { return v.cp(); }

				int64_t asInt64() const;
				bool asBool() const;
			};

		private:
			friend class IniParser;
			Value v;
			Line(StringRef key, StringRef value, int lineNo) : v(key, value, lineNo) {}

		public:
			// Since both Line::key() and (StringRef)Line::value() point inside mutable buffer passed to execute(), they are can be const_cast<> to mutable (char*) too.
			StringRef key() const noexcept { return v.k; }
			const Value& value() const noexcept { return v; }
			int lineNo() const noexcept { return v.l; }
		};

		// If `onLine` returns false, exception "unknown key" is thrown.
		static void execute(StringRef::Mutable contents, std::function<bool(const Line& l)> onLine);
	};
}
