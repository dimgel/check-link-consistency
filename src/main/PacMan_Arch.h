#pragma once

#include <regex>
#include "ELFInspector.h"
#include "PacMan.h"


namespace dimgel {
	class PacMan_Arch final : public PacMan {
		std::string installedInfoPath = "/var/lib/pacman/local";
		std::string archivesPath = "/var/cache/pacman/pkg/";
		std::string archivesURL = "file://" + archivesPath;
		std::regex rPacmanSWPLine {"^(\\S+) (\\S+)$"};
		std::regex rPkginfoSWPLine {"^(\\S+)\\s*=\\s*(\\S.*)$"};


		class ParseArchiveTask : public PacMan::ParseArchiveTask {
			PacMan_Arch& owner;
		public:
			ParseArchiveTask(PacMan_Arch& owner, alloc::String optDepName, alloc::String archiveName)
				: PacMan::ParseArchiveTask(owner, optDepName, archiveName), owner(owner) {}
			void impl(Package* p) final override;
		};


	protected:
		// Here installedPackageUniqueID is subdirectory name inside `installedInfoPath`.
		void iterateInstalledPackages(std::function<void(std::string installedPackageUniqueID)> f) override;
		parseInstalledPackage_Result parseInstalledPackage(const std::string& installedPackageUniqueID) override;

		virtual void downloadOptionalDependencies_impl() override;
		std::unique_ptr<PacMan::ParseArchiveTask> createParseArchiveTask(alloc::String optDepName, alloc::String archiveName) override {
			return std::make_unique<ParseArchiveTask>(*this, optDepName, archiveName);
		}

	public:
		PacMan_Arch(Context& ctx, Data& data, ELFInspector& elfInspector) : PacMan(ctx, data, elfInspector) {}
	};
}
