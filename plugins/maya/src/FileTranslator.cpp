#include "FileTranslator.h"

#include "ImportCommand.h"

#include <maya/MFileObject.h>
#include <maya/MGlobal.h>

#include <cstring>

namespace SisirMaya {

const char* FileTranslator::kName = "Sisir Scene";

MStatus FileTranslator::reader(const MFileObject& file, const MString& options, FileAccessMode) {
	ImportRequest request;
	request.file = QString::fromUtf8(file.resolvedFullName().asUTF8());
	request.options = ImportOptions::fromString(QString::fromUtf8(options.asUTF8()), ImportOptions::loadRemembered());
	request.interactive = MGlobal::mayaState() == MGlobal::kInteractive;
	request.showDialog = request.interactive;
	const ImportOutcome outcome = importSirFile(request);
	if (outcome.ok || outcome.cancelled || outcome.dialogShown) return MS::kSuccess;
	return MS::kFailure;
}

MPxFileTranslator::MFileKind FileTranslator::identifyFile(const MFileObject& file, const char* buffer, short size) const {
	const MString name = file.resolvedName();
	const unsigned int n = name.length();
	const bool sirExtension = n > 4 && name.substring(n - 4, n - 1).toLowerCase() == ".sir";
	if (!sirExtension) return kNotMyFileType;
	// A .sir is a ZIP archive.
	if (size >= 2 && buffer && std::memcmp(buffer, "PK", 2) == 0) return kIsMyFileType;
	return kCouldBeMyFileType;
}

} // namespace SisirMaya
