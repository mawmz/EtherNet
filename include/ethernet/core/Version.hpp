// Created by block on 2/10/2023.

#pragma once

#include <string_view>

#include "fmt/core.h"
#include <skylaunch/utils/cpputils.hpp>
#include <ethernet/core/Utils.hpp>
#include <ethernet/core/Logger.hpp>

#include <engine/xc2/ml/ProcDesktop.hpp>

namespace ethernet::core::version {

	enum class GameType {
		Invalid,
		SpaceTravel = (1 << 0),
		BF2         = (1 << 1),
		IRA         = (1 << 2),
		BFSW        = (1 << 3),
		BF3         = (1 << 4),
		// multibuild psuedo types
		BF2_IRA = BF2 | IRA
	};

	struct SemVer {
		std::uint8_t major {};
		std::uint8_t minor {};
		std::uint8_t patch {};

		static const SemVer v1_0_0;
		static const SemVer v1_0_1;
		static const SemVer v1_0_2;
		static const SemVer v1_1_0;
		static const SemVer v1_1_1;
		static const SemVer v1_1_2;
		static const SemVer v1_2_0;
		static const SemVer v1_2_1;
		static const SemVer v1_3_0;
		static const SemVer v1_3_1;
		static const SemVer v1_4_0;
		static const SemVer v1_4_1;
		static const SemVer v1_5_0;
		static const SemVer v1_5_1;
		static const SemVer v1_5_2;
		static const SemVer v2_0_0;
		static const SemVer v2_0_1;
		static const SemVer v2_0_2;
		static const SemVer v2_1_0;
		static const SemVer v2_1_1;
		static const SemVer v2_2_0;

		inline bool IsValid() const {
			return major != 0 && minor != 0 && patch != 0;
		}
		[[nodiscard]] inline unsigned int AsInteger() const {
			return (major << (sizeof(major) + sizeof(minor))) | (minor << (sizeof(minor))) | patch;
		}

		inline bool operator==(const SemVer& other) const { return this->AsInteger() == other.AsInteger(); }
		inline bool operator!=(const SemVer& other) const { return !(*this == other); }

		inline bool operator<(const SemVer& other) const { return this->AsInteger() < other.AsInteger(); }
		inline bool operator>(const SemVer& other) const { return other < *this; }
		inline bool operator<=(const SemVer& other) const { return !(other > *this); }
		inline bool operator>=(const SemVer& other) const { return !(*this < other); }

/*#define TEST_SEMVER(x) g_Logger->LogInfo(#x " : {}", (x));

		static void Test() {
			TEST_SEMVER(v1_0_0 == v2_0_0);
			TEST_SEMVER(v1_1_0 == v2_0_0);
			TEST_SEMVER(v2_0_0 == v2_0_0);
			TEST_SEMVER(v2_1_0 == v2_0_0);

			TEST_SEMVER(v1_0_0 != v2_0_0);
			TEST_SEMVER(v1_1_0 != v2_0_0);
			TEST_SEMVER(v2_0_0 != v2_0_0);
			TEST_SEMVER(v2_1_0 != v2_0_0);

			TEST_SEMVER(v1_0_0 < v2_0_0);
			TEST_SEMVER(v1_1_0 < v2_0_0);
			TEST_SEMVER(v2_0_0 < v2_0_0);
			TEST_SEMVER(v2_1_0 < v2_0_0);

			TEST_SEMVER(v1_0_0 > v2_0_0);
			TEST_SEMVER(v1_1_0 > v2_0_0);
			TEST_SEMVER(v2_0_0 > v2_0_0);
			TEST_SEMVER(v2_1_0 > v2_0_0);

			TEST_SEMVER(v1_0_0 <= v2_0_0);
			TEST_SEMVER(v1_1_0 <= v2_0_0);
			TEST_SEMVER(v2_0_0 <= v2_0_0);
			TEST_SEMVER(v2_1_0 <= v2_0_0);

			TEST_SEMVER(v1_0_0 >= v2_0_0);
			TEST_SEMVER(v1_1_0 >= v2_0_0);
			TEST_SEMVER(v2_0_0 >= v2_0_0);
			TEST_SEMVER(v2_1_0 >= v2_0_0);
		}

#undef TEST_SEMVER*/
	};

	const char* BuildGitVersion();
	const char* BuildTimestamp();

