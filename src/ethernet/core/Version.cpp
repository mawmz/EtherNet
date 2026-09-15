// Created by block on 2/10/2023.

#include <ethernet/core/Version.hpp>

#include <nn/oe.h>
#include <skylaunch/nx/kernel/svc.h>

namespace ethernet::core::version {
	static constexpr const char* InternalVersion = "v0.3.1-save-loader";

	const char* BuildGitVersion() {
		return InternalVersion;
	}
	const char* BuildTimestamp() {
		return __DATE__ " " __TIME__;
	}

	char EtherNetVersionBuf[128];
	char EtherNetFullVersionBuf[128];

	const char* EtherNetVersion() {
		if (EtherNetVersionBuf[0] == 0)
			sprintf(&EtherNetVersionBuf[0], "EtherNet %s", version::BuildGitVersion());
		return &EtherNetVersionBuf[0];
	}
	const char* EtherNetFullVersion() {
		if (EtherNetFullVersionBuf[0] == 0)
			sprintf(&EtherNetFullVersionBuf[0], "EtherNet %s%s [%s]", version::BuildGitVersion(), version::BuildIsDebug ? " (debug)" : "", ETHERNET_CODENAME_STR);
		return &EtherNetFullVersionBuf[0];
	}

	unsigned long RuntimeProgramID() {
		static unsigned long progid = 0;

		if (progid != 0)
			return progid;

		if (R_FAILED(svcGetInfo(&progid, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0)))
			progid = 0;

		return progid;
	}

	GameType RuntimeGame() {
		static GameType actualGame = GameType::Invalid;

		if (actualGame != GameType::Invalid)
			return actualGame;

		switch(RuntimeProgramID()) {
			case 0x0100E95004038000:
			case 0x0100F3400332C000: // JP version
				actualGame = GameType::BF2;
				break;
			case 0x0100C9F009F7A000:
				actualGame = GameType::IRA;
				break;
			case 0x0100FF500E34A000:
				actualGame = GameType::BFSW;
				break;
			case 0x010074F013262000:
				actualGame = GameType::BF3;
				break;
			default:
				actualGame = GameType::Invalid;
				break;
		}

		return actualGame;
	}

	SemVer RuntimeVersion() {
		static SemVer semver {};

		if (semver.IsValid())
			return semver;

		nn::oe::DisplayVersion nnVer {};
		nn::oe::GetDisplayVersion(&nnVer);

		if (std::sscanf(nnVer.name, "%d.%d.%d", &semver.major, &semver.minor, &semver.patch) == 3)
			return semver;

		return {0,0,0};
	}

	const SemVer SemVer::v1_0_0 = {1,0,0};
	const SemVer SemVer::v1_0_1 = {1,0,1};
	const SemVer SemVer::v1_0_2 = {1,0,2};
	const SemVer SemVer::v1_1_0 = {1,1,0};
	const SemVer SemVer::v1_1_1 = {1,1,1};
	const SemVer SemVer::v1_1_2 = {1,1,2};
	const SemVer SemVer::v1_2_0 = {1,2,0};
	const SemVer SemVer::v1_2_1 = {1,2,1};
	const SemVer SemVer::v1_3_0 = {1,3,0};
	const SemVer SemVer::v1_3_1 = {1,3,1};
	const SemVer SemVer::v1_4_0 = {1,4,0};
	const SemVer SemVer::v1_4_1 = {1,4,1};
	const SemVer SemVer::v1_5_0 = {1,5,0};
	const SemVer SemVer::v1_5_1 = {1,5,1};
	const SemVer SemVer::v1_5_2 = {1,5,2};
	const SemVer SemVer::v2_0_0 = {2,0,0};
	const SemVer SemVer::v2_0_1 = {2,0,1};
	const SemVer SemVer::v2_0_2 = {2,0,2};
	const SemVer SemVer::v2_1_0 = {2,1,0};
	const SemVer SemVer::v2_1_1 = {2,1,1};
	const SemVer SemVer::v2_2_0 = {2,2,0};

} // namespace ethernet::core::version
