#include <ethernet/tether/FieldTether.hpp>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace ethernet::tether;
namespace camera = ethernet::camera;
void Check(bool value,const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
bool Near(float a,float b) { return std::abs(a-b)<0.0001f; }
bool Same(Vec3 a,Vec3 b) { return Near(a.x,b.x)&&Near(a.y,b.y)&&Near(a.z,b.z); }
camera::Subject Body(float x,float z,float radius=0) { return {{x,0,z},{x,1,z},radius}; }
int main() {
    ScreenBoundary view;
    view.valid=true; view.atNativeLimit=true;
    view.verticalFovRadians=1.57079632679f; view.aspect=1;
    auto step=[&](Vec3 v,camera::Subject body,float dt=0.1f) {
        return ConstrainToScreen(v,body,view,dt);
    };
    Check(Same(step({3,0,0},Body(5,10)),{3,0,0}),"full speed inside screen; no soft resistance");
    Check(Same(step({2,0,2},Body(10,10)),{2,0,2}),"slide along right frustum edge");
    Check(Same(step({2,3,-2},Body(10,10)),{0,3,0}),"right outward normal stopped, Y preserved");
    Check(Same(step({-2,0,2},Body(10,10)),{-2,0,2}),"inward motion unchanged");
    Check(Same(step({-2,0,-2},Body(-10,10)),{0,0,0}),"left edge symmetric");
    auto v=step({5,0,0},Body(9.9f,10));
    Check(9.9f+v.x*0.1f <= 10+v.z*0.1f+0.00001f,"predicted crossing clipped before leaving screen");
    Check(v.x>0 && v.x<5,"only final step portion clipped");
    Check(Same(step({5,0,0},Body(9,10)),{5,0,0}),"nearby but non-crossing step not slowed");
    view.atNativeLimit=false;
    Check(Same(step({20,4,-20},Body(10,10)),{20,4,-20}),"camera can expand: no constraint");
    view.atNativeLimit=true;
    const auto partner=Body(-10,10);
    v=ConstrainToScreen({2,0,2},Body(5,10),view,0.1f,&partner);
    Check(Same(v,{}),"camera-follow cannot push stationary partner off left edge");
    v=ConstrainToScreen({-2,0,-2},Body(5,10),view,0.1f,&partner);
    Check(Same(v,{-2,0,-2}),"moving toward partner remains unrestricted");
    const auto insidePartner=Body(-5,10);
    v=ConstrainToScreen({2,0,2},Body(5,10),view,0.1f,&insidePartner);
    Check(Same(v,{2,0,2}),"camera-follow guard has no soft band");
    view.aspect=2;
    Check(Same(step({5,0,0},Body(10,10)),{5,0,0}),"aspect ratio uses actual wider screen");
    view.aspect=1;
    v=step({2,0,-2},Body(9.4f,10,0.3f));
    Check(Same(v,{}),"body radius kept inside, not just center");
    Check(Same(step({-2,0,2},Body(12,10)),{-2,0,2}),"already outside may re-enter");
    Check(Same(step({2,0,-2},Body(12,10)),{}),"already outside cannot worsen edge");
    view.eye={100,0,50};
    Check(Same(step({2,0,-2},Body(110,60)),{}),"camera position translates screen volume");
    view.eye={};
    view.right={0,0,-1}; view.forward={1,0,0};
    Check(Same(step({-2,0,-2},Body(10,-10)),{}),"camera rotation rotates physical boundary");
    view.right={1,0,0};
    const float q=std::sqrt(0.5f);
    view.up={0,q,q}; view.forward={0,-q,q};
    camera::Subject lower{{0,-10*q,-10*q},{0,-10*q,-10*q},0};
    // Centered screen boundary, with no vertical offset.
    lower.feet={0,-10,0}; lower.look=lower.feet;
    v=step({2,5,-2},lower);
    Check(Near(v.x,2)&&Near(v.z,0)&&Near(v.y,5),"pitched bottom edge blocks depth but permits sideways and native Y");
    view.up={0,1,0}; view.forward={0,0,1};
    camera::Subject bottom{{0,-10,10},{0,-10,10},0};
    camera::Subject top{{0,10,10},{0,10,10},0};
    Check(Same(step({2,3,-2},bottom),{2,3,0}),"bottom boundary centered on visible screen");
    Check(Same(step({2,3,-2},top),{2,3,0}),"top boundary centered on visible screen");
    view.insetPercent=10;
    camera::Subject insetBottom{{0,-8,10},{0,-8,10},0};
    camera::Subject insetTop{{0,8,10},{0,8,10},0};
    Check(Same(step({0,0,-1},insetBottom),{}) && Same(step({0,0,-1},insetTop),{}),
        "inset shrinks symmetrically around screen center");
    Check(Same(step({1,0,-0.8f},Body(8,10)),{}),"inset also moves side boundary inward");
    view.atNativeLimit=false;
    Check(Same(step({1,0,-0.8f},Body(8,10)),{1,0,-0.8f}),"adjuster preserves unrestricted expansion");
    view.atNativeLimit=true;
    view.insetPercent=100;
    Check(Same(step({1,0,-0.5f},Body(5,10)),{}),"inset clamps at 25 percent");
    view.insetPercent=std::numeric_limits<float>::quiet_NaN();
    Check(Same(step({2,0,-2},Body(10,10)),{}),"invalid inset defaults to zero");
    view.insetPercent=0;
    Check(Same(step({1,0,0},Body(0,10),0),{1,0,0}),"zero delta ignored");
    view.valid=false;
    Check(Same(step({2,0,-2},Body(10,10)),{2,0,-2}),"invalid or stale native view bypassed");
    view.valid=true;
    view.aspect=std::numeric_limits<float>::quiet_NaN();
    Check(Same(step({2,0,-2},Body(10,10)),{2,0,-2}),"bad projection bypassed");
    std::cout << "PASS: screen-edge clipping, camera limits, perspective, body bounds and movement preservation\n";
}
