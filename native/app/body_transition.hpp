#pragma once
#include <string>
namespace hades {
struct BodyTransitionOptions {float duration=0,easeIn=0,easeOut=0;bool smoothStep=false,pingPong=false;};
// Scalar BodyComponentTransitionHelper state, RVAs2b5420/2b7440/310640.
// Owner's actual Body phase supplies elapsed and UndoModifier; this helper does
// not run on a renderer clock or imply actor/component registration.
// Native sgg::Interpolate 0x1eaba0 (ease > smooth-step Hermite > linear).
float bodyInterpolate(float start,float end,float t,const BodyTransitionOptions&);
class BodyTransition {
    bool initialized_=false,active_=false;
    float start_=1,end_=1,remaining_=0,fraction_=0,phase_=0;
    BodyTransitionOptions options_;
public:
    bool adjust(float target,float initial,const BodyTransitionOptions&,bool& changed,std::string&);
    bool advance(float elapsed,float undoModifier,std::string&);
    float current()const;
    bool active()const{return active_;}
};
}
