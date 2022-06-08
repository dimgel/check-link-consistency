#include <fcntl.h>
#include <string.h>
#include "ELFInspector.h"
#include "util/Abort.h"
#include "util/Closeable.h"
#include "util/Error.h"
#include "util/Finally.h"
#include "util/Log.h"
#include "util/SplitMutableString.h"
#include "util/ThreadPool.h"
#include "util/util.h"

#define FILE_LINE "ELFInspector:" LINE ": "


namespace dimgel {

	ELFInspector::ELFInspector(Context& ctx, Data& data) : ctx(ctx), data(data) {
		if (elf_version(EV_CURRENT) == EV_NONE) {
			throw std::runtime_error(FILE_LINE "failed to initialize libelf");
		}
	}


	void ELFInspector::processOne_impl_scanDynamicSection(Elf* e, File& f, GElf_Shdr& hdr, Elf_Data* data, std::function<void(SearchPath)> scanAdditionalDir) {
		GElf_Dyn dynMem;
		GElf_Dyn* dyn;

		auto parseRunPath = [&](const char* description, std::vector<SearchPath>& runPaths) {
			std::string s = elf_strptr(e, hdr.sh_link, dyn->d_un.d_val);
			for (auto sv : SplitMutableString(s, ":", true)) {
				char originReplaced[PATH_MAX];
				auto svEffective = sv;   // For better messages.
				if (sv[0] != '/') {
					if (sv == "$ORIGIN" || sv.starts_with("$ORIGIN/")) {
						// Google says that $ORIGIN is resolved "relative to executable". My guess is that means "..., not library", but:
						// 1. For example, most of /usr/lib/qtcreator/plugins/*.so have RPATH="$ORIGIN:$ORIGIN/../:$ORIGIN/../../Qt/lib",
						//    and `ldd` resolves their neededLibs as if $ORIGIN was library's contianing directory itself, not /usr/bin/qtcreator executable's
						//    containing directory /usr/bin: e.g. libStudioWelcome.so needs libCore.so (in the same dir), libUtils.so (in parent dir), etc.
						// 2. If shared library is linked to executables located in different dirs, and its $ORIGIN resolved
						//    against those different dirs, then we'd need its multiple copies in memory, which is ridiculous.
						// TODO So let's pray that $ORIGIN is actually relative to library itself, not to what it's linked to.
						auto lastSlash = f.path1.sv().rfind('/');
						svEffective = util::concatStringViews(originReplaced, sizeof(originReplaced), {"/", f.path1.substr(0, lastSlash), sv.substr(7).sv()});
					} else {
						// Ignore because we don't know which current dir this path is relative to.
						// WARN only if verbose, because so many warnings is non-informative for user; and maybe PacMan will resolve all problems.
						if (ctx.verbosity >= Verbosity_WarnAndExec) {
							ctx.log.warn(FILE_LINE "`/%s`: skip %s `%s`: non-absolute path", ConstCharPtr{f.path1}, ConstCharPtr{description}, ConstCharPtr{sv});
						}
						continue;
					}
				}
				char path0[PATH_MAX];
				if (!util::realPath(svEffective.cp(), path0)) {
					if (ctx.verbosity >= Verbosity_WarnAndExec) {
						ctx.log.warn(FILE_LINE "`/%s`: skip %s `%s`: missing path", ConstCharPtr{f.path1}, ConstCharPtr{description}, ConstCharPtr{sv});
					}
					continue;
				}
				if (strcmp(sv.cp(), path0) && ctx.verbosity >= Verbosity_Debug) {
					ctx.log.debug(
						FILE_LINE "`/%s`: rewrite %s `%s` ---> `%s`",
						ConstCharPtr{f.path1}, ConstCharPtr{description}, ConstCharPtr{sv}, ConstCharPtr{path0}
					);
				}
				auto st = util::statx(path0);
				if (!S_ISDIR(st.mode)) {
					if (ctx.verbosity >= Verbosity_WarnAndExec) {
						ctx.log.warn(FILE_LINE "`/%s`: skip %s `%s`: not a directory", ConstCharPtr{f.path1}, ConstCharPtr{description}, ConstCharPtr{sv});
					}
					continue;
				}
				// FilesCollector does not scan same dir twice, so no checks are needed here.
				SearchPath sp {
					.path1 = alloc::String{ctx.mm, path0 + 1},
					.inode = st.inode
				};
				scanAdditionalDir(sp);
				runPaths.push_back(sp);
				if (ctx.verbosity >= Verbosity_Debug) {
					ctx.log.debug(FILE_LINE "`/%s`: add %s `%s`", ConstCharPtr{f.path1}, ConstCharPtr{description}, ConstCharPtr{sv});
				}
			}
		};

		// Pass 1/2: compute number of needed libs.
		// gelf_getdyn() fails on bad section type (data->d_type != ELF_T_DYN) or bad index.
		size_t numNeeded = 0;
		for (int i = 0;  (dyn = gelf_getdyn(data, i, &dynMem)) != nullptr;  i++) {
			if (dyn->d_tag == DT_NEEDED) {
				numNeeded++;
			} else if (dyn->d_tag == DT_RPATH) {
				parseRunPath("RPATH", f.rPaths);
			} else if (dyn->d_tag == DT_RUNPATH) {
				parseRunPath("RUNPATH", f.runPaths);
			}
		}

		// Pass 2/2: read needed libs.
		if (numNeeded > 0) {
			f.neededLibs.reserve(numNeeded);
			for (int i = 0;  (dyn = gelf_getdyn(data, i, &dynMem)) != nullptr;  i++) {
				if (dyn->d_tag == DT_NEEDED) {
					const char* value = elf_strptr(e, hdr.sh_link, dyn->d_un.d_val);
					if (value[0] != '/' && strchr(value, '/') != nullptr) {
						// I saw examples like "./subdir", but I don't know which current dir is to search against.
						if (ctx.verbosity >= Verbosity_WarnAndExec) {
							ctx.log.warn(FILE_LINE "`/%s`: skip needed lib `%s`: non-absolute but contains '/'", ConstCharPtr{f.path1}, ConstCharPtr{value});
						}
						continue;
					}
					if (f.neededLibs.insert(alloc::String{ctx.mm, value}).second) {
						if (ctx.verbosity >= Verbosity_Debug) {
							ctx.log.debug(FILE_LINE "`/%s`: add needed lib `%s`", ConstCharPtr{f.path1}, ConstCharPtr{value});
						}
					} else {
						if (ctx.verbosity >= Verbosity_Debug) {
							ctx.log.debug(FILE_LINE "`/%s`: skip needed lib `%s`: already added (by config?)", ConstCharPtr{f.path1}, ConstCharPtr{value});
						}
					}
				}
			}
		}
	}


