#pragma once
#include "interaction_runtime.hpp"
#include <array>

namespace hades {
// Actual Thing polygon and draw state, supplied separately from the existing
// Useable owner-frame provider. No screen layout or generic button box is made
// by the selector. Source screen geometry/lifetimes must already be current.
struct CursorInteractionGeometry {
 InteractionKey key;
 bool screenSpace=false;
 float parallax=1;
 int32_t drawGroup=-1;
 std::vector<std::array<float,2>> polygon;
};
using CursorInteractionGeometryProvider=std::function<bool(InteractionKey,CursorInteractionGeometry&,std::string&)>;

// PolygonHelpers.PointInPolygon2ceaf0 (float cross-product parity) and
// Rectangle.ContainsF20ed30 (integer rectangle, half-open right/bottom).
bool cursorPointInPolygon(const std::vector<std::array<float,2>>&,float x,float y);
bool cursorPointInTextArea(const InteractionRect&,float x,float y);

// Cursor.CheckUseable191a80 -> PlayerUnitInput.FindUseable20fa20, bounded to
// live screen CursorOnly+UseExtents Things with parallax1/z0. Native pointer
// hash order is not invented: unresolved polygon ties or order-sensitive
// overlapping text matches reject. Caller owns highlight/use/cursor updates.
bool selectCursorInteraction(const InteractionWorld&,const InteractionUserFrame&,
 InteractionClock,bool useMouse,float cursorX,float cursorY,
 const InteractionProviders&,const CursorInteractionGeometryProvider&,
 InteractionKey& selected,std::string& error);
}
