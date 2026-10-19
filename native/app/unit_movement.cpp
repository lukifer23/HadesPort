#include "unit_movement.hpp"
#include <cmath>
namespace hades {
bool applyUnitMoveInput(PhysicsState&p,UnitMovementState&s,const UnitMoveData&d,float angle,float fraction,float dt,const PhysicsForceOwner&force,const PhysicsForceCallbacks&callbacks,std::string&e){
 for(float x:{angle,fraction,dt,d.speed,d.speedAddition,d.speedModifier,d.playerSpeedMultiplier,d.acceleration,d.ownerAngle})if(!std::isfinite(x)){e="Nonfinite original Unit.MoveInput value";return false;}if(dt<0||fraction<0||fraction>1){e="Invalid native Unit.MoveInput fraction/elapsed";return false;}if(!d.canMove)return true;s.speedFraction=fraction;if(d.onlyForward)angle=d.ownerAngle;const float maximum=(d.speed+d.speedAddition)*d.speedModifier*(d.playerControlled?d.playerSpeedMultiplier:1.f),speed=fraction*maximum;const float squared=p.velocity.x*p.velocity.x+p.velocity.y*p.velocity.y;
 if(p.footingLost>0&&(squared<1||(maximum-1)*(maximum-1)<squared))p.footingLost=0;
 if(p.footingLost<=0){if(squared<(maximum+1)*(maximum+1)){const PhysicsVector v{std::cos(angle)*speed,-std::sin(angle)*speed};if(v.x!=0||v.y!=0){if(!callbacks.onForce){e="Actual Unit.MoveInput Thing.OnForce provider unavailable";return false;}p.velocity=v;p.lastForceSelfApplied=true;if(!callbacks.onForce(0,e))return false;}}
 else if(!applyPhysicsAngleForce(p,force,angle,speed*dt,p.lastForceSelfApplied,maximum,0,callbacks,e))return false;
 }else if(!applyPhysicsAngleForce(p,force,angle,dt*d.acceleration,true,maximum,0,callbacks,e))return false;return true;
}
namespace {
float cross(MovementPoint a,MovementPoint b,MovementPoint c){return(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);}
bool inside(MovementPoint p,std::span<const MovementPoint>v){bool yes=false;for(size_t i=0,j=v.size()-1;i<v.size();j=i++)if((v[i].y>p.y)!=(v[j].y>p.y)&&p.x<(v[j].x-v[i].x)*(p.y-v[i].y)/(v[j].y-v[i].y)+v[i].x)yes=!yes;return yes;}
}
bool movementPolygonsOverlap(std::span<const MovementPoint>a,std::span<const MovementPoint>b){if(a.size()<3||b.size()<3)return false;for(auto p:a)if(inside(p,b))return true;for(auto p:b)if(inside(p,a))return true;for(size_t i=0;i<a.size();++i)for(size_t j=0;j<b.size();++j){const auto x=a[i],y=a[(i+1)%a.size()],u=b[j],v=b[(j+1)%b.size()];if((cross(x,y,u)>0)!=(cross(x,y,v)>0)&&(cross(u,v,x)>0)!=(cross(u,v,y)>0))return true;}return false;}
}
