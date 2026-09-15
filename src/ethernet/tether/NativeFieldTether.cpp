#include <ethernet/core/UpdatableModule.hpp>
#include <ethernet/tether/FieldTether.hpp>
#include <ethernet/camera/FieldView.hpp>
#include <ethernet/LocalPlayers.hpp>
#include <ethernet/PartnerMovement.hpp>
#include <ethernet/FieldRecovery.hpp>
#include <engine/xc2/gf/Manager.hpp>
#include <engine/xc2/gf/Party.hpp>
#include <engine/xc2/gf/PlayerController.hpp>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace ethernet::tether {
namespace {
template<class T> T Read(const void* p, std::size_t offset) {
    T result;
    std::memcpy(&result,static_cast<const unsigned char*>(p)+offset,sizeof(result));
    return result;
}
bool Finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool ValidHandle(gf::GF_OBJ_HANDLE* h) { return h && h != reinterpret_cast<gf::GF_OBJ_HANDLE*>(-1); }
struct Actor {
    gf::GF_OBJ_HANDLE* handle{};
    void* object{};
    gf::GfComPropertyPc* property{};
    Vec3 feet{};
    bool traversal{};
};
using MountFn = unsigned(*)(gf::GF_OBJ_HANDLE*,unsigned);
MountFn MountType{};
bool Installed{}, Enabled = true;
float BoundaryInsetPercent = 0;
std::array<Actor,2> LastActors{};
std::uint64_t BindingGeneration{}, LastViewSerial{};
unsigned StaleFrames = 2;
const char* Status = "Native movement hooks not installed";
void Reset(const char* reason) {
    LastActors = {}; LastViewSerial = 0; StaleFrames = 2;
    Status = reason;
}
bool FieldGate() {
    // isField rejects the native player-pause states; no native pause query
    // with side effects is called here. Scripted shots retain ownership.
    return Installed && Enabled && !ethernet::core::IsSceneTransitionActive() &&
        !IsFieldPlayerRecovering(0) && !IsFieldPlayerRecovering(1) &&
        ethernet::core::HidInput::GetPlayer(1)->padConnected &&
        ethernet::core::HidInput::GetPlayer(2)->padConnected &&
        ((gf::GfGameManager::isField() && gf::GfGameManager::isControlFree()) ||
         IsPartnerFieldActive()) &&
        !gf::GfGameManager::isBattle() &&
        ethernet::core::version::RuntimeGame() != ethernet::core::version::GameType::IRA &&
        camera::AllowsFieldTether();
}
bool ReadActor(gf::GF_OBJ_HANDLE* handle, Actor& a) {
    if (!ValidHandle(handle)) return false;
    a.handle = handle;
    a.object = gf::GfObjUtil::getObj(handle);
    if (!a.object) return false;
    a.property = Read<gf::GfComPropertyPc*>(a.object,0x60);
    if (!a.property || !a.property->getRTTI()->isKindOf(&gf::GfComPropertyPc::m_rtti)) return false;
    const unsigned flags = Read<unsigned>(a.property,0x110);
    if (!(flags & (1u<<10))) return false; // Actual native manual/pad-control flag.
    const auto* generic = gf::GfObjUtil::getProperty(handle);
    const auto* movement = generic ? Read<void*>(generic,0x18) : nullptr;
    const auto* collision = Read<void*>(a.property,0x28);
    if (!movement || !collision) return false;
    const unsigned mount = MountType(handle,0);
    a.traversal = (flags & ((1u<<5)|(1u<<6)|(1u<<9))) ||
        Read<std::uintptr_t>(movement,0xc0) != ~std::uintptr_t{} ||
        mount == 0 || mount == 1 || mount == 3;
    gf::GfObjAcc accessor(handle);
    mm::Vec3 position{}; float rotation{};
    if (!accessor.getObjPosRot(position,rotation)) return false;
    a.feet = Read<Vec3>(&position,0);
    if (!Finite(a.feet)) return false;
    return true;
}
bool ReadPair(std::array<Actor,2>& actors) {
    auto* p1 = gf::GfGameParty::getHandleMover(0);
    auto* p2 = gf::GfGameParty::getHandleMover(1);
    return p1 != p2 && p1 == gf::GfGameManager::getControlMover() &&
        IsPlayerTwoBound(p2) && ReadActor(p1,actors[0]) && ReadActor(p2,actors[1]);
}
bool SamePair(const std::array<Actor,2>& a, const std::array<Actor,2>& b) {
    for (unsigned i=0;i<2;++i)
        if (a[i].handle != b[i].handle || a[i].object != b[i].object ||
            a[i].property != b[i].property) return false;
    return true;
}
void Filter(gf::GfComPropertyPc& property, const fw::UpdateInfo& update) {
    if (!FieldGate() || StaleFrames > 1) return;
    std::array<Actor,2> actors{};
    if (!ReadPair(actors) || BindingGeneration != PlayerBindingGeneration() ||
        !SamePair(actors,LastActors)) return;
    const int slot = actors[0].property == &property ? 0 : actors[1].property == &property ? 1 : -1;
    if (slot < 0 || actors[slot].traversal) return;
    const auto view = camera::ReadFieldView();
    if (!view.valid || view.bindingGeneration != BindingGeneration) return;
    auto body = view.subjects[slot];
    // Use live mover position; the camera observation supplies only body height.
    const auto current = actors[slot].feet;
    const auto shift = Vec3{current.x-body.feet.x,current.y-body.feet.y,current.z-body.feet.z};
    if (std::hypot(std::hypot(shift.x,shift.z),shift.y) > 8) return;
    body.feet = current;
    body.look = {body.look.x+shift.x,body.look.y+shift.y,body.look.z+shift.z};
    auto partner = view.subjects[1-slot];
    const auto other = actors[1-slot].feet;
    const Vec3 otherShift{other.x-partner.feet.x,other.y-partner.feet.y,other.z-partner.feet.z};
    if (std::hypot(std::hypot(otherShift.x,otherShift.z),otherShift.y)>8) return;
    partner.feet = other;
    partner.look = {partner.look.x+otherShift.x,partner.look.y+otherShift.y,partner.look.z+otherShift.z};
    const auto velocity = Read<Vec3>(&property,0x80);
    auto boundary = view.boundary;
    boundary.insetPercent = BoundaryInsetPercent;
    const auto filtered = ConstrainToScreen(velocity,body,boundary,update.updateDelta,&partner);
    // Native voluntary world velocity only. No pad, position, gravity, root
    // motion, collision or turning-speed writes. See tether native evidence.
    auto* bytes = reinterpret_cast<unsigned char*>(&property);
    std::memcpy(bytes+0x80,&filtered.x,sizeof(float));
    std::memcpy(bytes+0x88,&filtered.z,sizeof(float));
}
template<class T> struct Movement : skylaunch::hook::Trampoline<T> {
    static void Hook(const fw::UpdateInfo& update, gf::GfComPropertyPc& property) {
        T::Orig(update,property); Filter(property,update);
    }
};
struct Walk : Movement<Walk> {};
struct Run : Movement<Run> {};
struct Swim : Movement<Swim> {};
struct Jump : Movement<Jump> {};
struct JumpBack : Movement<JumpBack> {};
void DrawSettings() {
    ImGui::TextWrapped("%s",Status);
    if (ImGui::Checkbox("Enable screen boundary",&Enabled)) Reset("Screen boundary setting changed");
    ImGui::SliderFloat("Boundary inset",&BoundaryInsetPercent,0,25,"%.1f%%",ImGuiSliderFlags_AlwaysClamp);
}
constexpr const char* WalkSymbol = "_ZN2gf2pc13MoveUtilField14updateMoveWalkERKN2fw10UpdateInfoERNS_15GfComPropertyPcE";
constexpr const char* RunSymbol = "_ZN2gf2pc13MoveUtilField13updateMoveRunERKN2fw10UpdateInfoERNS_15GfComPropertyPcE";
constexpr const char* SwimSymbol = "_ZN2gf2pc13MoveUtilField14updateMoveSwimERKN2fw10UpdateInfoERNS_15GfComPropertyPcE";
constexpr const char* JumpSymbol = "_ZN2gf2pc13MoveUtilField14updateMoveJumpERKN2fw10UpdateInfoERNS_15GfComPropertyPcE";
constexpr const char* JumpBackSymbol = "_ZN2gf2pc13MoveUtilField18updateMoveJumpBackERKN2fw10UpdateInfoERNS_15GfComPropertyPcE";
constexpr const char* MountSymbol = "_ZN2gf14GfMountManager12getMountTypeEPNS_13GF_OBJ_HANDLEENS_8GFATTACHE";
}

