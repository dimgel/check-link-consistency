#pragma once

#include <gelf.h>
#include <filesystem>
#include <libelf.h>
#include "data.h"
#include "util/ThreadPool.h"


namespace dimgel {
	class FilesCollector;


	class ELFInspector final {
		Context& ctx;
		Data& data;

		void processOne_impl_scanDynamicSection(Elf* e, File& f, GElf_Shdr& hdr, Elf_Data* data, std::function<void(SearchPath)> scanAdditionalDir);
		void processOne_impl(Elf* e, File& f, bool fromArchive, std::function<void(SearchPath)> scanAdditionalDir);

	public:
		ELFInspector(Context& ctx, Data& data);

		// Param `onRunPath` is called on each entry of DT_RPATH and DT_RUNPATH (if that entry is existing directory).
		void processOne_file(File& f, std::function<void(SearchPath)> scanAdditionalDir);
		void processOne_fromArchive(File& f, char* buf, size_t size);
	};
}