	// returns a string like "EtherNet 1234567~"
	const char* EtherNetVersion();
	// returns a string like "EtherNet 1234567~ (debug) [???]"
	const char* EtherNetFullVersion();

	const GameType BuildGame =
#if ETHERNET_CODENAME(bf2)
	GameType::BF2;
#elif ETHERNET_CODENAME(ira)
	GameType::IRA;
#elif ETHERNET_CODENAME(bfsw)
	GameType::BFSW;
#elif ETHERNET_CODENAME(bf3)
	GameType::BF3;
#elif ETHERNET_CODENAME(bf2_ira)
	GameType::BF2_IRA;
#else
	GameType::Invalid;
#endif
	const bool BuildIsDebug =
#if _DEBUG
	true;
#else
	false;
#endif

	unsigned long RuntimeProgramID();
	GameType RuntimeGame();
	SemVer RuntimeVersion();
#if !ETHERNET_CODENAME(bf3)
	inline const char* RuntimeBuildRevision() { return ml::ProcDesktop::getBuildRevision()->buffer; }
#else
	inline const char* RuntimeBuildRevision() {
		// ml::_dsk::s_revision
		if (RuntimeVersion() == SemVer::v2_0_0)
			return reinterpret_cast<mm::mtl::FixStr<128>*>(skylaunch::utils::AddrFromBase(0x7101b6f500))->buffer;
		else if (RuntimeVersion() == SemVer::v2_1_0 || RuntimeVersion() == SemVer::v2_1_1 || RuntimeVersion() == SemVer::v2_2_0)
			return reinterpret_cast<mm::mtl::FixStr<128>*>(skylaunch::utils::AddrFromBase(0x7101b70500))->buffer;

		return nullptr;
	}
#endif

} // namespace ethernet::core::version

template<>
struct fmt::formatter<ethernet::core::version::GameType> : fmt::formatter<std::string_view> {
	enum class ParseType {
		Default,
		Shorter,
		Codename
	};
	ParseType parsetype = ParseType::Default;

	constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin()) {
		auto it = ctx.begin(), end = ctx.end();

		if (it != end) {
			// if there's literally any format
			if (*it == 's' || *it == 'S')
				parsetype = ParseType::Shorter;
			else if (*it == 'c' || *it == 'C')
				parsetype = ParseType::Codename;
		}

		return ++it;
	}

	template<typename FormatContext>
	inline auto format(ethernet::core::version::GameType type, FormatContext& ctx) {
		std::string_view name;
		using enum ethernet::core::version::GameType;

		// clang-format off
		if (parsetype == ParseType::Default) {
			switch(type) {
				case SpaceTravel: name = "Xenoblade X"; break;
				case BF2: name = "Xenoblade 2"; break;
				case IRA: name = "Xenoblade 2: Torna"; break;
				case BFSW: name = "Xenoblade: Definitive Edition"; break;
				case BF3: name = "Xenoblade 3"; break;
				case BF2_IRA: name = "Xenoblade 2/Torna"; break;
				default: name = "Invalid game"; break;
			}
		}
		else if (parsetype == ParseType::Shorter) {
			switch(type) {
				case SpaceTravel: name = "XBX"; break;
				case BF2: name = "XB2"; break;
				case IRA: name = "Torna"; break;
				case BFSW: name = "XBDE"; break;
				case BF3: name = "XB3"; break;
				case BF2_IRA: name = "XB2/Torna"; break;
				default: name = "Invalid"; break;
			}
		}
		else if (parsetype == ParseType::Codename) {
			switch(type) {
				case SpaceTravel: name = "SpaceTravel"; break;
				case BF2: name = "bf2"; break;
				case IRA: name = "ira"; break;
				case BFSW: name = "bfsw"; break;
				case BF3: name = "bf3"; break;
				case BF2_IRA: name = "bf2/ira"; break;
				default: name = "???"; break;
			}
		}
		// clang-format on

		return formatter<std::string_view>::format(name, ctx);
	}
};

template<>
struct fmt::formatter<ethernet::core::version::SemVer> : fmt::formatter<std::string> {
	template<typename FormatContext>
	inline auto format(const ethernet::core::version::SemVer& semver, FormatContext& ctx) {
		return fmt::format_to(ctx.out(), "{}.{}.{}", semver.major, semver.minor, semver.patch);
	}
};