#include "SceneBuilder.h"

#include "HairShape.h"
#include "MetaFolders.h"
#include "SirBakeReader.h"

#include <maya/MDGMessage.h>
#include <maya/MDagModifier.h>
#include <maya/MDistance.h>
#include <maya/MDoubleArray.h>
#include <maya/MFloatArray.h>
#include <maya/MFloatPointArray.h>
#include <maya/MFnDagNode.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnMesh.h>
#include <maya/MFnNurbsCurve.h>
#include <maya/MFnPointArrayData.h>
#include <maya/MFnStringData.h>
#include <maya/MFnTransform.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MGlobal.h>
#include <maya/MIntArray.h>
#include <maya/MPlug.h>
#include <maya/MPointArray.h>
#include <maya/MSelectionList.h>
#include <maya/MStringArray.h>
#include <maya/MVectorArray.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace SisirMaya {

namespace {

MString toM(const QString& s) { return MString(reinterpret_cast<const wchar_t*>(s.utf16())); }
QString toQ(const MString& s) { return QString::fromUtf8(s.asUTF8()); }

// Paths inside MEL string literals.
QString melPath(const QString& path) { return QDir::fromNativeSeparators(path).replace('"', "\\\""); }

bool mel(const QString& command, MString* result = nullptr) {
	MString out;
	const MStatus s = MGlobal::executeCommand(toM(command), out, false, false);
	if (result) *result = out;
	return (bool)s;
}

bool melArray(const QString& command, MStringArray& result) {
	return (bool)MGlobal::executeCommand(toM(command), result, false, false);
}

// An "mtllib" or "usemtl" with no name (RizomUV writes a bare "mtllib") makes the OBJ importer of
// Maya 2022/2023 drop the whole mesh as invalid data; 2025 just skips the line. Such statements name
// nothing, so a copy without them imports the same geometry. Returns the path to import.
QString objForImport(const QString& path) {
	auto isEmptyMaterialStatement = [](const QByteArray& line) {
		const QByteArray t = line.trimmed();
		return t == "mtllib" || t == "usemtl";
	};
	QFile in(path);
	if (!in.open(QIODevice::ReadOnly)) return path;
	bool found = false;
	while (!in.atEnd() && !found) found = isEmptyMaterialStatement(in.readLine());
	if (!found) return path;

	const QFileInfo info(path);
	const QString cleaned = info.dir().filePath(info.completeBaseName() + "_maya.obj");
	QFile out(cleaned);
	if (!in.seek(0) || !out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return path;
	while (!in.atEnd()) {
		const QByteArray line = in.readLine();
		if (!isEmptyMaterialStatement(line) && out.write(line) != line.size()) return path;
	}
	return out.flush() ? cleaned : path;
}

MObject nodeByName(const MString& name) {
	MSelectionList sel;
	MObject obj;
	if (sel.add(name) && sel.getDependNode(0, obj)) return obj;
	return MObject::kNullObj;
}

QString pathName(const MObject& node) {
	MFnDagNode dag(node);
	return toQ(dag.fullPathName());
}

void setColorOverride(const MObject& transform, const QJsonArray& color) {
	MFnDependencyNode fn(transform);
	fn.findPlug("overrideEnabled", true).setBool(true);
	fn.findPlug("overrideRGBColors", true).setBool(true);
	MPlug rgb = fn.findPlug("overrideColorRGB", true);
	for (unsigned int k = 0; k < 3 && k < rgb.numChildren(); k++) rgb.child(k).setFloat((float)color.at((int)k).toDouble(0.8));
}

void addStringAttr(const MObject& node, const char* longName, const char* shortName, const QString& value) {
	MFnDependencyNode fn(node);
	if (!fn.hasAttribute(longName)) {
		MFnTypedAttribute typed;
		MFnStringData sd;
		MObject attr = typed.create(longName, shortName, MFnData::kString, sd.create(""));
		fn.addAttribute(attr);
	}
	fn.findPlug(longName, true).setString(toM(value));
}

void nodeAdded(MObject& node, void* client) {
	static_cast<std::vector<MObjectHandle>*>(client)->emplace_back(node);
}

} // namespace

QString legalMayaName(const QString& name, const QString& fallback) {
	QString out;
	for (const QChar c : name) out += (c.isLetterOrNumber() && c.unicode() < 128) || c == '_' ? c : QChar('_');
	while (out.contains("__")) out.replace("__", "_");
	if (out.isEmpty() || out == "_") out = fallback;
	if (out.at(0).isDigit()) out.prepend('_');
	return out;
}

SceneBuilder::SceneBuilder(const SirBake::Reader& bake, const ImportOptions& options, const QString& assetsDir, const QString& sourceFile)
	: m_bake(bake), m_options(options), m_assetsDir(assetsDir), m_sourceFile(sourceFile) {
	m_scene = legalMayaName(bake.meta().value("sceneName").toString(), "Sisir");
	const double internalPerMetre = MDistance(1.0, MDistance::kMeters).as(MDistance::internalUnit());
	m_scales.internalPerUi = MDistance(1.0, MDistance::uiUnit()).as(MDistance::internalUnit());
	// Numbers in Maya's current unit = Sisir metres in the chosen unit (ImportOptions::unit).
	const QString unit = options.resolvedUnit();
	if (const double metres = ImportOptions::metresPerUnit(unit); metres > 0.0) {
		m_scales.sisirToInternal = (1.0 / metres) * m_scales.internalPerUi;
	} else if (unit == "scene") {
		m_scales.sisirToInternal = internalPerMetre;
	} else {
		// "mesh": the user's mesh file numbers, in Maya's current unit: Sisir metres / importScale.
		const double importScale = bake.meta().value("mesh").toObject().value("importScale").toDouble(0.01);
		m_scales.sisirToInternal = (importScale > 1e-12 ? 1.0 / importScale : 100.0) * m_scales.internalPerUi;
	}
	m_hairMaterialSg.resize(10);
}

bool SceneBuilder::selected(const QStringList& wanted, const QString& name, bool visible) const {
	if (!wanted.isEmpty()) return wanted.contains(name);
	return visible || m_options.includeHidden;
}

void SceneBuilder::track(const MObject& node) { m_created.emplace_back(node); }

MObject SceneBuilder::folderParent(const QJsonObject& layer, const MObject& root, const QString& kind) {
	MObject parent = root;
	const std::vector<MetaFolder> path = metaFolders(layer);
	for (size_t d = 0; d < path.size(); d++) {
		const QString key = kind + QChar(0x1E) + metaFolderKey(path, d);
		auto it = m_folderGroups.find(key);
		if (it == m_folderGroups.end()) {
			const MObject group = makeGroup(path[d].name, parent);
			// A hidden folder hides what is in it, the way Sisir showed it.
			if (!path[d].visible) MFnDagNode(group).findPlug("visibility", true).setBool(false);
			it = m_folderGroups.emplace(key, group).first;
		}
		parent = it->second;
	}
	return parent;
}

MString SceneBuilder::uniqueName(const QString& base) const { return toM(legalMayaName(base)); }

MObject SceneBuilder::makeGroup(const QString& name, const MObject& parent) {
	MFnTransform fn;
	MObject node = fn.create(parent);
	fn.setName(toM(legalMayaName(name)));
	return node;
}

// ---------------------------------------------------------------------------------------------
// Shading

MString SceneBuilder::surfaceShader(const QString& name, MString* shadingGroup) {
	MString shader;
	const QString type = m_arnold ? "aiStandardSurface" : "standardSurface";
	if (!mel(QString("shadingNode -asShader %1 -name \"%2\"").arg(type, legalMayaName(name)), &shader)) return {};
	MString sg;
	mel(QString("sets -renderable true -noSurfaceShader true -empty -name \"%1SG\"").arg(toQ(shader)), &sg);
	mel(QString("connectAttr -f \"%1.outColor\" \"%2.surfaceShader\"").arg(toQ(shader), toQ(sg)));
	if (shadingGroup) *shadingGroup = sg;
	return shader;
}

MString SceneBuilder::fileTexture(const QString& relativePath, bool color, const QString& name) {
	if (relativePath.isEmpty()) return {};
	const QString path = QDir(m_assetsDir).filePath(relativePath);
	if (!QFileInfo(path).isFile()) return {};
	MString file, place;
	if (!mel(QString("shadingNode -asTexture -isColorManaged file -name \"%1\"").arg(legalMayaName(name)), &file)) return {};
	mel(QString("shadingNode -asUtility place2dTexture -name \"%1_place2d\"").arg(toQ(file)), &place);
	static const char* kPlaceLinks[] = { "coverage", "translateFrame", "rotateFrame", "mirrorU", "mirrorV", "stagger",
	                                     "wrapU", "wrapV", "repeatUV", "offset", "rotateUV", "noiseUV",
	                                     "vertexUvOne", "vertexUvTwo", "vertexUvThree", "vertexCameraOne" };
	for (const char* a : kPlaceLinks) mel(QString("connectAttr -f \"%1.%3\" \"%2.%3\"").arg(toQ(place), toQ(file), a));
	mel(QString("connectAttr -f \"%1.outUV\" \"%2.uv\"").arg(toQ(place), toQ(file)));
	mel(QString("connectAttr -f \"%1.outUvFilterSize\" \"%2.uvFilterSize\"").arg(toQ(place), toQ(file)));
	mel(QString("setAttr -type \"string\" \"%1.fileTextureName\" \"%2\"").arg(toQ(file), melPath(path)));
	// Data maps (roughness, normals, masks) are not colour: no view transform on them.
	mel(QString("setAttr -type \"string\" \"%1.colorSpace\" \"%2\"").arg(toQ(file), color ? "sRGB" : "Raw"));
	if (!color) mel(QString("setAttr \"%1.ignoreColorSpaceFileRules\" 1").arg(toQ(file)));
	return file;
}

void SceneBuilder::assign(const MString& shadingGroup, const MString& node) {
	if (shadingGroup.length() == 0) return;
	mel(QString("sets -e -forceElement \"%1\" \"%2\"").arg(toQ(shadingGroup), toQ(node)));
}

MString SceneBuilder::meshMaterial(bool reference, const QString& textureRel) {
	const QJsonObject m = m_bake.meta().value("meshMaterial").toObject();
	MString sg;
	const MString shader = surfaceShader(m_scene + (reference ? "_RefMeshMat" : "_MeshMat"), &sg);
	if (shader.length() == 0) return {};
	const QString s = toQ(shader);
	const QString albedo = reference ? textureRel : m.value("albedoTexture").toString();
	if (MString tex = fileTexture(albedo, true, s + "_albedo"); tex.length()) {
		mel(QString("connectAttr -f \"%1.outColor\" \"%2.baseColor\"").arg(toQ(tex), s));
	} else if (!reference) {
		const QJsonArray c = m.value("albedoColor").toArray();
		mel(QString("setAttr \"%1.baseColor\" %2 %3 %4").arg(s).arg(c.at(0).toDouble(1)).arg(c.at(1).toDouble(1)).arg(c.at(2).toDouble(1)));
	}
	if (reference) {
		mel(QString("setAttr \"%1.specularRoughness\" 0.6").arg(s));
		return sg;
	}
	mel(QString("setAttr \"%1.specularRoughness\" %2").arg(s).arg(m.value("roughness").toDouble(0.5)));
	if (MString tex = fileTexture(m.value("roughnessTexture").toString(), false, s + "_roughness"); tex.length()) {
		mel(QString("connectAttr -f \"%1.outColorR\" \"%2.specularRoughness\"").arg(toQ(tex), s));
	}
	mel(QString("setAttr \"%1.metalness\" %2").arg(s).arg(m.value("metalness").toDouble(0.0)));
	if (MString tex = fileTexture(m.value("metalnessTexture").toString(), false, s + "_metalness"); tex.length()) {
		mel(QString("connectAttr -f \"%1.outColorR\" \"%2.metalness\"").arg(toQ(tex), s));
	}
	if (MString tex = fileTexture(m.value("normalTexture").toString(), false, s + "_normal"); tex.length()) {
		if (m_arnold) {
			MString nm;
			mel(QString("shadingNode -asUtility aiNormalMap -name \"%1_normalMap\"").arg(s), &nm);
			mel(QString("connectAttr -f \"%1.outColor\" \"%2.input\"").arg(toQ(tex), toQ(nm)));
			// Sisir's Flip Y is on for OpenGL-convention maps (its V is flipped on load), which is
			// exactly what Arnold reads unflipped.
			mel(QString("setAttr \"%1.invertY\" %2").arg(toQ(nm)).arg(m.value("normalFlipY").toBool(true) ? 0 : 1));
			mel(QString("setAttr \"%1.strength\" %2").arg(toQ(nm)).arg(m.value("normalStrength").toDouble(1.0)));
			mel(QString("connectAttr -f \"%1.outValue\" \"%2.normalCamera\"").arg(toQ(nm), s));
		} else {
			MString bump;
			mel(QString("shadingNode -asUtility bump2d -name \"%1_bump\"").arg(s), &bump);
			mel(QString("setAttr \"%1.bumpInterp\" 1").arg(toQ(bump)));
			mel(QString("connectAttr -f \"%1.outAlpha\" \"%2.bumpValue\"").arg(toQ(tex), toQ(bump)));
			mel(QString("connectAttr -f \"%1.outNormal\" \"%2.normalCamera\"").arg(toQ(bump), s));
		}
	}
	if (m.value("subsurface").toBool(false)) {
		const QJsonArray c = m.value("subsurfaceColor").toArray();
		// Sisir's subsurface weight drives its viewport approximation; Arnold's is physical and reads
		// far stronger at the same number. A tenth of it matches (Sisir 0.5 -> Arnold 0.05).
		mel(QString("setAttr \"%1.subsurface\" %2").arg(s).arg(0.1 * m.value("subsurfaceWeight").toDouble(0.5)));
		mel(QString("setAttr \"%1.subsurfaceColor\" %2 %3 %4").arg(s).arg(c.at(0).toDouble(1)).arg(c.at(1).toDouble(0.4)).arg(c.at(2).toDouble(0.3)));
		mel(QString("setAttr \"%1.subsurfaceScale\" %2").arg(s).arg(m.value("subsurfaceRadius").toDouble(0.07) * m_scales.sisirToInternal));
	}
	return sg;
}

MString SceneBuilder::hairMaterial(int materialId) {
	materialId = std::clamp(materialId, 0, 9);
	if (!m_arnold) return {};
	if (m_hairMaterialSg[(size_t)materialId].length()) return m_hairMaterialSg[(size_t)materialId];
	const QJsonObject m = m_bake.meta().value("hairMaterials").toArray().at(materialId).toObject();
	MString shader, sg, userColor;
	if (!mel(QString("shadingNode -asShader aiStandardHair -name \"%1_HairMat%2\"").arg(m_scene).arg(materialId), &shader)) return {};
	mel(QString("sets -renderable true -noSurfaceShader true -empty -name \"%1SG\"").arg(toQ(shader)), &sg);
	mel(QString("connectAttr -f \"%1.outColor\" \"%2.surfaceShader\"").arg(toQ(shader), toQ(sg)));
	// Colour: the per-point albedo Sisir computed, read from the curves' user data; melanin off so
	// base colour is the whole story, as in Sisir's preset colours.
	mel(QString("shadingNode -asUtility aiUserDataColor -name \"%1_albedo\"").arg(toQ(shader)), &userColor);
	mel(QString("setAttr -type \"string\" \"%1.attribute\" \"sisir_albedo\"").arg(toQ(userColor)));
	mel(QString("connectAttr -f \"%1.outColor\" \"%2.baseColor\"").arg(toQ(userColor), toQ(shader)));
	mel(QString("setAttr \"%1.melanin\" 0").arg(toQ(shader)));
	mel(QString("setAttr \"%1.base\" 1").arg(toQ(shader)));
	// Sisir's longitudinal roughness (default 0.05) to Arnold's perceptual one (default 0.2).
	const double roughness = std::clamp(std::sqrt(std::max(m.value("roughness").toDouble(0.05), 0.0)), 0.02, 1.0);
	mel(QString("setAttr \"%1.roughness\" %2").arg(toQ(shader)).arg(roughness));
	mel(QString("setAttr \"%1.ior\" %2").arg(toQ(shader)).arg(m.value("ior").toDouble(1.55)));
	// Sisir's Specular scales the primary highlight; aiStandardHair tints it.
	const double spec = std::clamp(m.value("specularScale").toDouble(1.0), 0.0, 1.0);
	mel(QString("setAttr \"%1.specularTint\" %2 %2 %2").arg(toQ(shader)).arg(spec));
	m_hairMaterialSg[(size_t)materialId] = sg;
	return sg;
}

MString SceneBuilder::cardMaterial(int uvId, const QString& layerName, const float layerColor[3]) {
	const QString key = uvId >= 0 ? QString("uv%1").arg(uvId) : "layer_" + layerName;
	for (const auto& e : m_cardMaterialSg) if (e.first == key) return e.second;
	MString sg;
	const MString shader = surfaceShader(uvId >= 0 ? QString("%1_CardMat_UV%2").arg(m_scene).arg(uvId + 1)
	                                               : QString("%1_CardMat_%2").arg(m_scene, legalMayaName(layerName)), &sg);
	if (shader.length() == 0) return {};
	const QString s = toQ(shader);
	// Cards are sheets seen from both sides.
	mel(QString("setAttr \"%1.thinWalled\" 1").arg(s));
	bool textured = false;
	if (uvId >= 0) {
		const QJsonObject m = m_bake.meta().value("cardMaterials").toArray().at(uvId).toObject();
		mel(QString("setAttr \"%1.specularRoughness\" %2").arg(s).arg(m.value("roughness").toDouble(0.35)));
		mel(QString("setAttr \"%1.specular\" %2").arg(s).arg(m.value("specular").toDouble(0.5)));
		const MString color = fileTexture(m.value("colorTexture").toString(), true, s + "_color");
		if (color.length()) {
			textured = true;
			const QJsonArray tint = m.value("baseColorTint").toArray();
			mel(QString("setAttr \"%1.colorGain\" %2 %3 %4").arg(toQ(color)).arg(tint.at(0).toDouble(1)).arg(tint.at(1).toDouble(1)).arg(tint.at(2).toDouble(1)));
			mel(QString("connectAttr -f \"%1.outColor\" \"%2.baseColor\"").arg(toQ(color), s));
		}
		// Opacity: the mask map, else the colour map's alpha.
		const MString mask = fileTexture(m.value("maskTexture").toString(), false, s + "_mask");
		const QString opacitySource = mask.length() ? toQ(mask) + ".outColorR" : (color.length() ? toQ(color) + ".outAlpha" : QString());
		if (!opacitySource.isEmpty()) {
			for (const char* ch : { "R", "G", "B" }) mel(QString("connectAttr -f \"%1\" \"%2.opacity%3\"").arg(opacitySource, s, ch));
			if (!mask.length()) mel(QString("setAttr \"%1.alphaIsLuminance\" 0").arg(toQ(color)));
			m_cardAlpha = true;
		}
		if (MString tex = fileTexture(m.value("roughnessTexture").toString(), false, s + "_roughness"); tex.length()) {
			mel(QString("connectAttr -f \"%1.outColorR\" \"%2.specularRoughness\"").arg(toQ(tex), s));
		}
		if (MString tex = fileTexture(m.value("normalTexture").toString(), false, s + "_normal"); tex.length() && m_arnold) {
			MString nm;
			mel(QString("shadingNode -asUtility aiNormalMap -name \"%1_normalMap\"").arg(s), &nm);
			mel(QString("connectAttr -f \"%1.outColor\" \"%2.input\"").arg(toQ(tex), toQ(nm)));
			mel(QString("setAttr \"%1.invertY\" %2").arg(toQ(nm)).arg(m.value("normalFlipGreen").toBool(false) ? 1 : 0));
			mel(QString("setAttr \"%1.strength\" %2").arg(toQ(nm)).arg(m.value("normalStrength").toDouble(1.0)));
			mel(QString("connectAttr -f \"%1.outValue\" \"%2.normalCamera\"").arg(toQ(nm), s));
		}
	}
	if (!textured) {
		mel(QString("setAttr \"%1.baseColor\" %2 %3 %4").arg(s).arg(layerColor[0]).arg(layerColor[1]).arg(layerColor[2]));
	}
	m_cardMaterialSg.emplace_back(key, sg);
	return sg;
}

// ---------------------------------------------------------------------------------------------
// Meshes

bool SceneBuilder::importMesh(const char* key, const QString& groupName, const MObject& parent, BuildReport& report) {
	const QJsonObject m = m_bake.meta().value(key).toObject();
	if (!m.value("present").toBool(false)) return true;
	const QString file = m.value("file").toString();
	const QString path = QDir(m_assetsDir).filePath(file);
	if (file.isEmpty() || !QFileInfo(path).isFile()) {
		report.warnings << QString("%1: the mesh file is missing from the bake").arg(groupName);
		return true;
	}
	mel("if (!`pluginInfo -q -loaded objExport`) loadPlugin -quiet objExport");
	MStringArray newNodes;
	const QString cmd = QString("file -import -type \"OBJ\" -ignoreVersion -mergeNamespacesOnClash false -renamingPrefix \"%1\" "
	                            "-options \"mo=1\" -returnNewNodes \"%2\"").arg(legalMayaName(groupName), melPath(objForImport(path)));
	if (!melArray(cmd, newNodes)) {
		report.warnings << QString("%1: Maya could not import %2").arg(groupName, QFileInfo(path).fileName());
		return true;
	}
	const MObject group = makeGroup(groupName, parent);
	const double importScale = m.value("importScale").toDouble(0.01);
	const double scale = importScale * m_scales.sisirToInternal / m_scales.internalPerUi;
	if (std::abs(scale - 1.0) > 1e-9) {
		const double s3[3] = { scale, scale, scale };
		MFnTransform(group).setScale(s3);
	}
	// Top-level transforms of the import go under the group; the importer's own materials go once
	// ours are assigned.
	MStringArray meshes, importedMaterials;
	for (unsigned int i = 0; i < newNodes.length(); i++) {
		const MObject node = nodeByName(newNodes[i]);
		if (node.isNull()) continue;
		if (node.hasFn(MFn::kTransform)) {
			MFnDagNode dag(node);
			if (dag.parentCount() > 0 && dag.parent(0).hasFn(MFn::kWorld)) {
				MDagModifier mod;
				mod.reparentNode(node, group);
				mod.doIt();
				meshes.append(dag.fullPathName());
			}
		} else if (node.hasFn(MFn::kShadingEngine) || node.hasFn(MFn::kLambert) || node.apiType() == MFn::kMaterialInfo) {
			importedMaterials.append(newNodes[i]);
		}
		track(node);
	}
	report.meshes += (int)meshes.length();
	if (m_options.materials) {
		const MString sg = meshMaterial(std::string(key) == "refMesh", m.value("texture").toString());
		if (sg.length()) {
			for (unsigned int i = 0; i < meshes.length(); i++) assign(sg, meshes[i]);
			report.materials++;
			for (unsigned int i = 0; i < importedMaterials.length(); i++) {
				if (importedMaterials[i] != "initialShadingGroup") mel(QString("catchQuiet(`delete \"%1\"`)").arg(toQ(importedMaterials[i])));
			}
		}
	}
	return true;
}

// ---------------------------------------------------------------------------------------------
// Guides: the Scripts/Maya layout -- a group per layer holding one curve per guide.

void SceneBuilder::buildGuides(const MObject& parent, BuildReport& report, const Progress& progress) {
	const QJsonArray layers = m_bake.meta().value("guideLayers").toArray();
	MObject guidesGroup;
	for (int li = 0; li < layers.size(); li++) {
		const QJsonObject l = layers.at(li).toObject();
		const QString name = l.value("name").toString();
		if (!selected(m_options.guideLayers, name, metaLayerShown(l))) continue;
		const SirBake::CurvesView* v = m_bake.guides((uint32_t)l.value("chunk").toInt());
		if (!v || v->curveCount == 0) continue;
		if (guidesGroup.isNull()) guidesGroup = makeGroup(m_scene + "_Guides", parent);
		const MObject layerGroup = makeGroup(name, folderParent(l, guidesGroup, QStringLiteral("guides")));
		setColorOverride(layerGroup, l.value("color").toArray());
		// Export Guides (PLY) writes the layer back under Sisir's own id and name: a Maya node name
		// cannot hold every name Sisir can ("Top Lashes" becomes Top_Lashes).
		addStringAttr(layerGroup, "sisirLayerName", "sln", name);
		addStringAttr(layerGroup, "sisirLayerId", "sli", QString::number(li));
		if (!l.value("visible").toBool(true)) MFnDagNode(layerGroup).findPlug("visibility", true).setBool(false);
		const double s = m_scales.sisirToInternal;
		uint64_t offset = 0;
		for (uint32_t c = 0; c < v->curveCount; c++) {
			const uint32_t n = v->pointCounts[c];
			MPointArray points(n);
			for (uint32_t p = 0; p < n; p++) {
				const float* q = v->positions + (offset + p) * 3;
				points.set(p, q[0] * s, q[1] * s, q[2] * s);
			}
			offset += n;
			MFnTransform tfn;
			MObject layerParent = layerGroup;
			MObject transform = tfn.create(layerParent);
			MString curveName("GuideCurve_");
			curveName += (int)(c + 1);
			tfn.setName(curveName);
			MFnNurbsCurve curve;
			MStatus st;
			if (m_options.guideDegree == 3 && n >= 4) {
				curve.createWithEditPoints(points, 3, MFnNurbsCurve::kOpen, false, false, true, transform, &st);
				// A cubic curve holds CVs solved from these points; its edit points come back only to the
				// solve's rounding. Export Guides (PLY) writes these exact points while the curve is unedited.
				MFnTypedAttribute typed;
				MFnPointArrayData data;
				MObject attr = typed.create("sisirGuidePoints", "sgp", MFnData::kPointArray, MObject::kNullObj);
				typed.setHidden(true);
				MFnDependencyNode node(transform);
				node.addAttribute(attr);
				node.findPlug(attr, true).setMObject(data.create(points)); // set, not a default: saved with the scene
			} else {
				MDoubleArray knots(n);
				for (uint32_t k = 0; k < n; k++) knots.set((double)k, k);
				curve.create(points, knots, 1, MFnNurbsCurve::kOpen, false, false, transform, &st);
			}
			if (st) report.guideCurves++;
		}
		if (progress && !progress(-1.0f, QString())) return;
	}
}

// ---------------------------------------------------------------------------------------------
// Hair

bool SceneBuilder::buildHair(const MObject& parent, BuildReport& report, const Progress& progress) {
	const QJsonArray sets = m_bake.meta().value("hairSets").toArray();
	MObject hairGroup;
	// Viewport budget: past this many points a groom is drawn thinned by default (Display % on the
	// shape brings it back); rendering always uses every strand.
	constexpr double kViewportPointBudget = 6.0e6;
	double totalPoints = 0;
	for (const QJsonValue& v : sets) {
		const QJsonObject o = v.toObject();
		if (selected(m_options.hairSets, o.value("name").toString(), o.value("visible").toBool(true))) totalPoints += o.value("points").toDouble();
	}
	const double budgetPercent = totalPoints > kViewportPointBudget ? 100.0 * kViewportPointBudget / totalPoints : 100.0;
	const double displayPercent = std::min(m_options.displayPercent, budgetPercent);
	if (displayPercent < m_options.displayPercent - 0.01) {
		report.warnings << QString("Hair is shown at %1% in the viewport to keep it responsive (Display Percent on each hair shape); renders use every strand.")
		                       .arg(displayPercent, 0, 'f', 0);
	}

	int done = 0;
	double donePoints = 0;
	for (int si = 0; si < sets.size(); si++) {
		const QJsonObject o = sets.at(si).toObject();
		const QString name = o.value("name").toString();
		if (!selected(m_options.hairSets, name, o.value("visible").toBool(true))) continue;
		const SirBake::HairView* v = m_bake.hair((uint32_t)o.value("chunk").toInt());
		if (!v || v->strandCount == 0) continue;
		if (hairGroup.isNull()) hairGroup = makeGroup(m_scene + "_Hair", parent);

		auto b = std::make_shared<HairBuffers>();
		const float s = (float)m_scales.sisirToInternal;
		b->pointCounts.assign(v->pointCounts, v->pointCounts + v->strandCount);
		b->positions.resize((size_t)v->pointCount * 3);
		for (size_t i = 0; i < b->positions.size(); i++) b->positions[i] = v->positions[i] * s;
		b->widths.resize(v->pointCount);
		for (size_t i = 0; i < b->widths.size(); i++) b->widths[i] = v->widths[i] * s;
		b->albedo.assign(v->albedo, v->albedo + (size_t)v->pointCount * 4);
		b->rootUvs.assign(v->rootUvs, v->rootUvs + (size_t)v->strandCount * 2);
		b->clumpIds.assign(v->clumpIds, v->clumpIds + v->strandCount);
		b->randoms.assign(v->randoms, v->randoms + v->strandCount);
		if (!b->finalize()) {
			report.warnings << QString("Hair set %1 is inconsistent and was skipped").arg(name);
			continue;
		}

		MObject transform = makeGroup(name, hairGroup);
		MFnDagNode dag;
		MStatus st;
		const MObject shapeObj = dag.create(HairShape::id, transform, &st);
		if (!st) {
			report.warnings << QString("Hair set %1: the hair node could not be created").arg(name);
			continue;
		}
		dag.setName(toM(legalMayaName(name) + "Shape"));
		auto* shape = dynamic_cast<HairShape*>(dag.userNode());
		if (!shape || !shape->setBuffers(b)) {
			report.warnings << QString("Hair set %1: the hair could not be stored").arg(name);
			continue;
		}
		MFnDependencyNode fn(shapeObj);
		fn.findPlug(HairShape::aDisplayPercent, true).setFloat((float)displayPercent);
		fn.findPlug(HairShape::aMaterialId, true).setInt(o.value("materialId").toInt());
		fn.findPlug(HairShape::aSourceFile, true).setString(toM(m_sourceFile));
		fn.findPlug(HairShape::aHairSetName, true).setString(toM(name));
		// As Sisir's ABC export takes them from the set's render material.
		const QJsonObject mat = m_bake.meta().value("hairMaterials").toArray().at(std::clamp(o.value("materialId").toInt(), 0, 9)).toObject();
		fn.findPlug(HairShape::aGroomRoughness, true).setFloat((float)std::clamp(mat.value("roughness").toDouble(0.05), 0.0, 1.0));
		fn.findPlug(HairShape::aGroomAo, true).setFloat(mat.value("showHairAO").toBool(false)
			? (float)std::clamp(mat.value("hairAOIntensity").toDouble(1.0), 0.0, 1.0) : 1.0f);
		const QJsonArray c = o.value("color").toArray();
		MPlug color = fn.findPlug(HairShape::aSetColor, true);
		for (unsigned int k = 0; k < 3; k++) color.child(k).setFloat((float)c.at((int)k).toDouble(0.8));
		if (!o.value("visible").toBool(true)) MFnDagNode(transform).findPlug("visibility", true).setBool(false);
		if (m_options.materials) {
			if (MString sg = hairMaterial(o.value("materialId").toInt()); sg.length()) assign(sg, dag.fullPathName());
		}
		report.hairSets++;
		report.hairStrands += v->strandCount;
		done++;
		// build() gives hair 35% to 85% of the bar, shared by point count.
		donePoints += o.value("points").toDouble();
		const float fraction = 0.35f + 0.5f * (float)std::min(donePoints / std::max(totalPoints, 1.0), 1.0);
		if (progress && !progress(fraction, "Hair: " + name)) return false;
	}
	return true;
}

// ---------------------------------------------------------------------------------------------
// Cards

void SceneBuilder::buildCards(const MObject& parent, BuildReport& report, const Progress& progress) {
	const QJsonArray layers = m_bake.meta().value("cardLayers").toArray();
	MObject cardsGroup;
	for (int li = 0; li < layers.size(); li++) {
		const QJsonObject l = layers.at(li).toObject();
		const QString name = l.value("name").toString();
		if (!selected(m_options.cardLayers, name, metaLayerShown(l))) continue;
		const SirBake::CardsView* v = m_bake.cards((uint32_t)l.value("chunk").toInt(), m_options.smoothCards);
		if (!v) v = m_bake.cards((uint32_t)l.value("chunk").toInt(), !m_options.smoothCards);
		if (!v || v->faceCount == 0) continue;
		if (cardsGroup.isNull()) cardsGroup = makeGroup(m_scene + "_Cards", parent);

		const float s = (float)m_scales.sisirToInternal;
		MFloatPointArray points(v->vertexCount);
		for (uint32_t i = 0; i < v->vertexCount; i++) {
			points.set(i, v->positions[i * 3] * s, v->positions[i * 3 + 1] * s, v->positions[i * 3 + 2] * s);
		}
		MIntArray counts(v->faceCount), connects(v->cornerCount);
		for (uint32_t f = 0; f < v->faceCount; f++) counts.set((int)v->faceSizes[f], f);
		for (uint32_t c = 0; c < v->cornerCount; c++) connects.set((int)v->corners[c], c);
		MFloatArray us(v->vertexCount), vs(v->vertexCount);
		for (uint32_t i = 0; i < v->vertexCount; i++) {
			us.set(v->uvs[i * 2], i);
			vs.set(v->uvs[i * 2 + 1], i);
		}
		MObject transform = makeGroup(name, folderParent(l, cardsGroup, QStringLiteral("cards")));
		MFnMesh mesh;
		MStatus st;
		const MObject meshObj = mesh.create((int)v->vertexCount, (int)v->faceCount, points, counts, connects, us, vs, transform, &st);
		if (!st) {
			report.warnings << QString("Cards %1: the mesh could not be created").arg(name);
			continue;
		}
		mesh.assignUVs(counts, connects);
		MVectorArray normals(v->vertexCount);
		MIntArray vertexIds(v->vertexCount);
		for (uint32_t i = 0; i < v->vertexCount; i++) {
			normals.set(MVector(v->normals[i * 3], v->normals[i * 3 + 1], v->normals[i * 3 + 2]), i);
			vertexIds.set((int)i, i);
		}
		mesh.setVertexNormals(normals, vertexIds);
		mesh.setName(toM(legalMayaName(name) + "Shape"));
		if (!l.value("visible").toBool(true)) MFnDagNode(transform).findPlug("visibility", true).setBool(false);
		if (m_options.materials) {
			const QJsonArray c = l.value("color").toArray();
			const float layerColor[3] = { (float)c.at(0).toDouble(0.8), (float)c.at(1).toDouble(0.8), (float)c.at(2).toDouble(0.8) };
			const MString sg = cardMaterial(l.value("uvId").toInt(-1), name, layerColor);
			// Arnold 7 reads the shader's opacity directly; MtoA's old per-shape aiOpaque is deprecated.
			assign(sg, mesh.fullPathName());
		}
		report.cardLayers++;
		// build() gives cards the last 15% of the bar.
		if (progress && !progress(0.85f + 0.15f * (float)(li + 1) / (float)layers.size(), "Cards: " + name)) return;
	}
}

// ---------------------------------------------------------------------------------------------

// Viewport 2.0 draws a card's texture-driven opacity only with the Alpha Cut transparency algorithm
// (its default, Object Sorting, leaves the cards solid) and only with textures shown. Both are
// viewport settings Arnold never reads, so renders are unchanged. Interactive sessions only.
void SceneBuilder::showCardAlphaInViewport(BuildReport& report) {
	if (MGlobal::mayaState() != MGlobal::kInteractive) return;
	constexpr int kAlphaCut = 5; // hardwareRenderingGlobals.transparencyAlgorithm
	int algorithm = -1;
	MGlobal::executeCommand("getAttr \"hardwareRenderingGlobals.transparencyAlgorithm\"", algorithm, false, false);
	if (algorithm != kAlphaCut) {
		mel(QString("setAttr \"hardwareRenderingGlobals.transparencyAlgorithm\" %1").arg(kAlphaCut));
		report.warnings << "Viewport 2.0 transparency set to Alpha Cut so the cards' alpha shows in the viewport; renders are not affected.";
	}
	mel("for ($p in `getPanel -type modelPanel`) modelEditor -e -displayTextures true $p;");
}

void SceneBuilder::raiseArnoldTransparencyDepth(BuildReport& report) {
	constexpr int kDepth = 64;
	if (!m_arnold) return;
	int exists = 0;
	MGlobal::executeCommand("objExists \"defaultArnoldRenderOptions\"", exists, false, false);
	if (!exists) {
		// A scene Arnold has not rendered yet has no options node: MtoA makes it, defaults and all.
		mel("catchQuiet(`python(\"import mtoa.core; mtoa.core.createOptions()\")`)");
		MGlobal::executeCommand("objExists \"defaultArnoldRenderOptions\"", exists, false, false);
	}
	if (!exists) return;
	int depth = kDepth;
	MGlobal::executeCommand("getAttr \"defaultArnoldRenderOptions.autoTransparencyDepth\"", depth, false, false);
	if (depth >= kDepth) return;
	mel(QString("setAttr \"defaultArnoldRenderOptions.autoTransparencyDepth\" %1").arg(kDepth));
	report.warnings << QString("Arnold Auto Transparency Depth raised from %1 to %2 so overlapping card cut-outs render "
	                           "(Render Settings > Arnold Renderer > Ray Depth); the viewport is not affected.")
	                       .arg(depth).arg(kDepth);
}

bool SceneBuilder::build(BuildReport& report, QString* error, const Progress& progress) {
	MStatus st;
	const MCallbackId cb = MDGMessage::addNodeAddedCallback(nodeAdded, "dependNode", &m_created, &st);
	struct Remove { MCallbackId id; bool ok; ~Remove() { if (ok) MMessage::removeCallback(id); } } remover{ cb, (bool)st };

	if (m_options.materials) {
		mel("if (!`pluginInfo -q -loaded mtoa`) catchQuiet(`loadPlugin -quiet mtoa`)");
		int loaded = 0;
		MGlobal::executeCommand("pluginInfo -q -loaded mtoa", loaded, false, false);
		m_arnold = loaded != 0;
		if (!m_arnold) {
			report.warnings << (m_options.hair
				? "Arnold (MtoA) is not loaded: the hair shows in the viewport but renders only in Arnold "
				  "(or export it with Sisir > Export Hair (ABC)); surfaces use Standard Surface."
				: "Arnold (MtoA) is not loaded: surfaces use Standard Surface.");
		}
	}

	const MObject root = makeGroup(m_scene + "_Sisir", MObject::kNullObj);
	addStringAttr(root, "sisirSourceFile", "ssf", m_sourceFile);
	// What one internal unit of everything under the root is in metres: Export Hair (ABC) and Export
	// Guides (PLY) convert back with it, whatever the scene's unit is or becomes.
	addStringAttr(root, "sisirMetresPerInternal", "smi", QString::number(1.0 / m_scales.sisirToInternal, 'g', 17));
	report.root = root;

	auto step = [&](float f, const QString& label) { return !progress || progress(f, label); };
	if (m_options.mesh) {
		if (!step(0.05f, "Mesh")) return false;
		importMesh("mesh", m_scene + "_Mesh", root, report);
	}
	if (m_options.refMesh) {
		if (!step(0.15f, "Reference mesh")) return false;
		importMesh("refMesh", m_scene + "_RefMesh", root, report);
	}
	if (m_options.guides) {
		if (!step(0.25f, "Guides")) return false;
		buildGuides(root, report, progress);
	}
	if (m_options.hair) {
		if (!step(0.35f, "Hair")) return false;
		if (!buildHair(root, report, progress)) {
			if (error) *error = "Import cancelled";
			return false;
		}
	}
	if (m_options.cards) {
		if (!step(0.85f, "Cards")) return false;
		buildCards(root, report, progress);
		if (report.cardLayers > 0 && m_cardAlpha) {
			showCardAlphaInViewport(report);
			raiseArnoldTransparencyDepth(report);
		}
	}
	step(1.0f, "Done");
	MObject selectRoot = root;
	MGlobal::select(selectRoot, MGlobal::kReplaceList);
	return true;
}

} // namespace SisirMaya
