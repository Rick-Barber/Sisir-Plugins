#pragma once

// Builds the 3ds Max scene for one .sirbake, laid out as Sisir for Maya lays out its groups, with
// Dummy helpers in their place:
//
//   <Scene>_Sisir                       (user property sisir_source_file = the .sir)
//     <Scene>_Mesh       the mesh, read from its OBJ (polygons as authored), with its material
//     <Scene>_RefMesh    the reference mesh
//     <Scene>_Guides     one spline shape per guide layer, one spline per guide (Scripts/3DSMax layout)
//     <Scene>_Hair       one Sisir Hair object per hair set, named after the set
//     <Scene>_Cards      one Editable Poly per Cards layer, one material per UV ID
//
// Sisir is Y-up and Max Z-up: every point is converted (MaxUtil sisirToMax), so all nodes keep an
// identity transform. Every node it creates is recorded, so the import can be undone as one step.

#include "ImportOptions.h"

#include <max.h>

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <map>
#include <vector>

namespace SirBake { class Reader; }

namespace SisirMax {

using SisirDcc::ImportOptions;

struct BuildReport {
	int meshes = 0;
	int guideCurves = 0;
	int hairSets = 0;
	long long hairStrands = 0;
	int cardLayers = 0;
	int materials = 0;
	QStringList warnings;
	INode* root = nullptr;
};

class SceneBuilder {
public:
	using Progress = std::function<bool(float, const QString&)>;
	SceneBuilder(const SirBake::Reader& bake, const ImportOptions& options, const QString& assetsDir, const QString& sourceFile);

	// False only on a hard failure (or Cancel); partial problems land in report.warnings.
	bool build(BuildReport& report, QString* error, const Progress& progress);
	// Every node created, parents before children: what undo deletes.
	const std::vector<INode*>& createdNodes() const { return m_created; }

private:
	INode* addNode(Object* object, const QString& name, INode* parent);
	INode* makeGroup(const QString& name, INode* parent);
	// The dummy a META layer entry goes under: `root`, or nested dummies named after its Layers panel
	// folders, made once per folder (hidden when the folder's eye was off). `kind` keeps the guides'
	// and the cards' folders apart.
	INode* folderParent(const QJsonObject& layer, INode* root, const QString& kind);
	void importMesh(const char* key, const QString& groupName, INode* parent, BuildReport& report);
	void buildGuides(INode* parent, BuildReport& report, const Progress& progress);
	bool buildHair(INode* parent, BuildReport& report, const Progress& progress);
	void buildCards(INode* parent, BuildReport& report, const Progress& progress);

	// Materials, built through MAXScript (as the Maya plug-in builds its through MEL).
	Mtl* runMaterialScript(const QString& script);
	QString textureScript(const QString& relativePath, bool color, const QString& var, const QString& name);
	Mtl* meshMaterial(bool reference, const QString& textureRel);
	Mtl* hairMaterial(int materialId);
	Mtl* cardMaterial(int uvId, const QString& layerName, const float layerColor[3]);

	bool selected(const QStringList& wanted, const QString& name, bool visible) const;
	// Arnold stops a ray after Auto Transparency Depth cut-out surfaces (10 by default) and treats
	// the rest as opaque, which is what dense card tips -- many cut-outs deep -- render as. Raised,
	// never lowered, when Arnold is the renderer and the cards are cut out.
	void raiseArnoldTransparencyDepth(BuildReport& report);

	const SirBake::Reader& m_bake;
	ImportOptions m_options;
	QString m_assetsDir;
	QString m_sourceFile;
	QString m_scene;
	double m_sisirToSystem = 100.0; // Sisir metres -> Max system units
	bool m_arnold = false;
	bool m_cardAlpha = false; // a card material is cut out by an opacity map
	std::map<int, Mtl*> m_hairMaterials;
	std::map<QString, Mtl*> m_cardMaterials;
	std::map<QString, INode*> m_folderGroups;
	std::vector<INode*> m_created;
};

} // namespace SisirMax
