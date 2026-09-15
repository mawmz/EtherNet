#include <ethernet/core/UpdatableModule.hpp>
#include <ethernet/LocalPlayers.hpp>
#include <ethernet/FieldRecovery.hpp>
#include <engine/xc2/gf/Manager.hpp>
#include <engine/xc2/gf/Party.hpp>
#include <skylaunch/hookng/Hooks.hpp>
#include <array>
#include <cstring>

namespace ethernet {
namespace {
using Handle = gf::GF_OBJ_HANDLE*;
constexpr std::uintptr_t Invalid = ~std::uintptr_t{};
Handle NoTarget() { return reinterpret_cast<Handle>(Invalid); }
bool Valid(Handle h) { return h && h != NoTarget(); }
template<class T> T Read(const void* p, std::size_t offset) {
    T value;
    std::memcpy(&value, static_cast<const char*>(p) + offset, sizeof(value));
    return value;
}
template<class T> void Write(void* p, std::size_t offset, T value) {
    std::memcpy(static_cast<char*>(p) + offset, &value, sizeof(value));
}

// XC2 2.1.0 GfSearchParam / GFSEARCH_INFO share their first 0x68 bytes.
// Keep the retail field-search mask, radius and sorting parameters. Only its
// origin and excluded actor differ. No borrowed transform reference is kept.
alignas(16) std::array<char, 0x68> SearchTemplate{};
bool TemplateReady{}, Installed{};
std::uintptr_t Search = Invalid;
Handle SearchActor = NoTarget();
std::uint64_t SearchGeneration{};
std::array<Handle, 2> AccessTargets{NoTarget(), NoTarget()};
std::array<Handle, 2> AccessActors{NoTarget(), NoTarget()};
Handle PublishedTarget = NoTarget();
unsigned PromptOwner{};
int AccessContext = -1;
void* TargetContextBehavior{};
Handle TargetContextHandle = NoTarget();

using CreateSearchFn = std::uintptr_t(*)(const void*);
using SearchInfoFn = void*(*)(std::uintptr_t);
using DestroySearchFn = void(*)(std::uintptr_t);
using ParamIndexFn = unsigned(*)(void*, void*);
using ParamFn = const void*(*)(void*, unsigned);
using RangeFn = bool(*)(void*, const void*, Handle);
using VisibleFn = bool(*)(Handle);
CreateSearchFn CreateSearch{};
SearchInfoFn SearchInfo{}, SearchList{};
DestroySearchFn DestroySearch{};
ParamIndexFn ParamIndex{};
ParamFn AccessParam{};
RangeFn ActiveRange{};
VisibleFn IsVisible{};

bool FieldGate() {
    return Installed && !ethernet::core::IsSceneTransitionActive() &&
        gf::GfGameManager::isField() && gf::GfGameManager::isControlFree() &&
        !gf::GfGameManager::isBattle() &&
        ethernet::core::version::RuntimeGame() != ethernet::core::version::GameType::IRA;
}

void ClearTargets() {
    AccessTargets = {NoTarget(), NoTarget()};
    AccessActors = {NoTarget(), NoTarget()};
    PublishedTarget = NoTarget();
    PromptOwner = 0;
}
void ReleaseSearch() {
    if (Search != Invalid) DestroySearch(Search);
    Search = Invalid;
    SearchActor = NoTarget();
    ClearTargets();
}

struct CreateTargetSearchHook : skylaunch::hook::Trampoline<CreateTargetSearchHook> {
    static void Hook(void* target) {
        Orig(target);
        ReleaseSearch();
        TemplateReady = false;
        const auto nativeSearch = Read<std::uintptr_t>(target, 0xa8);
        if (nativeSearch == Invalid) return;
        auto* info = SearchInfo(nativeSearch);
        if (!info || Read<std::uintptr_t>(info, 0x70) != nativeSearch) return;
        std::memcpy(SearchTemplate.data(), info, SearchTemplate.size());
        Write<void*>(SearchTemplate.data(), 0x48, nullptr);
        TemplateReady = true;
    }
};
struct DestroyTargetSearchHook : skylaunch::hook::Trampoline<DestroyTargetSearchHook> {
    static void Hook(void* target) {
        ReleaseSearch();
        TemplateReady = false;
        Orig(target);
    }
};
struct FinalizeSearchHook : skylaunch::hook::Trampoline<FinalizeSearchHook> {
    static void Hook(void* manager) {
        // Native finalize destroys every registered search, including ours.
        Search = Invalid;
        SearchActor = NoTarget();
        TemplateReady = false;
        ClearTargets();
        Orig(manager);
    }
};
struct SearchUpdateHook : skylaunch::hook::Trampoline<SearchUpdateHook> {
    static void Hook(const void* update) {
        const auto actor = gf::GfGameParty::getHandleMover(1);
        const auto generation = PlayerBindingGeneration();
        if (Search != Invalid && (actor != SearchActor || generation != SearchGeneration ||
                                  !IsPlayerTwoBound(actor)))
            ReleaseSearch();
        if (TemplateReady && Search == Invalid && FieldGate() && IsPlayerTwoBound(actor)) {
            auto param = SearchTemplate;
            Write<Handle>(param.data(), 0x40, actor); // Native actor-origin path.
            Write<Handle>(param.data(), 0x60, actor); // Exclude self.
            Search = CreateSearch(param.data());
            SearchActor = actor;
            SearchGeneration = generation;
        }
        Orig(update); // Both search results are produced by the same native pass.
    }
};

Handle FindAccessTarget(void* plugin, void* behavior) {
    if (Search == Invalid || !FieldGate() || !IsPlayerTwoBound(SearchActor)) return NoTarget();
    auto* info = SearchInfo(Search);
    if (!info || Read<std::uintptr_t>(info, 0x70) != Search) return NoTarget();
    auto* object = Read<void*>(behavior, 8);
    auto* property = Read<void*>(object, 0x60);
    const auto* param = AccessParam(plugin, ParamIndex(plugin, property));
    auto* list = SearchList(Search);
    if (!param || !list) return NoTarget();

    // Same nearest-first FixedList traversal as GfGameTarget::setTargetNearest.
    // Every candidate must also pass the native AccessPlugin eligibility query,
    // actor-relative horizontal/vertical range and target-specific radius offset.
    auto index = Read<std::uint16_t>(list, 10);
    for (unsigned count = 0; index && index <= 256 && count < 256; ++count) {
        auto* node = static_cast<char*>(list) + index * 0x10;
        auto* candidate = Read<void*>(node, 0);
        if (candidate) {
            const auto handle = Read<Handle>(candidate, 0);
            auto* target = Valid(handle) ? gf::GfObjUtil::getObj(handle) : nullptr;
            if (target && (Read<unsigned>(target, 0x110) & 4) &&
                (Read<unsigned char>(candidate, 0x2c) & 1) && IsVisible(handle) &&
                ActiveRange(property, param, handle))
                return handle;
        }
        index = Read<std::uint16_t>(node, 10);
    }
    return NoTarget();
}

struct ActorTargetHook : skylaunch::hook::Trampoline<ActorTargetHook> {
    static Handle Hook(const void* behavior) {
        if (behavior == TargetContextBehavior) return TargetContextHandle;
        return Orig(behavior);
    }
};

struct AccessCommandHook : skylaunch::hook::Trampoline<AccessCommandHook> {
    static std::uintptr_t Hook(Handle target) {
        // Per-actor plugins must not overwrite the single native prompt with
        // competing queued commands. Publish the selected actor's result below.
        if (AccessContext >= 0) return Invalid;
        return Orig(target);
    }
};

bool ActorStillValid(unsigned slot) {
    const auto actor = slot ? gf::GfGameParty::getHandleMover(1) :
                              gf::GfGameManager::getControlMover();
    return actor == AccessActors[slot] && Valid(actor) && !IsFieldRecovering(actor) &&
        (!slot || IsPlayerTwoBound(actor)) && Valid(AccessTargets[slot]) &&
        gf::GfObjUtil::getObj(AccessTargets[slot]);
}
void PublishPrompt() {
    const bool p1 = ActorStillValid(0), p2 = ActorStillValid(1);
    if ((PromptOwner == 0 && !p1) || (PromptOwner == 1 && !p2)) PromptOwner = p1 ? 0 : 1;
    const auto selected = (PromptOwner == 0 ? p1 : p2) ? AccessTargets[PromptOwner] : NoTarget();
    if (selected != PublishedTarget) {
        PublishedTarget = selected;
        AccessCommandHook::Orig(selected);
    }
}

int InvalidatePlugin(void* plugin) {
    auto* behavior = Read<void*>(plugin, 0x20);
    auto* object = behavior ? Read<void*>(behavior, 8) : nullptr;
    const auto actor = object ? Read<Handle>(object, 0xf8) : NoTarget();
    for (unsigned slot = 0; slot < 2; ++slot) {
        if (Valid(actor) && actor == AccessActors[slot]) {
            AccessTargets[slot] = NoTarget();
            return static_cast<int>(slot);
        }
    }
    return -1;
}
struct AccessDeactivateHook : skylaunch::hook::Trampoline<AccessDeactivateHook> {
    static void Hook(void* plugin) {
        const auto previous = AccessContext;
        const auto slot = InvalidatePlugin(plugin);
        AccessContext = FieldGate() ? slot : -1;
        Orig(plugin);
        AccessContext = previous;
        if (FieldGate()) PublishPrompt();
        else PublishedTarget = NoTarget();
    }
};
struct AccessCleanupHook : skylaunch::hook::Trampoline<AccessCleanupHook> {
    static void Hook(void* plugin, void* behavior) {
        const auto previous = AccessContext;
        const auto slot = InvalidatePlugin(plugin);
        AccessContext = FieldGate() ? slot : -1;
        Orig(plugin, behavior);
        AccessContext = previous;
        if (FieldGate()) PublishPrompt();
        else PublishedTarget = NoTarget();
    }
};

struct AccessUpdateHook : skylaunch::hook::Trampoline<AccessUpdateHook> {
    static void Hook(void* plugin, void* behavior, const void* update) {
        auto* object = Read<void*>(behavior, 8);
        const auto actor = object ? Read<Handle>(object, 0xf8) : NoTarget();
        if (IsFieldRecovering(actor)) { InvalidatePlugin(plugin); PublishPrompt(); return; }
        const bool p2 = actor == gf::GfGameParty::getHandleMover(1) && IsPlayerTwoBound(actor);
        const bool p1 = actor == gf::GfGameManager::getControlMover();
        if (!FieldGate() || (!p1 && !p2)) {
            Orig(plugin, behavior, update);
            // Native event/menu code may have changed the shared prompt.
            PublishedTarget = NoTarget();
            return;
        }

        const auto previousContext = AccessContext;
        auto* previousBehavior = TargetContextBehavior;
        const auto previousHandle = TargetContextHandle;
        AccessContext = p2 ? 1 : 0;
        if (p2) {
            TargetContextBehavior = behavior;
            TargetContextHandle = FindAccessTarget(plugin, behavior);
        }
        Orig(plugin, behavior, update); // Retain native query replies, access type and HFSM target.
        AccessTargets[p2 ? 1 : 0] = Read<Handle>(plugin, 0x10);
        AccessActors[p2 ? 1 : 0] = actor;
        TargetContextBehavior = previousBehavior;
        TargetContextHandle = previousHandle;
        AccessContext = previousContext;
        if (FieldGate()) PublishPrompt();
    }
};

struct DecideActionHook : skylaunch::hook::Trampoline<DecideActionHook> {
    static void Hook(const void* pad) {
        Orig(pad);
        if (!FieldGate()) return;
        auto* property = Read<void*>(pad, 0x40);
        if (!property || !(Read<std::uint64_t>(property, 0x68) & 1)) return;
        auto* object = Read<void*>(property, 8);
        const auto actor = object ? Read<Handle>(object, 0xf8) : NoTarget();
        for (unsigned slot = 0; slot < 2; ++slot) {
            if (actor == AccessActors[slot] && ActorStillValid(slot)) {
                PromptOwner = slot;
                PublishPrompt();
                break;
            }
        }
    }
};

struct PlayerInteractions : ethernet::core::UpdatableModule {
    void Initialize() override {
        UpdatableModule::Initialize();
        if (ethernet::core::version::RuntimeGame() != ethernet::core::version::GameType::BF2) return;
        struct Entry { const char* name; std::uintptr_t address{}; } entries[] = {
            {"_ZN2gf15GfGameSearchObj9createObjERKNS_13GfSearchParamE"},
            {"_ZN2gf15GfGameSearchObj7getInfoEPNS_13GF_SEARCH_OBJE"},
            {"_ZN2gf15GfGameSearchObj6getObjEPNS_13GF_SEARCH_OBJE"},
            {"_ZN2gf15GfGameSearchObj10destroyObjEPNS_13GF_SEARCH_OBJE"},
            {"_ZNK2gf2pc12AccessPlugin13getParamIndexERNS_15GfComPropertyPcE"},
            {"_ZNK2gf2pc12AccessPlugin8getParamEj"},
            {"_ZN2gf2pc13isActiveRangeERNS_15GfComPropertyPcERKNS_11AccessParamEPNS_13GF_OBJ_HANDLEE"},
            {"_ZN2gf13GfGameVisible9isVisibleEPNS_13GF_OBJ_HANDLEE"},
            {"_ZN2gf12GfGameTarget15createSearchObjEv"},
            {"_ZN2gf12GfGameTarget16destroySearchObjEv"},
            {"_ZN2gf15GfGameSearchObj8finalizeEv"},
            {"_ZN2gf15GfGameSearchObj6updateERKN2fw10UpdateInfoE"},
            {"_ZN2gf2pc9StateUtil9getTargetERKNS_15GfComBehaviorPcE"},
            {"_ZN2gf18SCOM_ACCESS_TARGET6createEPNS_13GF_OBJ_HANDLEE"},
            {"_ZN2gf2pc12AccessPlugin6updateERNS_15GfComBehaviorPcERKN2fw10UpdateInfoE"},
            {"_ZNK2gf2pc8PadField12decideActionEv"},
            {"_ZN2gf2pc12AccessPlugin12onDeactivateEv"},
            {"_ZN2gf2pc12AccessPlugin7cleanupERNS_15GfComBehaviorPcE"},
        };
        for (auto& entry : entries) {
            entry.address = skylaunch::hook::detail::ResolveSymbolBase(entry.name);
            if (!entry.address || entry.address == skylaunch::hook::INVALID_FUNCTION_PTR) {
                ethernet::core::g_Logger->LogError("EtherNet interactions: missing {}", entry.name);
                return;
            }
        }
        CreateSearch = reinterpret_cast<CreateSearchFn>(entries[0].address);
        SearchInfo = reinterpret_cast<SearchInfoFn>(entries[1].address);
        SearchList = reinterpret_cast<SearchInfoFn>(entries[2].address);
        DestroySearch = reinterpret_cast<DestroySearchFn>(entries[3].address);
        ParamIndex = reinterpret_cast<ParamIndexFn>(entries[4].address);
        AccessParam = reinterpret_cast<ParamFn>(entries[5].address);
        ActiveRange = reinterpret_cast<RangeFn>(entries[6].address);
        IsVisible = reinterpret_cast<VisibleFn>(entries[7].address);
        CreateTargetSearchHook::HookAt(entries[8].address);
        DestroyTargetSearchHook::HookAt(entries[9].address);
        FinalizeSearchHook::HookAt(entries[10].address);
        SearchUpdateHook::HookAt(entries[11].address);
        ActorTargetHook::HookAt(entries[12].address);
        AccessCommandHook::HookAt(entries[13].address);
        AccessUpdateHook::HookAt(entries[14].address);
        DecideActionHook::HookAt(entries[15].address);
        AccessDeactivateHook::HookAt(entries[16].address);
        AccessCleanupHook::HookAt(entries[17].address);
        Installed = CreateTargetSearchHook::HasApplied() && DestroyTargetSearchHook::HasApplied() &&
            FinalizeSearchHook::HasApplied() && SearchUpdateHook::HasApplied() &&
            ActorTargetHook::HasApplied() && AccessCommandHook::HasApplied() &&
            AccessUpdateHook::HasApplied() && DecideActionHook::HasApplied() &&
            AccessDeactivateHook::HasApplied() && AccessCleanupHook::HasApplied();
        if (Installed) ethernet::core::g_Logger->LogInfo("EtherNet P2 native interaction detection enabled");
        else ethernet::core::g_Logger->LogError("EtherNet interaction hook installation failed");
    }
    void OnSceneTransition() override { ClearTargets(); }
};
ETHERNET_REGISTER_MODULE(PlayerInteractions);
} // namespace
} // namespace ethernet