	void ELFInspector::processOne_impl(Elf* e, File& f, bool fromArchive, std::function<void(SearchPath)> scanAdditionalDir) {
		if (f.isInspected.exchange(true)) {
			throw Error(FILE_LINE "`/%s`: internal error: already inspected", ConstCharPtr{f.path1});
		}

		try {
			if (elf_kind(e) != ELF_K_ELF) {
				if (ctx.verbosity >= Verbosity_Debug) {
					ctx.log.debug(FILE_LINE "`/%s`: skip: not ELF", ConstCharPtr{f.path1});
				}
				return;
			}

			int i;
			GElf_Ehdr ehdr;
			if (gelf_getehdr(e, &ehdr) == nullptr) {
				throw Error(FILE_LINE "`/%s`: skip: gelf_getehdr() failed: %s", ConstCharPtr{f.path1}, ConstCharPtr{elf_errmsg(-1)});
			}
			if (i = gelf_getclass(e);  i != ELFCLASS32 && i != ELFCLASS64) {
				throw Error(FILE_LINE "`/%s`: skip: gelf_getclass() failed: %s", ConstCharPtr{f.path1}, ConstCharPtr{elf_errmsg(-1)});
			}
			f.is32 = i == ELFCLASS32;
			if (ehdr.e_type != ET_EXEC && ehdr.e_type != ET_DYN) {
				if (ctx.verbosity >= Verbosity_Debug) {
					ctx.log.debug(FILE_LINE "`/%s`: skip: e_type != EXEC|DYN", ConstCharPtr{f.path1});
				}
				return;
			}

			bool foundDynamicSection = false;
			Elf_Scn* scn = nullptr;
			while ((scn = elf_nextscn(e, scn)) != nullptr) {
				GElf_Shdr hdr;
				if (gelf_getshdr(scn, &hdr) == nullptr) {
					throw Error(FILE_LINE "`/%s`: skip: gelf_getshdr() failed: %s", ConstCharPtr{f.path1.cp()}, ConstCharPtr{elf_errmsg(-1)});
				}

				if (hdr.sh_type != SHT_DYNAMIC) {
					continue;
				}
				if (foundDynamicSection) {
					throw Error(FILE_LINE "`/%s`: skip: found multiple DYNAMIC sections", ConstCharPtr{f.path1});
				}
				foundDynamicSection = true;
				if (fromArchive) {
					break;
				}

				// Check sh_link points to string table.
				{
					if (hdr.sh_link == 0) {
						throw Error(FILE_LINE "`/%s`: skip: sh_link==0 in DYNAMIC section", ConstCharPtr{f.path1});
					}
					Elf_Scn* scnStr = nullptr;
					if ((scnStr = elf_getscn(e, hdr.sh_link)) == nullptr) {
						throw Error(FILE_LINE "`/%s`: skip: elf_getscn(sh_link) failed in DYNAMIC section: %s", ConstCharPtr{f.path1}, ConstCharPtr{elf_errmsg(-1)});
					}
					GElf_Shdr strHdr;
					if (gelf_getshdr(scnStr, &strHdr) == nullptr) {
						throw Error(FILE_LINE "`/%s`: skip: gelf_getshdr(sh_link) failed in DYNAMIC section: %s", ConstCharPtr{f.path1}, ConstCharPtr{elf_errmsg(-1)});
					}
					if (strHdr.sh_type != SHT_STRTAB) {
						throw Error(FILE_LINE "`/%s`: skip: sh_link points to non-strings in DYNAMIC section", ConstCharPtr{f.path1});
					}
				}

				Elf_Data* data = elf_getdata(scn, nullptr);
				if (data == nullptr) {
					throw Error(FILE_LINE "`/%s`: skip: no data in DYNAMIC section", ConstCharPtr{f.path1});
				}
				if (elf_getdata(scn, data) != nullptr) {
					throw Error(FILE_LINE "`/%s`: skip: multiple data in DYNAMIC section", ConstCharPtr{f.path1});
				}

				processOne_impl_scanDynamicSection(e, f, hdr, data, scanAdditionalDir);
			}

			if (!foundDynamicSection) {
				if (ctx.verbosity >= Verbosity_Debug) {
					ctx.log.debug(FILE_LINE "`/%s`: skip: not dynamic ELF", ConstCharPtr{f.path1});
				}
				return;
			}

			// Dynamic executable may have type ET_EXEC or ET_DYN, shared library is always ET_DYN.
			// But even ET_EXEC can export symbols that are imported by its plugins, e.g. gcc's `/usr/lib/gcc/*/*/cc1` and `/usr/lib/gcc/*/*/plugin/libcc1plugin.so`.
			// It won't hurt to consider all ET_DYN files as potential libs.
			f.isLib = (ehdr.e_type == ET_DYN);
			if (ctx.verbosity >= Verbosity_Debug) {
				ctx.log.debug(
					FILE_LINE "`/%s`: is %s-bit %s%s",
					ConstCharPtr{f.path1}, (f.is32 ? "32" : "64"), (f.isLib ? "library" : "executable"), (f.isSecure ? ", secure" : "")
				);
			}

			// All done, consider file for future processing.
			f.isDynamicELF = true;

		} catch (Abort& e) {
			// Do nothing.
		} catch (std::exception& e) {
			ctx.log.error("%s", e.what());
		}
	}


