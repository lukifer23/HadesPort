#pragma once
#include <algorithm>

namespace hades {
// Android adaptation. Thermal values are PowerManager's public API29 status
// (2 moderate, 3 severe, 4+ critical). These limits do not alter saved settings
// or the original fixed 60 Hz simulation. A lower user cap always wins.
inline int displayFrameCap(int requested,int gameMode,int thermalStatus,bool playing){
    int limit=playing?0:30; // menus must not run indefinitely at display rate
    if(gameMode==3)limit=limit?std::min(limit,60):60;
    const int thermalLimit=thermalStatus>=4?15:thermalStatus>=3?30:thermalStatus>=2?60:0;
    if(thermalLimit)limit=limit?std::min(limit,thermalLimit):thermalLimit;
    return requested>0?(limit?std::min(requested,limit):requested):limit;
}
}
