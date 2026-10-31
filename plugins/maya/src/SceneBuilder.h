#pragma once

// Builds the Maya scene for one .sirbake:
//
//   <Scene>_Sisir                       (sisirSourceFile = the .sir)
//     <Scene>_Mesh       the mesh, imported from its OBJ, with its material
//     <Scene>_RefMesh    the reference mesh
//     <Scene>_Guides     one group per guide layer > GuideCurve_<n>   (Scripts/Maya layout)
//     <Scene>_Hair       one sisirHairShape per hair set, named after the set
//     <Scene>_Cards      one mesh per Cards layer, one material per UV ID
//
// A layer in Layers panel folders sits in nested groups named after them (META "folders"), each
// hidden when its folder's eye was off, under <Scene>_Guides / <Scene>_Cards.
//
// Every node it creates is recorded, so the import can be undone as one step.

#include "DccShared.h"

#include <maya/MObject.h>
#include <maya/MObjectHandle.h>
#include <maya/MString.h>

#include <QJsonObject>
#include <QString>

#include <functional>
#include <map>
#include <vector>

namespace SirBake { class Reader; }

namespace SisirMaya {

struct BuildReport {
	int meshes = 0;
	int guideCurves = 0;
	int hairSets = 0;
	long long hairStrands = 0;
	int cardLayers = 0;
	int materials = 0;
	QStringList warnings;
	MObject root;
};

class SceneBuilder {
public:
	using Progress = std::function<bool(float, const QString&)>;
	SceneBuilder(const SirBake::Reader& bake, const ImportOptions& options, const QString& assetsDir, const QString& sourceFile);

	// False only on a hard failure (or Cancel); partial problems land in report.warnings.
	bool build(BuildReport& report, QString* error, const Progress& progress);
	// Everything created, for undo. Node handles, so already-deleted children are skipped safely.
	const std::vector<MObjectHandle>& createdNodes() const { return m_created; }

private:
	struct Scales {
		double sisirToInternal = 100.0; // Sisir metres -> Maya internal units
		double internalPerUi = 1.0;     // one UI unit in internal units
	};

	MObject makeGroup(const QString& name, const MObject& parent);
	// The group a META layer entry goes into: `root`, or the nested folder groups below it, made
	// once per folder. `kind` keeps the guides' and the cards' folders apart.
	MObject folderParent(const QJsonObject& layer, const MObject& root, const QString& kind);
	void showCardAlphaInViewport(BuildReport& report);
	// Arnold stops a ray after Auto Transparency Depth cut-out surfaces (10 by default) and treats
	// the rest as opaque, which is what dense card tips -- many cut-outs deep -- render as. Raised,
	// never lowered, when the cards are cut out.
	void raiseArnoldTransparencyDepth(BuildReport& report);
	bool importMesh(const char* key, const QString& groupName, const MObject& parent, BuildReport& report);
	void buildGuides(const MObject& parent, BuildReport& report, const Progress& progress);
	bool buildHair(const MObject& parent, BuildReport& report, const Progress& progress);
	void buildCards(const MObject& parent, BuildReport& report, const Progress& progress);

	// Shading
	bool arnold() const { return m_arnold; }
	MString surfaceShader(const QString& name, MString* shadingGroup);
	MString fileTexture(const QString& relativePath, bool color, const QString& name);
	MString hairMaterial(int materialId);
	MString cardMaterial(int uvId, const QString& layerName, const float layerColor[3]);
	MString meshMaterial(bool reference, const QString& textureRel);
	void assign(const MString& shadingGroup, const MString& node);

	bool selected(const QStringList& wanted, const QString& name, bool visible) const;
	MString uniqueName(const QString& base) const;
	void track(const MObject& node);

	const SirBake::Reader& m_bake;
	ImportOptions m_options;
	QString m_assetsDir;
	QString m_sourceFile;
	QString m_scene; // legal Maya name for the scene
	Scales m_scales;
	bool m_arnold = false;
	bool m_cardAlpha = false; // a card material reads an alpha mask
	std::vector<MObjectHandle> m_created;
	std::vector<MString> m_hairMaterialSg; // per material id
	std::vector<std::pair<QString, MString>> m_cardMaterialSg;
	std::map<QString, MObject> m_folderGroups;
};

// [A-Za-z0-9_], not starting with a digit.
QString legalMayaName(const QString& name, const QString& fallback = QStringLiteral("Sisir"));

} // namespace SisirMaya