	void ELFInspector::processOne_file(File& f, std::function<void(SearchPath)> scanAdditionalDir) {
		Closeable fd {::open(f.path1.cp(), O_RDONLY)};
		if (fd == -1) {
			// Don't throw: few broken files (including files with broken ELF structure libelf will fail on) should not break whole thing.
			ctx.log.error(FILE_LINE "`/%s`: open() failed: %s", ConstCharPtr{f.path1}, ConstCharPtr{strerror(errno)});
			return;
		}
		Elf* e = elf_begin(fd, ELF_C_READ, nullptr);
		if (e == nullptr) {
			ctx.log.error(FILE_LINE "`/%s`: elf_begin() failed: %s", ConstCharPtr{f.path1}, ConstCharPtr{elf_errmsg(-1)});
			return;
		}
		Finally eFin([&] {
			elf_end(e);
		});
		processOne_impl(e, f, false, scanAdditionalDir);
	}


	void ELFInspector::processOne_fromArchive(File& f, char* buf, size_t size) {
		Elf* e = elf_memory(buf, size);
		if (e == nullptr) {
			ctx.log.error(FILE_LINE "`/%s`: elf_memory() failed: %s", ConstCharPtr{f.path1}, ConstCharPtr{elf_errmsg(-1)});
			return;
		}
		Finally eFin([&] {
			elf_end(e);
		});
		processOne_impl(e, f, true, [](SearchPath){});
	}
}
