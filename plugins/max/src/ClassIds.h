#pragma once

// Every 3ds Max class and interface ID Sisir for 3ds Max registers, in one place.
//
// Never change or reuse one: .max files store these numbers, and a scene saved with an ID only
// loads while that ID means the same class (the Maya counterpart is maya/src/PluginIds.h).

#include <maxtypes.h>
#include <ifnpub.h>

namespace SisirMax {

inline const Class_ID kHairObjectClassId(0x5478e83f, 0x30cc8199);
inline const Class_ID kImporterClassId(0x10d40253, 0x5cfbb668);
inline const Interface_ID kSisirInterfaceId(0x7795ea6b, 0x5e685a96);
// Reserved for the next class: 0x559c204a, 0x18e470ec.

// Chunks inside a Sisir Hair object's saved data.
inline constexpr USHORT kHairDataChunk = 0x5300;

} // namespace SisirMax
