// The plug-in's tooltip catalogue, behind the app's UiTooltips interface so the shared controls
// (ResetToDefault, the dialogs) look their text up the same way. The ids follow the app's dotted
// scheme, so a future translation table covers the plug-in too. Unlike the app, nothing installs
// an application-wide event filter: inside Maya and 3ds Max, tooltips are set on each widget
// directly. Ids start with the host's prefix (HostInfo::tooltipPrefix) where the wording differs.

#include "UiTooltips.h"

#include <QHash>
#include <QString>

#include <functional>

namespace UiTooltips {

namespace {

struct Entry {
	const char* id;
	const char* text;
};

const Entry kCatalogue[] = {
	{ "common.resetToDefault", "Reset to default (%1)." },
	{ "common.resetSection", "Reset every value in this section to its default." },
	{ "common.plyExport.unit", "The unit the guide file is written in. Sisir's Import Curves (PLY) asks for it: pick the same one there." },

	{ "maya.import.mesh", "Import the scene's mesh with its UVs. An OBJ comes in exactly as you authored it; the mannequin or a morphed mesh comes as Sisir holds it." },
	{ "maya.import.refMesh", "Import the reference mesh." },
	{ "maya.import.materials", "Create Arnold shaders: the mesh's maps, a hair shader per Sisir hair material, a card shader per UV ID. Textures are copied into the texture folder." },
	{ "maya.import.guides", "Import the guides as NURBS curves: a group per layer, one curve per guide, in the layer's colour." },
	{ "maya.import.guideLayers", "The guide layers to import. Hidden layers are listed but start unchecked." },
	{ "maya.import.guideDegree", "Linear keeps Sisir's points exactly. Cubic runs a smooth curve through them." },
	{ "maya.import.hair", "Import the generated hair as Sisir hair shapes: every strand with its width, colour and material, ready to render in Arnold." },
	{ "maya.import.hairSets", "The hair sets to import, each with its strand count. Hidden sets are listed but start unchecked." },
	{ "maya.import.displayPercent", "How much of the hair the viewport draws. Renders always use every strand. Changeable later on each hair shape." },
	{ "maya.import.cards", "Import the hair cards as polygon meshes, one per Cards layer, with their UVs and normals." },
	{ "maya.import.cardLayers", "The Cards layers to import. Hidden layers are listed but start unchecked." },
	{ "maya.import.smoothCards", "Smooth cards follow the refined spine Sisir's Smooth Cards draws. Off imports the authored stations: a lighter mesh." },
	{ "maya.import.unit", "What one unit of the scene's linear unit stands for. Centimeters, with Maya in centimeters, brings everything in at its real size; a 30 cm head is 30 units. Exports convert back from it." },
	{ "maya.import.textureFolder", "Where meshes and textures are copied. Empty uses the project's sourceimages/sisir folder." },

	{ "max.import.mesh", "Import the scene's mesh with its UVs, polygons as authored. An OBJ comes in exactly as you authored it; the mannequin or a morphed mesh comes as Sisir holds it." },
	{ "max.import.refMesh", "Import the reference mesh." },
	{ "max.import.materials", "Create Arnold materials: the mesh's maps, a hair material per Sisir hair material, a card material per UV ID. Textures are copied into the texture folder." },
	{ "max.import.guides", "Import the guides as splines: one shape per layer, one spline per guide, in the layer's colour." },
	{ "max.import.guideLayers", "The guide layers to import. Hidden layers are listed but start unchecked." },
	{ "max.import.guideDegree", "Linear keeps Sisir's points exactly. Cubic runs a smooth spline through them." },
	{ "max.import.hair", "Import the generated hair as Sisir hair objects: every strand with its width, colour and material, ready to render in Arnold." },
	{ "max.import.hairSets", "The hair sets to import, each with its strand count. Hidden sets are listed but start unchecked." },
	{ "max.import.displayPercent", "How much of the hair the viewport draws. Renders always use every strand. Changeable later on each hair object." },
	{ "max.import.cards", "Import the hair cards as polygon objects, one per Cards layer, with their UVs and normals." },
	{ "max.import.cardLayers", "The Cards layers to import. Hidden layers are listed but start unchecked." },
	{ "max.import.smoothCards", "Smooth cards follow the refined spine Sisir's Smooth Cards draws. Off imports the authored stations: a lighter mesh." },
	{ "max.import.unit", "What one system unit stands for. Centimeters, with the system unit in centimeters, brings everything in at its real size; a 30 cm head is 30 units. Exports convert back from it." },
	{ "max.import.textureFolder", "Where meshes and textures are copied. Empty uses the project's sceneassets/images/sisir folder." },

	{ "blender.import.mesh", "Import the scene's mesh with its UVs, polygons as authored. An OBJ comes in exactly as you authored it; the mannequin or a morphed mesh comes as Sisir holds it." },
	{ "blender.import.refMesh", "Import the reference mesh." },
	{ "blender.import.materials", "Create Cycles and Eevee materials: the mesh's maps, a hair material per Sisir hair material, a card material per UV ID. Textures are copied into the texture folder." },
	{ "blender.import.guides", "Import the guides as curves: one object per layer, one curve per guide, in the layer's colour." },
	{ "blender.import.guideLayers", "The guide layers to import. Hidden layers are listed but start unchecked." },
	{ "blender.import.guideDegree", "Linear keeps Sisir's points exactly. Cubic runs a smooth curve through them." },
	{ "blender.import.hair", "Import the generated hair as Sisir hair objects: every strand with its width, colour and material, ready to render in Cycles and Eevee." },
	{ "blender.import.hairSets", "The hair sets to import, each with its strand count. Hidden sets are listed but start unchecked." },
	{ "blender.import.displayPercent", "How much of the hair the viewport draws. Renders always use every strand. Changeable later on each hair object." },
	{ "blender.import.cards", "Import the hair cards as mesh objects, one per Cards layer, with their UVs and normals." },
	{ "blender.import.cardLayers", "The Cards layers to import. Hidden layers are listed but start unchecked." },
	{ "blender.import.smoothCards", "Smooth cards follow the refined spine Sisir's Smooth Cards draws. Off imports the authored stations: a lighter mesh." },
	{ "blender.import.unit", "What one Blender unit stands for. Meters, Blender's own, brings everything in at its real size; a 30 cm head is 0.3 units. Exports convert back from it." },
	{ "blender.import.textureFolder", "Where meshes and textures are copied. Empty uses a sisir folder next to the .blend file (in Documents/Blender until the file is saved)." },

	// The hair object's Modify panel (3ds Max).
	{ "max.hair.widthScale", "Multiplies every strand's width, in the viewport and in renders." },
	{ "max.hair.displayPercent", "How much of the hair the viewport draws, thinned evenly. Renders are not affected." },
	{ "max.hair.renderPercent", "How much of the hair Arnold renders, the same even subset the viewport thins to." },
	{ "max.hair.colorMode", "Auto shows Sisir's colours while the hair wears its Sisir material, and the material's colour once another one is assigned." },
	{ "max.hair.setColor", "The hair set's colour from Sisir, used by Hair Set Color." },
};

std::function<QString(std::string_view)>& languageLookup() {
	static std::function<QString(std::string_view)> lookup;
	return lookup;
}

} // namespace

QString text(std::string_view id) {
	if (auto& lookup = languageLookup()) {
		const QString translated = lookup(id);
		if (!translated.isEmpty()) return translated;
	}
	for (const Entry& e : kCatalogue) {
		if (id == e.id) return QString::fromUtf8(e.text);
	}
	return {};
}

void setLanguageLookup(std::function<QString(std::string_view)> lookup) { languageLookup() = std::move(lookup); }

void install(QApplication&) {}

QString hoverText(const QWidget* w) { return w ? w->toolTip() : QString(); }

std::string_view resolveId(const QWidget*) { return {}; }

std::vector<std::string_view> catalogueIds() {
	std::vector<std::string_view> ids;
	for (const Entry& e : kCatalogue) ids.push_back(e.id);
	return ids;
}

} // namespace UiTooltips
