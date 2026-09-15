#pragma once

#include <engine/xc2/mtl/Allocator.hpp>
#include <engine/xc2/mtl/MemoryInfo.hpp>
#include <ethernet/core/Version.hpp>

namespace mtl {

	class MemManager {
	   public:
		static bool getMemoryInfo(mtl::ALLOC_HANDLE, mtl::MemoryInfo*);

		// ethernet::core
		/// `mtl::MemoryInfo` accessor compatible with all games
		static bool GET_MEMORY_INFO(mtl::ALLOC_HANDLE handle, mtl::MemoryInfo* out) {
#if !ETHERNET_CODENAME(bf3)
			return getMemoryInfo(handle, out);
#else
			uintptr_t getMemoryInfo;
			if(ethernet::core::version::RuntimeVersion() == ethernet::core::version::SemVer::v2_0_0)
				getMemoryInfo = skylaunch::utils::AddrFromBase(0x710128a670);
			else if(ethernet::core::version::RuntimeVersion() == ethernet::core::version::SemVer::v2_1_0)
				getMemoryInfo = skylaunch::utils::AddrFromBase(0x710128a9a0);
			else if(ethernet::core::version::RuntimeVersion() == ethernet::core::version::SemVer::v2_1_1)
				getMemoryInfo = skylaunch::utils::AddrFromBase(0x710128a9e0);
			else if(ethernet::core::version::RuntimeVersion() == ethernet::core::version::SemVer::v2_2_0)
				getMemoryInfo = skylaunch::utils::AddrFromBase(0x710128b550);

			return ((bool (*)(mtl::ALLOC_HANDLE, mtl::MemoryInfo*))getMemoryInfo)(handle, out);
#endif
		}
	};

} // namespace mtl