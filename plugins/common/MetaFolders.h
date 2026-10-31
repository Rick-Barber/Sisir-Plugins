#pragma once

// The Layers panel folders a baked layer sits in: META's guideLayers / cardLayers entries carry
// "folders", the chain outermost first, each with its own eye. Every plug-in reads them here, so
// Maya, 3ds Max and the import dialog nest and pre-check layers the same way (the Blender add-on
// mirrors these rules in builder.py). A bake from before folders has none: every layer at the top
// level, exactly as it always imported.

#include <QChar>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <vector>

namespace SisirDcc {

struct MetaFolder {
	QString name;
	bool visible = true;
};

inline std::vector<MetaFolder> metaFolders(const QJsonObject& layer) {
	std::vector<MetaFolder> path;
	for (const QJsonValue& v : layer.value("folders").toArray()) {
		const QJsonObject f = v.toObject();
		MetaFolder folder;
		folder.name = f.value("name").toString();
		if (folder.name.isEmpty()) folder.name = QStringLiteral("Folder");
		folder.visible = f.value("visible").toBool(true);
		path.push_back(folder);
	}
	return path;
}

// What Sisir shows: the layer's own eye, and every folder above it showing. What an import takes
// by default.
inline bool metaLayerShown(const QJsonObject& layer) {
	if (!layer.value("visible").toBool(true)) return false;
	for (const MetaFolder& f : metaFolders(layer)) {
		if (!f.visible) return false;
	}
	return true;
}

// "Hairline / Front / " in front of a layer's name in the import dialog; empty at the top level.
inline QString metaFolderPrefix(const QJsonObject& layer) {
	QString prefix;
	for (const MetaFolder& f : metaFolders(layer)) prefix += f.name + QStringLiteral(" / ");
	return prefix;
}

// Identifies the folder at `depth` of `path` (with everything above it), for building each folder's
// group once: two layers in the same folder go into the same group.
inline QString metaFolderKey(const std::vector<MetaFolder>& path, size_t depth) {
	QString key;
	for (size_t d = 0; d <= depth && d < path.size(); d++) key += path[d].name + QChar(0x1F);
	return key;
}

} // namespace SisirDcc
