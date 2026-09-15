#include <ethernet/tether/FieldTether.hpp>
#include <array>
#include <algorithm>
#include <cmath>

namespace ethernet::tether {
namespace {
float Dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
Vec3 Sub(Vec3 a, Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
Vec3 Plane(Vec3 f, float tangent, Vec3 axis, float sign) {
    return {f.x*tangent+axis.x*sign,f.y*tangent+axis.y*sign,f.z*tangent+axis.z*sign};
}
bool Finite(Vec3 v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
struct Constraint { float x,z,b; }; // x*dx + z*dz >= b; origin is feasible.
}
Vec3 ConstrainToScreen(Vec3 velocity, const camera::Subject& body,
                       const ScreenBoundary& view, float dt, const camera::Subject* partner) {
    if (!view.valid || !view.atNativeLimit || !Finite(velocity) || !Finite(body.feet) ||
        !Finite(body.look) || !Finite(view.eye) || !Finite(view.right) || !Finite(view.up) ||
        !Finite(view.forward) || !std::isfinite(body.radius) || body.radius<0 ||
        !std::isfinite(dt) || dt<=0 || !std::isfinite(view.verticalFovRadians) ||
        view.verticalFovRadians<=0 || view.verticalFovRadians>=3.14159f ||
        !std::isfinite(view.aspect) || view.aspect<=0) return velocity;
    if (std::abs(Dot(view.right,view.right)-1)>0.001f ||
        std::abs(Dot(view.up,view.up)-1)>0.001f ||
        std::abs(Dot(view.forward,view.forward)-1)>0.001f ||
        std::abs(Dot(view.right,view.up))>0.001f ||
        std::abs(Dot(view.right,view.forward))>0.001f ||
        std::abs(Dot(view.up,view.forward))>0.001f) return velocity;
    const float fullTanY=std::tan(view.verticalFovRadians*0.5f);
    const float inset=std::isfinite(view.insetPercent) ? std::clamp(view.insetPercent,0.0f,25.0f) : 0;
    const float ty=fullTanY*(1-2*inset/100), tx=ty*view.aspect;
    if (!std::isfinite(tx) || !std::isfinite(ty)) return velocity;
    const std::array<Vec3,5> normals{{Plane(view.forward,tx,view.right,1),
        Plane(view.forward,tx,view.right,-1),Plane(view.forward,ty,view.up,1),
        Plane(view.forward,ty,view.up,-1),view.forward}};
    if (partner && (!Finite(partner->feet) || !Finite(partner->look) ||
        !std::isfinite(partner->radius) || partner->radius<0)) return velocity;
    std::array<Constraint,10> constraints{};
    unsigned count=0;
    auto addBody=[&](const camera::Subject& subject,float direction) {
        const std::array<float,5> padding{{subject.radius*(1+tx),subject.radius*(1+tx),
            subject.radius*(1+ty),subject.radius*(1+ty),subject.radius+0.01f}};
        for (unsigned i=0;i<normals.size();++i) {
            const auto n=normals[i];
            const float clearance=std::min(Dot(Sub(subject.feet,view.eye),n),
                Dot(Sub(subject.look,view.eye),n))-padding[i];
            constraints[count++]={n.x*direction,n.z*direction,-std::max(0.0f,clearance)};
        }
    };
    // Camera turns may put an actor outside already. Prevent worsening, never snap.
    addBody(body,1);
    // Conservative one-step camera-follow guard. The shared anchor can translate
    // with this mover; reserve enough screen room for the stationary partner
    // even if the shot follows the entire step. No world-separation threshold.
    if (partner) addBody(*partner,-1);
    const float dx=velocity.x*dt, dz=velocity.z*dt;
    if (!std::isfinite(dx) || !std::isfinite(dz)) return velocity;
    auto feasible=[&](float x,float z) {
        for (unsigned i=0;i<count;++i) {
            const auto c=constraints[i];
            if (c.x*x+c.z*z<c.b-0.000001f) return false;
        }
        return true;
    };
    if (feasible(dx,dz)) return velocity;
    // Closest point in the convex XZ half-plane intersection: edge or corner.
    float bestX=0,bestZ=0,bestDistance=dx*dx+dz*dz;
    auto consider=[&](float x,float z) {
        const float distance=(x-dx)*(x-dx)+(z-dz)*(z-dz);
        if (std::isfinite(distance) && distance<bestDistance && feasible(x,z)) {
            bestX=x; bestZ=z; bestDistance=distance;
        }
    };
    for (unsigned i=0;i<count;++i) {
        const auto a=constraints[i];
        const float length=a.x*a.x+a.z*a.z;
        if (length>0.0000001f) {
            const float t=(a.b-a.x*dx-a.z*dz)/length;
            consider(dx+t*a.x,dz+t*a.z);
        }
        for (unsigned j=i+1;j<count;++j) {
            const auto b=constraints[j];
            const float determinant=a.x*b.z-a.z*b.x;
            if (std::abs(determinant)>0.0000001f)
                consider((a.b*b.z-a.z*b.b)/determinant,(a.x*b.b-a.b*b.x)/determinant);
        }
    }
    return {bestX/dt,velocity.y,bestZ/dt};
}
}
