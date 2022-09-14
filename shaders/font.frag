#version 330 core

// Based on: http://wdobbie.com/post/gpu-text-rendering-with-vector-textures/

struct Glyph {
	int start, count;
};

struct Curve {
	vec2 p0, p1, p2;
};

uniform isamplerBuffer glyphs;
uniform samplerBuffer curves;
uniform vec4 color;


// Controls for debugging and exploring:

// Size of the window (in pixels) used for 1-dimensional anti-aliasing along each rays.
//   0 - no anti-aliasing
//   1 - normal anti-aliasing
// >=2 - exaggerated effect 
uniform float antiAliasingWindowSize = 1.0;

// Enable a second ray along the y-axis to achieve 2-dimensional anti-aliasing.
uniform bool enableSuperSamplingAntiAliasing = true;

// Draw control points for debugging (green - on curve, magenta - off curve).
uniform bool enableControlPointsVisualization = false;


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

float computeCoverage(float inverseDiameter, vec2 p0, vec2 p1, vec2 p2) {
	if (p0.y > 0 && p1.y > 0 && p2.y > 0) return 0.0;
	if (p0.y < 0 && p1.y < 0 && p2.y < 0) return 0.0;

	// Note: Simplified from abc formula by extracting a factor of (-2) from b.
	vec2 a = p0 - 2*p1 + p2;
	vec2 b = p0 - p1;
	vec2 c = p0;

	float t0, t1;
	if (abs(a.y) >= 1e-5) {
		// Quadratic segment, solve abc formula to find roots.
		float radicand = b.y*b.y - a.y*c.y;
		if (radicand <= 0) return 0.0;
	
		float s = sqrt(radicand);
		t0 = (b.y - s) / a.y;
		t1 = (b.y + s) / a.y;
	} else {
		// Linear segment, avoid division by a.y, which is near zero.
		// There is only one root, so we have to decide which variable to
		// assign it to based on the direction of the segment, to ensure that
		// the ray always exits the shape at t0 and enters at t1. For a
		// quadratic segment this works 'automatically', see readme.
		float t = p0.y / (p0.y - p2.y);
		if (p0.y < p2.y) {
			t0 = -1.0;
			t1 = t;
		} else {
			t0 = t;
			t1 = -1.0;
		}
	}

	float alpha = 0;
	
	if (t0 >= 0 && t0 < 1) {
		float x = (a.x*t0 - 2.0*b.x)*t0 + c.x;
		alpha += clamp(x * inverseDiameter + 0.5, 0, 1);
	}

	if (t1 >= 0 && t1 < 1) {
		float x = (a.x*t1 - 2.0*b.x)*t1 + c.x;
		alpha -= clamp(x * inverseDiameter + 0.5, 0, 1);
	}

	return alpha;
}

vec2 rotate(vec2 v) {
	return vec2(v.y, -v.x);
}

void main() {
	float alpha = 0;

	// Inverse of the diameter of a pixel in uv units for anti-aliasing.
	vec2 inverseDiameter = 1.0 / (antiAliasingWindowSize * fwidth(uv));

	Glyph glyph = loadGlyph(bufferIndex);
	for (int i = 0; i < glyph.count; i++) {
		Curve curve = loadCurve(glyph.start + i);

		vec2 p0 = curve.p0 - uv;
		vec2 p1 = curve.p1 - uv;
		vec2 p2 = curve.p2 - uv;

		alpha += computeCoverage(inverseDiameter.x, p0, p1, p2);
		if (enableSuperSamplingAntiAliasing) {
			alpha += computeCoverage(inverseDiameter.y, rotate(p0), rotate(p1), rotate(p2));
		}
	}

	if (enableSuperSamplingAntiAliasing) {
		alpha *= 0.5;
	}

	alpha = clamp(alpha, 0.0, 1.0);
	result = color * alpha;

	if (enableControlPointsVisualization) {
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
}
