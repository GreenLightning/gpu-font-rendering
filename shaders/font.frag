#version 330 core

struct Glyph {
	int start, count;
};

struct Curve {
	vec2 p0, p1, p2;
};

uniform isamplerBuffer glyphs;
uniform samplerBuffer curves;
uniform vec4 color;

in vec2 uv;
flat in int bufferIndex;

out vec4 result;

Glyph loadGlyph(int index) {
	Glyph result;
	ivec2 data = texelFetch(glyphs, index).xy;
	result.start = data.x;
	result.count = data.y;
	return result;
}

Curve loadCurve(int index) {
	Curve result;
	result.p0 = texelFetch(curves, 3*index+0).xy;
	result.p1 = texelFetch(curves, 3*index+1).xy;
	result.p2 = texelFetch(curves, 3*index+2).xy;
	return result;
}

void main() {
	Glyph glyph = loadGlyph(bufferIndex);

	result = vec4(1, 1, 1, 1) * 0.3;

	// Visualize control points.
	vec2 fw = fwidth(uv);
	float r = 4.0 * 0.5 * (fw.x + fw.y);
	for (int i = 0; i < glyph.count; i++) {
		Curve curve = loadCurve(glyph.start + i);

		vec2 p0 = curve.p0 - uv;
		vec2 p1 = curve.p1 - uv;
		vec2 p2 = curve.p2 - uv;

		if (dot(p0, p0) < r*r || dot(p2, p2) < r*r) {
			result = vec4(0, 1, 0, 1);
			return;
		}

		if (dot(p1, p1) < r*r) {
			result = vec4(1, 0, 1, 1);
			return;
		}
	}
}
