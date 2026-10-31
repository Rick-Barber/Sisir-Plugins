// The selection wireframe of Sisir hair in the 3ds Max viewport: a thinned set of strands as plain
// lines in Max's selection colour, drawn over the hair while it is selected (as Sisir for Maya does).

float4x4 gWorldViewProj : WorldViewProjection;
float4 gColor = float4(1.0, 1.0, 1.0, 1.0);

float4 VS(float3 pos : POSITION) : SV_Position {
	return mul(float4(pos, 1.0), gWorldViewProj);
}

float4 PS() : SV_Target {
	return gColor;
}

technique11 Main {
	pass P0 {
		SetVertexShader(CompileShader(vs_5_0, VS()));
		SetGeometryShader(NULL);
		SetPixelShader(CompileShader(ps_5_0, PS()));
	}
}
