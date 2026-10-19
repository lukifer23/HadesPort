#pragma once
#include <string>
namespace hades {class AssetStore;}
class ScriptRuntime;
// Saves through the registered SaveCheckpoint action into a new directory,
// reloads in a fresh ScriptRuntime and compares the luabins state.
bool runSaveProbe(hades::AssetStore& assets,ScriptRuntime& booted,const std::string& directory,const std::string& reportPath);
