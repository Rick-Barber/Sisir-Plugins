#pragma once

// Nitrous drawing for one node of a Sisir Hair object: line segments that shaders/SisirHair.fx
// expands into camera-facing ribbons with the strands' own widths and colours, as Sisir for Maya's
// HairDrawOverride does. Buffers are built once per hair change; Display % only changes how much of
// a strand-shuffled index buffer is drawn, so it thins the groom evenly. While the node is selected
// a thinned set of strands is drawn over it as lines in Max's selection colour.

#include "HairBuffers.h"
#include "HairViewColor.h"

#include <Graphics/HLSLMaterialHandle.h>
#include <Graphics/ICustomRenderItem.h>
#include <Graphics/IndexBufferHandle.h>
#include <Graphics/MaterialRequiredStreams.h>
#include <Graphics/VertexBufferHandle.h>

#include <QString>

#include <memory>
#include <vector>

class INode;
class Texmap;

namespace SisirMax {

class HairObject;

class HairRenderItem : public MaxSDK::Graphics::ICustomRenderItem {
public:
	explicit HairRenderItem(HairObject& hair);
	~HairRenderItem() override;

	// The object is going away: draw nothing from now on.
	void detach() { m_hair = nullptr; }

	void Realize(MaxSDK::Graphics::DrawContext& drawContext) override;
	void Display(MaxSDK::Graphics::DrawContext& drawContext) override;
	size_t GetPrimitiveCount() const override { return m_segments; }

	// Where the shaders are; set once at load.
	static void setShaderDirectory(const QString& dir);

private:
	struct ViewColor {
		bool perPoint = true;
		float rgb[3] = { 0.8f, 0.8f, 0.8f };
		bool gradient = false;
		float gradientColors[SisirDcc::kGradientSamples * 3] = {};
	};
	ViewColor viewColor(INode* node, int mode);
	bool sampleAlongStrand(Texmap* map, float out[SisirDcc::kGradientSamples * 3]);
	void rebuildBuffers(const SisirDcc::HairBuffers& b);
	MaxSDK::Graphics::IndexBufferHandle buildIndices(const SisirDcc::HairBuffers& b, float percent, size_t& segments) const;
	static bool ensureShaders();

	HairObject* m_hair;
	MaxSDK::Graphics::VertexBufferHandleArray m_streams;     // position, colour, tangent, width/along/random
	MaxSDK::Graphics::VertexBufferHandleArray m_wireStreams; // position only
	MaxSDK::Graphics::IndexBufferHandle m_indices;
	MaxSDK::Graphics::IndexBufferHandle m_wireIndices;
	std::vector<uint32_t> m_strandOrder; // strands sorted by their random: any prefix is an even subset
	size_t m_segments = 0;
	size_t m_wireSegments = 0;
	uint64_t m_dataRevision = 0;
	float m_displayPercent = -1.0f;
	// Not drawn twice in a frame when both the shaded and the wireframe item are visible (edged faces).
	int m_lastFrame = -1;
	const void* m_lastView = nullptr;
	// Texture sampling is slow: cached per map, refreshed at most once a second.
	Texmap* m_sampledMap = nullptr;
	unsigned long m_sampledAt = 0;
	bool m_sampleOk = false;
	float m_sampled[SisirDcc::kGradientSamples * 3] = {};
};

} // namespace SisirMax
