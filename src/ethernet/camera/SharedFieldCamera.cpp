#include <ethernet/core/UpdatableModule.hpp>
#include <ethernet/camera/SharedFraming.hpp>
#include <ethernet/camera/FieldView.hpp>
#include <ethernet/camera/MenuCamera.hpp>
#include <ethernet/LocalPlayers.hpp>
#include <ethernet/PartnerMovement.hpp>
#include <ethernet/FieldRecovery.hpp>
#include <engine/xc2/gf/Manager.hpp>
#include <engine/xc2/gf/Party.hpp>
#include <engine/xc2/gf/PlayerController.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace ethernet::camera {
namespace {
// Native layout evidence comes from XC2 2.1.0. See docs/ethernet-camera-native.md.
// Live framing adapts collision vectors. Pause retention locks the displayed layer.
template<class T> T Read(const void* p, std::size_t offset) {
    T value;
    std::memcpy(&value, static_cast<const unsigned char*>(p) + offset, sizeof(T));
    return value;
}
Vec3 Add(Vec3 a, Vec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
Vec3 Sub(Vec3 a, Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
Vec3 Scale(Vec3 a, float k) { return {a.x*k,a.y*k,a.z*k}; }
float Dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
Vec3 Cross(Vec3 a, Vec3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
float Length(Vec3 a) { return std::sqrt(Dot(a,a)); }
bool Finite(Vec3 a) { return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z); }
Vec3 FromNative(const mm::Vec3& v) { return Read<Vec3>(&v,0); }
void ToNative(mm::Vec3& out, Vec3 v) { std::memcpy(&out,&v,sizeof(v)); }
bool ValidHandle(gf::GF_OBJ_HANDLE* h) { return h && h != reinterpret_cast<gf::GF_OBJ_HANDLE*>(-1); }

using LookBoneFn = void(*)(gf::GF_OBJ_HANDLE*, mm::Vec3&);
using MountTypeFn = unsigned(*)(gf::GF_OBJ_HANDLE*, unsigned);
using GameStateFn = unsigned(*)();
using PauseFn = bool(*)(unsigned);
using SetViewFn = void(*)(void*, const void*);
SetViewFn SetView{};
using TutorialFn = bool(*)();
TutorialFn IsTutorial{};
void** CameraManagerSlot{};
void** CameraTargetSlot{};
LookBoneFn LookBone{};
MountTypeFn MountType{};
GameStateFn GameState{}, ReservedGameState{};
PauseFn IsPause{};
void** GameStateSlot{};
void** GameSceneSlot{};
bool Installed{}, Enabled = true, SeenThisFrame{};
const char* Status = "Native hooks not installed";
State SolverState;
Frame CurrentFrame;
Result LastResult;
bool PostCollisionFits{};
float PostCollisionDistance{};
std::uint64_t ViewSerial{};
FieldView LatestView;
std::uint64_t Generation = 1, LastBindingGeneration{};
void* LastCamera{};
void* LastPreset{};
std::array<void*,2> LastObjects{};
std::array<gf::GF_OBJ_HANDLE*,2> LastHandles{};
std::array<Vec3,2> LastFeet{};
std::array<bool,2> LastRecovering{};
// Capture only after CameraLayer has published the final view, not from a
// solver/collision intermediate. These bytes contain eye, look and view matrix.
std::array<unsigned char,0x60> DisplayedPose{}; // camera +0x40 through +0x9f
void* DisplayedCamera{};
void* DisplayedLayer{};
bool NativeCameraPaused{};
bool WatchingNativePause{};
bool AwaitingSharedResume{}, SharedUpdatedThisFrame{};
bool MenuOpenRequested{};

struct UpdateContext {
    void* camera{};
    void* layer{};
    float delta{};
    bool attempted{}, injected{};
    Vec3 anchorDelta{};
};
UpdateContext* Context{}; // Scoped to the synchronous native camera update.

bool UnsupportedTargetMount(void* target) {
    if (!Read<unsigned char>(target,0x45)) return false;
    auto* handle = Read<gf::GF_OBJ_HANDLE*>(target,0x70);
    if (!ValidHandle(handle)) handle = gf::GfGameManager::getControlMover();
    if (!ValidHandle(handle)) return true;
    const auto mount = MountType(handle,0);
    // Native isBattleWeapon (1.5.1 main+0x3ba7d0) accepts types 1/3.
    // writeTargetInfo sets +0x45 for 0/1/3: this is NOT purely traversal.
    return mount != 1 && mount != 3;
}

bool PartnerCameraActive() {
    // Ordinary talk uses player-pause tokens and state 4, not a full world
    // pause. Keep early full-screen menu locking and genuine pauses intact.
    return !MenuOpenRequested && IsPause && !IsPause(1) && IsPartnerFieldActive();
}

bool IsFieldScene() {
    // Ordinary battle shares state 3 and PlayerCamera; its separate scene
    // battle bit must not disable shared framing (native isField: 0x381de4).
    // isField() also rejects player pause (main+0x381de4). Preserve field
    // camera ownership while input is paused; retain its state/reserve checks.
    // createResetBladeForBladeSwitch pushes state 24 until the native Blade
    // request completes. This is still the same field camera, not a scripted
    // shot. Cover both entry/exit reserve gaps without yielding one frame to P1.
    const auto fieldOrBladeSwitch = [](unsigned state) { return state == 3 || state == 24; };
    return GameStateSlot && *GameStateSlot && Read<void*>(*GameStateSlot,0x10) &&
        ((fieldOrBladeSwitch(GameState()) && fieldOrBladeSwitch(ReservedGameState())) || PartnerCameraActive());
}

void Reset(const char* reason) {
    SolverState.Reset();
    LastCamera = nullptr;
    Status = reason;
    LastResult = {};
    PostCollisionFits = false;
    PostCollisionDistance = 0;
    LatestView = {};
}

void* EligibleLayer(void* camera) {
    if (!CameraManagerSlot || !*CameraManagerSlot) return nullptr;
    // GfGamePlayerCamera::setPlayerCamera pushes onto CameraManager +0x20.
    auto* layer = static_cast<unsigned char*>(*CameraManagerSlot) + 0x20;
    // CameraLayer::update selects head -0x10; blend/plugin shots are excluded.
    if (Read<void*>(layer,8) != static_cast<unsigned char*>(camera)+0x10 ||
        Read<unsigned char>(layer,0xc8) ||
        Read<void*>(layer,0x20) != layer+0x20) return nullptr;
    return layer;
}

bool OwnsDisplayedShot(void* camera) {
    if (!Installed || !Enabled || IsTutorial() || !DisplayedCamera || camera != DisplayedCamera ||
        !CameraManagerSlot || !*CameraManagerSlot ||
        DisplayedLayer != static_cast<unsigned char*>(*CameraManagerSlot)+0x20 ||
        Read<void*>(DisplayedLayer,8) != static_cast<unsigned char*>(camera)+0x10 ||
        Read<void*>(DisplayedLayer,0x20) != static_cast<unsigned char*>(DisplayedLayer)+0x20 ||
        ethernet::core::IsSceneTransitionActive()) return false;
    return true;
}

bool HoldPausedShot(void* camera) {
    if (!OwnsDisplayedShot(camera)) return false;
    if (IsFieldPlayerRecovering(0) && IsFieldPlayerRecovering(1)) return true;
    if (PartnerCameraActive()) return false;
    // Party availability, input capture and reserved state are NOT view
    // ownership. None may discard the displayed shot while the game is paused.
    return MenuOpenRequested || NativeCameraPaused || IsPause(1) || (GameSceneSlot && *GameSceneSlot &&
        (Read<unsigned>(*GameSceneSlot,0x144) != 0 ||
         (Read<unsigned>(*GameSceneSlot,0x148) & 4) != 0));
}

bool HoldViewWrite(void* camera) {
    // A native layer blend is not a change of camera owner. On close, retain
    // the frozen XYZ until the shared solver has produced the replacement.
    return HoldPausedShot(camera) ||
        (AwaitingSharedResume && !SharedUpdatedThisFrame && OwnsDisplayedShot(camera) &&
         !(Context && Context->camera == camera && Context->injected));
}

void RestoreDisplayedPose(void* camera) {
    std::memcpy(static_cast<unsigned char*>(camera)+0x40,DisplayedPose.data(),DisplayedPose.size());
}

void PublishDisplayedPose() {
    RestoreDisplayedPose(DisplayedCamera);
    auto* matrix = static_cast<unsigned char*>(DisplayedLayer)+0x40;
    std::memcpy(matrix,DisplayedPose.data()+0x20,0x40);
    SetView(Read<void*>(DisplayedLayer,0x38),matrix);
    AwaitingSharedResume = true;
    Status = "Shared: displayed camera locked";
}

// The actual button path queues OpenMenu2, whose start/update waits THREE
// updates before opening the fullscreen UI. Lock before building that sequence,
// not at the later pause/menu-layer callbacks. See menu2-native.txt.
struct OpenMenuRequest : skylaunch::hook::Trampoline<OpenMenuRequest> {
    static unsigned Hook(unsigned flags, unsigned menu, void* signal) {
        const bool previous = MenuOpenRequested;
        if (OwnsDisplayedShot(DisplayedCamera)) {
            MenuOpenRequested = true;
            PublishDisplayedPose();
        }
        const auto accepted = Orig(flags,menu,signal);
        if (!accepted) MenuOpenRequested = previous;
        return accepted;
    }
};
struct OpenMenuEnd : skylaunch::hook::Trampoline<OpenMenuEnd> {
    static void Hook(void* command) {
        Orig(command); // Native immediate gimmick refresh runs while still held.
        MenuOpenRequested = false;
        // AwaitingSharedResume keeps the view until a shared shot replaces it.
    }
};

// A pause can be acquired after the layer's update but before rendering.
// Publish immediately at acquisition rather than waiting for the next frame.
struct PauseRequest : skylaunch::hook::Trampoline<PauseRequest> {
    static std::uintptr_t Hook(unsigned mask) {
        const auto handle = Orig(mask);
        if ((mask & 1) && HoldPausedShot(DisplayedCamera)) PublishDisplayedPose();
        return handle;
    }
};
struct PlayerPauseRequest : skylaunch::hook::Trampoline<PlayerPauseRequest> {
    static int Hook(void* scene) {
        const int token = Orig(scene);
        if (token >= 0 && HoldPausedShot(DisplayedCamera)) PublishDisplayedPose();
        return token;
    }
};

struct NativePauseQuery : skylaunch::hook::Trampoline<NativePauseQuery> {
    static bool Hook() {
        const bool paused = Orig(); // Consume native one-shot flags exactly once.
        // Only the eligible shared-camera update can ignore ordinary talk's
        // player-pause token. Other native pause consumers remain unchanged.
        if (WatchingNativePause && Context && PartnerCameraActive()) {
            NativeCameraPaused = false;
            return false;
        }
        if (WatchingNativePause) NativeCameraPaused = paused;
        return paused;
    }
};

// Both methods can write the P1 pose outside PlayerCamera::update. Intercept
// BEFORE the native writes, not after they have replaced the shared XYZ.
struct PlayerActivate : skylaunch::hook::Trampoline<PlayerActivate> {
    static void Hook(void* camera) {
        if (HoldViewWrite(camera)) { RestoreDisplayedPose(camera); return; }
        Orig(camera);
    }
};
struct PlayerReset : skylaunch::hook::Trampoline<PlayerReset> {
    static void Hook(void* camera) {
        // PlayerCameraTarget::update consumes its reset request separately.
        // Do not replace yaw/tracking history with P1 while the shot is held.
        if (HoldViewWrite(camera)) return;
        Orig(camera);
    }
};
struct PlayerViewMatrix : skylaunch::hook::Trampoline<PlayerViewMatrix> {
    static void Hook(void* camera) {
        if (HoldViewWrite(camera)) { RestoreDisplayedPose(camera); return; }
        Orig(camera);
    }
};
struct SceneViewMatrix : skylaunch::hook::Trampoline<SceneViewMatrix> {
    static void Hook(void* camera, const void* matrix) {
        // Keep secondary/menu-model cameras native. Only the field scene
        // camera receives the exact matrix that was displayed at acquisition.
        if (HoldViewWrite(DisplayedCamera) &&
            camera == Read<void*>(DisplayedLayer,0x38))
            matrix = DisplayedPose.data()+0x20;
        Orig(camera,matrix);
    }
};

void RefreshSharedShotForResume(void* camera, const fw::UpdateInfo& update);

struct LayerUpdate : skylaunch::hook::Trampoline<LayerUpdate> {
    static void Hook(void* layer, const fw::UpdateInfo& update) {
        const bool mainLayer = CameraManagerSlot && *CameraManagerSlot &&
            layer == static_cast<unsigned char*>(*CameraManagerSlot)+0x20;
        if (!mainLayer) { Orig(layer,update); return; }
        // Includes active tutorial UI and StartTutorial scripts, not just fades.
        // Discard the old field shot so it cannot reappear after the handoff.
        const bool tutorial = Installed && IsTutorial();
        if (tutorial) {
            DisplayedCamera = DisplayedLayer = nullptr;
            AwaitingSharedResume = false;
            MenuOpenRequested = false;
        }
        // A frozen view must not wait indefinitely for the separately scheduled
        // PlayerCamera update. Once the native sequence has released pause and
        // returned to field control, explicitly hand back to the shared solver
        // BEFORE deciding whether the held matrix may be released. CameraManager
        // updates its target before this layer, so the target is current here.
        if (AwaitingSharedResume && !SharedUpdatedThisFrame &&
            OwnsDisplayedShot(DisplayedCamera) && !HoldPausedShot(DisplayedCamera) &&
            IsFieldScene() && EligibleLayer(DisplayedCamera) == layer)
            RefreshSharedShotForResume(DisplayedCamera,update);
        const bool resumeGap = HoldViewWrite(DisplayedCamera);
        if (layer == DisplayedLayer && (HoldPausedShot(DisplayedCamera) || resumeGap)) {
            // Projection and native blend timing still advance. The pose writer
            // and final scene submission are intercepted before any P1 write.
            Orig(layer,update);
            PublishDisplayedPose();
            NativeCameraPaused = false;
            SharedUpdatedThisFrame = false;
            return;
        }
        AwaitingSharedResume = false;
        if (layer == DisplayedLayer && !OwnsDisplayedShot(DisplayedCamera)) {
            DisplayedCamera = nullptr;
            DisplayedLayer = nullptr;
        }
        Orig(layer,update);
        NativeCameraPaused = false;
        if (!tutorial && SharedUpdatedThisFrame && Installed && Enabled && LastCamera && LastResult.valid &&
            EligibleLayer(LastCamera) == layer && IsFieldScene()) {
            std::memcpy(DisplayedPose.data(),static_cast<unsigned char*>(LastCamera)+0x40,
                        DisplayedPose.size());
            // This is the matrix CameraLayer actually submitted to ScnObjCam.
            std::memcpy(DisplayedPose.data()+0x20,static_cast<unsigned char*>(layer)+0x40,0x40);
            DisplayedCamera = LastCamera;
            DisplayedLayer = layer;
        }
        SharedUpdatedThisFrame = false;
    }
};

bool ReadSubject(gf::GF_OBJ_HANDLE* handle, Subject& subject, void*& object) {
    if (!ValidHandle(handle) || !(object = gf::GfObjUtil::getObj(handle))) return false;
    auto* property = Read<gf::GfComPropertyPc*>(object,0x60);
    if (!property || !property->getRTTI()->isKindOf(&gf::GfComPropertyPc::m_rtti)) return false;
    // Same property paths used by PlayerCameraTarget::writeTargetInfo.
    const auto* genericProperty = gf::GfObjUtil::getProperty(handle);
    const auto* movement = genericProperty ? Read<void*>(genericProperty,0x18) : nullptr;
    const auto* collision = Read<void*>(property,0x28);
    if (!movement || !collision || Read<std::uintptr_t>(movement,0xc0) != ~std::uintptr_t{})
        return false; // Attached mover; water alone supports retail tracking.
    const auto mount = MountType(handle,0);
    if (mount == 0) return false; // Types 1/3 are ordinary drawn battle weapons.
    gf::GfObjAcc accessor(handle);
    mm::Vec3 feet{}, look{};
    float rotation{};
    if (!accessor.getObjPosRot(feet,rotation)) return false;
    // Native routine falls back to root +1.5 when no camera bone is available.
    LookBone(handle,look);
    subject = {FromNative(feet), FromNative(look), 0.3f};
    return Finite(subject.feet) && Finite(subject.look);
}

bool SetBasis(Frame& frame, Vec3 eye, Vec3 look) {
    const auto ray = Sub(look,eye);
    const float distance = Length(ray);
    if (!std::isfinite(distance) || distance < 0.01f) return false;
    frame.forward = Scale(ray,1/distance);
    const auto right = Cross(frame.forward,{0,1,0});
    const float width = Length(right);
    if (!std::isfinite(width) || width < 0.001f) return false;
    frame.right = Scale(right,1/width);
    frame.up = Cross(frame.right,frame.forward);
    return true;
}

// Called once at whichever retail collision stage executes first. The rest of
// PlayerCamera::update consumes these same stack vectors to set its real view.
void Inject(void* camera, mm::Vec3& eyeNative, mm::Vec3& lookNative) {
    if (!Context || Context->camera != camera || Context->attempted) return;
    if (HoldPausedShot(camera)) return; // Preserve the solver's pre-pause history.
    Context->attempted = true;
    auto* target = *CameraTargetSlot;
    if (!target || !Read<unsigned char>(target,0x78) || Read<unsigned char>(camera,0x124) ||
        Read<unsigned char>(target,0x43) || UnsupportedTargetMount(target)) {
        Reset("Retail: traversal/target override"); return;
    }
    // target+0x46 is water, not scripted ownership. Retail applies its
    // surface-height adjustment before these shot vectors reach collision.
    auto* p1 = gf::GfGameParty::getHandleMover(0);
    auto* p2 = gf::GfGameParty::getHandleMover(1);
    const auto targetHandle = Read<gf::GF_OBJ_HANDLE*>(target,0x70);
    if (!ValidHandle(p1) || !ValidHandle(p2) || p1 == p2 ||
        p1 != gf::GfGameManager::getControlMover() ||
        (ValidHandle(targetHandle) && targetHandle != p1) || !IsPlayerTwoBound(p2)) {
        Reset("Retail: waiting for two bound party movers"); return;
    }
    Frame frame{};
    std::array<void*,2> objects{};
    if (!ReadSubject(p1,frame.subjects[0],objects[0]) ||
        !ReadSubject(p2,frame.subjects[1],objects[1])) {
        Reset("Retail: mover unavailable or unsupported traversal"); return;
    }
    auto* presetHolder = Read<void*>(camera,0xb0);
    auto* preset = presetHolder ? Read<void*>(presetHolder,8) : nullptr;
    auto* sceneCamera = Read<void*>(Context->layer,0x38);
    auto* projection = sceneCamera ? Read<void*>(sceneCamera,0x80) : nullptr;
    if (!preset || !projection || std::abs(Read<float>(camera,0xcc)) > 0.001f) {
        Reset("Retail: projection/preset unavailable or camera roll"); return;
    }
    const auto eye = FromNative(eyeNative), look = FromNative(lookNative);
    if (!SetBasis(frame,eye,look)) { Reset("Retail: degenerate view"); return; }
    const auto center1 = Scale(Add(frame.subjects[0].feet,frame.subjects[0].look),0.5f);
    const std::array<bool,2> recovering{IsFieldRecovering(p1),IsFieldRecovering(p2)};
    // One survivor is a single camera subject. Preserve the real native P1
    // center for collision translation, but remove the fallen actor from fit.
    Vec3 framingLook = look;
    // Native P1 height smoothing must not follow a hidden falling actor. Keep
    // the previous tracking offset, then track the living subject instead.
    frame.eyeOffset = recovering[0] && LastResult.valid ? CurrentFrame.eyeOffset : Sub(look,center1);
    if (!FrameSurvivingPlayers(frame,framingLook,recovering)) return;
    frame.maximumDistance = Read<float>(preset,0xc);
    const float manual = Read<float>(camera,0xd4);
    const float nativeDistance = Length(Sub(eye,look));
    if (!std::isfinite(manual) || manual <= 0 || !std::isfinite(frame.maximumDistance) ||
        manual > frame.maximumDistance + 0.001f || nativeDistance > frame.maximumDistance + 0.001f) {
        Reset("Retail: camera distance outside preset"); return;
    }
    // A reconstructed ray at maximum zoom can differ by a few float ULPs.
    frame.manualDistance = std::min(frame.maximumDistance,std::max(nativeDistance,manual));
    // Extra shared-framing room only; native manual zoom and collision remain
    // untouched. The boundary observer consumes this same extended ceiling.
    frame.maximumDistance *= SharedMaximumDistanceScale;
    frame.verticalFovRadians = Read<float>(preset,0)*0.017453292519943295f;
    frame.aspect = Read<float>(projection,0x1f0);
    frame.deltaSeconds = Context->delta;
    const std::array<gf::GF_OBJ_HANDLE*,2> handles{p1,p2};
    const auto bindingGeneration = PlayerBindingGeneration();
    if (camera != LastCamera || preset != LastPreset || handles != LastHandles ||
        objects != LastObjects || bindingGeneration != LastBindingGeneration ||
        recovering != LastRecovering) ++Generation;
    frame.generation = Generation;
    frame.discontinuity = !LastCamera ||
        Length(Sub(frame.subjects[0].feet,LastFeet[0])) > 15 ||
        Length(Sub(frame.subjects[1].feet,LastFeet[1])) > 15;
    const auto result = ComputeRetailRelativeFraming(frame,framingLook,ethernet::core::GetState().config.sharedCamera,SolverState);
    if (!result.valid) { Reset("Retail: invalid framing inputs"); return; }
    LastCamera = camera; LastPreset = preset; LastHandles = handles; LastObjects = objects;
    LastBindingGeneration = bindingGeneration;
    LastRecovering = recovering;
    LastFeet = {frame.subjects[0].feet,frame.subjects[1].feet};
    CurrentFrame = frame; LastResult = result;
    // Keep retail height only: its P1-relative XZ tracking/look displacement
    // would move the shared pivot away from the exact player midpoint.
    const Vec3 sharedLook{result.anchor.x,result.anchor.y+frame.eyeOffset.y,result.anchor.z};
    Context->anchorDelta = Sub(sharedLook,look);
    ToNative(eyeNative,result.eye);
    ToNative(lookNative,sharedLook);
    Context->injected = true;
    Status = result.distanceLimited ? "Shared: extended distance limit reached" : "Shared camera active";
}

struct CollisionSide : skylaunch::hook::Trampoline<CollisionSide> {
    static void Hook(void* camera, mm::Vec3& eye, mm::Vec3& look) {
        Inject(camera,eye,look);
        Orig(camera,eye,look);
    }
};
struct CollisionUp : skylaunch::hook::Trampoline<CollisionUp> {
    static void Hook(void* camera, const mm::Vec3& pivot, mm::Vec3& eye, mm::Vec3& look) {
        Inject(camera,eye,look);
        if (Context && Context->camera == camera && Context->injected) {
            // Native pivot XZ uses look AFTER side collision; Y still uses P1's
            // root. Correct Y and use current look XZ rather than translating twice.
            auto sharedPivot = FromNative(pivot);
            auto sharedLook = FromNative(look);
            sharedPivot.x = sharedLook.x; sharedPivot.z = sharedLook.z;
            sharedPivot.y += Context->anchorDelta.y;
            mm::Vec3 adjusted{}; ToNative(adjusted,sharedPivot);
            Orig(camera,adjusted,eye,look);
        } else Orig(camera,pivot,eye,look);
    }
};
struct CollisionDistance : skylaunch::hook::Trampoline<CollisionDistance> {
    static void Hook(void* camera, mm::Vec3& eye, mm::Vec3& look) {
        Inject(camera,eye,look);
        Orig(camera,eye,look); // Keep the native collision-resolved eye.
        // updateViewMatrix can run another collision pass on the stored view.
        // Observe it, but never inject again outside PlayerCamera::update.
        if ((Context && Context->camera == camera && Context->injected) ||
            (!Context && camera == LastCamera && LastResult.valid && AllowsFieldTether())) {
            Frame actual = CurrentFrame;
            auto correctedLook=FromNative(look);
            if (CorrectOffscreenAim(actual,FromNative(eye),correctedLook))
                ToNative(look,correctedLook);
            const bool basisValid = SetBasis(actual,FromNative(eye),FromNative(look));
            PostCollisionFits = basisValid &&
                SubjectsFit(actual,ethernet::core::GetState().config.sharedCamera,FromNative(eye));
            PostCollisionDistance = Length(Sub(FromNative(eye),FromNative(look)));
            ++ViewSerial;
            LatestView = {ViewSerial,basisValid,PostCollisionFits,
                {FromNative(eye),actual.right,actual.up,actual.forward,
                 actual.verticalFovRadians,actual.aspect,basisValid,
                 LastResult.distanceLimited || LastResult.distance >= actual.maximumDistance-0.01f},
                actual.subjects,LastBindingGeneration};
        }
    }
};
struct PlayerUpdate : skylaunch::hook::Trampoline<PlayerUpdate> {
    static void Hook(void* camera, const fw::UpdateInfo& update) {
        SeenThisFrame = true;
        NativeCameraPaused = false;
        SharedUpdatedThisFrame = false;
        auto* layer = Installed && Enabled ? EligibleLayer(camera) : nullptr;
        if (HoldPausedShot(camera) || (HoldViewWrite(camera) && !IsFieldScene())) {
            // Consume only native one-shot pause bookkeeping. Do not execute
            // the native tracking/position writer with the P1 target at all.
            NativePauseQuery::Orig();
            PublishDisplayedPose();
            return;
        }
        if (IsTutorial() || !layer || !IsFieldScene() ||
            ethernet::core::version::RuntimeGame() == ethernet::core::version::GameType::IRA ||
            !ethernet::core::HidInput::GetPlayer(1)->padConnected ||
            !ethernet::core::HidInput::GetPlayer(2)->padConnected ||
            (ethernet::core::IsSceneTransitionActive() && !ethernet::core::IsSceneTransitionCompletionArmed())) {
            Reset(!Enabled ? "Retail: disabled" : "Retail: camera ownership, game state or controller gate");
            WatchingNativePause = true;
            Orig(camera,update);
            WatchingNativePause = false;
            if (HoldPausedShot(camera)) PublishDisplayedPose();
            return;
        }
        UpdateContext context{camera,layer,update.updateDelta};
        auto* previous = Context;
        Context = &context;
        WatchingNativePause = true;
        Orig(camera,update);
        WatchingNativePause = false;
        Context = previous;
        SharedUpdatedThisFrame = context.injected;
        // Let native pause/one-shot bookkeeping run, then restore before any
        // later camera consumer can observe the temporary P1 pose.
        if (HoldPausedShot(camera)) PublishDisplayedPose();
        // Native pause returns before changing the shot; keep damping history.
        if (!context.attempted) Status = "Shared: native camera update suspended";
        if (context.injected && ethernet::core::IsSceneTransitionCompletionArmed())
            ethernet::core::EndSceneTransition();
    }
};

void RefreshSharedShotForResume(void* camera, const fw::UpdateInfo& update) {
    // Use the normal shared-anchor/collision path, not an unhooked native P1
    // update. A successful injection sets SharedUpdatedThisFrame; LayerUpdate
    // then publishes that shot and clears AwaitingSharedResume in the same pass.
    PlayerUpdate::Hook(camera,update);
}

void DrawSettings() {
    auto& settings = ethernet::core::GetState().config.sharedCamera;
    ImGui::TextWrapped("%s",Status);
    if (ImGui::Checkbox("Enable shared camera",&Enabled)) Reset("Camera setting changed");
    if (LastResult.valid) {
        ImGui::Text("Dolly %.2f / requested %.2f / maximum %.2f",LastResult.distance,
            LastResult.requestedDistance,CurrentFrame.maximumDistance);
        ImGui::Text("After collision: %.2f; both in safe frame: %s",PostCollisionDistance,
            PostCollisionFits ? "yes" : "no");
    }
    ImGui::Separator();
    ImGui::SliderFloat("P1 height weight", &settings.playerOneWeight, 0, 1, "%.2f");
    ImGui::SliderFloat("Safe margin per edge", &settings.safeMargin, 0, 0.4f, "%.2f");
    ImGui::SliderFloat("Height damping (s)", &settings.anchorSeconds, 0, 2, "%.2f");
    ImGui::SliderFloat("Expand damping (s)", &settings.expandSeconds, 0, 2, "%.2f");
    ImGui::SliderFloat("Contract damping (s)", &settings.contractSeconds, 0, 2, "%.2f");
    if (ImGui::Button("Reset camera settings")) { settings = {}; Reset("Camera settings reset"); }
    settings = Sanitize(settings);
}

constexpr const char* UpdateSymbol = "_ZN2gf12PlayerCamera6updateERKN2fw10UpdateInfoE";
constexpr const char* StateSymbol = "_ZN2gf11GfGameState8getStateEv";
constexpr const char* ReserveSymbol = "_ZN2gf11GfGameState15getStateReserveEv";
constexpr const char* StateSlotSymbol = "_ZZN2mm3mtl12PtrSingletonIN2gf11GfGameStateEE3sysEvE10s_instance";
constexpr const char* SceneSlotSymbol = "_ZZN2mm3mtl12PtrSingletonIN2gf11GfGameSceneEE3sysEvE10s_instance";
constexpr const char* PauseSymbol = "_ZN2gf7GfPause7isPauseEj";
constexpr const char* NativePauseSymbol = "_ZN2gf13GfGameManager19isPlayerCameraPauseEv";
constexpr const char* LayerUpdateSymbol = "_ZN2fw11CameraLayer6updateERKNS_10UpdateInfoE";
constexpr const char* SetViewSymbol = "_ZN2ml9ScnObjCam13setViewMatrixERKN2mm5Mat44E";
constexpr const char* PauseRequestSymbol = "_ZN2gf7GfPause8reqPauseEj";
constexpr const char* PlayerPauseRequestSymbol = "_ZN2gf11GfGameScene17createPlayerPauseEv";
constexpr const char* TutorialSymbol = "_ZN2gf13GfGameManager14isExecTutorialEv";
constexpr const char* OpenMenuRequestSymbol = "_ZN2gf13GfPlayFactory15createOpenMenu2EjNS_8MAINMENUEPNS_22GfSequentialPlaySignalE";
constexpr const char* OpenMenuEndSymbol = "_ZN2gf18GfPlayComOpenMenu23endEv";
constexpr const char* ActivateSymbol = "_ZN2gf12PlayerCamera10onActivateEv";
constexpr const char* ResetSymbol = "_ZN2gf12PlayerCamera5resetEv";
constexpr const char* PlayerViewSymbol = "_ZN2gf12PlayerCamera16updateViewMatrixEv";
constexpr const char* SideSymbol = "_ZN2gf12PlayerCamera19updateCollisionSideERN2mm4Vec3ES3_";
constexpr const char* UpSymbol = "_ZN2gf12PlayerCamera17updateCollisionUpERKN2mm4Vec3ERS2_S5_";
constexpr const char* DistanceSymbol = "_ZN2gf12PlayerCamera16updateCollision2ERN2mm4Vec3ES3_";
constexpr const char* ManagerSymbol = "_ZZN2mm3mtl12PtrSingletonIN2fw13CameraManagerEE3sysEvE10s_instance";
constexpr const char* TargetSymbol = "_ZZN2mm3mtl12PtrSingletonIN2gf18PlayerCameraTargetEE3sysEvE10s_instance";
constexpr const char* BoneSymbol = "_ZN2gf15CameraUtilities25getCameraLookBonePositionEPNS_13GF_OBJ_HANDLEERN2mm4Vec3E";
constexpr const char* MountSymbol = "_ZN2gf14GfMountManager12getMountTypeEPNS_13GF_OBJ_HANDLEENS_8GFATTACHE";
}

void PreserveMenuEntryShot() {
    if (OwnsDisplayedShot(DisplayedCamera))
        PublishDisplayedPose();
    // Do not latch an accepted menu here: listeners precede factory validation.
    // OpenMenuRequest owns that latch; an unaccepted request resumes normally.
}

bool AllowsFieldTether() {
    if (!Installed || !Enabled || !LastCamera || !IsFieldScene() || !EligibleLayer(LastCamera)) return false;
    auto* target = CameraTargetSlot ? *CameraTargetSlot : nullptr;
    return target && Read<unsigned char>(target,0x78) &&
        !Read<unsigned char>(LastCamera,0x124) &&
        !Read<unsigned char>(target,0x43) && !UnsupportedTargetMount(target);
}
FieldView ReadFieldView() {
    auto result = LatestView;
    result.valid = result.valid && LastResult.valid && AllowsFieldTether();
    result.boundary.valid = result.valid;
    return result;
}

struct SharedFieldCamera : ethernet::core::UpdatableModule {
    void Initialize() override {
        UpdatableModule::Initialize();
        ethernet::core::g_Menu->RegisterSection("sharedCamera", "Shared Camera")->RegisterRenderCallback(&DrawSettings);
        if (ethernet::core::version::RuntimeGame() != ethernet::core::version::GameType::BF2) {
            Status = "Unsupported game: camera requires XC2"; return;
        }
        for (auto* symbol : {UpdateSymbol,SideSymbol,UpSymbol,DistanceSymbol,ManagerSymbol,TargetSymbol,BoneSymbol,MountSymbol,
                            StateSymbol,ReserveSymbol,StateSlotSymbol,SceneSlotSymbol,PauseSymbol,
                            NativePauseSymbol,LayerUpdateSymbol,SetViewSymbol,PauseRequestSymbol,
                            PlayerPauseRequestSymbol,TutorialSymbol,OpenMenuRequestSymbol,OpenMenuEndSymbol,
                            ActivateSymbol,ResetSymbol,PlayerViewSymbol}) {
            const auto address = skylaunch::hook::detail::ResolveSymbolBase(symbol);
            if (!address || address == skylaunch::hook::INVALID_FUNCTION_PTR) {
                Status = "Native symbol unavailable; camera remains retail";
                ethernet::core::g_Logger->LogError("EtherNet camera: missing {}",symbol); return;
            }
        }
        CameraManagerSlot = skylaunch::hook::detail::ResolveSymbol<void**>(ManagerSymbol);
        CameraTargetSlot = skylaunch::hook::detail::ResolveSymbol<void**>(TargetSymbol);
        LookBone = skylaunch::hook::detail::ResolveSymbol<LookBoneFn>(BoneSymbol);
        MountType = skylaunch::hook::detail::ResolveSymbol<MountTypeFn>(MountSymbol);
        GameState = skylaunch::hook::detail::ResolveSymbol<GameStateFn>(StateSymbol);
        ReservedGameState = skylaunch::hook::detail::ResolveSymbol<GameStateFn>(ReserveSymbol);
        GameStateSlot = skylaunch::hook::detail::ResolveSymbol<void**>(StateSlotSymbol);
        GameSceneSlot = skylaunch::hook::detail::ResolveSymbol<void**>(SceneSlotSymbol);
        IsPause = skylaunch::hook::detail::ResolveSymbol<PauseFn>(PauseSymbol);
        SetView = skylaunch::hook::detail::ResolveSymbol<SetViewFn>(SetViewSymbol);
        IsTutorial = skylaunch::hook::detail::ResolveSymbol<TutorialFn>(TutorialSymbol);
        CollisionSide::HookAt(SideSymbol); CollisionUp::HookAt(UpSymbol);
        CollisionDistance::HookAt(DistanceSymbol);
        if (!CollisionSide::HasApplied() || !CollisionUp::HasApplied() || !CollisionDistance::HasApplied()) return;
        PlayerUpdate::HookAt(UpdateSymbol);
        NativePauseQuery::HookAt(NativePauseSymbol);
        LayerUpdate::HookAt(LayerUpdateSymbol);
        PauseRequest::HookAt(PauseRequestSymbol);
        PlayerPauseRequest::HookAt(PlayerPauseRequestSymbol);
        OpenMenuRequest::HookAt(OpenMenuRequestSymbol);
        OpenMenuEnd::HookAt(OpenMenuEndSymbol);
        PlayerActivate::HookAt(ActivateSymbol);
        PlayerReset::HookAt(ResetSymbol);
        PlayerViewMatrix::HookAt(PlayerViewSymbol);
        SceneViewMatrix::HookAt(SetViewSymbol);
        Installed = PlayerUpdate::HasApplied() && NativePauseQuery::HasApplied() && LayerUpdate::HasApplied() &&
            PauseRequest::HasApplied() && PlayerPauseRequest::HasApplied() &&
            OpenMenuRequest::HasApplied() && OpenMenuEnd::HasApplied() &&
            PlayerActivate::HasApplied() && PlayerReset::HasApplied() &&
            PlayerViewMatrix::HasApplied() && SceneViewMatrix::HasApplied();
        Status = Installed ? "Ready: waiting for two field players" : "Camera update hook failed";
        ethernet::core::g_Logger->LogInfo("EtherNet shared camera hooks: {}. Switch validation pending.",Installed ? "installed" : "failed");
    }
    bool NeedsUpdate() const override { return true; }
    bool UpdatesDuringSceneTransition() const override { return true; }
    void Update(fw::UpdateInfo*) override {
        if (Installed && !SeenThisFrame) {
            if (Enabled && LastCamera && IsFieldScene() && EligibleLayer(LastCamera))
                Status = "Shared: native camera update suspended";
            else Reset("Retail: player camera not updating");
        }
        SeenThisFrame = false;
    }
    void OnSceneTransition() override {
        DisplayedCamera = DisplayedLayer = nullptr; NativeCameraPaused = false;
        AwaitingSharedResume = SharedUpdatedThisFrame = false;
        MenuOpenRequested = false;
        ++Generation; Reset("Retail: scene transition");
    }
    void OnMapChange(unsigned short) override {
        DisplayedCamera = DisplayedLayer = nullptr; NativeCameraPaused = false;
        AwaitingSharedResume = SharedUpdatedThisFrame = false;
        MenuOpenRequested = false;
        ++Generation; Reset("Retail: map changed");
    }
};
ETHERNET_REGISTER_MODULE(SharedFieldCamera);
}
