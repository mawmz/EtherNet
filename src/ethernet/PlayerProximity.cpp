#include <ethernet/core/UpdatableModule.hpp>
#include <ethernet/LocalPlayers.hpp>
#include <ethernet/FieldRecovery.hpp>
#include <engine/xc2/gf/Manager.hpp>
#include <engine/xc2/gf/Party.hpp>
#include <skylaunch/hookng/Hooks.hpp>
#include <cmath>
#include <cstring>

namespace ethernet {
namespace {
struct Position { float x, y, z; };
template<class T> T Read(const void* p, std::size_t offset) {
    T value;
    std::memcpy(&value, static_cast<const char*>(p) + offset, sizeof(value));
    return value;
}
enum Function { CollectionPop, CollectionIcon, JumpIcon, JumpPop, JumpDepop,
                WarpPop, WarpDepop, WarpIcon, TreasurePop, TreasureDepop,
                Salvage, MapPop, MapDepop, Interest, FunctionCount };
std::uintptr_t Addresses[FunctionCount]{};
struct Query {
    const Position* center{};
    float horizontalSquared{}, verticalSquared{};
    bool cylinder{};
    std::uintptr_t caller{}, secondCaller{};
    bool secondEndpoint{};
};
Query Current{};
bool Installed{};
struct ScopedQuery {
    Query previous = Current;
    explicit ScopedQuery(Query query) { Current = query; }
    ~ScopedQuery() { Current = previous; }
};
float HorizontalSquared(Position p, Position c) {
    return (p.x-c.x)*(p.x-c.x) + (p.z-c.z)*(p.z-c.z);
}
float VerticalSquared(Position p, Position c) { return (p.y-c.y)*(p.y-c.y); }

struct InterestHook : skylaunch::hook::Trampoline<InterestHook> {
    static const Position* Hook() {
        const auto caller = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
        const auto* native = Orig();
        // Restrict the override to the audited radius callsites, not all nested
        // gimmick work, event positioning, world streaming or camera interest.
        if (!Installed || !native || !Current.center ||
            (caller != Current.caller && caller != Current.secondCaller) ||
            ethernet::core::version::RuntimeGame() == ethernet::core::version::GameType::IRA) return native;
        // A P2-started sequence must not despawn its own object when native
        // control locks. Original gimmick conditions still decide eligibility.
        const auto actor = gf::GfGameParty::getHandleMover(1);
        if (!IsPlayerTwoBound(actor) || IsFieldRecovering(actor)) return native;
        gf::GfObjAcc access(actor);
        const auto* second = reinterpret_cast<const Position*>(access.getWorldTransform());
        if (!second || !std::isfinite(second->x) || !std::isfinite(second->y) ||
            !std::isfinite(second->z)) return native;
        // Warp has two endpoint icons. Its second position is a Vec3 at +0x10.
        const auto* center = Current.secondEndpoint && caller == Current.secondCaller ?
            reinterpret_cast<const Position*>(reinterpret_cast<const char*>(Current.center) + 0x10) :
            Current.center;
        const auto p1h = HorizontalSquared(*native, *center);
        const auto p2h = HorizontalSquared(*second, *center);
        const auto p1v = VerticalSquared(*native, *center);
        const auto p2v = VerticalSquared(*second, *center);
        if (Current.cylinder) {
            // Keep a cylinder loaded when either player meets BOTH limits.
            // Euclidean-nearest alone is incorrect on vertically separated terrain.
            const bool p1Inside = p1h <= Current.horizontalSquared && p1v <= Current.verticalSquared;
            const bool p2Inside = p2h <= Current.horizontalSquared && p2v <= Current.verticalSquared;
            return !p1Inside && p2Inside ? second : native;
        }
        return p2h + p2v < p1h + p1v ? second : native;
    }
};

struct CollectionPopHook : skylaunch::hook::Trampoline<CollectionPopHook> {
    static bool Hook(const void* bdat, const void* gimmick) {
        ScopedQuery query({Read<const Position*>(gimmick, 0x10), 0, 0, false,
                           Addresses[CollectionPop] + 0x84});
        return Orig(bdat, gimmick);
    }
};
struct CollectionIconHook : skylaunch::hook::Trampoline<CollectionIconHook> {
    static void Hook(void* gimmick, float delta) {
        ScopedQuery query({Read<const Position*>(gimmick, 0x10), 0, 0, false,
                           Addresses[CollectionIcon] + 0x154});
        Orig(gimmick, delta);
    }
};
struct JumpIconHook : skylaunch::hook::Trampoline<JumpIconHook> {
    static void Hook(void* gimmick, float delta) {
        ScopedQuery query({Read<const Position*>(gimmick, 0x28), 0, 0, false,
                           Addresses[JumpIcon] + 0x188});
        Orig(gimmick, delta);
    }
};
template<Function Id, std::size_t ThresholdOffset, std::size_t ReturnOffset>
struct JumpRangeHook : skylaunch::hook::Trampoline<JumpRangeHook<Id, ThresholdOffset, ReturnOffset>> {
    static bool Hook(const void* gimmick) {
        ScopedQuery query({Read<const Position*>(gimmick, 0x28),
            Read<float>(gimmick, ThresholdOffset), Read<float>(gimmick, ThresholdOffset + 4),
            true, Addresses[Id] + ReturnOffset});
        return JumpRangeHook::Orig(gimmick);
    }
};
using JumpPopHook = JumpRangeHook<JumpPop, 0x60, 0x7c>;
using JumpDepopHook = JumpRangeHook<JumpDepop, 0x68, 0x7c>;
template<Function Id, std::size_t ThresholdOffset, std::size_t ReturnOffset>
struct WarpRangeHook : skylaunch::hook::Trampoline<WarpRangeHook<Id, ThresholdOffset, ReturnOffset>> {
    static bool Hook(const void* bdat, const Position* center) {
        ScopedQuery query({center, Read<float>(bdat, ThresholdOffset),
            Read<float>(bdat, ThresholdOffset + 4), true, Addresses[Id] + ReturnOffset});
        return WarpRangeHook::Orig(bdat, center);
    }
};
using WarpPopHook = WarpRangeHook<WarpPop, 8, 0x94>;
using WarpDepopHook = WarpRangeHook<WarpDepop, 0x10, 0x90>;
struct WarpIconHook : skylaunch::hook::Trampoline<WarpIconHook> {
    static void Hook(void* gimmick, float delta) {
        ScopedQuery query({Read<const Position*>(gimmick, 0x28), 0, 0, false,
                           Addresses[WarpIcon] + 0x524, Addresses[WarpIcon] + 0x5b0, true});
        Orig(gimmick, delta);
    }
};
template<Function Id, std::size_t ReturnOffset>
struct TreasureRangeHook : skylaunch::hook::Trampoline<TreasureRangeHook<Id, ReturnOffset>> {
    static bool Hook(const void* bdat, const Position* center) {
        ScopedQuery query({center, 0, 0, false, Addresses[Id] + ReturnOffset});
        return TreasureRangeHook::Orig(bdat, center);
    }
};
using TreasurePopHook = TreasureRangeHook<TreasurePop, 0x88>;
using TreasureDepopHook = TreasureRangeHook<TreasureDepop, 0x84>;
struct SalvageHook : skylaunch::hook::Trampoline<SalvageHook> {
    static void Hook(void* gimmick, float delta) {
        ScopedQuery query({Read<const Position*>(gimmick, 0x10), 0, 0, false,
                           Addresses[Salvage] + 0x9c, Addresses[Salvage] + 0x300});
        Orig(gimmick, delta);
    }
};
template<Function Id, std::size_t ReturnOffset>
struct MapRangeHook : skylaunch::hook::Trampoline<MapRangeHook<Id, ReturnOffset>> {
    static bool Hook(const void* gimmick) {
        ScopedQuery query({Read<const Position*>(gimmick, 0x10), 0, 0, false,
                           Addresses[Id] + ReturnOffset});
        return MapRangeHook::Orig(gimmick);
    }
};
using MapPopHook = MapRangeHook<MapPop, 0xfc>;
using MapDepopHook = MapRangeHook<MapDepop, 0xd0>;

struct PlayerProximity : ethernet::core::UpdatableModule {
    void Initialize() override {
        UpdatableModule::Initialize();
        if (ethernet::core::version::RuntimeGame() != ethernet::core::version::GameType::BF2) return;
        const char* names[FunctionCount] = {
            "_ZNK3gmk13GmkCollection8BdatInfo16isPopCondMatchedERKNS_8AGmkBaseE",
            "_ZN3gmk13GmkCollection17seqUpdatePopDepopEf",
            "_ZN3gmk7GmkJump6updateEf",
            "_ZNK3gmk7GmkJump13canPopGobInstEv",
            "_ZNK3gmk7GmkJump15canDepopGobInstEv",
            "_ZNK3gmk7GmkWarp8BdatInfo16isPopCondMatchedERKN2mm4Vec3E",
            "_ZNK3gmk7GmkWarp8BdatInfo18isDepopCondMatchedERKN2mm4Vec3E",
            "_ZN3gmk7GmkWarp6updateEf",
            "_ZNK3gmk7GmkTbox8BdatInfo16isPopCondMatchedERKN2mm4Vec3E",
            "_ZNK3gmk7GmkTbox8BdatInfo18isDepopCondMatchedERKN2mm4Vec3E",
            "_ZN3gmk10GmkSalvage17seqUpdatePopDepopEf",
            "_ZNK3gmk13GmkMapGimmick13canPopGobInstEv",
            "_ZNK3gmk13GmkMapGimmick15canDepopGobInstEv",
            "_ZN3gmk14getInterestPosEv",
        };
        for (unsigned i = 0; i < FunctionCount; ++i) {
            Addresses[i] = skylaunch::hook::detail::ResolveSymbolBase(names[i]);
            if (!Addresses[i] || Addresses[i] == skylaunch::hook::INVALID_FUNCTION_PTR) {
                ethernet::core::g_Logger->LogError("EtherNet proximity: missing {}", names[i]);
                return;
            }
        }
        CollectionPopHook::HookAt(Addresses[CollectionPop]);
        CollectionIconHook::HookAt(Addresses[CollectionIcon]);
        JumpIconHook::HookAt(Addresses[JumpIcon]);
        JumpPopHook::HookAt(Addresses[JumpPop]);
        JumpDepopHook::HookAt(Addresses[JumpDepop]);
        WarpPopHook::HookAt(Addresses[WarpPop]);
        WarpDepopHook::HookAt(Addresses[WarpDepop]);
        WarpIconHook::HookAt(Addresses[WarpIcon]);
        TreasurePopHook::HookAt(Addresses[TreasurePop]);
        TreasureDepopHook::HookAt(Addresses[TreasureDepop]);
        SalvageHook::HookAt(Addresses[Salvage]);
        MapPopHook::HookAt(Addresses[MapPop]);
        MapDepopHook::HookAt(Addresses[MapDepop]);
        InterestHook::HookAt(Addresses[Interest]);
        Installed = CollectionPopHook::HasApplied() && CollectionIconHook::HasApplied() &&
            JumpIconHook::HasApplied() && JumpPopHook::HasApplied() && JumpDepopHook::HasApplied() &&
            WarpPopHook::HasApplied() && WarpDepopHook::HasApplied() && WarpIconHook::HasApplied() &&
            TreasurePopHook::HasApplied() && TreasureDepopHook::HasApplied() && SalvageHook::HasApplied() &&
            MapPopHook::HasApplied() && MapDepopHook::HasApplied() &&
            InterestHook::HasApplied();
        if (Installed) ethernet::core::g_Logger->LogInfo("EtherNet P2 native interaction proximity enabled");
    }
};
ETHERNET_REGISTER_MODULE(PlayerProximity);
} // namespace
} // namespace ethernet
