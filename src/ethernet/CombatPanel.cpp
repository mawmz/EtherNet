#include <ethernet/core/UpdatableModule.hpp>
#include <ethernet/BattlePlayers.hpp>
#include <ethernet/BattleTargets.hpp>
#include <ethernet/LocalPlayers.hpp>
#include <engine/xc2/gf/Manager.hpp>
#include <engine/xc2/gf/Party.hpp>
#include <nn/os.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

namespace ethernet {
namespace {
// Effective XC2 1.5.1 ExeFS. Readouts use native getters. Debug kill requests
// cross to the game-update thread; rendering never mutates native actors.
template<class T> T Read(const void* base, std::size_t offset) {
    T result{};
    std::memcpy(&result, static_cast<const std::uint8_t*>(base) + offset, sizeof(result));
    return result;
}

struct DataParam { void* vtable{}; void* data{}; gf::GF_OBJ_HANDLE* handle{}; };
static_assert(sizeof(DataParam) == 0x18);
void (*ConstructData)(DataParam*, gf::GF_OBJ_HANDLE*){};
void (*DestroyData)(DataParam*){};
bool (*ValidData)(const DataParam*){};
const char* (*DataName)(const DataParam*){};
const char* (*ArtsName)(unsigned){};
gf::GF_OBJ_HANDLE* (*SelectedBlade)(unsigned, int){};
const void* (*GetParameter)(const void*, bool){};
const void* (*GetSlot)(const void*, int, bool, int){};
bool (*IsDead)(const void*, unsigned){};
void (*SetHP)(void*, unsigned){};
unsigned (*SpecialLevel)(gf::GF_OBJ_HANDLE*){};
const void* (*LookupActor)(const void*, gf::GF_OBJ_HANDLE*){};
void** BattleManagerSlot{};
bool Available{};

bool& CombatOverlayOpen() {
    return ethernet::core::g_Menu->WindowVisibility("combat_overlay");
}

struct NameCache {
    gf::GF_OBJ_HANDLE* handle{};
    void* object{};
    std::string name;
};
std::array<std::array<NameCache, 3>, 2> Names{};

std::string ObjectName(gf::GF_OBJ_HANDLE* handle, NameCache& cache) {
    if (!handle || handle == reinterpret_cast<gf::GF_OBJ_HANDLE*>(-1)) {
        cache = {};
        return "None";
    }
    auto* object = gf::GfObjUtil::getObj(handle);
    if (!object) { cache = {}; return "N/A"; }
    if (cache.handle != handle || cache.object != object) {
        cache = {handle, object, "N/A"};
        DataParam access{};
        ConstructData(&access, handle);
        if (ValidData(&access)) {
            const auto* name = DataName(&access);
            if (name && *name) cache.name = name; // owned copy, never retain native text
        }
        DestroyData(&access);
    }
    return cache.name;
}

struct ArtReadout {
    unsigned id{};
    std::string name;
    float charge{};
    bool valid{};
};
struct KillRequest {
    gf::GF_OBJ_HANDLE* handle{};
    void* object{};
    const void* actor{};
    std::uint64_t generation{};
};
struct PlayerReadout {
    KillRequest killTarget{};
    bool valid{};
    bool battleData{};
    bool engaged{};
    bool hasTarget{};
    bool downed{};
    bool downedValid{};
    std::string character;
    std::string blade;
    std::string target;
    std::array<ArtReadout, 3> arts{};
    unsigned specialLevel{};
    float specialCharge{};
    bool specialValid{};
    bool specialChargeValid{};
};
std::array<PlayerReadout, 2> Snapshot{};
std::array<KillRequest, 2> PendingKills{};
struct alignas(8) SnapshotMutexStorage { nn::os::MutexType value{}; } SnapshotMutex;
struct SnapshotLock {
    SnapshotLock() { nn::os::LockMutex(&SnapshotMutex.value); }
    ~SnapshotLock() { nn::os::UnlockMutex(&SnapshotMutex.value); }
};

void ResetSnapshot() {
    const SnapshotLock lock;
    Snapshot = {};
    PendingKills = {};
    Names = {};
}

void ProcessKills() {
    std::array<KillRequest, 2> requests;
    { const SnapshotLock lock; requests = PendingKills; PendingKills = {}; }
    if (!SetHP || ethernet::core::IsSceneTransitionActive() || !gf::GfGameManager::isBattle()) return;
    for (unsigned player = 0; player < requests.size(); ++player) {
        const auto& request = requests[player];
        if (!request.handle || request.generation != PlayerBindingGeneration() ||
            gf::GfGameParty::getHandleMover(player) != request.handle ||
            gf::GfObjUtil::getObj(request.handle) != request.object) continue;
        const auto* actor = GetBoundBattleActor(player);
        if (!actor || actor != request.actor || !IsPlayerEngaged(player) || IsDead(actor, 0)) continue;
        const auto* parameter = GetParameter(actor, false);
        if (!parameter) continue;
        // Native CharacterParameter::SetHP (main+0x840f8) writes this actor's
        // HP through its validated property. AttackUpdate (main+0x41c04)
        // detects IsDead and invokes DeadProc; don't synthesize fall death,
        // force a game-over, or bypass native revival handling.
        SetHP(const_cast<void*>(parameter), 0);
    }
}

bool HasPalette(const void* parameter) {
    return parameter && Read<const void*>(parameter, 0x628) &&
        Read<std::uint8_t>(parameter, 0x640) != 0;
}

void SampleSpecial(PlayerReadout& out, gf::GF_OBJ_HANDLE* driver, gf::GF_OBJ_HANDLE* blade) {
    if (!BattleManagerSlot || !*BattleManagerSlot || !blade ||
        blade == reinterpret_cast<gf::GF_OBJ_HANDLE*>(-1) || !gf::GfObjUtil::getObj(blade)) return;
    const auto* actor = LookupActor(*BattleManagerSlot, blade);
    if (!actor || Read<gf::GF_OBJ_HANDLE*>(actor, 0x118) != blade ||
        !HasPalette(GetParameter(actor, false))) return;
    out.specialLevel = SpecialLevel(driver);
    out.specialValid = out.specialLevel <= 4;
    if (!out.specialValid) return;
    // DataManager::UpdateProc reads the selected BLADE's palette, not the
    // Driver's Arts slots. I/II/III use 0/1/2; IV has a separate slot 15.
    const auto* data = GetSlot(actor, out.specialLevel < 3 ? out.specialLevel : 15, false, -1);
    if (!data) return;
    const auto flags = Read<std::uint32_t>(data, 0);
    const auto charge = Read<float>(data, 4);
    if (!(flags & 0x7ff) || (flags & 0x80000000) || !std::isfinite(charge) || charge < 0) return;
    out.specialCharge = charge;
    out.specialChargeValid = true;
}

void Sample() {
    std::array<PlayerReadout, 2> next{};
    for (unsigned player = 0; player < next.size(); ++player) {
        // Identity is now available before encounter entry. Keep palette
        // sampling at its previous global-battle lifecycle boundary.
        const auto* actor = gf::GfGameManager::isBattle() ? GetBoundBattleActor(player) : nullptr;
        auto* driver = gf::GfGameParty::getHandleMover(player);
        if (ethernet::core::IsSceneTransitionActive() || !driver ||
            driver == reinterpret_cast<gf::GF_OBJ_HANDLE*>(-1) || !gf::GfObjUtil::getObj(driver) ||
            (player == 1 && !IsPlayerTwoBound(driver))) { Names[player] = {}; continue; }
        auto& out = next[player];
        out.valid = true;
        out.battleData = actor != nullptr;
        out.engaged = IsPlayerEngaged(player);
        auto* blade = SelectedBlade(player, -1);
        out.character = ObjectName(driver, Names[player][0]);
        auto* target = GetPlayerTarget(player);
        out.hasTarget = target && target != reinterpret_cast<gf::GF_OBJ_HANDLE*>(-1) && gf::GfObjUtil::getObj(target);
        out.target = ObjectName(target, Names[player][1]);
        out.blade = ObjectName(blade, Names[player][2]);
        if (!actor) continue;
        const auto* parameter = GetParameter(actor, false);
        out.downedValid = parameter != nullptr;
        if (parameter) out.downed = IsDead(actor, 0);
        if (SetHP && parameter && out.engaged && !out.downed) {
            out.killTarget = {driver, gf::GfObjUtil::getObj(driver), actor, PlayerBindingGeneration()};
        }
        SampleSpecial(out, driver, blade);
        // Native GetArtsSlotData assumes an initialized PlayerParameter and
        // palette storage; never call it on an actor still being registered.
        if (!HasPalette(parameter)) continue;
        for (unsigned slot = 0; slot < out.arts.size(); ++slot) {
            // Native GetArtsSlotIndex searches precisely 0..2. false selects
            // Driver Arts (not supporting Blade slots); -1 is the active palette.
            const auto* data = GetSlot(actor, slot, false, -1);
            if (!data) continue;
            const auto flags = Read<std::uint32_t>(data, 0);
            const auto charge = Read<float>(data, 4);
            auto& art = out.arts[slot];
            art.id = flags & 0x7ff;
            if ((flags & 0x80000000) || !art.id || !std::isfinite(charge) || charge < 0) continue;
            art.valid = true;
            art.charge = charge;
            const auto* name = ArtsName(art.id);
            art.name = name && *name ? name : "Unnamed Art";
        }
    }
    const SnapshotLock lock;
    Snapshot = std::move(next);
}

void DrawCombat() {
    if (!Available) { ImGui::TextUnformatted("Combat readout unavailable"); return; }
    // imgui-xeno draws from the NVN presentation callback, separately from the
    // framework update callback. Copy under a short lock; never hold it while
    // drawing or reading native game objects.
    std::array<PlayerReadout, 2> snapshot;
    { const SnapshotLock lock; snapshot = Snapshot; }
    for (unsigned player = 0; player < snapshot.size(); ++player) {
        const auto& data = snapshot[player];
        ImGui::PushID(static_cast<int>(player));
        if (player) ImGui::Separator();
        if (!data.valid) {
            ImGui::Text("P%u: unavailable", player + 1);
            ImGui::PopID();
            continue;
        }
        ImGui::Text("P%u: %s | %s", player + 1, data.character.c_str(),
            data.downed ? "Downed" : data.engaged ? "Engaged" : data.hasTarget ? "Selected" : "No target");
        ImGui::Text("Blade: %s", data.blade.c_str());
        ImGui::Text("Target: %s", data.target.c_str());
        ImGui::BeginDisabled(!data.killTarget.handle);
        if (ImGui::SmallButton("Insta-kill")) {
            const SnapshotLock lock;
            PendingKills[player] = data.killTarget;
        }
        ImGui::EndDisabled();
        if (!data.battleData) { ImGui::PopID(); continue; }
        for (unsigned slot = 0; slot < data.arts.size(); ++slot) {
            const auto& art = data.arts[slot];
            if (!art.valid) { ImGui::Text("Art %u: N/A", slot + 1); continue; }
            char label[160]{};
            if (art.charge >= 1) std::snprintf(label, sizeof(label), "%s | Charged", art.name.c_str());
            else std::snprintf(label, sizeof(label), "%s | %.0f%%", art.name.c_str(), art.charge * 100);
            ImGui::ProgressBar(std::clamp(art.charge, 0.f, 1.f), ImVec2(-1, 0), label);
        }
        if (!data.specialValid) ImGui::TextUnformatted("Special: N/A");
        else {
            constexpr const char* levels[] = {"0", "I", "II", "III", "IV"};
            if (!data.specialChargeValid) ImGui::Text("Special %s | Recharge N/A", levels[data.specialLevel]);
            else {
                char label[48]{};
                if (data.specialLevel == 4) std::snprintf(label, sizeof(label), "Special IV | Charged");
                else std::snprintf(label, sizeof(label), "Special %s > %s | %.0f%%",
                    levels[data.specialLevel], levels[data.specialLevel + 1],
                    std::clamp(data.specialCharge, 0.f, 1.f) * 100);
                ImGui::ProgressBar(std::clamp(data.specialCharge, 0.f, 1.f), ImVec2(-1, 0), label);
            }
        }
        ImGui::PopID();
    }
}

void DrawCombatMenu() {
    ImGui::Checkbox("Show combat overlay", &CombatOverlayOpen());
}

void DrawCombatOverlay() {
    auto& open = CombatOverlayOpen();
    if (!open) return;
    const bool menuOpen = ethernet::core::g_Menu->IsOpen();
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize;
    // Keep drawing after the debug menu closes, without acquiring mouse or
    // controller navigation. Opening the menu permits moving/closing.
    if (!menuOpen) flags |= ImGuiWindowFlags_NoInputs;
    ImGui::SetNextWindowPos(ImVec2(20, 60), ImGuiCond_FirstUseEver);
    // Constrain width and fit height every frame, including old saved 360x560
    // window settings. Inherit XenoModsTAS's theme WindowBg alpha unchanged.
    ImGui::SetNextWindowSizeConstraints(ImVec2(300, 0),
        ImVec2(300, std::max(120.f, ImGui::GetIO().DisplaySize.y - 80.f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 5));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 2));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 1));
    if (ImGui::Begin("Combat overlay", menuOpen ? &open : nullptr, flags))
        DrawCombat();
    ImGui::End();
    ImGui::PopStyleVar(3);
}

