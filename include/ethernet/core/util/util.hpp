#ifndef STUFF_UTIL
#define STUFF_UTIL

#include <ethernet/core/Logger.hpp>
#include <type_traits>

#include <nn/ro.h>
#include <skylaunch/inlinehook/And64InlineHook.hpp>
#include <skylaunch/nx/arm/counter.h>

namespace util {

#define _STRINGIFY(x) #x
#define STRINGIFY(x) _STRINGIFY(x)

#if NOLOG
	#define LOG(...)
#else
	#define LOG(fmt, ...) \
		::ethernet::core::g_Logger->LogDebug("[{}]: " fmt, __PRETTY_FUNCTION__ __VA_OPT__(, ) __VA_ARGS__);
#endif

} // namespace util

#endif
