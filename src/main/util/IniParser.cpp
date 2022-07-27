#include <unordered_set>
#include "Error.h"
#include "IniParser.h"
#include "SplitMutableString.h"
#include "util.h"


namespace dimgel {

	void IniParser::execute(StringRef::Mutable contents, std::function<bool(const Line& l)> onLine) {
		SplitMutableString lines(contents);
		for (auto it = lines.begin();  it != lines.end();  ++it) {
			std::string_view sv = it->sv();   // Mutable StringRef!
			util::trimInplace(sv);
			if (sv.empty() || sv.starts_with('#')) {
				continue;
			}

			std::string_view k;   // Mutable!
			std::string_view v;   // Mutable!
			int lineNo = it.getPartNo();

			auto iEq = sv.find('=');
			if (iEq != std::string::npos) {
				k = sv.substr(0, iEq);
				util::trimInplace(k);
			}
			if (k.empty()) {
				throw Error("Wrong syntax @ line %d: expect key=value", lineNo);
			}
			v = sv.substr(iEq + 1);
			util::trimInplace(v);

			// All these string_view-s are backed up by mutable string reference stored in SplitMutableString.
			const_cast<char*>(k.data())[k.size()] = '\0';
			const_cast<char*>(v.data())[v.size()] = '\0';
			auto kr = StringRef::createUnsafe(k);
			auto vr = StringRef::createUnsafe(v);

			if (!onLine({kr, vr, lineNo})) {
				throw Error("Unknown key `%s` @ line %d", kr.cp(), lineNo);
			}
		}
	}


	int64_t IniParser::Line::Value::asInt64() const {
		char* end;
		auto x = strtoll(v.cp(), &end, 10);
		static_assert(sizeof(x) == 8);

		// Note: value is already trimmed, so I can check for errors & incomplete parsing short way:
		if ((x == 0 && v != "0") || *end != '\0') {
			throw Error("Value of `%s` must integer @ line %d", k.cp(), l);
		}
		return x;
	}


	bool IniParser::Line::Value::asBool() const {
		bool x = v == "true";
		if (!x && v != "false") {
			throw Error("Value of `%s` must be boolean (true|false) at line %d", k.cp(), l);
		}
		return x;
	}
}