struct NativeFieldTether : ethernet::core::UpdatableModule {
    void Initialize() override {
        UpdatableModule::Initialize();
        ethernet::core::g_Menu->RegisterSection("fieldTether","Screen Boundary")->RegisterRenderCallback(&DrawSettings);
        if (ethernet::core::version::RuntimeGame() != ethernet::core::version::GameType::BF2) {
            Status = "Field tether requires XC2"; return;
        }
        for (auto* symbol : {WalkSymbol,RunSymbol,SwimSymbol,JumpSymbol,JumpBackSymbol,MountSymbol}) {
            const auto address = skylaunch::hook::detail::ResolveSymbolBase(symbol);
            if (!address || address == skylaunch::hook::INVALID_FUNCTION_PTR) {
                Status = "Native symbol unavailable; tether disabled";
                ethernet::core::g_Logger->LogError("EtherNet tether: missing {}",symbol); return;
            }
        }
        MountType = skylaunch::hook::detail::ResolveSymbol<MountFn>(MountSymbol);
        Walk::HookAt(WalkSymbol); Run::HookAt(RunSymbol); Swim::HookAt(SwimSymbol);
        Jump::HookAt(JumpSymbol); JumpBack::HookAt(JumpBackSymbol);
        Installed = Walk::HasApplied() && Run::HasApplied() && Swim::HasApplied() &&
            Jump::HasApplied() && JumpBack::HasApplied();
        Status = Installed ? "Ready: waiting for two field players" : "Movement hook installation failed";
    }
    bool NeedsUpdate() const override { return true; }
    bool UpdatesDuringSceneTransition() const override { return true; }
    void Update(fw::UpdateInfo*) override {
        if (!FieldGate()) { Reset("Screen boundary suspended: menu, camera ownership or game state"); return; }
        std::array<Actor,2> actors{};
        if (!ReadPair(actors)) { Reset("Waiting for two bound field players"); return; }
        if (!SamePair(actors,LastActors) || BindingGeneration != PlayerBindingGeneration())
            Reset("Screen boundary reset: player binding changed");
        BindingGeneration = PlayerBindingGeneration();
        LastActors = actors;
        const auto view = camera::ReadFieldView();
        StaleFrames = view.valid && view.serial != LastViewSerial ? 0 : std::min(StaleFrames+1,3u);
        LastViewSerial = view.serial;
        Status = !view.valid || StaleFrames > 1 ? "Waiting for current shared-camera view" :
            view.boundary.atNativeLimit ? "Shared zoom limit: screen boundary active" :
            "Camera can expand: unrestricted movement";
    }
    void OnSceneTransition() override { Reset("Tether reset: scene transition"); }
    void OnMapChange(unsigned short) override { Reset("Tether reset: map change"); }
};
ETHERNET_REGISTER_MODULE(NativeFieldTether);
}