template<class T> bool Resolve(T& function, const char* symbol) {
    const auto address = skylaunch::hook::detail::ResolveSymbolBase(symbol);
    if (!address || address == skylaunch::hook::INVALID_FUNCTION_PTR) {
        ethernet::core::g_Logger->LogError("EtherNet Combat readout: missing {}", symbol);
        return false;
    }
    function = reinterpret_cast<T>(address);
    return true;
}

struct CombatPanel : ethernet::core::UpdatableModule {
    void Initialize() override {
        UpdatableModule::Initialize();
        nn::os::InitializeMutex(&SnapshotMutex.value, false, 0);
        ethernet::core::g_Menu->RegisterSection("combat", "Combat")->RegisterRenderCallback(&DrawCombatMenu);
        ethernet::core::g_Menu->RegisterRenderCallback(&DrawCombatOverlay, false);
        if (ethernet::core::version::RuntimeGame() != ethernet::core::version::GameType::BF2) return;
        Available = Resolve(ConstructData, "_ZN2gf12DataParamAccC1EPNS_13GF_OBJ_HANDLEE") &&
            Resolve(DestroyData, "_ZN2gf12DataParamAccD1Ev") &&
            Resolve(ValidData, "_ZNK2gf12DataParamAcc7isValidEv") &&
            Resolve(DataName, "_ZNK2gf12DataParamAcc7getNameEv") &&
            Resolve(ArtsName, "_ZN2gf16GfDataDriverArts7getNameEj") &&
            Resolve(SelectedBlade, "_ZN2gf11GfGameParty14getHandleBladeEji") &&
            Resolve(GetParameter, "_ZNK3btl15BattleCharacter26GetCharacterParameterConstEb") &&
            Resolve(GetSlot, "_ZNK3btl15BattleCharacter15GetArtsSlotDataEibi") &&
            Resolve(IsDead, "_ZNK3btl15BattleCharacter6IsDeadEj") &&
            Resolve(SpecialLevel, "_ZN3btl7Utility19AI_GetSpAttackLevelEPN2gf13GF_OBJ_HANDLEE") &&
            Resolve(LookupActor, "_ZNK3btl16CharacterManager17GetCharacterConstEPN2gf13GF_OBJ_HANDLEE") &&
            Resolve(BattleManagerSlot, "_ZZN2mm3mtl12PtrSingletonIN3btl16CharacterManagerEE3sysEvE10s_instance");
        // A missing test helper must not disable the existing readouts.
        Resolve(SetHP, "_ZN3btl18CharacterParameter5SetHPEj");
    }
    bool NeedsUpdate() const override { return Available; }
    bool UpdatesDuringSceneTransition() const override { return true; }
    void Update(fw::UpdateInfo*) override { ProcessKills(); Sample(); }
    void OnSceneTransition() override { ResetSnapshot(); }
    void OnMapChange(unsigned short) override { ResetSnapshot(); }
};
ETHERNET_REGISTER_MODULE(CombatPanel);
}
}
