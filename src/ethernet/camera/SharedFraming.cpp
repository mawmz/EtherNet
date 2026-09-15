#include <ethernet/camera/SharedFraming.hpp>
#include <algorithm>
#include <cmath>

namespace ethernet::camera {
namespace {
constexpr float Pi = 3.14159265358979323846f;
Vec3 Add(Vec3 a, Vec3 b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
Vec3 Sub(Vec3 a, Vec3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
Vec3 Scale(Vec3 v, float s) { return {v.x*s, v.y*s, v.z*s}; }
float Dot(Vec3 a, Vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
bool Finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
float ClampFinite(float value, float fallback, float lo, float hi) {
    return std::isfinite(value) ? std::clamp(value, lo, hi) : fallback;
}
float Alpha(float dt, float seconds) {
    return seconds <= 0 ? 1.0f : -std::expm1(-dt / seconds);
}
bool Valid(const Frame& f) {
    if (!Finite(f.right) || !Finite(f.up) || !Finite(f.forward) || !Finite(f.eyeOffset) ||
        !std::isfinite(f.verticalFovRadians) || f.verticalFovRadians <= 0 || f.verticalFovRadians >= Pi ||
        !std::isfinite(f.aspect) || f.aspect <= 0 ||
        !std::isfinite(f.manualDistance) || f.manualDistance <= 0 ||
        !std::isfinite(f.maximumDistance) || f.maximumDistance < f.manualDistance ||
        !std::isfinite(f.deltaSeconds) || f.deltaSeconds < 0) return false;
    // Require an orthonormal basis. Never quietly reinterpret a bad native matrix.
    if (std::abs(Dot(f.right,f.right)-1) > 0.001f ||
        std::abs(Dot(f.up,f.up)-1) > 0.001f ||
        std::abs(Dot(f.forward,f.forward)-1) > 0.001f ||
        std::abs(Dot(f.right,f.up)) > 0.001f ||
        std::abs(Dot(f.right,f.forward)) > 0.001f ||
        std::abs(Dot(f.up,f.forward)) > 0.001f) return false;
    for (const auto& s : f.subjects)
        if (!Finite(s.feet) || !Finite(s.look) || !std::isfinite(s.radius) || s.radius < 0) return false;
    return true;
}
}
Settings Sanitize(Settings s) {
    s.playerOneWeight = ClampFinite(s.playerOneWeight, 0.6f, 0, 1);
    s.safeMargin = ClampFinite(s.safeMargin, 0.15f, 0, 0.4f);
    s.anchorSeconds = ClampFinite(s.anchorSeconds, 0.2f, 0, 2);
    s.expandSeconds = ClampFinite(s.expandSeconds, 0.1f, 0, 2);
    s.contractSeconds = ClampFinite(s.contractSeconds, 0.5f, 0, 2);
    return s;
}
bool FrameSurvivingPlayers(Frame& frame, Vec3& retailLook, std::array<bool,2> recovering) {
    if (recovering[0] && recovering[1]) return false;
    if (recovering[0]) {
        frame.subjects[0] = frame.subjects[1];
        const auto center = Scale(Add(frame.subjects[0].feet,frame.subjects[0].look),0.5f);
        retailLook = Add(center,frame.eyeOffset);
    } else if (recovering[1]) {
        frame.subjects[1] = frame.subjects[0];
    }
    return true;
}
bool SubjectsFit(const Frame& f, const Settings& settings, Vec3 eye) {
    if (!Valid(f) || !Finite(eye)) return false;
    const auto s = Sanitize(settings);
    const float tanY = std::tan(f.verticalFovRadians * 0.5f) * (1 - 2*s.safeMargin);
    const float tanX = tanY * f.aspect;
    for (const auto& subject : f.subjects) {
        for (auto point : {subject.feet, subject.look}) {
            const auto relative = Sub(point, eye);
            const float nearestDepth = Dot(relative, f.forward) - subject.radius;
            if (nearestDepth <= 0 ||
                std::abs(Dot(relative, f.right)) + subject.radius > nearestDepth*tanX + 0.0001f ||
                std::abs(Dot(relative, f.up)) + subject.radius > nearestDepth*tanY + 0.0001f)
                return false;
        }
    }
    return true;
}
bool CorrectOffscreenAim(const Frame& input, Vec3 eye, Vec3& look) {
    if (!Valid(input) || !Finite(eye) || !Finite(look)) return false;
    const auto ray=Sub(look,eye);
    const float distance=std::sqrt(Dot(ray,ray));
    if (!std::isfinite(distance) || distance<0.01f) return false;
    auto cross=[](Vec3 a,Vec3 b) { return Vec3{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; };
    auto orient=[&](Vec3 direction,Frame& f) {
        const float length=std::sqrt(Dot(direction,direction));
        if (!std::isfinite(length) || length<0.001f) return false;
        f.forward=Scale(direction,1/length);
        auto right=cross(f.forward,{0,1,0});
        const float width=std::sqrt(Dot(right,right));
        if (width<0.001f) return false;
        f.right=Scale(right,1/width);
        f.up=cross(f.right,f.forward);
        return true;
    };
    Frame actual=input;
    if (!orient(ray,actual)) return false;
    Settings visible; visible.safeMargin=0;
    if (SubjectsFit(actual,visible,eye)) return false;

    // Fit angular body bounds, not the old retail look target (which can lie
    // far beyond both characters when pitched). Unwrap yaw around the current
    // view so a pair spanning +/-pi cannot send the camera the long way round.
    const float reference=std::atan2(actual.forward.x,actual.forward.z);
    float minYaw=Pi,maxYaw=-Pi,minPitch=Pi,maxPitch=-Pi;
    for (const auto& subject:input.subjects) for (auto point:{subject.feet,subject.look}) {
        const auto offset=Sub(point,eye);
        const float horizontal=std::hypot(offset.x,offset.z);
        const float length=std::sqrt(Dot(offset,offset));
        if (!std::isfinite(length) || length<=subject.radius+0.01f) return false;
        const float yaw=std::remainder(std::atan2(offset.x,offset.z)-reference,2*Pi);
        const float pitch=std::atan2(offset.y,horizontal);
        const float pitchRadius=std::asin(std::min(1.0f,subject.radius/length));
        const float yawRadius=std::asin(std::min(1.0f,subject.radius/std::max(horizontal,0.001f)));
        minYaw=std::min(minYaw,yaw-yawRadius); maxYaw=std::max(maxYaw,yaw+yawRadius);
        minPitch=std::min(minPitch,pitch-pitchRadius); maxPitch=std::max(maxPitch,pitch+pitchRadius);
    }
    const float yaw=reference+(minYaw+maxYaw)*0.5f;
    const float pitch=std::clamp((minPitch+maxPitch)*0.5f,-Pi*0.5f+0.002f,Pi*0.5f-0.002f);
    const Vec3 desired{std::sin(yaw)*std::cos(pitch),std::sin(pitch),std::cos(yaw)*std::cos(pitch)};
    Frame centered=input;
    if (!orient(desired,centered) || !SubjectsFit(centered,visible,eye)) return false;
    // Keep as much of the player's chosen angle as possible. The centered
    // direction is a verified fallback; each accepted trial is also fit-tested.
    float low=0,high=1;
    Vec3 accepted=centered.forward;
    Settings safety=visible; safety.safeMargin=0.02f;
    const auto fitSettings=SubjectsFit(centered,safety,eye) ? safety : visible;
    for (unsigned i=0;i<18;++i) {
        const float t=(low+high)*0.5f;
        Frame trial=input;
        if (orient(Add(Scale(actual.forward,1-t),Scale(desired,t)),trial) &&
            SubjectsFit(trial,fitSettings,eye)) {
            high=t; accepted=trial.forward;
        } else low=t;
    }
    look=Add(eye,Scale(accepted,distance));
    return true;
}
Result ComputeSharedFraming(const Frame& f, const Settings& settings, State& state) {
    Result result;
    if (!Valid(f)) { state.Reset(); return result; }
    const auto s = Sanitize(settings);
    const bool reset = !state.initialized || state.generation != f.generation || f.discontinuity;
    const auto center1 = Scale(Add(f.subjects[0].feet, f.subjects[0].look), 0.5f);
    const auto center2 = Scale(Add(f.subjects[1].feet, f.subjects[1].look), 0.5f);
    const auto desiredAnchor = Add(Scale(center1, s.playerOneWeight), Scale(center2, 1-s.playerOneWeight));
    const float dt = std::min(f.deltaSeconds, 0.1f);
    result.anchor = reset ? desiredAnchor : Add(state.anchor,
        Scale(Sub(desiredAnchor, state.anchor), Alpha(dt, s.anchorSeconds)));
    // Horizontal tracking is symmetric and immediate, including while only
    // one player moves. Bone animation, P1 weighting and old damping history
    // must not pull the world-space pivot away from the two mover roots.
    result.anchor.x = (f.subjects[0].feet.x + f.subjects[1].feet.x) * 0.5f;
    result.anchor.z = (f.subjects[0].feet.z + f.subjects[1].feet.z) * 0.5f;
    const Vec3 origin{result.anchor.x, result.anchor.y + f.eyeOffset.y, result.anchor.z};
    const float tanY = std::tan(f.verticalFovRadians * 0.5f) * (1 - 2*s.safeMargin);
    const float tanX = tanY * f.aspect;
    // Add the pair's radius to P1's preferred distance, even when both bodies
    // already fit. This makes separation pull back the camera in every direction,
    // rather than waiting for a screen edge; coincidence retains native zoom.
    const auto separation = Sub(f.subjects[1].feet,f.subjects[0].feet);
    float required = f.manualDistance + 0.5f*std::sqrt(Dot(separation,separation));
    // Conservative camera-space body bounds. Solving each endpoint plus radius
    // includes horizontal, vertical and depth separation without changing FOV.
    for (const auto& subject : f.subjects) {
        for (auto point : {subject.feet, subject.look}) {
            const auto relative = Sub(point, origin);
            const float z = Dot(relative, f.forward);
            required = std::max(required, (std::abs(Dot(relative,f.right)) + subject.radius)/tanX - z + subject.radius);
            required = std::max(required, (std::abs(Dot(relative,f.up)) + subject.radius)/tanY - z + subject.radius);
        }
    }
    if (!Finite(result.anchor) || !std::isfinite(required)) { state.Reset(); return {}; }
    result.requestedDistance = required;
    result.distanceLimited = required > f.maximumDistance;
    const float target = std::clamp(required, f.manualDistance, f.maximumDistance);
    const float seconds = target > state.distance ? s.expandSeconds : s.contractSeconds;
    result.distance = reset ? target : std::clamp(
        state.distance + (target-state.distance)*Alpha(dt, seconds), f.manualDistance, f.maximumDistance);
    result.eye = Sub(origin, Scale(f.forward, result.distance));
    result.valid = Finite(result.eye);
    result.fits = result.valid && SubjectsFit(f, s, result.eye);
    if (!result.valid) { state.Reset(); return {}; }
    state = {result.anchor, result.distance, f.generation, true};
    return result;
}
Result ComputeRetailRelativeFraming(Frame frame, Vec3 retailLook, const Settings& settings, State& state) {
    const auto origin = Scale(Add(frame.subjects[0].feet,frame.subjects[0].look),0.5f);
    for (auto& subject : frame.subjects) {
        subject.feet = Sub(subject.feet,origin);
        subject.look = Sub(subject.look,origin);
    }
    frame.eyeOffset = Sub(retailLook,origin);
    auto result = ComputeSharedFraming(frame,settings,state);
    if (!result.valid) return result;
    result.anchor = Add(result.anchor,origin);
    result.eye = Add(result.eye,origin);
    if (!Finite(result.anchor) || !Finite(result.eye)) { state.Reset(); return {}; }
    return result;
}
}
