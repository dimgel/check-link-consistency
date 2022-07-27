#include <filesystem>
#include <regex>
#include <string.h>
#include <unistd.h>
#include "data.h"
#include "ELFInspector.h"
#include "FilesCollector.h"
#include "PacMan.h"
#include "Resolver.h"
#include "util/Abort.h"
#include "util/alloc/Arena.h"
#include "util/Error.h"
#include "util/IniParser.h"
#include "util/Log.h"
#include "util/SplitMutableString.h"
#include "util/ThreadPool.h"
#include "util/util.h"
#include STRINGIZE(CONCAT2(defaults_, DISTRO.hpp))

namespace fs = std::filesystem;

// Clang has __FILE_NAME__, gcc does not. I also dislike full filename here.
// See more ugly options (which won't work for included files) here: https://stackoverflow.com/questions/8487986
#define FILE_LINE "main:" LINE ": "


static constexpr int ExitStatus_OK = 0;
static constexpr int ExitStatus_Inconsistent = 1;
static constexpr int ExitStatus_Error = 2;


int main(int argc, char* argv[]) {
	using namespace dimgel;

	// MultiArena didn't give any profit, even made things a little worse (same number of pages, but less occupation of last pages).
	alloc::Arena ctx_mm(1024*1024);

	// Parse command line.
	int ctx_verbosity = Verbosity_Default;
	bool ctx_wideOutput = true;
	bool ctx_useOptionalDeps = true;
	bool ctx_noNetwork = false;
	bool ctx_colorize = true;
	Colors* ctx_colors = &Colors::enabled;
	{
		bool ok = true;
		int opt;
		opterr = false;
		while (ok && (opt = getopt(argc, argv, "qvONWC")) != -1) {
			switch (opt) {
				case 'q': {
					ctx_verbosity = Verbosity_Quiet;
					break;
				}
				case 'v': {
					ctx_verbosity = std::max(ctx_verbosity, Verbosity_Default) + 1;
					break;
				}
				case 'O': {
					ctx_useOptionalDeps = false;
					break;
				}
				case 'N': {
					ctx_noNetwork = true;
					break;
				}
				case 'W': {
					ctx_wideOutput = false;
					break;
				}
				case 'C': {
					ctx_colorize = false;
					ctx_colors = &Colors::disabled;
					break;
				}
				default: {
					ok = false;
				}
			}
		} // while()
		if (!ok) {
			fprintf(stderr, "Usage: %s [options]\n"
					"    -q  = Suppress INFO messages, output only errors\n"
					"    -v  = Output warnings + exec() command lines + `pacman -Sw` output (useful to investigate)\n"
					"    -vv = Huge (~1.3G on my system) but grep-friendly debug output\n"
					"    -O  = Don't download & analyze optional dependencies\n"
					"    -N  = No network: pretend optional dependencies are already downloaded;\n"
					"          bypass `pacman -Sw` but otherwise process optdeps as usual\n"
					"    -W  = Disable wide output\n"
					"    -C  = Don't colorize output\n"
					"Status codes:\n"
					"     0  = system is consistent :)\n"
					"     1  = not consistent :(\n"
					"     2  = some bad error :(((\n"
					"   139  = even worse, if you catch my meaning ;)\n",
				fs::path(argv[0]).filename().c_str()
			);
			return ExitStatus_Error;
		}
	}

	Log ctx_log(*ctx_colors);


	try {
		// To process relative paths in /var/lib/pacman/local/*/files as absolute ones, like pacman does.
		if (chdir("/") == -1) {
			throw Error("chdir(\"/\") failed: %s", strerror(errno));
		}

		// Configuration.
		// --------------

		std::vector<SearchPath> ctx_scanBins;
		std::vector<SearchPath> ctx_scanDefaultLibs;
		std::vector<SearchPath> ctx_scanMoreLibs;
		alloc::StringHashMap<std::vector<AddLibPath>> ctx_addLibPathsByFilePath1Prefix;
		alloc::StringHashMap<std::vector<AddLibPath>> ctx_addLibPathsByPackageName;
		std::unordered_map<std::string, std::vector<AddOptDepend>> ctx_addOptDependsByPackageName;

		{
			// See /notes/decisions.txt.
			auto append = [&](const char* source, const char* path, std::vector<SearchPath>& target) {
				if (path[0] == '\0') {
					throw Error("Config: invalid %s entry `%s`: path is empty", source, path);
				}
				if (path[0] != '/') {
					throw Error("Config: invalid %s entry `%s`: path must be absolute", source, path);
				}

				char path0[PATH_MAX];
				if (!util::realPath(path, path0)) {
					if (ctx_verbosity >= Verbosity_WarnAndExec) {
						ctx_log.warn("Config: skipping %s entry `%s`: directory does not exist", source, path);
					}
					return;
				}
				if (strcmp(path0, path)) {
					if (ctx_verbosity >= Verbosity_Debug) {
						ctx_log.debug("Config: rewritten %s entry `%s` ---> `%s`", source, path, path0);
					}
					path = path0;
				}

				// "Stat() lacks functionallity for remote filesystems and collects all the information of a file at once which might lead to slow operations."
				// I also would be able to explicitly check stx_mask for inodes support.
				auto st = util::statx(path);
				if (!S_ISDIR(st.mode)) {
					if (ctx_verbosity >= Verbosity_WarnAndExec) {
						ctx_log.warn("Config: skipping %s entry `%s`: not a directory, or unsupported filesystem", source, path);
					}
					return;
				}
				for (auto& sp : target) {
					if (sp.inode == st.inode) {
						if (ctx_verbosity >= Verbosity_Debug) {
							ctx_log.debug("Config: skipping %s entry `%s`: duplicate", source, path);
						}
						return;
					}
				}

				target.push_back(SearchPath{
					.path1 = alloc::String{ctx_mm, path0 + 1},
					.inode = st.inode
				});
			};

			auto print = [&](Log::F f, const char* pfx, const std::vector<SearchPath>& spp) {
				std::ostringstream os;
				os << pfx;
				for (auto& sp : spp) {
					os << " `" << sp.path1 << "`";
				}
				(ctx_log.*f)("%s", os.str().c_str());
			};

			// Apply defaults.
			ctx_scanDefaultLibs.reserve(scanDefaultLibs.size());
			for (auto s : scanDefaultBins) {
				append("scanDefaultBins", s, ctx_scanBins);
			}
			for (auto s : scanDefaultLibs) {
				append("scanDefaultLibs", s, ctx_scanDefaultLibs);
			}

			// Read LD_LIBRARY_PATH -- before config, because it has priority over config/scanMoreLibs.
			{
				char* s0 = getenv("LD_LIBRARY_PATH");
				if (s0 != nullptr) {
					// Spaces and escaping in paths are not allowed for legacy reasons: https://stackoverflow.com/a/10073337
					std::string s = s0;
					for (auto sv : SplitMutableString(s, ":", true)) {
						append("LD_LIBRARY_PATH", sv.cp(), ctx_scanMoreLibs);
					}
				}
				if (!ctx_scanMoreLibs.empty() && ctx_verbosity >= Verbosity_Default) {
					print(&Log::info, "Using non-empty LD_LIBRARY_PATH =", ctx_scanMoreLibs);
				}
			}

			// Read config file.
			{
				auto readConfig = [&](const std::string& path, bool isLocal) -> bool {
					if (!fs::exists(path)) {
						return false;
					}
					if (ctx_verbosity >= (isLocal ? Verbosity_VeryImportantWarn : Verbosity_Debug)) {
						Log::F f = isLocal ? &Log::warn : &Log::debug;
						(ctx_log.*f)("Reading config file: `%s`...", path.c_str());
					}
					auto contents = util::readFile(path.c_str());
					std::regex rAddLib{"^\\s*(\\S+)\\s+(\\S+)\\s*$"};
					std::regex& rAddOptDepend = rAddLib;
					IniParser::execute(contents.ref, [&](const IniParser::Line& l) {

						auto scanMore = [&](std::vector<SearchPath>& target) {
							for (auto sv : SplitMutableString(l.value().srMutable(), " \t", true)) {
								append(l.key().cp(), sv.cp(), target);
							}
						};

						auto parseAddLibPath = [&]() {
							std::cmatch m;
							if (!util::regex_match(l.value(), m, rAddLib)) {
								throw Error("Config line %d: bad %s: invalid syntax", l.lineNo(), l.key().cp());
							}
							std::string where = m[1];   // Where to add (package | /file/path | /directory/**).
							std::string what = m[2];    // What to add (/lib/path | /search/path).

							if (what[0] != '/') {
								throw Error("Config line %d: bad %s: `%s` must be absolute path", l.lineNo(), l.key().cp(), what.c_str());
							}
							char whatReal0[PATH_MAX];
							const char* whatEffective;
							util::statx_Result whatSt;
							if (util::realPath(what.c_str(), whatReal0)) {
								whatEffective = whatReal0;
								whatSt = util::statx(whatEffective);
								if (!S_ISDIR(whatSt.mode)) {
									throw Error("Config line %d: bad %s: `%s` is not a directory", l.lineNo(), l.key().cp(), whatEffective);
								}
							} else {
								if (ctx_verbosity >= Verbosity_WarnAndExec) {
									ctx_log.warn(
										"Config line %d: suspicious %s: `%s` does not exist; optional dependency?",
										l.lineNo(), l.key().cp(), what.c_str()
									);
								}
								whatEffective = what.c_str();
								whatSt.inode = 0;
							}

							if (where.find('/') == where.npos) {
								if (ctx_verbosity >= Verbosity_Debug) {
									ctx_log.debug(
										"Config line %d: %s = `%s` ---> to all files in package `%s`",
										l.lineNo(), l.key().cp(), whatEffective, where.c_str()
									);
								}
								ctx_addLibPathsByPackageName[alloc::String{ctx_mm, where}].push_back(AddLibPath{
									.configLineNo = l.lineNo(), .path0 = alloc::String{ctx_mm, whatEffective}, .inode = whatSt.inode
								});
							} else if (!where.starts_with('/')) {
								throw Error(
									"Config: line %d, bad %s: `%s` is neither package name nor absolute path",
									l.lineNo(), l.key().cp(), where.c_str()
								);
							} else if (where.ends_with('/') || where.ends_with("/*")) {
								throw Error(
									"Config line %d: bad %s: `%s` ends with `/` or `/*`, did you mean `/**`?",
									l.lineNo(), l.key().cp(), where.c_str()
								);
							} else {
								bool whereIsDir = where.ends_with("/**");
								if (whereIsDir) {
									where = where.substr(0, where.length() - 3);
								}
								char whereReal0[PATH_MAX];
								if (!util::realPath(where.c_str(), whereReal0)) {
									if (ctx_verbosity >= Verbosity_WarnAndExec) {
										ctx_log.warn(
											FILE_LINE "Config line %d: ignore %s: `%s` does not exist",
											l.lineNo(), l.key().cp(), where.c_str()
										);
									}
									return;
								}
								auto whereSt = util::statx(whereReal0);
								if ((whereIsDir && !S_ISDIR(whereSt.mode)) || (!whereIsDir && !S_ISREG(whereSt.mode))) {
									throw Error(
										"Config line %d: bad %s: `%s` is not a %s",
										l.lineNo(), l.key().cp(), where.c_str(), (whereIsDir ? "directory" : "regular file")
									);
								}

								if (whereIsDir) {
									// To simply match File::path1 by starts_with() + length() equality; see also (whereReal + 1) below.
									auto n = strlen(whereReal0);
									whereReal0[n] = '/';
									whereReal0[n + 1] = '\0';
								}

								if (ctx_verbosity >= Verbosity_Debug) {
									ctx_log.debug(
										"Config line %d: %s = `%s` ---> to %s `%s`",
										l.lineNo(), l.key().cp(), whatEffective, (whereIsDir ? "all files in directory" : "file"), whereReal0 + 1
									);
								}
								ctx_addLibPathsByFilePath1Prefix[alloc::String{ctx_mm, whereReal0 + 1}].push_back(AddLibPath{
									.configLineNo = l.lineNo(), .path0 = alloc::String{ctx_mm, whatEffective}, .inode = whatSt.inode
								});
							}
						};   // parseAddLibPath()


						if (l.key() == "scanMoreBins") {
							scanMore(ctx_scanBins);
						} else if (l.key() == "scanMoreLibs") {
							scanMore(ctx_scanMoreLibs);
						} else if (l.key() == "addOptDepend") {

							std::cmatch m;
							if (!util::regex_match(l.value(), m, rAddOptDepend)) {
								throw Error("Config line %d: bad %s: invalid syntax", l.lineNo(), l.key().cp());
							}
							std::string package = m[1];
							std::string optdep = m[2];
							if (package.find('/') != std::string::npos) {
								throw Error("Config line %d: bad %s: package name `%s` contains '/'", l.lineNo(), l.key().cp(), package.c_str());
							}
							if (optdep.find('/') != std::string::npos) {
								throw Error("Config line %d: bad %s: optional dependency `%s` contains '/'", l.lineNo(), l.key().cp(), optdep.c_str());
							}

							ctx_addOptDependsByPackageName[package].push_back({.configLineNo = l.lineNo(), .optDepName = alloc::String{ctx_mm, optdep}});
							if (ctx_verbosity >= Verbosity_Debug) {
								ctx_log.debug( "Config line %d: add optional dependency `%s` to package `%s`", l.lineNo(), optdep.c_str(), package.c_str());
							}

						} else if (l.key() == "addLibPath") {
							parseAddLibPath();
						} else {
							return false;
						}
						return true;
					});   // IniParser::execute(...)
					return true;
				};   // readConfig()

				// To debug-run from project's `target` directory (i.e. without installation), trying next-to-binary *.conf.sample first.
				if (!readConfig(std::string(argv[0]) + ".conf.sample", true) &&
					!readConfig("/etc/" + (std::string)fs::path(argv[0]).filename() + ".conf", false) &&
					ctx_verbosity >= Verbosity_VeryImportantWarn
				) {
					ctx_log.warn(
						"Config file not found; expect false errors.\n"
						"      Please copy /usr/share/check-link-consistency/*.conf.sample to /etc/*.conf and edit."
					);
				}
			}

			if (ctx_verbosity >= Verbosity_Debug) {
				print(&Log::debug, "Config: scanBins =", ctx_scanBins);
				print(&Log::debug, "Config: scanDefaultLins =", ctx_scanDefaultLibs);
				print(&Log::debug, "Config: scanMoreLibs =", ctx_scanMoreLibs);
			}
		} // Configuration.


		ThreadPool ctx_threadPool {0, [&](const char* taskExceptionMessage) {
			// Called asynchronously (it's ok, Log synchronizes its output) for all exceptions except Abort (it's ok too).
			if (taskExceptionMessage != nullptr || *taskExceptionMessage != '\0') {
				ctx_log.error("%s", taskExceptionMessage);
			}
		}};

		Context ctx {
			.verbosity = ctx_verbosity,
			.wideOutput = ctx_wideOutput,
			.colorize = ctx_colorize,
			.colors = *ctx_colors,
			.useOptionalDeps = ctx_useOptionalDeps,
			.noNetwork = ctx_noNetwork,

			.scanBins = ctx_scanBins,
			.scanDefaultLibs = ctx_scanDefaultLibs,
			.scanMoreLibs = ctx_scanMoreLibs,
			.addLibPathsByFilePath1Prefix = ctx_addLibPathsByFilePath1Prefix,
			.addLibPathsByPackage {},

			.log = ctx_log,
			.threadPool = ctx_threadPool,
			.mm = ctx_mm
		};


		// Let's go.
		// ---------

		Data data;
		ELFInspector elfInspector(ctx, data);
		FilesCollector filesCollector(ctx, data, elfInspector);
		Resolver soResolver(ctx, data);
		auto pacman = createPacMan(ctx, data, elfInspector);

		auto ok = [&]{
			// Config file will contain package-specific lib paths. So to simplify everything, load packages first.
			pacman->parseInstalledPackages();

			// Some configuration checks / transformations.
			ctx.addLibPathsByPackage.reserve(ctx_addLibPathsByPackageName.size());
			for (auto& [pName, addLibs] : ctx_addLibPathsByPackageName) {
				if (auto it = data.packagesByName.find(pName);  it != data.packagesByName.end()) {
					ctx.addLibPathsByPackage.insert({it->second, std::move(addLibs)});
				} else if (ctx.verbosity >= Verbosity_WarnAndExec) {
					ctx.log.warn("Config file references non-installed package '%s'.", pName.cp());
				}
			}
			for (auto& [pName, addOptDepends] : ctx_addOptDependsByPackageName) {
				if (auto it = data.packagesByName.find(pName);  it != data.packagesByName.end()) {
					Package* p = it->second;
					for (auto& optdep : addOptDepends) {
						if (p->optDepends.insert(optdep.optDepName).second) {
							if (ctx.verbosity >= Verbosity_Debug) {
								ctx.log.debug(
									FILE_LINE "add optional dependency `%s` to package `%s` from config file line %d.",
									optdep.optDepName.cp(), pName.c_str(), optdep.configLineNo
								);
							}
						} else {
							if (ctx.verbosity >= Verbosity_WarnAndExec) {
								ctx.log.warn(
									FILE_LINE "skip optional dependency `%s` to package `%s` from config file line %d: already added",
									optdep.optDepName.cp(), pName.c_str(), optdep.configLineNo
								);
							}
						}
					}
				} else if (ctx.verbosity >= Verbosity_WarnAndExec) {
					ctx.log.warn("Config file references non-installed package '%s'.", pName.c_str());
				}
			}
			ctx_addLibPathsByPackageName.clear();
			ctx_addOptDependsByPackageName.clear();

			// ...Let's go on.
			filesCollector.execute();
			if (soResolver.execute()) {
				return true;
			}
			if (!ctx.useOptionalDeps) {
				return false;
			}
			pacman->calculateOptionalDependencies();
			if (data.archiveNamesByOptDepend.empty()) {
				// Nothing more we can do.
				return false;
			}

			pacman->downloadOptionalDependencies();
			pacman->processOptionalDependencies();
			return soResolver.execute();
		}();

		if (ctx.verbosity >= Verbosity_Debug) {
			ctx.mm.debugOutputStats(ctx.log, "ctx.mm");
		}
		if (!ok) {
			soResolver.dumpErrors();
		} else if (ctx.verbosity >= Verbosity_Default) {
			ctx.log.info("All good. :)");
		}

		return ok ? ExitStatus_OK : ExitStatus_Inconsistent;
	} catch (Abort& e) {
		return ExitStatus_Error;
	} catch (std::exception& e) {
		ctx_log.error("%s", e.what());
		return ExitStatus_Error;
	}
}
