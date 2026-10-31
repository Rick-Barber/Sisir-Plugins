// Sisir hair in Viewport 2.0 (DirectX 11), drawn as Sisir draws it: strand segments expanded into
// ribbons facing the camera at the strands' real widths (soft through the viewport's multisample
// anti-aliasing, as in Sisir), shaded with Sisir's own viewport hair shading. gMinPixelWidth can
// hold thin strands to a minimum on-screen width; 0 matches Sisir.

float4x4 gWorld : World;
float4x4 gViewProj : ViewProjection;
float4x4 gViewInv : ViewInverse;
float4x4 gProj : Projection;
float2 gViewportSize : ViewportPixelSize;

float gWidthScale = 1.0;
float gMinPixelWidth = 0.0; // Sisir draws real widths and relies on MSAA
float gUseSetColor = 0.0;
float4 gSetColor = float4(0.9, 0.8, 0.65, 1.0);
float4 gTint = float4(1.0, 1.0, 1.0, 0.0); // selection highlight: rgb, amount
// A texture-driven base colour sampled root (0) to tip (1), 16 rgb steps, times gSetColor.
float gUseGradient = 0.0;
float gGradient[48];

struct VSIn {
	float3 pos : POSITION;
	float4 col : COLOR0;
	float3 tan : TANGENT;
	float3 widthAlong : TEXCOORD0; // width, position along the strand (root 0, tip 1), strand random
};

struct GSIn {
	float3 wpos : TEXCOORD1;
	float3 wtan : TEXCOORD2;
	float4 col : COLOR0;
	float width : TEXCOORD0;
	float2 alongRandom : TEXCOORD3;
};

struct PSIn {
	float4 hpos : SV_Position;
	float3 wpos : TEXCOORD1;
	float3 wtan : TEXCOORD2;
	float4 col : COLOR0;
	float2 alongRandom : TEXCOORD3;
};

GSIn VS(VSIn i) {
	GSIn o;
	o.wpos = mul(float4(i.pos, 1.0), gWorld).xyz;
	o.wtan = mul(float4(i.tan, 0.0), gWorld).xyz;
	o.col = (gUseSetColor > 0.5) ? gSetColor : i.col;
	if (gUseGradient > 0.5) {
		float x = saturate(i.widthAlong.y) * 15.0;
		int i0 = min((int)x, 14);
		float f = x - (float)i0;
		float3 a = float3(gGradient[i0 * 3], gGradient[i0 * 3 + 1], gGradient[i0 * 3 + 2]);
		float3 b = float3(gGradient[i0 * 3 + 3], gGradient[i0 * 3 + 4], gGradient[i0 * 3 + 5]);
		o.col = float4(lerp(a, b, f) * gSetColor.rgb, 1.0);
	}
	o.alongRandom = i.widthAlong.yz;
	// Object-space width to world: the transform's uniform scale.
	o.width = i.widthAlong.x * gWidthScale * length(mul(float4(1.0, 0.0, 0.0, 0.0), gWorld).xyz);
	return o;
}

float3 ribbonSide(GSIn v, float3 eye) {
	float3 toEye = eye - v.wpos;
	float dist = length(toEye);
	toEye /= max(dist, 1e-6);
	float3 t = normalize(v.wtan + 1e-9);
	float3 side = cross(t, toEye);
	float len = length(side);
	side = (len > 1e-6) ? side / len : normalize(cross(t, float3(0.0, 1.0, 0.0)) + 1e-9);
	// One pixel at this distance, from the projection's vertical scale.
	float pixel = 2.0 * dist / max(gProj[1][1] * gViewportSize.y, 1e-6);
	return side * 0.5 * max(v.width, gMinPixelWidth * pixel);
}

[maxvertexcount(4)]
void GS(line GSIn v[2], inout TriangleStream<PSIn> strip) {
	float3 eye = gViewInv[3].xyz;
	float3 s0 = ribbonSide(v[0], eye);
	float3 s1 = ribbonSide(v[1], eye);
	PSIn o;
	o.wpos = v[0].wpos - s0; o.wtan = v[0].wtan; o.col = v[0].col; o.alongRandom = v[0].alongRandom; o.hpos = mul(float4(o.wpos, 1.0), gViewProj); strip.Append(o);
	o.wpos = v[0].wpos + s0; o.hpos = mul(float4(o.wpos, 1.0), gViewProj); strip.Append(o);
	o.wpos = v[1].wpos - s1; o.wtan = v[1].wtan; o.col = v[1].col; o.alongRandom = v[1].alongRandom; o.hpos = mul(float4(o.wpos, 1.0), gViewProj); strip.Append(o);
	o.wpos = v[1].wpos + s1; o.hpos = mul(float4(o.wpos, 1.0), gViewProj); strip.Append(o);
}

float4 PS(PSIn i) : SV_Target {
	float3 eye = gViewInv[3].xyz;
	float3 v = normalize(eye - i.wpos);
	float3 t = normalize(i.wtan + 1e-9);
	// Exactly Sisir's viewport hair shading (Renderer.cpp kHairFs): lit from the eye, the strand's
	// normal is the view direction off its tangent. Same constants, so the hair looks as in Sisir.
	float tv = dot(t, v);
	float ndl = sqrt(saturate(1.0 - tv * tv));
	float spec = pow(ndl, 32.0);
	float3 c = i.col.rgb * (0.3 + 0.7 * ndl) + 0.08 * spec;
	c = lerp(c, gTint.rgb, gTint.a);
	return float4(c, 1.0);
}

technique11 Main {
	pass P0 {
		SetVertexShader(CompileShader(vs_5_0, VS()));
		SetGeometryShader(CompileShader(gs_5_0, GS()));
		SetPixelShader(CompileShader(ps_5_0, PS()));
	}
}
