#include "SceneBuilder.h"
#include "MetaFolders.h"

#include "HairObject.h"
#include "HairViewColor.h"
#include "MaxUtil.h"
#include "ObjReader.h"
#include "RenderingSpace.h"
#include "SirBakeReader.h"

#include <dummy.h>
#include <ifnpub.h>
#include <inode.h>
#include <MNNormalSpec.h>
#include <polyobj.h>
#include <splshape.h>

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>

#include <algorithm>
#include <cmath>

namespace SisirMax {

using SisirDcc::metaLayerShown;

namespace {

DWORD wireColor(const QJsonArray& c) {
	auto ch = [&](int k) { return (int)std::lround(std::clamp(c.at(k).toDouble(0.8), 0.0, 1.0) * 255.0); };
	return RGB(ch(0), ch(1), ch(2));
}

QString num(double v) { return QString::number(v, 'g', 9); }

QString msColor(double r, double g, double b) {
	return QString("(color %1 %2 %3)").arg(num(r * 255.0), num(g * 255.0), num(b * 255.0));
}

// An Editable Poly from polygons with per-corner vertex, uv and normal indices.
Object* makePoly(const std::vector<Point3>& verts, const std::vector<UVVert>& uvs, const std::vector<Point3>& normals,
                 const std::vector<uint32_t>& faceSizes, const std::vector<uint32_t>& faceVerts,
                 const std::vector<int32_t>* faceUvs, const std::vector<int32_t>* faceNormals) {
	PolyObject* poly = CreateEditablePolyObject();
	MNMesh& mm = poly->GetMesh();
	mm.setNumVerts((int)verts.size());
	for (size_t i = 0; i < verts.size(); i++) mm.V((int)i)->p = verts[i];
	mm.setNumFaces((int)faceSizes.size());
	std::vector<int> corner;
	size_t c = 0;
	for (size_t f = 0; f < faceSizes.size(); f++) {
		const int deg = (int)faceSizes[f];
		corner.assign(faceVerts.begin() + c, faceVerts.begin() + c + deg);
		mm.F((int)f)->MakePoly(deg, corner.data());
		mm.F((int)f)->smGroup = 1;
		c += deg;
	}
	if (faceUvs && !uvs.empty()) {
		mm.SetMapNum(2);
		mm.M(0)->SetFlag(MN_DEAD);
		MNMap* map = mm.M(1);
		map->ClearFlag(MN_DEAD);
		map->setNumVerts((int)uvs.size());
		for (size_t i = 0; i < uvs.size(); i++) map->v[i] = uvs[i];
		map->setNumFaces((int)faceSizes.size());
		c = 0;
		for (size_t f = 0; f < faceSizes.size(); f++) {
			const int deg = (int)faceSizes[f];
			corner.assign(faceUvs->begin() + c, faceUvs->begin() + c + deg);
			map->F((int)f)->MakePoly(deg, corner.data());
			c += deg;
		}
	}
	if (faceNormals && !normals.empty()) {
		mm.SpecifyNormals();
		MNNormalSpec* spec = mm.GetSpecifiedNormals();
		spec->SetParent(&mm);
		spec->SetNumNormals((int)normals.size());
		for (size_t i = 0; i < normals.size(); i++) {
			spec->Normal((int)i) = Normalize(normals[i]);
			spec->SetNormalExplicit((int)i, true);
		}
		spec->SetNumFaces((int)faceSizes.size());
		c = 0;
		for (size_t f = 0; f < faceSizes.size(); f++) {
			const int deg = (int)faceSizes[f];
			MNNormalFace& nf = spec->Face((int)f);
			nf.SetDegree(deg);
			nf.SpecifyAll();
			for (int k = 0; k < deg; k++) nf.SetNormalID(k, (*faceNormals)[c + k]);
			c += deg;
		}
		spec->CheckNormals();
	}
	mm.InvalidateGeomCache();
	mm.FillInMesh();
	return poly;
}

} // namespace

SceneBuilder::SceneBuilder(const SirBake::Reader& bake, const ImportOptions& options, const QString& assetsDir, const QString& sourceFile)
	: m_bake(bake), m_options(options), m_assetsDir(assetsDir), m_sourceFile(sourceFile) {
	m_scene = legalName(bake.meta().value("sceneName").toString(), "Sisir");
	// System-unit numbers = Sisir metres in the chosen unit (ImportOptions::unit).
	const QString unit = options.resolvedUnit();
	if (const double metres = ImportOptions::metresPerUnit(unit); metres > 0.0) {
		m_sisirToSystem = 1.0 / metres;
	} else if (unit == "scene") {
		m_sisirToSystem = systemUnitsPerMetre();
	} else {
		// "mesh": the user's mesh file numbers, as system units: Sisir metres / importScale.
		const double importScale = bake.meta().value("mesh").toObject().value("importScale").toDouble(0.01);
		m_sisirToSystem = importScale > 1e-12 ? 1.0 / importScale : 100.0;
	}
}

bool SceneBuilder::selected(const QStringList& wanted, const QString& name, bool visible) const {
	if (!wanted.isEmpty()) return wanted.contains(name);
	return visible || m_options.includeHidden;
}

INode* SceneBuilder::addNode(Object* object, const QString& name, INode* parent) {
	Interface* ip = GetCOREInterface();
	INode* node = ip->CreateObjectNode(object);
	if (!node) return nullptr;
	// Numbered only when the name is taken (MakeNameUnique would always append "001").
	MSTR unique = toM(legalName(name));
	if (ip->GetINodeByName(unique)) ip->MakeNameUnique(unique);
	node->SetName(unique);
	// What one of this node's numbers is in metres: Export Hair (ABC) and Export Guides (PLY) convert
	// back with it, whatever the system unit is or becomes.
	node->SetUserPropString(_M("sisir_metres_per_unit"), toM(QString::number(1.0 / m_sisirToSystem, 'g', 17)));
	if (parent) parent->AttachChild(node, FALSE);
	m_created.push_back(node);
	return node;
}

void SceneBuilder::raiseArnoldTransparencyDepth(BuildReport& report) {
	if (!m_arnold || !runMaxScriptBool("(try (classOf renderers.current == Arnold) catch (false))")) return;
	// The MAXtoA spelling of Arnold's auto_transparency_depth, whichever this version has.
	const QString names = "#(#auto_transparency_depth, #transparency_depth)";
	const bool found = runMaxScriptBool(QString(
		"(try (local r = renderers.current; local f = false; for n in %1 where isProperty r n do f = true; f) catch (false))").arg(names));
	if (!found) {
		report.warnings << "Raise Arnold's Auto Transparency Depth (Render Setup > Arnold Renderer > System / Ray Depth) to 64 "
		                   "if overlapping card cut-outs render dark.";
		return;
	}
	const bool raised = runMaxScriptBool(QString(
		"(try (local r = renderers.current; local seen = false; local raised = false; "
		"for n in %1 where not seen and isProperty r n do (seen = true; if (getProperty r n) < 64 do (setProperty r n 64; raised = true)); raised) "
		"catch (false))").arg(names));
	if (raised) {
		report.warnings << "Arnold Auto Transparency Depth raised to 64 so overlapping card cut-outs render; the viewport is not affected.";
	}
}

INode* SceneBuilder::folderParent(const QJsonObject& layer, INode* root, const QString& kind) {
	INode* parent = root;
	const std::vector<SisirDcc::MetaFolder> path = SisirDcc::metaFolders(layer);
	for (size_t d = 0; d < path.size(); d++) {
		const QString key = kind + QChar(0x1E) + SisirDcc::metaFolderKey(path, d);
		auto it = m_folderGroups.find(key);
		if (it == m_folderGroups.end()) {
			INode* group = makeGroup(path[d].name, parent);
			if (!group) return parent;
			if (!path[d].visible) group->Hide(TRUE);
			it = m_folderGroups.emplace(key, group).first;
		}
		parent = it->second;
	}
	return parent;
}

INode* SceneBuilder::makeGroup(const QString& name, INode* parent) {
	auto* dummy = static_cast<DummyObject*>(CreateInstance(HELPER_CLASS_ID, Class_ID(DUMMY_CLASS_ID, 0)));
	if (!dummy) return nullptr;
	// A small box, a couple of centimetres: a handle to pick the group by, not a frame around it.
	const float half = (float)(0.01 * systemUnitsPerMetre());
	dummy->SetBox(Box3(Point3(-half, -half, -half), Point3(half, half, half)));
	return addNode(dummy, name, parent);
}

// ---------------------------------------------------------------------------------------------
// Materials

// The material scripts are one statement per line: the first creates `m`, the last returns it.
// Every statement in between runs on its own, so a property one MAXtoA version spells or types
// differently (ai_image's color_space is an integer in MAXtoA 5.0) costs that one setting, never
// the whole material -- and the listener says which statement failed and why.
Mtl* SceneBuilder::runMaterialScript(const QString& script) {
	const QStringList lines = script.split('\n', Qt::SkipEmptyParts);
	if (lines.size() < 2) return nullptr;
	QString wrapped = "(\nlocal albedo, rough, metal, nrm, col, op, u\n" + lines.first() + "\n";
	for (int i = 1; i + 1 < lines.size(); i++) {
		QString statement = lines[i];
		if (statement.startsWith("local ")) statement = statement.mid(6); // declared above, so it outlives the try
		wrapped += QString("try (%1) catch (format \"Sisir warning: material step failed (%): %\\n\" %2 (getCurrentException()))\n")
		               .arg(statement, msQuote(statement.left(120)));
	}
	wrapped += lines.last() + "\n)";
	FPValue v;
	if (!runMaxScript(wrapped, &v) || v.type != TYPE_MTL || !v.mtl) {
		logWarning("a material could not be created: " + lines.first());
		return nullptr;
	}
	return v.mtl;
}

// MAXScript that leaves a map in `var` (or undefined), as Max bitmaps for every renderer: colour
// maps sRGB, so the viewport shows them; data maps (roughness, normals, masks) at gamma 1.0, never
// colour-corrected -- the same in Arnold, which reads a Max bitmap's gamma, as in Max's renderers.
QString SceneBuilder::textureScript(const QString& relativePath, bool color, const QString& var, const QString& name) {
	const QString path = relativePath.isEmpty() ? QString() : QDir(m_assetsDir).filePath(relativePath);
	if (path.isEmpty() || !QFileInfo(path).isFile()) return QString("local %1 = undefined\n").arg(var);
	const QString file = msQuote(QDir::toNativeSeparators(path));
	if (color) return QString("local %1 = Bitmaptexture name:%2 filename:%3\n").arg(var, msQuote(legalName(name)), file);
	return QString("local %1 = Bitmaptexture name:%2 bitmap:(openBitmap %3 gamma:1.0)\n").arg(var, msQuote(legalName(name)), file);
}

Mtl* SceneBuilder::meshMaterial(bool reference, const QString& textureRel) {
	const QJsonObject m = m_bake.meta().value("meshMaterial").toObject();
	const QString name = m_scene + (reference ? "_RefMeshMat" : "_MeshMat");
	QString s;
	if (m_arnold) {
		s += QString("local m = ai_standard_surface name:%1\n").arg(msQuote(name));
		const QString albedo = reference ? textureRel : m.value("albedoTexture").toString();
		s += textureScript(albedo, true, "albedo", name + "_albedo");
		s += "if albedo != undefined then (m.base_color_shader = albedo; m.base_color_connected = true; showTextureMap m albedo true)";
		if (!reference) {
			const QJsonArray c = m.value("albedoColor").toArray();
			s += QString(" else (m.base_color = %1)").arg(msColor(c.at(0).toDouble(1), c.at(1).toDouble(1), c.at(2).toDouble(1)));
		}
		s += "\n";
		if (reference) {
			s += "m.specular_roughness = 0.6\nm\n";
			return runMaterialScript(s);
		}
		s += QString("m.specular_roughness = %1\n").arg(num(m.value("roughness").toDouble(0.5)));
		s += textureScript(m.value("roughnessTexture").toString(), false, "rough", name + "_roughness");
		s += "if rough != undefined do (m.specular_roughness_shader = rough; m.specular_roughness_connected = true)\n";
		s += QString("m.metalness = %1\n").arg(num(m.value("metalness").toDouble(0.0)));
		s += textureScript(m.value("metalnessTexture").toString(), false, "metal", name + "_metalness");
		s += "if metal != undefined do (m.metalness_shader = metal; m.metalness_connected = true)\n";
		s += textureScript(m.value("normalTexture").toString(), false, "nrm", name + "_normal");
		// Sisir's Flip Y is on for OpenGL-convention maps (its V is flipped on load), which is exactly
		// what Arnold reads unflipped.
		s += QString("if nrm != undefined do (local nm = ai_normal_map name:%1; nm.input_shader = nrm; nm.input_connected = true; "
		             "nm.invert_y = %2; nm.strength = %3; m.normal_shader = nm; m.normal_connected = true)\n")
		         .arg(msQuote(name + "_normalMap"), m.value("normalFlipY").toBool(true) ? "false" : "true",
		              num(m.value("normalStrength").toDouble(1.0)));
		if (m.value("subsurface").toBool(false)) {
			const QJsonArray c = m.value("subsurfaceColor").toArray();
			// Sisir's subsurface weight drives its viewport approximation; Arnold's is physical and reads
			// far stronger at the same number. A tenth of it matches (Sisir 0.5 -> Arnold 0.05).
			s += QString("m.subsurface = %1\nm.subsurface_color = %2\nm.subsurface_scale = %3\n")
			         .arg(num(0.1 * m.value("subsurfaceWeight").toDouble(0.5)))
			         .arg(msColor(c.at(0).toDouble(1), c.at(1).toDouble(0.4), c.at(2).toDouble(0.3)))
			         .arg(num(m.value("subsurfaceRadius").toDouble(0.07) * m_sisirToSystem));
		}
	} else {
		s += QString("local m = PhysicalMaterial name:%1\n").arg(msQuote(name));
		const QString albedo = reference ? textureRel : m.value("albedoTexture").toString();
		s += textureScript(albedo, true, "albedo", name + "_albedo");
		s += "if albedo != undefined then (m.base_color_map = albedo; m.base_color_map_on = true; showTextureMap m albedo true)";
		if (!reference) {
			const QJsonArray c = m.value("albedoColor").toArray();
			s += QString(" else (m.base_color = %1)").arg(msColor(c.at(0).toDouble(1), c.at(1).toDouble(1), c.at(2).toDouble(1)));
		}
		s += "\n";
		if (reference) {
			s += "m.roughness = 0.6\nm\n";
			return runMaterialScript(s);
		}
		s += QString("m.roughness = %1\nm.metalness = %2\n").arg(num(m.value("roughness").toDouble(0.5)), num(m.value("metalness").toDouble(0.0)));
		s += textureScript(m.value("roughnessTexture").toString(), false, "rough", name + "_roughness");
		s += "if rough != undefined do (m.roughness_map = rough; m.roughness_map_on = true)\n";
		s += textureScript(m.value("metalnessTexture").toString(), false, "metal", name + "_metalness");
		s += "if metal != undefined do (m.metalness_map = metal; m.metalness_map_on = true)\n";
		s += textureScript(m.value("normalTexture").toString(), false, "nrm", name + "_normal");
		s += QString("if nrm != undefined do (local nb = Normal_Bump normal_map:nrm flipgreen:%1; m.bump_map = nb; m.bump_map_on = true; m.bump_map_amt = %2)\n")
		         .arg(m.value("normalFlipY").toBool(true) ? "false" : "true", num(m.value("normalStrength").toDouble(1.0)));
	}
	s += "m\n";
	return runMaterialScript(s);
}

Mtl* SceneBuilder::hairMaterial(int materialId) {
	materialId = std::clamp(materialId, 0, 9);
	if (!m_arnold) return nullptr;
	if (auto it = m_hairMaterials.find(materialId); it != m_hairMaterials.end()) return it->second;
	const QJsonObject m = m_bake.meta().value("hairMaterials").toArray().at(materialId).toObject();
	const QString name = QString("%1_HairMat%2").arg(m_scene).arg(materialId);
	// Colour: the per-point albedo Sisir computed, read from the curves' user data; melanin off so
	// base colour is the whole story, as in Sisir's preset colours.
	QString s = QString("local m = ai_standard_hair name:%1\n").arg(msQuote(name));
	s += QString("local u = ai_user_data_rgb name:%1\nu.attribute = \"sisir_albedo\"\n").arg(msQuote(name + "_albedo"));
	s += "m.base_color_shader = u\nm.base_color_connected = true\nm.melanin = 0\nm.base = 1\n";
	// Sisir's longitudinal roughness (default 0.05) to Arnold's perceptual one (default 0.2).
	const double roughness = std::clamp(std::sqrt(std::max(m.value("roughness").toDouble(0.05), 0.0)), 0.02, 1.0);
	s += QString("m.roughness = %1\nm.ior = %2\n").arg(num(roughness), num(m.value("ior").toDouble(1.55)));
	// Sisir's Specular scales the primary highlight; aiStandardHair tints it.
	const double spec = std::clamp(m.value("specularScale").toDouble(1.0), 0.0, 1.0);
	s += QString("m.specular_tint = %1\nm\n").arg(msColor(spec, spec, spec));
	Mtl* mtl = runMaterialScript(s);
	m_hairMaterials[materialId] = mtl;
	return mtl;
}

Mtl* SceneBuilder::cardMaterial(int uvId, const QString& layerName, const float layerColor[3]) {
	const QString key = uvId >= 0 ? QString("uv%1").arg(uvId) : "layer_" + layerName;
	if (auto it = m_cardMaterials.find(key); it != m_cardMaterials.end()) return it->second;
	const QString name = uvId >= 0 ? QString("%1_CardMat_UV%2").arg(m_scene).arg(uvId + 1)
	                               : QString("%1_CardMat_%2").arg(m_scene, legalName(layerName));
	const QJsonObject m = uvId >= 0 ? m_bake.meta().value("cardMaterials").toArray().at(uvId).toObject() : QJsonObject();
	const QString colorRel = m.value("colorTexture").toString();
	const QString maskRel = m.value("maskTexture").toString();
	const QJsonArray tint = m.value("baseColorTint").toArray();
	const bool tinted = !tint.isEmpty() && (tint.at(0).toDouble(1) != 1.0 || tint.at(1).toDouble(1) != 1.0 || tint.at(2).toDouble(1) != 1.0);
	QString s;
	// Cards are sheets seen from both sides.
	if (m_arnold) {
		s += QString("local m = ai_standard_surface name:%1\nm.thin_walled = true\n").arg(msQuote(name));
		if (uvId >= 0) {
			s += QString("m.specular_roughness = %1\nm.specular = %2\n").arg(num(m.value("roughness").toDouble(0.35)), num(m.value("specular").toDouble(0.5)));
		}
	} else {
		s += QString("local m = PhysicalMaterial name:%1\nm.thin_walled = true\n").arg(msQuote(name));
		if (uvId >= 0) s += QString("m.roughness = %1\n").arg(num(m.value("roughness").toDouble(0.35)));
	}
	s += textureScript(uvId >= 0 ? colorRel : QString(), true, "col", name + "_color");
	if (tinted) {
		// Sisir's tint is sRGB: linearised, then into the rendering space, as the hair's albedo is.
		const float srgb[3] = { (float)tint.at(0).toDouble(1), (float)tint.at(1).toDouble(1), (float)tint.at(2).toDouble(1) };
		float linear[3], toRendering[9], gain[3];
		SisirDcc::cardTintLinear(srgb, linear);
		renderingSpaceMatrix(toRendering);
		for (int r = 0; r < 3; r++) gain[r] = toRendering[r * 3] * linear[0] + toRendering[r * 3 + 1] * linear[1] + toRendering[r * 3 + 2] * linear[2];
		s += QString("if col != undefined do try (col = RGB_Multiply name:%1 map1:col color2:%2 map2Enabled:false) catch ()\n")
		         .arg(msQuote(name + "_tint"), msColor(gain[0], gain[1], gain[2]));
	}
	const QString slot = m_arnold ? "base_color_shader" : "base_color_map";
	const QString on = m_arnold ? "base_color_connected" : "base_color_map_on";
	s += QString("if col != undefined then (m.%1 = col; m.%2 = true; showTextureMap m col true) else (m.base_color = %3)\n")
	         .arg(slot, on, msColor(layerColor[0], layerColor[1], layerColor[2]));
	// Opacity: the mask map, else the colour map's alpha.
	if (uvId >= 0) {
		const QString maskPath = maskRel.isEmpty() ? QString() : QDir(m_assetsDir).filePath(maskRel);
		const QString colorPath = colorRel.isEmpty() ? QString() : QDir(m_assetsDir).filePath(colorRel);
		const bool haveMask = !maskPath.isEmpty() && QFileInfo(maskPath).isFile();
		const bool haveColor = !colorPath.isEmpty() && QFileInfo(colorPath).isFile();
		if (haveMask || haveColor) {
			m_cardAlpha = true;
			const QString file = msQuote(QDir::toNativeSeparators(haveMask ? maskPath : colorPath));
			if (m_arnold && haveMask) {
				// The mask, uncorrected.
				s += QString("local op = Bitmaptexture name:%1 bitmap:(openBitmap %2 gamma:1.0)\nm.opacity_shader = op\nm.opacity_connected = true\n")
				         .arg(msQuote(legalName(name + "_opacity")), file);
			} else if (m_arnold) {
				// The colour map's alpha, as one channel (Arnold never colour-manages alpha).
				s += QString("local op = ai_image name:%1 filename:%2 single_channel:true start_channel:3\nm.opacity_shader = op\nm.opacity_connected = true\n")
				         .arg(msQuote(legalName(name + "_opacity")), file);
			} else {
				s += QString("local op = Bitmaptexture name:%1 bitmap:(openBitmap %2 gamma:1.0)\n%3m.cutout_map = op\nm.cutout_map_on = true\n")
				         .arg(msQuote(legalName(name + "_opacity")), file, haveMask ? QString() : QString("op.rgbOutput = 1\n"));
			}
		}
		s += textureScript(m.value("roughnessTexture").toString(), false, "rough", name + "_roughness");
		s += m_arnold ? "if rough != undefined do (m.specular_roughness_shader = rough; m.specular_roughness_connected = true)\n"
		              : "if rough != undefined do (m.roughness_map = rough; m.roughness_map_on = true)\n";
		if (m_arnold) {
			s += textureScript(m.value("normalTexture").toString(), false, "nrm", name + "_normal");
			s += QString("if nrm != undefined do (local nm = ai_normal_map name:%1; nm.input_shader = nrm; nm.input_connected = true; "
			             "nm.invert_y = %2; nm.strength = %3; m.normal_shader = nm; m.normal_connected = true)\n")
			         .arg(msQuote(name + "_normalMap"), m.value("normalFlipGreen").toBool(false) ? "true" : "false",
			              num(m.value("normalStrength").toDouble(1.0)));
		}
	}
	s += "m\n";
	Mtl* mtl = runMaterialScript(s);
	m_cardMaterials[key] = mtl;
	return mtl;
}

// ---------------------------------------------------------------------------------------------
// Meshes

void SceneBuilder::importMesh(const char* key, const QString& groupName, INode* parent, BuildReport& report) {
	const QJsonObject m = m_bake.meta().value(key).toObject();
	if (!m.value("present").toBool(false)) return;
	const QString file = m.value("file").toString();
	const QString path = QDir(m_assetsDir).filePath(file);
	if (file.isEmpty() || !QFileInfo(path).isFile()) {
		report.warnings << QString("%1: the mesh file is missing from the bake").arg(groupName);
		return;
	}
	std::vector<ObjObject> objects;
	QString error;
	if (!readObj(path, objects, &error)) {
		report.warnings << QString("%1: %2").arg(groupName, error);
		return;
	}
	INode* group = makeGroup(groupName, parent);
	// File numbers -> Sisir metres -> system units, Y-up -> Z-up.
	const float s = (float)(m.value("importScale").toDouble(0.01) * m_sisirToSystem);
	Mtl* material = m_options.materials ? meshMaterial(std::string(key) == "refMesh", m.value("texture").toString()) : nullptr;
	if (material) report.materials++;
	for (const ObjObject& o : objects) {
		std::vector<Point3> verts(o.positions.size() / 3), normals(o.normals.size() / 3);
		for (size_t i = 0; i < verts.size(); i++) verts[i] = sisirToMax(o.positions[i * 3], o.positions[i * 3 + 1], o.positions[i * 3 + 2], s);
		for (size_t i = 0; i < normals.size(); i++) normals[i] = sisirToMax(o.normals[i * 3], o.normals[i * 3 + 1], o.normals[i * 3 + 2], 1.0f);
		std::vector<UVVert> uvs(o.uvs.size() / 2);
		for (size_t i = 0; i < uvs.size(); i++) uvs[i] = UVVert(o.uvs[i * 2], o.uvs[i * 2 + 1], 0.0f);
		Object* poly = makePoly(verts, uvs, normals, o.faceSizes, o.faceVerts, o.hasUvs ? &o.faceUvs : nullptr,
		                        o.hasNormals ? &o.faceNormals : nullptr);
		INode* node = addNode(poly, o.name.isEmpty() ? groupName + "Shape" : o.name, group);
		if (!node) continue;
		if (material) node->SetMtl(material);
		report.meshes++;
	}
}

// ---------------------------------------------------------------------------------------------
// Guides: the Scripts/3DSMax layout -- one spline shape per layer, one spline per guide.

void SceneBuilder::buildGuides(INode* parent, BuildReport& report, const Progress& progress) {
	const QJsonArray layers = m_bake.meta().value("guideLayers").toArray();
	INode* guidesGroup = nullptr;
	const float s = (float)m_sisirToSystem;
	for (int li = 0; li < layers.size(); li++) {
		const QJsonObject l = layers.at(li).toObject();
		const QString name = l.value("name").toString();
		if (!selected(m_options.guideLayers, name, metaLayerShown(l))) continue;
		const SirBake::CurvesView* v = m_bake.guides((uint32_t)l.value("chunk").toInt());
		if (!v || v->curveCount == 0) continue;
		if (!guidesGroup) guidesGroup = makeGroup(m_scene + "_Guides", parent);
		auto* shape = static_cast<SplineShape*>(CreateInstance(SHAPE_CLASS_ID, splineShapeClassID));
		if (!shape) continue;
		BezierShape& bez = shape->shape;
		const int knotType = m_options.guideDegree == 3 ? KTYPE_AUTO : KTYPE_CORNER;
		uint64_t offset = 0;
		for (uint32_t c = 0; c < v->curveCount; c++) {
			const uint32_t n = v->pointCounts[c];
			Spline3D* spline = bez.NewSpline();
			for (uint32_t p = 0; p < n; p++) {
				const float* q = v->positions + (offset + p) * 3;
				const Point3 pt = sisirToMax(q[0], q[1], q[2], s);
				spline->AddKnot(SplineKnot(knotType, LTYPE_CURVE, pt, pt, pt));
			}
			offset += n;
			spline->SetClosed(0);
			spline->ComputeBezPoints();
			report.guideCurves++;
		}
		bez.UpdateSels();
		bez.InvalidateGeomCache();
		INode* node = addNode(shape, name, folderParent(l, guidesGroup, QStringLiteral("guides")));
		if (!node) continue;
		node->SetWireColor(wireColor(l.value("color").toArray()));
		// A Max parent's Hide does not reach its children: a hidden folder hides each node in it.
		if (!metaLayerShown(l)) node->Hide(TRUE);
		// As Scripts/3DSMax/PLY_to_3DSMax.py tags them; 3DSMax_to_PLY.py keeps each as its own layer.
		node->SetUserPropString(_M("sisir_layer_id"), toM(QString::number(li)));
		node->SetUserPropString(_M("sisir_layer_name"), toM(name));
		node->SetUserPropString(_M("sisir_source_path"), toM(m_sourceFile));
		node->SetUserPropInt(_M("sisir_strand_count"), (int)v->curveCount);
		if (progress && !progress(-1.0f, QString())) return;
	}
}

// ---------------------------------------------------------------------------------------------
// Hair

bool SceneBuilder::buildHair(INode* parent, BuildReport& report, const Progress& progress) {
	const QJsonArray sets = m_bake.meta().value("hairSets").toArray();
	INode* hairGroup = nullptr;
	// Viewport budget: past this many points a groom is drawn thinned by default (Display % on the
	// object brings it back); rendering always uses every strand.
	constexpr double kViewportPointBudget = 6.0e6;
	double totalPoints = 0;
	for (const QJsonValue& v : sets) {
		const QJsonObject o = v.toObject();
		if (selected(m_options.hairSets, o.value("name").toString(), o.value("visible").toBool(true))) totalPoints += o.value("points").toDouble();
	}
	const double budgetPercent = totalPoints > kViewportPointBudget ? 100.0 * kViewportPointBudget / totalPoints : 100.0;
	const double displayPercent = std::min(m_options.displayPercent, budgetPercent);
	if (displayPercent < m_options.displayPercent - 0.01) {
		report.warnings << QString("Hair is shown at %1% in the viewport to keep it responsive (Display % on each hair object); renders use every strand.")
		                       .arg(displayPercent, 0, 'f', 0);
	}

	double donePoints = 0;
	const float s = (float)m_sisirToSystem;
	const TimeValue t = 0;
	for (int si = 0; si < sets.size(); si++) {
		const QJsonObject o = sets.at(si).toObject();
		const QString name = o.value("name").toString();
		if (!selected(m_options.hairSets, name, o.value("visible").toBool(true))) continue;
		const SirBake::HairView* v = m_bake.hair((uint32_t)o.value("chunk").toInt());
		if (!v || v->strandCount == 0) continue;
		if (!hairGroup) hairGroup = makeGroup(m_scene + "_Hair", parent);

		auto b = std::make_shared<HairBuffers>();
		b->pointCounts.assign(v->pointCounts, v->pointCounts + v->strandCount);
		b->positions.resize((size_t)v->pointCount * 3);
		for (size_t i = 0; i < v->pointCount; i++) {
			const Point3 p = sisirToMax(v->positions[i * 3], v->positions[i * 3 + 1], v->positions[i * 3 + 2], s);
			b->positions[i * 3] = p.x;
			b->positions[i * 3 + 1] = p.y;
			b->positions[i * 3 + 2] = p.z;
		}
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

		auto* hair = new HairObject();
		hair->setBuffers(b);
		IParamBlock2* pb = hair->params();
		pb->SetValue(kDisplayPercent, t, (float)displayPercent);
		pb->SetValue(kMaterialId, t, o.value("materialId").toInt());
		pb->SetValue(kSourceFile, t, toM(m_sourceFile).data());
		pb->SetValue(kHairSetName, t, toM(name).data());
		// As Sisir's ABC export takes them from the set's render material.
		const QJsonObject mat = m_bake.meta().value("hairMaterials").toArray().at(std::clamp(o.value("materialId").toInt(), 0, 9)).toObject();
		pb->SetValue(kGroomRoughness, t, (float)std::clamp(mat.value("roughness").toDouble(0.05), 0.0, 1.0));
		pb->SetValue(kGroomAo, t, mat.value("showHairAO").toBool(false) ? (float)std::clamp(mat.value("hairAOIntensity").toDouble(1.0), 0.0, 1.0) : 1.0f);
		const QJsonArray c = o.value("color").toArray();
		pb->SetValue(kSetColor, t, Color((float)c.at(0).toDouble(0.8), (float)c.at(1).toDouble(0.8), (float)c.at(2).toDouble(0.8)));
		INode* node = addNode(hair, name, hairGroup);
		if (!node) {
			report.warnings << QString("Hair set %1: the hair object could not be created").arg(name);
			continue;
		}
		const QJsonArray sc = o.value("color").toArray();
		node->SetWireColor(wireColor(sc));
		if (!o.value("visible").toBool(true)) node->Hide(TRUE);
		if (m_options.materials) {
			if (Mtl* mtl = hairMaterial(o.value("materialId").toInt())) node->SetMtl(mtl);
		}
		report.hairSets++;
		report.hairStrands += v->strandCount;
		// build() gives hair 35% to 85% of the bar, shared by point count.
		donePoints += o.value("points").toDouble();
		const float fraction = 0.35f + 0.5f * (float)std::min(donePoints / std::max(totalPoints, 1.0), 1.0);
		if (progress && !progress(fraction, "Hair: " + name)) return false;
	}
	return true;
}

// ---------------------------------------------------------------------------------------------
// Cards

void SceneBuilder::buildCards(INode* parent, BuildReport& report, const Progress& progress) {
	const QJsonArray layers = m_bake.meta().value("cardLayers").toArray();
	INode* cardsGroup = nullptr;
	const float s = (float)m_sisirToSystem;
	for (int li = 0; li < layers.size(); li++) {
		const QJsonObject l = layers.at(li).toObject();
		const QString name = l.value("name").toString();
		if (!selected(m_options.cardLayers, name, metaLayerShown(l))) continue;
		const SirBake::CardsView* v = m_bake.cards((uint32_t)l.value("chunk").toInt(), m_options.smoothCards);
		if (!v) v = m_bake.cards((uint32_t)l.value("chunk").toInt(), !m_options.smoothCards);
		if (!v || v->faceCount == 0) continue;
		if (!cardsGroup) cardsGroup = makeGroup(m_scene + "_Cards", parent);

		std::vector<Point3> verts(v->vertexCount), normals(v->vertexCount);
		std::vector<UVVert> uvs(v->vertexCount);
		for (uint32_t i = 0; i < v->vertexCount; i++) {
			verts[i] = sisirToMax(v->positions[i * 3], v->positions[i * 3 + 1], v->positions[i * 3 + 2], s);
			normals[i] = sisirToMax(v->normals[i * 3], v->normals[i * 3 + 1], v->normals[i * 3 + 2], 1.0f);
			uvs[i] = UVVert(v->uvs[i * 2], v->uvs[i * 2 + 1], 0.0f);
		}
		const std::vector<uint32_t> sizes(v->faceSizes, v->faceSizes + v->faceCount);
		const std::vector<uint32_t> corners(v->corners, v->corners + v->cornerCount);
		const std::vector<int32_t> perCorner(v->corners, v->corners + v->cornerCount);
		Object* poly = makePoly(verts, uvs, normals, sizes, corners, &perCorner, &perCorner);
		INode* node = addNode(poly, name, folderParent(l, cardsGroup, QStringLiteral("cards")));
		if (!node) {
			report.warnings << QString("Cards %1: the mesh could not be created").arg(name);
			continue;
		}
		const QJsonArray c = l.value("color").toArray();
		node->SetWireColor(wireColor(c));
		if (!metaLayerShown(l)) node->Hide(TRUE);
		if (m_options.materials) {
			const float layerColor[3] = { (float)c.at(0).toDouble(0.8), (float)c.at(1).toDouble(0.8), (float)c.at(2).toDouble(0.8) };
			if (Mtl* mtl = cardMaterial(l.value("uvId").toInt(-1), name, layerColor)) node->SetMtl(mtl);
		}
		report.cardLayers++;
		// build() gives cards the last 15% of the bar.
		if (progress && !progress(0.85f + 0.15f * (float)(li + 1) / (float)layers.size(), "Cards: " + name)) return;
	}
}

// ---------------------------------------------------------------------------------------------

bool SceneBuilder::build(BuildReport& report, QString* error, const Progress& progress) {
	if (m_options.materials) {
		m_arnold = runMaxScriptBool("(try (classOf ai_standard_hair == material) catch (false))");
		if (!m_arnold) {
			report.warnings << (m_options.hair
				? "Arnold (MAXtoA) is not installed: the hair shows in the viewport but renders only in Arnold "
				  "(or export it with Sisir > Export Hair (ABC)); surfaces use Physical Material."
				: "Arnold (MAXtoA) is not installed: surfaces use Physical Material.");
		}
	}

	INode* root = makeGroup(m_scene + "_Sisir", nullptr);
	if (!root) {
		if (error) *error = "the scene's root could not be created";
		return false;
	}
	root->SetUserPropString(_M("sisir_source_file"), toM(m_sourceFile));
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
		if (report.cardLayers > 0 && m_cardAlpha) raiseArnoldTransparencyDepth(report);
	}
	step(1.0f, "Done");
	GetCOREInterface()->SelectNode(root, 1);
	return true;
}

} // namespace SisirMax
