#pragma once

// Every Maya type ID the plug-in registers, in one place.
//
// Sisir's Maya Node ID block, registered with Autodesk (Oct 2026): 0x00143000 to 0x0014303F,
// 64 IDs. Every Sisir node or data type, in any plug-in, takes its ID from here -- never reuse
// or renumber one: binary scenes (.mb) store these numbers, and a scene saved with an ID only
// loads while that ID means the same type. Questions about the block: adn.sparks@autodesk.com.

#include <maya/MTypeId.h>

namespace SisirMaya {

inline constexpr unsigned int kIdBlockBase = 0x00143000;
inline constexpr unsigned int kIdBlockSize = 64;

inline constexpr unsigned int kHairDataId = kIdBlockBase + 0;
inline constexpr unsigned int kHairShapeId = kIdBlockBase + 1;

static_assert(kHairShapeId < kIdBlockBase + kIdBlockSize, "past the end of Sisir's Node ID block");

} // namespace SisirMaya
