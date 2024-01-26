#include <libelf.h>
#include <mutex>
#include <optional>
#include <sstream>
#include "Resolver.h"
#include "util/Log.h"
#include "util/ThreadPool.h"
#include "util/util.h"

#define FILE_LINE "Resolver:" LINE ": "


namespace dimgel {

	bool Resolver::execute() {

		class ResolveLibsTask : public ThreadPool::Task {
			Resolver& owner;
			File& f;

		public:
			ResolveLibsTask(Resolver& owner, File& f) : owner(owner), f(f) {}

			void compute() override {
				auto verbosity = owner.ctx.verbosity;
				auto& log = owner.ctx.log;

				auto& libs = owner.data.libs;
				auto& ldCache = owner.data.ldCache;
				for (auto it = f.neededLibs.begin();  it != f.neededLibs.end();  ) {
					alloc::String name = *it;

					// ATTENTION!!! When called with ldCache, both `map` keys and `path1` are .so names (not paths).
					auto searchOne = [&](const char* description, PathAndBitnessMap& map, StringRef path1) -> bool {
						auto it2 = map.find(std::pair{path1, f.is32});
						if (it2 == map.end()) {
							return false;
						}
						File* f2 = it2->second;
						if (f2 == &f) {
							log.error(FILE_LINE "`/%s`: ignored needed lib `%s` ---> resolved to itself", f.path1.cp(), name.cp());
							it = f.neededLibs.erase(it);
							return true;
						}
						if (!f2->isDynamicELF || !f2->isLib) {
							log.error(
								FILE_LINE "`/%s`: ignored needed lib `%s` ---> `/%s` (%s): not a %s",
								f.path1.cp(), name.cp(), f2->path1.cp(), description, (f2->isDynamicELF ? "library" : "dynamic ELF")
							);
							it = f.neededLibs.erase(it);
							return true;
						}
						if (verbosity >= Verbosity_Debug) {
							log.debug(FILE_LINE "`/%s`: resolved needed lib `%s` ---> `/%s` (%s)", f.path1.cp(), name.cp(), f2->path1.cp(), description);
						}
						it = f.neededLibs.erase(it);
						return true;
					};

					auto searchPaths = [&](const char* searchPathsDescription, const std::vector<SearchPath>& searchPaths, alloc::String fileName) -> bool {
						for (auto& sp : searchPaths) {
							char buf[PATH_MAX];
							if (searchOne(searchPathsDescription, libs, util::concatStringViews(buf, sizeof(buf), {sp.path1.sv(), "/", fileName.sv()}))) {
								return true;
							}
						}
						return false;
					};

					// On library search order, see: `man 8 ld.so`, /notes/decisions.txt, src/etc/check-link-consistency.conf.sample.
					if (name[0] == '/') {
						if (searchOne("absPath", libs, name.substr(1))) {
							continue;
						}
					} else if (
						searchPaths("configPaths", f.configPaths, name) ||
						(f.runPaths.empty() && searchPaths("RPATH", f.rPaths, name)) ||
						(!f.isSecure && searchPaths("scanMoreLibs", owner.ctx.scanMoreLibs, name)) ||
						searchPaths("RUNPATH", f.runPaths, name) ||
						searchOne("ldCache", ldCache, name) ||
						searchPaths("scanDefaultLibs", owner.ctx.scanDefaultLibs, name)
					) {
						continue;
					}

					if (verbosity >= Verbosity_Debug) {
						log.debug(FILE_LINE "`/%s`: needed lib not found: `%s`", f.path1.cp(), name.cp());
					}
					++it;
				} // for (auto it = f.neededLibs.begin();  ...)
			} // void compute()


			void merge() override {
			}
		}; // class ResolveLibsTask


		if (ctx.verbosity >= Verbosity_Default) {
			ctx.log.info("Resolving libs...");
		}
		data.unresolvedNeededLibNames.reserve(150);

		// Remove files containing nothing to resolve.
		// Resolve neededLibs.
		{
			std::vector<std::unique_ptr<ThreadPool::Task>> tasks;
			tasks.reserve(data.uniqueFilesByPath1.size());
			for (auto it = data.uniqueFilesByPath1.begin();  it != data.uniqueFilesByPath1.end();  ) {
				File* f = it->second;
				// f->isDymamicELF maybe false if ELFInspector::processOne_impl() threw internally; but f->neededLibs may already be filled.
				if (f->isDynamicELF && !f->neededLibs.empty()) {
					tasks.push_back(std::make_unique<ResolveLibsTask>(*this, *f));
					++it;
				} else {
					it = data.uniqueFilesByPath1.erase(it);
				}
			}
			ctx.threadPool.addTasks(ctx.threadPool.groupTasks(std::move(tasks)));
			ctx.threadPool.waitAll();
		}

		// Remove successful files, fill data.unresolvedNeededLibsByName.
		for (auto it = data.uniqueFilesByPath1.begin();  it != data.uniqueFilesByPath1.end();  ) {
			File* f = it->second;
			if (f->neededLibs.empty()) {
				it = data.uniqueFilesByPath1.erase(it);
			} else {
				for (auto& nl : f->neededLibs) {
					data.unresolvedNeededLibNames.insert(nl);
				}
				it++;
			}
		}

		if (ctx.verbosity >= Verbosity_Debug) {
			ctx.log.debug(FILE_LINE "stats: data.uniqueFilesByPath1.size() = %lu", ulong{data.uniqueFilesByPath1.size()});
			ctx.log.debug(FILE_LINE "stats: data.unresolvedNeededLibNames.size() = %lu", ulong{data.unresolvedNeededLibNames.size()});
		}

		return data.uniqueFilesByPath1.empty();
	}


