#pragma once
#include <array>
#include <cstdint>

namespace ethernet::camera {
inline constexpr float SharedMaximumDistanceScale = 2.0f;
struct Vec3 { float x{}, y{}, z{}; };
struct Subject {
    Vec3 feet;
    Vec3 look;
    float radius = 0.3f;
};
struct Settings {
    float playerOneWeight = 0.6f; // Height only; XZ always uses the exact midpoint.
    float safeMargin = 0.15f;
    float anchorSeconds = 0.2f; // Height only; no horizontal tracking lag.
    float expandSeconds = 0.1f;
    float contractSeconds = 0.5f;
};
// Supplied by the native adapter; this core never reads game memory.
struct Frame {
    std::array<Subject, 2> subjects;
    Vec3 right{1, 0, 0}, up{0, 1, 0}, forward{0, 0, 1};
    Vec3 eyeOffset{}; // Retail tracking displacement; only Y is applied before dolly.
    float verticalFovRadians{};
    float aspect{};
    float manualDistance{};
    float maximumDistance{};
    float deltaSeconds{};
    std::uint64_t generation{}; // Adapter changes this on camera/subject rebind.
    bool discontinuity{};      // Warp/load; never smooth through old world state.
};
struct State {
    Vec3 anchor{};
    float distance{};
    std::uint64_t generation{};
    bool initialized{};
    void Reset() { *this = {}; }
};
struct Result {
    Vec3 anchor{}, eye{};
    float requestedDistance{}; // Unclamped fit, including the manual minimum.
    float distance{};          // Damped and clamped to the shared ceiling.
    bool valid{};
    bool distanceLimited{};
    bool fits{};               // At the returned distance, before native collision.
};
Settings Sanitize(Settings settings);
// Collapse a recovering pair to its survivor. eyeOffset is the last usable
// retail tracking offset when P1 is absent. False means neither player is active.
bool FrameSurvivingPlayers(Frame& frame, Vec3& retailLook, std::array<bool,2> recovering);
Result ComputeSharedFraming(const Frame& frame, const Settings& settings, State& state);
// Retain retail height tracking; XZ follows the exact mover-root midpoint.
// State.anchor is P1-relative, while the returned anchor/eye are world-space.
Result ComputeRetailRelativeFraming(Frame frame, Vec3 retailLook, const Settings& settings, State& state);
// Can also evaluate native post-collision eye position, if orientation is supplied.
bool SubjectsFit(const Frame& frame, const Settings& settings, Vec3 eye);
// Re-aim an off-screen shot without moving the collision-resolved eye or
// changing its eye/look distance. Leaves an already-visible shot untouched.
bool CorrectOffscreenAim(const Frame& frame, Vec3 eye, Vec3& look);
}
