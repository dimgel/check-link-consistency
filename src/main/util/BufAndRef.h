#pragma once

#include <memory>
#include "StringRef.h"


namespace dimgel {

	struct BufAndRef {
		std::unique_ptr<char[]> buf;   // Buf size = ref.size()+1, null-terminated.
		StringRef::Mutable ref;        // Points to buf.
	};
}
