#pragma once

// "Sisir Scene" in File > Open / File > Import (and drag and drop): a .sir goes through the same
// import as sisirImport. Interactive sessions get the options dialog; batch sessions read the
// option string (ImportOptions syntax).

#include <maya/MPxFileTranslator.h>

namespace SisirMaya {

class FileTranslator : public MPxFileTranslator {
public:
	static const char* kName;
	static void* creator() { return new FileTranslator(); }

	MStatus reader(const MFileObject& file, const MString& options, FileAccessMode mode) override;
	bool haveReadMethod() const override { return true; }
	bool haveWriteMethod() const override { return false; }
	bool canBeOpened() const override { return true; }
	MString defaultExtension() const override { return "sir"; }
	MString filter() const override { return "*.sir"; }
	MFileKind identifyFile(const MFileObject& file, const char* buffer, short size) const override;
};

} // namespace SisirMaya
