#include <ethernet/core/UpdatableModule.hpp>
#include <ethernet/LocalPlayers.hpp>
#include <ethernet/FieldRecovery.hpp>
#include <engine/xc2/gf/Manager.hpp>
#include <skylaunch/hookng/Hooks.hpp>
#include <array>
#include <cstring>

namespace ethernet {
namespace {
using DriverFn = gf::GF_OBJ_HANDLE*(*)(unsigned, unsigned);
DriverFn GetDriver{};
bool Installed{}, InPhantomPass{}, CopiedResults{};
constexpr std::size_t ResultStride = 0x5020;
// Native collision results contain inline hits plus non-owning collider pointers.
// The third result stays empty: enabling a two-player volume must not admit AI P3.
alignas(16) std::array<unsigned char, ResultStride * 3> PlayerResults{};
const void* SourceResults{};

bool P2VolumesAllowed() {
    // Occupancy must remain stable during pauses/events. Dropping P2 while
    // control is locked creates a false leave/re-enter when control returns.
    // The native event/action code retains its own isPlayEventOK etc. gates.
    return Installed &&
        ethernet::core::version::RuntimeGame() != ethernet::core::version::GameType::IRA &&
        IsPlayerTwoBound(GetDriver(1, 0));
}

struct PhantomPassHook : skylaunch::hook::Trampoline<PhantomPassHook> {
    static void Hook(void* manager, float delta) {
        // The manager fills all three native collision results before invoking
        // each phantom. Copy the human-player results once, at the first volume.
        const bool previousPass = InPhantomPass;
        InPhantomPass = P2VolumesAllowed();
        CopiedResults = false;
        SourceResults = nullptr;
        Orig(manager, delta);
        InPhantomPass = previousPass;
        CopiedResults = false;
    }
};
struct PhantomUpdateHook : skylaunch::hook::Trampoline<PhantomUpdateHook> {
    static void Hook(void* phantom, const void* results) {
        auto* bytes = static_cast<unsigned char*>(phantom);
        // Native all-party volumes already have their own intended policy.
        if (!InPhantomPass || bytes[0x68]) { Orig(phantom, results); return; }
        if (!CopiedResults || SourceResults != results) {
            std::memcpy(PlayerResults.data(), results, ResultStride * 2);
            for (unsigned slot=0;slot<2;++slot)
                if (IsFieldPlayerRecovering(slot))
                    std::memset(PlayerResults.data()+slot*ResultStride,0,ResultStride);
            SourceResults = results;
            CopiedResults = true;
        }
        // Execute the original union/enter/leave state machine once. Retaining
        // it prevents duplicated cutscenes when both players enter one volume.
        bytes[0x68] = 1;
        Orig(phantom, PlayerResults.data());
        bytes[0x68] = 0;
    }
};

struct PlayerWorldDetection : ethernet::core::UpdatableModule {
    void Initialize() override {
        UpdatableModule::Initialize();
        if (ethernet::core::version::RuntimeGame() != ethernet::core::version::GameType::BF2) return;
        const char* names[] = {
            "_ZN2gf11GfGameParty15getHandleDriverEjNS_5PTPOSE",
            "_ZN3gmk17GmkPhantomManager6updateEf",
            "_ZN3gmk10GmkPhantom6updateERKN2mm3mtl10FixedArrayIN2fw16TColiCheckResultILj256EEELm3EEE",
        };
        std::uintptr_t addresses[3]{};
        for (unsigned i = 0; i < 3; ++i) {
            addresses[i] = skylaunch::hook::detail::ResolveSymbolBase(names[i]);
            if (!addresses[i] || addresses[i] == skylaunch::hook::INVALID_FUNCTION_PTR) {
                ethernet::core::g_Logger->LogError("EtherNet world detection: missing {}", names[i]);
                return;
            }
        }
        GetDriver = reinterpret_cast<DriverFn>(addresses[0]);
        PhantomPassHook::HookAt(addresses[1]);
        PhantomUpdateHook::HookAt(addresses[2]);
        Installed = PhantomPassHook::HasApplied() && PhantomUpdateHook::HasApplied();
        if (Installed) ethernet::core::g_Logger->LogInfo("EtherNet P2 native trigger volumes enabled");
    }
};
ETHERNET_REGISTER_MODULE(PlayerWorldDetection);
} // namespace
} // namespace ethernet
