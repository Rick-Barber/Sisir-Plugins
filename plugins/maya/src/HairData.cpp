#include "HairData.h"
#include "PluginIds.h"

#include <maya/MArgList.h>
#include <maya/MString.h>

#include <QByteArray>

#include <algorithm>
#include <istream>
#include <ostream>

namespace SisirMaya {

const MTypeId HairData::id(kHairDataId);
const MString HairData::typeName("sisirHairData");

void* HairData::creator() { return new HairData(); }

void HairData::copy(const MPxData& src) {
	if (src.typeId() == id) m_buffers = static_cast<const HairData&>(src).m_buffers;
}

MStatus HairData::writeBinary(std::ostream& out) {
	const std::vector<char> blob = m_buffers ? serializeHair(*m_buffers) : std::vector<char>();
	if (!blob.empty()) out.write(blob.data(), (std::streamsize)blob.size());
	return out.good() ? MS::kSuccess : MS::kFailure;
}

MStatus HairData::readBinary(std::istream& in, unsigned int length) {
	m_buffers.reset();
	if (length == 0) return MS::kSuccess; // saved without hair
	std::vector<char> blob(length);
	in.read(blob.data(), length);
	if ((unsigned int)in.gcount() != length) return MS::kFailure;
	m_buffers = deserializeHair(blob.data(), blob.size());
	return m_buffers ? MS::kSuccess : MS::kFailure;
}

// ASCII scenes: the same compressed block, base64, split into tokens so no single MEL string gets
// unwieldy: <chunkCount> "<chunk>" "<chunk>" ...
MStatus HairData::writeASCII(std::ostream& out) {
	const std::vector<char> blob = m_buffers ? serializeHair(*m_buffers) : std::vector<char>();
	const QByteArray b64 = QByteArray(blob.data(), (int)blob.size()).toBase64();
	constexpr int kChunk = 65536;
	const int chunks = (b64.size() + kChunk - 1) / kChunk;
	out << chunks;
	for (int c = 0; c < chunks; c++) {
		out << " \"";
		out.write(b64.constData() + (size_t)c * kChunk, std::min<int>(kChunk, b64.size() - c * kChunk));
		out << "\"";
	}
	return out.good() ? MS::kSuccess : MS::kFailure;
}

MStatus HairData::readASCII(const MArgList& args, unsigned int& lastElement) {
	m_buffers.reset();
	MStatus status;
	const int chunks = args.asInt(lastElement++, &status);
	if (!status || chunks < 0) return MS::kFailure;
	if (chunks == 0) return MS::kSuccess;
	QByteArray b64;
	for (int c = 0; c < chunks; c++) {
		const MString part = args.asString(lastElement++, &status);
		if (!status) return MS::kFailure;
		b64.append(part.asChar(), (int)part.length());
	}
	const QByteArray blob = QByteArray::fromBase64(b64);
	m_buffers = deserializeHair(blob.constData(), (size_t)blob.size());
	return m_buffers ? MS::kSuccess : MS::kFailure;
}

} // namespace SisirMaya
