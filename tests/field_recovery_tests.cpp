#include <ethernet/RecoveryTimer.hpp>
#include <ethernet/camera/SharedFraming.hpp>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

using namespace ethernet;
using namespace ethernet::camera;
bool Near(float a, float b) { return std::abs(a-b)<0.001f; }
Frame Scene() {
    Frame f;
    f.subjects = {Subject{{0,0,0},{0,2,0},0.3f},Subject{{6,0,0},{6,2,0},0.3f}};
    f.eyeOffset = {0,0.25f,0};
    f.verticalFovRadians = 1; f.aspect = 16.0f/9;
    f.manualDistance = 6; f.maximumDistance = 20; f.deltaSeconds = 1.0f/60;
    return f;
}
int main() {
    RecoveryTimer p1,p2;
    assert(!p1.Advance(1,true));
    assert(p1.Begin());
    assert(!BothPlayersRecovering(p1,p2));
    assert(!p1.Advance(2,true));
    assert(!p1.Begin()); // repeated death notification neither restarts nor repeats voice
    assert(Near(p1.remaining,3));
    assert(p2.Begin());
    assert(BothPlayersRecovering(p1,p2));
    assert(!p1.Advance(100,false)); // menus/cutscenes cannot silently expire recovery
    assert(!p1.Advance(-1,true));
    assert(!p1.Advance(std::numeric_limits<float>::quiet_NaN(),true));
    assert(!p1.Advance(std::numeric_limits<float>::infinity(),true));
    assert(Near(p1.remaining,3));
    assert(p1.Advance(3,true));
    assert(!p2.Advance(3,true));
    assert(p1.active); // adapter retains exclusion until warp actually completes
    assert(BothPlayersRecovering(p1,p2)); // native wipe wins even at the expiry boundary
    p1.Reset();
    assert(!p1.active && p2.active);
    assert(!BothPlayersRecovering(p1,p2));
    assert(p2.Advance(2,true));
    p2.Reset();
    assert(p2.Begin());
    for (int i=0;i<299;++i) assert(!p2.Advance(1.0f/60,true));
    assert(p2.Advance(1.0f/60,true));
    p2.Reset(); // transition/rebinding clears timer; no deferred old-world respawn
    assert(!p2.Advance(10,true));

    auto f = Scene();
    Vec3 look{0,1.25f,0};
    assert(FrameSurvivingPlayers(f,look,{false,false}));
    assert(Near(f.subjects[1].feet.x,6) && Near(look.x,0));
    assert(FrameSurvivingPlayers(f,look,{false,true}));
    State state;
    auto shot = ComputeRetailRelativeFraming(f,look,{},state);
    assert(shot.valid && shot.fits && Near(shot.anchor.x,0));

    // P1 far below the world must not pull the survivor's view down with it.
    f = Scene(); f.subjects[0] = {{-30,-100,0},{-30,-98,0},0.3f};
    look = {-30,-70,0};
    assert(FrameSurvivingPlayers(f,look,{true,false}));
    assert(Near(look.x,6) && Near(look.y,1.25f));
    state.Reset(); shot = ComputeRetailRelativeFraming(f,look,{},state);
    assert(shot.valid && shot.fits && Near(shot.anchor.x,6) && Near(shot.anchor.y,1));
    assert(Near(shot.eye.y,1.25f));
    f = Scene();
    assert(!FrameSurvivingPlayers(f,look,{true,true})); // adapter holds displayed shot
    assert(Near(f.subjects[0].feet.x,0) && Near(f.subjects[1].feet.x,6));
    std::puts("PASS: recovery timers, pause/reset, double-death precedence, survivor framing and both-fallen hold");
}
