#pragma once
#include <string>
class ScriptRuntime;
namespace hades {class AssetStore;}
void captureLiveWorld(ScriptRuntime&,hades::AssetStore&,const std::string&,bool worldOnly=false);
