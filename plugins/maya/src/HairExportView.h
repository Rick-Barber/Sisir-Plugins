#pragma once

// The renderer-facing view of a sisirHairShape (common/HairExportView.h) and the entry point
// SisirForMaya.mll exports for it.

#include "../../common/HairExportView.h"

#include <maya/MObject.h>

// Exported by SisirForMaya.mll. False when `shapeNode` is not a sisirHairShape or has no hair.
extern "C" typedef bool (*SisirGetHairExportViewFn)(const MObject* shapeNode, SisirHairExportView* out);
inline constexpr const char* kSisirGetHairExportViewSymbol = "sisirGetHairExportView";
