#pragma once

// Viewport 2.0 drawing for sisirHairShape: one render item of line segments that the SisirHair
// effect (shaders/SisirHair.fx, .ogsfx) expands into camera-facing ribbons with the strands' own
// widths and colours. Buffers are built once per hair change; the display percentage only changes
// how much of a shuffled index buffer is drawn, so it thins the groom evenly.

#include "HairData.h"

#include <maya/MDagPath.h>
#include <maya/MDagPathArray.h>
#include <maya/MMatrixArray.h>
#include <maya/MPxSubSceneOverride.h>
#include <maya/MShaderManager.h>

#include <memory>
#include <vector>

namespace SisirMaya {

class HairShape;

class HairDrawOverride : public MHWRender::MPxSubSceneOverride {
public:
	static MHWRender::MPxSubSceneOverride* creator(const MObject& obj);
	~HairDrawOverride() override;

	MHWRender::DrawAPI supportedDrawAPIs() const override;
	bool requiresUpdate(const MHWRender::MSubSceneContainer& container, const MHWRender::MFrameContext& frameContext) const override;
	void update(MHWRender::MSubSceneContainer& container, const MHWRender::MFrameContext& frameContext) override;
	bool getSelectionPath(const MHWRender::MRenderItem& renderItem, MDagPath& dagPath) const override;
	bool getInstancedSelectionPath(const MHWRender::MRenderItem& renderItem, const MIntersection& intersection,
	                               MDagPath& dagPath) const override;

	// Where the plug-in's shaders live; set once at load.
	static void setShaderDirectory(const MString& dir);

private:
	explicit HairDrawOverride(const MObject& obj);
	struct State {
		uint64_t drawRevision = 0;
		uint64_t dataRevision = 0;
		float displayPercent = -1.0f;
		std::vector<MMatrix> matrices;
		std::vector<int> status;
		std::vector<char> visible; // per instance: hidden in the Outliner, a hidden parent, a display layer
		bool flatColor = false;    // one colour for the whole groom instead of Sisir's per-point albedo
		float color[3] = { 0, 0, 0 };
		// A base colour driven by a texture: the network sampled root (0) to tip (1), times color.
		bool gradient = false;
		float gradientColors[16 * 3] = {};
	};
	State currentState() const;
	void releaseShader();
	bool ensureShader();
	void rebuildBuffers(const HairBuffers& b);
	void rebuildIndices(const HairBuffers& b, float percent);
	std::unique_ptr<MHWRender::MIndexBuffer> buildIndices(const HairBuffers& b, float percent) const;
	void updateWireframe(MHWRender::MSubSceneContainer& container, const State& s, bool geometryChanged);

	MObject m_node;
	HairShape* m_shape = nullptr;
	MHWRender::MShaderInstance* m_shader = nullptr;
	bool m_customShader = false;
	std::vector<std::unique_ptr<MHWRender::MVertexBuffer>> m_vertexBuffers;
	std::unique_ptr<MHWRender::MIndexBuffer> m_indexBuffer;
	// Selection wireframe: a thinned set of strands as plain lines in Maya's selection colour,
	// drawn over the hair while it is selected -- the hair's version of a mesh's selection wireframe.
	std::unique_ptr<MHWRender::MIndexBuffer> m_wireIndexBuffer;
	MHWRender::MShaderInstance* m_wireShader = nullptr;
	bool m_wireGeometryCurrent = false;
	std::vector<uint32_t> m_strandOrder; // strands sorted by their random: any prefix is an even subset
	MDagPathArray m_paths;
	State m_state;
	bool m_haveGeometry = false;
};

} // namespace SisirMaya
