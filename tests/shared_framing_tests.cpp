#include <ethernet/camera/SharedFraming.hpp>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace ethernet::camera;
void Check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
bool Near(float a, float b) { return std::abs(a-b) < 0.0002f; }
float ShotDistance(Vec3 eye,Vec3 look) {
    return std::sqrt((look.x-eye.x)*(look.x-eye.x)+(look.y-eye.y)*(look.y-eye.y)+(look.z-eye.z)*(look.z-eye.z));
}
bool ShotFits(Frame frame,Vec3 eye,Vec3 look) {
    const float distance=ShotDistance(eye,look);
    frame.forward={(look.x-eye.x)/distance,(look.y-eye.y)/distance,(look.z-eye.z)/distance};
    const float width=std::hypot(frame.forward.x,frame.forward.z);
    frame.right={-frame.forward.z/width,0,frame.forward.x/width};
    const auto r=frame.right,f=frame.forward;
    frame.up={r.y*f.z-r.z*f.y,r.z*f.x-r.x*f.z,r.x*f.y-r.y*f.x};
    Settings visible; visible.safeMargin=0;
    return SubjectsFit(frame,visible,eye);
}
Frame Base() {
    Frame f;
    f.subjects = {Subject{{0,0,0},{0,1.6f,0},0.3f}, Subject{{0,0,0},{0,1.6f,0},0.3f}};
    f.verticalFovRadians = 1.04719755f;
    f.aspect = 16.0f/9;
    f.manualDistance = 4;
    f.maximumDistance = 100;
    f.deltaSeconds = 1.0f/60;
    return f;
}
int main() {
    {
        auto f=Base(); Settings s; State nativeState,sharedState;
        f.subjects[1].feet.x=f.subjects[1].look.x=15;
        f.maximumDistance=10;
        const auto nativeShot=ComputeSharedFraming(f,s,nativeState);
        Check(nativeShot.distanceLimited,"spread exceeds original native maximum");
        f.maximumDistance*=SharedMaximumDistanceScale;
        const auto sharedShot=ComputeSharedFraming(f,s,sharedState);
        Check(sharedShot.valid && sharedShot.fits && !sharedShot.distanceLimited &&
            sharedShot.distance>10 && sharedShot.distance<=20,
            "shared distance accommodates spread beyond native maximum");
        f.subjects[1].feet.x=f.subjects[1].look.x=100; f.discontinuity=true;
        const auto capped=ComputeSharedFraming(f,s,sharedState);
        Check(capped.distanceLimited && Near(capped.distance,20),"extended maximum remains bounded at 200 percent");
    }
    {
        // Already-visible players still cause a gradual pullback, regardless
        // of whether separation is across, along or diagonal to the camera.
        for (const Vec3 direction : {Vec3{1,0,0},Vec3{0,0,1},Vec3{0.6f,0,0.8f}}) {
            auto f=Base(); Settings s; State history;
            s.expandSeconds=s.contractSeconds=0;
            float previous=0;
            for (const float spread : {0.0f,1.0f,2.0f,4.0f,8.0f}) {
                f.subjects[1]={{direction.x*spread,0,direction.z*spread},
                    {direction.x*spread,1.6f,direction.z*spread},0.3f};
                const auto shot=ComputeSharedFraming(f,s,history);
                Check(shot.valid && shot.distance>previous && shot.distance>=4+spread*0.5f,
                    "distance grows with separation before players reach a screen edge");
                previous=shot.distance;
            }
            f.subjects[1]=f.subjects[0];
            Check(Near(ComputeSharedFraming(f,s,history).distance,4),
                "reuniting restores the native manual distance");
        }
        auto f=Base(); Settings s; State oldState,newState;
        f.forward={0,-0.5f,0.8660254f}; f.up={0,0.8660254f,0.5f};
        f.subjects[0]={{0,0,-8},{0,1.6f,-8},0.3f};
        f.subjects[1]={{0,0,8},{0,1.6f,8},0.3f};
        f.maximumDistance=11.5f;
        Check(!ComputeSharedFraming(f,s,oldState).fits,"old ceiling cannot fit pitched depth spread");
        f.maximumDistance=10*SharedMaximumDistanceScale;
        const auto shot=ComputeSharedFraming(f,s,newState);
        Check(shot.valid && shot.fits && shot.distance>11.5f && !shot.distanceLimited,
            "larger shared ceiling fits pitched depth spread");
    }
    Settings settings;
    State state;
    auto frame = Base();
    auto result = ComputeSharedFraming(frame,settings,state);
    Check(result.valid && result.fits && Near(result.distance,4), "coincident players retain manual distance");
    frame.subjects[1].feet.x = frame.subjects[1].look.x = 10;
    frame.discontinuity = true;
    result = ComputeSharedFraming(frame,settings,state);
    Check(Near(result.anchor.x,5), "exact horizontal midpoint");
    Check(result.distance > 4 && result.fits, "horizontal fit expands distance");
    const auto wideDistance = result.distance;
    frame.aspect = 4.0f/3;
    result = ComputeSharedFraming(frame,settings,state);
    Check(result.distance > wideDistance && result.fits, "narrow aspect requires greater horizontal distance");
    frame.maximumDistance = 5;
    result = ComputeSharedFraming(frame,settings,state);
    Check(result.distanceLimited && Near(result.distance,5) && !result.fits, "native maximum wins over impossible fit");
    frame = Base();
    frame.subjects[1].feet.y = 12; frame.subjects[1].look.y = 13.6f;
    state.Reset();
    result = ComputeSharedFraming(frame,settings,state);
    Check(result.distance > 4 && result.fits, "vertical separation fits full bodies");
    frame = Base(); frame.subjects[1].feet.z = frame.subjects[1].look.z = -12;
    state.Reset(); result = ComputeSharedFraming(frame,settings,state);
    Check(result.distance > 4 && result.fits, "nearer player depth included");
    Check(!SubjectsFit(frame,settings,{result.eye.x,result.eye.y,result.eye.z+10}), "post-collision inward push can lose fit");

    // Cover non-axis-aligned camera and retained height displacement.
    frame = Base();
    frame.right = {0.8f,0,-0.6f}; frame.forward = {0.6f,0,0.8f};
    frame.eyeOffset = {0.5f,1,0};
    for (float x : {-8.0f,0.0f,8.0f}) for (float y : {-6.0f,0.0f,6.0f}) for (float z : {-9.0f,0.0f,9.0f}) {
        frame.subjects[1] = {{x,y,z},{x,y+1.6f,z},0.3f};
        state.Reset(); result = ComputeSharedFraming(frame,settings,state);
        Check(result.valid && result.fits, "rotated camera-space fit grid");
    }
    // Time-based damping should agree over the same elapsed time for a fixed target.
    frame = Base(); State at30, at60;
    ComputeSharedFraming(frame,settings,at30); at60=at30;
    frame.subjects[1].feet.x = frame.subjects[1].look.x = 10;
    frame.subjects[1].feet.y = 4; frame.subjects[1].look.y = 5.6f;
    for(int i=0;i<30;++i) { frame.deltaSeconds=1.0f/30; ComputeSharedFraming(frame,settings,at30); }
    for(int i=0;i<60;++i) { frame.deltaSeconds=1.0f/60; ComputeSharedFraming(frame,settings,at60); }
    Check(Near(at30.anchor.y,at60.anchor.y), "height damping frame-rate independence");
    frame.generation=1; frame.subjects[1].feet.x=frame.subjects[1].look.x=100;
    result=ComputeSharedFraming(frame,settings,at30);
    Check(Near(result.anchor.x,50) && Near(result.anchor.y,2.4f), "generation change resets stale height history");
    frame.subjects[1].feet.x=frame.subjects[1].look.x=200; frame.discontinuity=true;
    result=ComputeSharedFraming(frame,settings,at30);
    Check(Near(result.anchor.x,100), "warp snaps without crossing the world");
    frame=Base(); frame.verticalFovRadians=std::numeric_limits<float>::quiet_NaN();
    result=ComputeSharedFraming(frame,settings,at30);
    Check(!result.valid && !at30.initialized, "invalid input clears state");
    frame=Base(); frame.right={2,0,0};
    Check(!ComputeSharedFraming(frame,settings,state).valid, "bad native basis rejected");
    settings.safeMargin=std::numeric_limits<float>::quiet_NaN();
    Check(Near(Sanitize(settings).safeMargin,0.15f), "invalid config sanitized");
    frame=Base(); frame.manualDistance=12;
    result=ComputeSharedFraming(frame,settings,state);
    Check(result.valid && result.distance>=12 && Near(frame.manualDistance,12), "manual preference remains unchanged");
    // Native adapter must retain retail tracking without damping P1 a second
    // time. Translate an already-stable shot and both bodies together.
    frame=Base(); settings={}; state.Reset();
    frame.subjects[1].feet.x=frame.subjects[1].look.x=8;
    const auto relative=ComputeRetailRelativeFraming(frame,{0,2,0},settings,state);
    for (auto& subject : frame.subjects) {
        subject.feet.x+=100; subject.look.x+=100;
        subject.feet.y+=20; subject.look.y+=20;
    }
    const auto translated=ComputeRetailRelativeFraming(frame,{100,22,0},settings,state);
    Check(translated.valid && Near(translated.eye.x-relative.eye.x,100) &&
        Near(translated.eye.y-relative.eye.y,20) && Near(translated.distance,relative.distance),
        "native tracking is not smoothed twice on common player motion");
    Check(SubjectsFit(frame,settings,translated.eye), "relative adapter result fits world-space bodies");
    Check(Near(translated.anchor.x,104), "relative adapter retains exact world midpoint");
    const auto invalid=ComputeRetailRelativeFraming(frame,{0,std::numeric_limits<float>::quiet_NaN(),0},settings,state);
    Check(!invalid.valid && !state.initialized, "invalid retail shot clears relative damping");
    // Old settings, native XZ look offsets and bone animation must not bias
    // the real shot pivot. Move each player independently without resetting.
    frame=Base(); state.Reset(); settings={};
    settings.playerOneWeight=1; settings.anchorSeconds=2;
    ComputeRetailRelativeFraming(frame,{0,2,0},settings,state);
    for (const unsigned moving : {0u,1u,0u,1u}) {
        frame.subjects[moving].feet.x+=6; frame.subjects[moving].look.x+=7;
        frame.subjects[moving].feet.z-=4; frame.subjects[moving].look.z-=3;
        const auto shot=ComputeRetailRelativeFraming(frame,{30,2,-40},settings,state);
        const float midpointX=(frame.subjects[0].feet.x+frame.subjects[1].feet.x)*0.5f;
        const float midpointZ=(frame.subjects[0].feet.z+frame.subjects[1].feet.z)*0.5f;
        Check(shot.valid && Near(shot.anchor.x,midpointX) && Near(shot.anchor.z,midpointZ),
            "each player's motion immediately pans XZ to exact root midpoint");
        Check(Near(shot.eye.x+frame.forward.x*shot.distance,midpointX) &&
            Near(shot.eye.z+frame.forward.z*shot.distance,midpointZ),
            "shot pivot ignores native P1 horizontal look offset");
    }
    frame=Base();
    frame.subjects[0]={{-1,0,0},{-1,1.6f,0},0.3f};
    frame.subjects[1]={{1,0,0},{1,1.6f,0},0.3f};
    for (const auto eye: {Vec3{0,10,-10},Vec3{0,3,-10}}) {
        Vec3 look=eye.y>5 ? Vec3{0,-10,-9} : Vec3{0,20,0};
        const float distance=ShotDistance(eye,look);
        Check(!ShotFits(frame,eye,look),"tilt reproduction starts with bodies offscreen");
        Check(CorrectOffscreenAim(frame,eye,look),"tilted shot re-aims toward both bodies");
        Check(ShotFits(frame,eye,look),"corrected tilted shot shows both full bodies");
        Check(Near(distance,ShotDistance(eye,look)),"aim correction retains native collision distance");
        const auto retained=look;
        Check(!CorrectOffscreenAim(frame,eye,look) && Near(look.x,retained.x) &&
            Near(look.y,retained.y) && Near(look.z,retained.z),"visible corrected shot remains untouched");
    }
    Vec3 compressedEye{0,2,-2}, compressedLook{0,12,0};
    Check(CorrectOffscreenAim(frame,compressedEye,compressedLook) &&
        ShotFits(frame,compressedEye,compressedLook),"collision-shortened shot gets visibility correction");
    frame.subjects[0]={{-100,0,0},{-100,1.6f,0},0.3f};
    frame.subjects[1]={{100,0,0},{100,1.6f,0},0.3f};
    Vec3 impossibleLook{0,12,0};
    Check(!CorrectOffscreenAim(frame,compressedEye,impossibleLook) && Near(impossibleLook.y,12),
        "impossible angular span does not override collision or fake a fit");
    frame=Base(); frame.aspect=std::numeric_limits<float>::quiet_NaN();
    Check(!CorrectOffscreenAim(frame,compressedEye,impossibleLook),"invalid projection rejects aim correction");
    std::cout << "PASS: EtherNet shared framing geometry, retail-relative tracking, limits, damping, resets and validation\n";
}
