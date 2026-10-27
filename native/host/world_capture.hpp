#pragma once
#include <memory>
#include <string>
class ScriptRuntime;
namespace hades {class AssetStore;class RadialBlurState;}
void captureLiveWorld(ScriptRuntime&,hades::AssetStore&,const std::string&,bool worldOnly=false);
// Runs the shared FullscreenBlurRenderer pass on an offscreen GL target with a
// fixed input and compares every pixel with a CPU evaluation of the original
// RadialBlur.frag (bilinear, clamp), so a broken GPU pass fails the replay.
class FullscreenEffectValidator {
    struct Impl;std::unique_ptr<Impl> impl_;
public:
    FullscreenEffectValidator();~FullscreenEffectValidator();
    bool validateRadialBlur(const hades::RadialBlurState&,int& maxChannelError,std::string& error);
};