	//----------------------------------------------------------------------------------------------------------------------------------------


	// Would take 0.25s instead of 0.33s (with -SO options) if I haven't used std::iostream here.
	void Resolver::dumpErrors() {
		constexpr StringRef titleP {"Package"};
		constexpr StringRef titleF {"Problematic File"};
		constexpr StringRef titleNL {"Unresolved Needed Libs"};
		constexpr StringRef unassignedP {"(unassigned)"};

		unsigned numUnassignedFiles = 0;
		std::unordered_map<Package*, std::vector<File*>> packages;
		std::vector<Package*> packagesSorted;
		std::unordered_map<File*, std::vector<alloc::String>> neededLibsSorted;
		neededLibsSorted.reserve(data.uniqueFilesByPath1.size());
		size_t lengthP  = std::max(titleP.length(), unassignedP.length());
		size_t lengthF  = titleF.length();
		size_t lengthNL = titleNL.length();
		for (auto [_, f] : data.uniqueFilesByPath1) {
			lengthF = std::max(lengthF, 1 + f->path1.length());

			Package* p = f->belongsToPackage;
			packages[p].push_back(f);
			if (p) {
				lengthP = std::max(lengthP, p->name.length() + 1 + p->version.length());
			} else {
				++numUnassignedFiles;
			}

			auto& nl = neededLibsSorted[f];
			for (auto s : f->neededLibs) {
				nl.push_back(s);
				lengthNL = std::max(lengthNL, s.length());
			}
			util::sort(nl);
		}
		packagesSorted.reserve(packages.size());
		for (auto& [p, ff] : packages) {
			packagesSorted.push_back(p);
			util::sort(ff, [&](File* a, File* b) {
				return a->path1 < b->path1;
			});
		}
		util::sort(packagesSorted, [&](Package* a, Package* b) -> bool {
			if (a == nullptr) { return false; }
			if (b == nullptr) { return true; }
			return a->name < b->name;
		});


		size_t tableWidth = lengthP + 3 + lengthF + 3 + lengthNL;
		auto dashesMem = std::make_unique<char[]>(tableWidth + 1);
		memset(dashesMem.get(), '-', tableWidth);
		dashesMem.get()[tableWidth] = 0;
		auto dashes = [&](size_t n) -> const char* {
			return dashesMem.get() + tableWidth - n;
		};
		auto line2 = [&]{
			ctx.log.error("%s", dashes(tableWidth));
		};

		if (ctx.wideOutput) {

			auto& c = ctx.colors;
			auto line1 = [&]{
				ctx.log.error(
					"%s   %s   %s",
					dashes(lengthP),
					dashes(lengthF),
					dashes(lengthNL)
				);
			};
			line1();
			ctx.log.error(
				"%*s   %*s   %*s",
				(int)(-lengthP),  titleP.cp(),
				(int)(-lengthF),  titleF.cp(),
				(int)(-lengthNL),  titleNL.cp()
			);
			line1();
			bool colorP;
			bool colorF;
			for (Package* p : packagesSorted) {
				colorP = true;
				for (File* f : packages[p]) {
					colorF = true;
					for (auto& nl : neededLibsSorted[f]) {
						char pNameVer[200];
						if (p != nullptr) {
							snprintf(pNameVer, sizeof(pNameVer), "%s %s", p->name.cp(), p->version.cp());
						}

						ctx.log.error(
							"%s%*s%s   %s/%*s%s   %*s",

							colorP ? c.white.cp() : "",
							(int)(-lengthP),  p != nullptr ? pNameVer : unassignedP.cp(),
							colorP ? c.off.cp() : "",

							colorF ? c.white.cp() : "",
							(int)(-(lengthF - 1)),  f->path1.cp(),
							colorF ? c.off.cp() : "",

							(int)(-lengthNL),  nl.cp()
						);
						colorP = false;
						colorF = false;
					} // for (size_t i ...)
				} // for (File* f ...)
			} // for (Package* p ...)
			line2();

		} else { // if (ctx.wideOutput)

			for (Package* p : packagesSorted) {
				if (p != nullptr) {
					ctx.log.error("Package: %s %s", p->name.cp(), p->version.cp());
				} else {
					ctx.log.error("%s", unassignedP.cp());
				}
				for (File* f : packages[p]) {
					ctx.log.error("    File: /%s", f->path1.cp());
					for (auto& nl : neededLibsSorted[f]) {
						ctx.log.error("        Lib: %s", nl.cp());
					}
				}
			}
		}

		{
			auto numFiles = data.uniqueFilesByPath1.size();
			ctx.log.error(
				"Total %lu problematic file(s): %lu in %lu package(s) + %lu unassigned.",
				ulong{numFiles}, ulong{numFiles - numUnassignedFiles}, ulong{packages.size()}, ulong{numUnassignedFiles}
			);
		}
		if (ctx.wideOutput) {
			line2();
		}
	} // dumpErrors()
}
