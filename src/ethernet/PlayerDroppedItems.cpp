#include <ethernet/core/UpdatableModule.hpp>
#include <ethernet/LocalPlayers.hpp>
#include <ethernet/FieldRecovery.hpp>
#include <engine/xc2/gf/Manager.hpp>
#include <engine/xc2/gf/Party.hpp>
#include <skylaunch/hookng/Hooks.hpp>

namespace ethernet {
namespace {
using Handle = gf::GF_OBJ_HANDLE*;
std::uintptr_t DropUpdateAddress{};
bool Installed{};
const void* PendingItem{};
Handle PendingCollector{};

struct CanGetItemHook : skylaunch::hook::Trampoline<CanGetItemHook> {
    static bool Hook(const void* item, Handle actor, float radiusBonus) {
        const auto caller = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
        // XC2 2.1.0 ordinary loot branch: first canGetItem returns to 0x35a114.
        // Healing pots already test the whole party; other callers stay native.
        if (!Installed || caller != DropUpdateAddress + 0x940)
            return !IsFieldRecovering(actor) && Orig(item, actor, radiusBonus);

        PendingItem = nullptr;
        PendingCollector = nullptr;
        if (!IsFieldRecovering(actor) && Orig(item, actor, radiusBonus)) return true; // P1 wins a simultaneous pickup.
        if (ethernet::core::version::RuntimeGame() == ethernet::core::version::GameType::IRA ||
            ethernet::core::IsSceneTransitionActive()) return false;
        const auto p2 = gf::GfGameParty::getHandleMover(1);
        if (p2 == actor || !IsPlayerTwoBound(p2) || IsFieldRecovering(p2) ||
            !Orig(item, p2, radiusBonus)) return false;

        // The immediately following native applyGetItem call consumes this.
        // No extra reward call or second update of the dropped object is added.
        PendingItem = item;
        PendingCollector = p2;
        return true;
    }
};

struct ApplyGetItemHook : skylaunch::hook::Trampoline<ApplyGetItemHook> {
    static void Hook(const void* item, Handle actor) {
        const auto caller = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
        if (Installed && caller == DropUpdateAddress + 0x950 && item == PendingItem) {
            const auto collector = PendingCollector;
            PendingItem = nullptr;
            PendingCollector = nullptr;
            if (!IsPlayerTwoBound(collector)) return;
            actor = collector;
        }
        // Native destruction, shared inventory/money, popup, sound and actor-
        // targeted pickup effect all run exactly once through the retail path.
        Orig(item, actor);
    }
};

struct PlayerDroppedItems : ethernet::core::UpdatableModule {
    void Initialize() override {
        UpdatableModule::Initialize();
        if (ethernet::core::version::RuntimeGame() != ethernet::core::version::GameType::BF2) return;
        const char* symbols[] = {
            "_ZN2gf14GfFobjDropitem6updateERKN2fw10UpdateInfoE",
            "_ZNK2gf14GfFobjDropitem10canGetItemEPNS_13GF_OBJ_HANDLEEf",
            "_ZNK2gf14GfFobjDropitem12applyGetItemEPNS_13GF_OBJ_HANDLEE",
        };
        std::uintptr_t addresses[3]{};
        for (unsigned i = 0; i < 3; ++i) {
            addresses[i] = skylaunch::hook::detail::ResolveSymbolBase(symbols[i]);
            if (!addresses[i] || addresses[i] == skylaunch::hook::INVALID_FUNCTION_PTR) {
                ethernet::core::g_Logger->LogError("EtherNet dropped items: missing {}", symbols[i]);
                return;
            }
        }
        DropUpdateAddress = addresses[0];
        CanGetItemHook::HookAt(addresses[1]);
        ApplyGetItemHook::HookAt(addresses[2]);
        Installed = CanGetItemHook::HasApplied() && ApplyGetItemHook::HasApplied();
        if (Installed) ethernet::core::g_Logger->LogInfo("EtherNet P2 dropped-item pickup enabled");
        else ethernet::core::g_Logger->LogError("EtherNet dropped-item hook installation failed");
    }
    void OnSceneTransition() override {
        PendingItem = nullptr;
        PendingCollector = nullptr;
    }
};
ETHERNET_REGISTER_MODULE(PlayerDroppedItems);
} // namespace
} // namespace ethernet
