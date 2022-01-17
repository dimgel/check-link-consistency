#include <experimental/array>

namespace dimgel {

	// See also config file: src/etc/check-link-consistency.conf.sample.


	// In ArchLinux, /bin, /sbin and /usr/sbin are symlinks to /usr/bin.
	// Although I could specify them all here: realpath() will be applied to all entries and duplicates will be merged.
	//
	// Order is NOT important.
	//
	// Didn't include "/usr/local/bin" (although it's contained in default system PATH) just for symmetry with scanDefaultLibs.
	//
	static constexpr auto scanDefaultBins {std::experimental::make_array("/usr/bin")};


	// ATTENTION! ONLY DEFAULT directories, as listed in `man 8 ld.so`.
	//
	// Order IS important: it's search priority.
	//
	// NOTE: `ldconfig -pNX >/dev/null` shows error "Can't stat /usr/libx32: No such file or directory",
	//        but it's nothing to deal with for now: if it finds that dir someday, it will add it to /etc/ld.so.cache.
	//        Since I take that cache into account too, listing default dirs here is overkill unless cache is outdated;
	//        but `man 8 ld.so` does it, so I'll do it too.
	//
	static constexpr auto scanDefaultLibs {std::experimental::make_array("/usr/lib", "/usr/lib32")};
}
