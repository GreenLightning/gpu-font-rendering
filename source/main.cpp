#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>

#include <defer.hpp>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "glm.hpp"

#include "shader_catalog.hpp"

class Font {
	struct Glyph {
		FT_UInt index;
		int32_t bufferIndex;

		// Important glyph metrics in font units.
		FT_Pos width, height;
		FT_Pos bearingX;
		FT_Pos bearingY;
		FT_Pos advance;
	};

	struct BufferGlyph {
		int32_t start, count; // range of bezier curves belonging to this glyph
	};

	struct BufferCurve {
		float x0, y0, x1, y1, x2, y2;
	};

	struct BufferVertex {
		float   x, y, u, v;
		int32_t bufferIndex;
	};

public:
	static FT_Face loadFace(FT_Library library, const std::string& filename, std::string& error) {
		FT_Face face;

		FT_Error ftError = FT_New_Face(library, filename.c_str(), 0, &face);
		if (ftError) {
			const char* ftErrorStr = FT_Error_String(ftError);
			if (ftErrorStr) {
				error = std::string(ftErrorStr);
			} else {
				// Fallback in case FT_Error_String returns NULL (e.g. if there
				// was an error or FT was compiled without error strings).
				std::stringstream stream;
				stream << "Error " << ftError;
				error = stream.str();
			}
			return face;
		}

		if (!(face->face_flags & FT_FACE_FLAG_SCALABLE)) {
			error = "non-scalable fonts are not supported";
			return face;
		}

		return face;
	}

	Font(FT_Face face) : face(face) {
		glGenVertexArrays(1, &vao);

		glGenBuffers(1, &vbo);
		glGenBuffers(1, &ebo);

		glGenTextures(1, &glyphTexture);
		glGenTextures(1, &curveTexture);

		glGenBuffers(1, &glyphBuffer);
		glGenBuffers(1, &curveBuffer);

		emSize = face->units_per_EM;

		glBindVertexArray(vao);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, false, sizeof(BufferVertex), (void*)offsetof(BufferVertex, x));
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 2, GL_FLOAT, false, sizeof(BufferVertex), (void*)offsetof(BufferVertex, u));
		glEnableVertexAttribArray(2);
		glVertexAttribIPointer(2, 1, GL_INT, sizeof(BufferVertex), (void*)offsetof(BufferVertex, bufferIndex));
		glBindVertexArray(0);

		std::vector<BufferGlyph> bufferGlyphs;
		std::vector<BufferCurve> bufferCurves;

		{
			uint32_t charcode = 0;
			FT_UInt glyphIndex = 0;
			FT_Error error = FT_Load_Glyph(face, glyphIndex, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP);
			if (error) {
				std::cerr << "[font] error while loading undefined glyph: " << error << std::endl;
				// Continue, because we always want an entry for the undefined glyph in our glyphs map!
			}

			buildGlyph(bufferGlyphs, bufferCurves, charcode, glyphIndex);
		}

		for (uint32_t charcode = 32; charcode < 128; charcode++) {
			FT_UInt glyphIndex = FT_Get_Char_Index(face, charcode);
			if (!glyphIndex) continue;

			FT_Error error = FT_Load_Glyph(face, glyphIndex, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP);
			if (error) {
				std::cerr << "[font] error while loading glyph for character " << charcode << ": " << error << std::endl;
				continue;
			}

			buildGlyph(bufferGlyphs, bufferCurves, charcode, glyphIndex);
		}

		glBindBuffer(GL_TEXTURE_BUFFER, glyphBuffer);
		glBufferData(GL_TEXTURE_BUFFER, sizeof(BufferGlyph) * bufferGlyphs.size(), bufferGlyphs.data(), GL_STATIC_DRAW);
		glBindBuffer(GL_TEXTURE_BUFFER, 0);

		glBindBuffer(GL_TEXTURE_BUFFER, curveBuffer);
		glBufferData(GL_TEXTURE_BUFFER, sizeof(BufferCurve) * bufferCurves.size(), bufferCurves.data(), GL_STATIC_DRAW);
		glBindBuffer(GL_TEXTURE_BUFFER, 0);

		glBindTexture(GL_TEXTURE_BUFFER, glyphTexture);
		glTexBuffer(GL_TEXTURE_BUFFER, GL_RG32I, glyphBuffer);
		glBindTexture(GL_TEXTURE_BUFFER, 0);

		glBindTexture(GL_TEXTURE_BUFFER, curveTexture);
		glTexBuffer(GL_TEXTURE_BUFFER, GL_RG32F, curveBuffer);
		glBindTexture(GL_TEXTURE_BUFFER, 0);
	}

	~Font() {
		glDeleteVertexArrays(1, &vao);

		glDeleteBuffers(1, &vbo);
		glDeleteBuffers(1, &ebo);

		glDeleteTextures(1, &glyphTexture);
		glDeleteTextures(1, &curveTexture);

		glDeleteBuffers(1, &glyphBuffer);
		glDeleteBuffers(1, &curveBuffer);
	}

private:
	void buildGlyph(std::vector<BufferGlyph>& bufferGlyphs, std::vector<BufferCurve>& bufferCurves, uint32_t charcode, FT_UInt glyphIndex) {
		BufferGlyph bufferGlyph;
		bufferGlyph.start = static_cast<int32_t>(bufferCurves.size());

		short start = 0;
		for (int i = 0; i < face->glyph->outline.n_contours; i++) {
			// Note: The end indices in face->glyph->outline.contours are inclusive.
			convertContour(bufferCurves, &face->glyph->outline, start, face->glyph->outline.contours[i], emSize);
			start = face->glyph->outline.contours[i]+1;
		}

		bufferGlyph.count = static_cast<int32_t>(bufferCurves.size()) - bufferGlyph.start;

		int32_t bufferIndex = static_cast<int32_t>(bufferGlyphs.size());
		bufferGlyphs.push_back(bufferGlyph);

		Glyph glyph;
		glyph.index = glyphIndex;
		glyph.bufferIndex = bufferIndex;
		glyph.width = face->glyph->metrics.width;
		glyph.height = face->glyph->metrics.height;
		glyph.bearingX = face->glyph->metrics.horiBearingX;
		glyph.bearingY = face->glyph->metrics.horiBearingY;
		glyph.advance = face->glyph->metrics.horiAdvance;
		glyphs[charcode] = glyph;
	}

	// This function takes a single contour (defined by firstIndex and
	// lastIndex, both inclusive) from outline and converts it into individual
	// quadratic bezier curves, which are added to the curves vector.
	void convertContour(std::vector<BufferCurve>& curves, const FT_Outline* outline, short firstIndex, short lastIndex, float emSize) {
		// TrueType fonts only contain quadratic bezier curves.
		// OpenType fonts may contain outline data in TrueType format
		// or in Compact Font Format, which also allows cubic beziers.

		// Each point in the contour has a tag specifying whether the point is
		// on the curve or not (off-curve points are control points).
		// A quadratic bezier curve is formed by three points: on - off - on.
		// A contour is a list of points that define a closed bezier spline (the
		// start point of a curve is the end point of the previous curve and the
		// end point of the last curve is the start point of the first curve).

		// Two consecutive points with the same type imply a virtual point of
		// the opposite type at their center. For example a line segment is
		// encoded as: on - on. Adding a virtual control point at the center
		// of the two endpoints (on - (off) - on) creates a quadratic bezier
		// curve representing the same line segment. Similarly, two consecutive
		// off points imply a virtual on point, so the sequence
		// on - off - off - on is expanded to on - off - (on) - off - on and
		// defines a chain of two bezier curves sharing the virtual point as one
		// of their end points.

		if (firstIndex == lastIndex) return;

		short dIndex = 1;
		if (outline->flags & FT_OUTLINE_REVERSE_FILL) {
			short tmpIndex = lastIndex;
			lastIndex = firstIndex;
			firstIndex = tmpIndex;
			dIndex = -1;
		}

		auto convert = [emSize](const FT_Vector& v) {
			return glm::vec2(
				(float)v.x / emSize,
				(float)v.y / emSize
			);
		};

		auto makeMidpoint = [](const glm::vec2& a, const glm::vec2& b) {
			return 0.5f * (a + b);
		};

		auto makeCurve = [](const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2) {
			BufferCurve result;
			result.x0 = p0.x;
			result.y0 = p0.y;
			result.x1 = p1.x;
			result.y1 = p1.y;
			result.x2 = p2.x;
			result.y2 = p2.y;
			return result;
		};

		// Find a point that is on the curve and remove it from the list.
		glm::vec2 first;
		bool firstOnCurve = (outline->tags[firstIndex] & FT_CURVE_TAG_ON);
		if (firstOnCurve) {
			first = convert(outline->points[firstIndex]);
			firstIndex += dIndex;
		} else {
			bool lastOnCurve = (outline->tags[lastIndex] & FT_CURVE_TAG_ON);
			if (lastOnCurve) {
				first = convert(outline->points[lastIndex]);
				lastIndex -= dIndex;
			} else {
				first = makeMidpoint(convert(outline->points[firstIndex]), convert(outline->points[lastIndex]));
				// This is a virtual point, so we don't have to remove it.
			}
		}

		glm::vec2 start = first;
		glm::vec2 control = first;
		glm::vec2 previous = first;
		char previousTag = FT_CURVE_TAG_ON;
		for (short index = firstIndex; index != lastIndex + dIndex; index += dIndex) {
			glm::vec2 current = convert(outline->points[index]);
			char currentTag = FT_CURVE_TAG(outline->tags[index]);
			if (currentTag == FT_CURVE_TAG_CUBIC) {
				// No-op, wait for more points.
				control = previous;
			} else if (currentTag == FT_CURVE_TAG_ON) {
				if (previousTag == FT_CURVE_TAG_CUBIC) {
					glm::vec2& b0 = start;
					glm::vec2& b1 = control;
					glm::vec2& b2 = previous;
					glm::vec2& b3 = current;

					glm::vec2 c0 = b0 + 0.75f * (b1 - b0);
					glm::vec2 c1 = b3 + 0.75f * (b2 - b3);

					glm::vec2 d = makeMidpoint(c0, c1);

					curves.push_back(makeCurve(b0, c0, d));
					curves.push_back(makeCurve(d, c1, b3));
				} else if (previousTag == FT_CURVE_TAG_ON) {
					// Linear segment.
					curves.push_back(makeCurve(previous, makeMidpoint(previous, current), current));
				} else {
					// Regular bezier curve.
					curves.push_back(makeCurve(start, previous, current));
				}
				start = current;
				control = current;
			} else /* currentTag == FT_CURVE_TAG_CONIC */ {
				if (previousTag == FT_CURVE_TAG_ON) {
					// No-op, wait for third point.
				} else {
					// Create virtual on point.
					glm::vec2 mid = makeMidpoint(previous, current);
					curves.push_back(makeCurve(start, previous, mid));
					start = mid;
					control = mid;
				}
			}
			previous = current;
			previousTag = currentTag;
		}

		// Close the contour.
		if (previousTag == FT_CURVE_TAG_CUBIC) {
			glm::vec2& b0 = start;
			glm::vec2& b1 = control;
			glm::vec2& b2 = previous;
			glm::vec2& b3 = first;

			glm::vec2 c0 = b0 + 0.75f * (b1 - b0);
			glm::vec2 c1 = b3 + 0.75f * (b2 - b3);

			glm::vec2 d = makeMidpoint(c0, c1);

			curves.push_back(makeCurve(b0, c0, d));
			curves.push_back(makeCurve(d, c1, b3));

		} else if (previousTag == FT_CURVE_TAG_ON) {
			// Linear segment.
			curves.push_back(makeCurve(previous, makeMidpoint(previous, first), first));
		} else {
			curves.push_back(makeCurve(start, previous, first));
		}
	}

public:
	void drawSetup() {
		GLint location;

		location = glGetUniformLocation(program, "glyphs");
		glUniform1i(location, 0);
		location = glGetUniformLocation(program, "curves");
		glUniform1i(location, 1);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_BUFFER, glyphTexture);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_BUFFER, curveTexture);

		glActiveTexture(GL_TEXTURE0);
	}

	void draw(float x, float y, std::string text) {
		glBindVertexArray(vao);

		std::vector<BufferVertex> vertices;
		std::vector<int32_t> indices;

		FT_UInt previous = 0;
		for (auto textIt = text.begin(); textIt != text.end(); textIt++) {
			uint32_t charcode = *textIt; // only support ASCII for now

			auto glyphIt = glyphs.find(charcode);
			Glyph& glyph = (glyphIt == glyphs.end()) ? glyphs[0] : glyphIt->second;

			if (previous != 0 && glyph.index != 0) {
				FT_Vector kerning;
				FT_Error error = FT_Get_Kerning(face, previous, glyph.index, FT_KERNING_UNSCALED, &kerning);
				if (!error) {
					x += (float)kerning.x / emSize * worldSize;
				}
			}

			FT_Pos d = (FT_Pos) (emSize * dilation);

			float u0 = (float)(glyph.bearingX-d) / emSize;
			float v0 = (float)(glyph.bearingY-glyph.height-d) / emSize;
			float u1 = (float)(glyph.bearingX+glyph.width+d) / emSize;
			float v1 = (float)(glyph.bearingY+d) / emSize;

			float x0 = x + u0 * worldSize;
			float y0 = y + v0 * worldSize;
			float x1 = x + u1 * worldSize;
			float y1 = y + v1 * worldSize;

			int32_t base = static_cast<int32_t>(vertices.size());
			vertices.push_back(BufferVertex{x0, y0, u0, v0, glyph.bufferIndex});
			vertices.push_back(BufferVertex{x1, y0, u1, v0, glyph.bufferIndex});
			vertices.push_back(BufferVertex{x1, y1, u1, v1, glyph.bufferIndex});
			vertices.push_back(BufferVertex{x0, y1, u0, v1, glyph.bufferIndex});
			indices.insert(indices.end(), { base, base+1, base+2, base+2, base+3, base });

			x += (float)glyph.advance / emSize * worldSize;
			previous = glyph.index;
		}

		glBufferData(GL_ARRAY_BUFFER, sizeof(BufferVertex) * vertices.size(), vertices.data(), GL_STREAM_DRAW);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(int32_t) * indices.size(), indices.data(), GL_STREAM_DRAW);

		glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, 0);

		glBindVertexArray(0);
	}

private:
	FT_Face face;

	GLuint vao, vbo, ebo;
	GLuint glyphTexture, curveTexture;
	GLuint glyphBuffer, curveBuffer;

	float emSize;
	std::unordered_map<uint32_t, Glyph> glyphs;

public:
	GLuint program   = 0;
	float  dilation  = 0;
	float  worldSize = 1;
};

struct Transform {
	float fovy         = glm::radians(60.0f);
	float distance     = 0.42f;
	glm::mat3 rotation = glm::mat3(1.0f);
	glm::vec3 position = glm::vec3(0.0f);

	glm::mat4 getProjectionMatrix(float aspect) {
		return glm::perspective(/* fovy = */ glm::radians(60.0f), aspect, 0.002f, 12.000f);
	}

	glm::mat4 getViewMatrix() {
		auto translation = glm::translate(position);
		return glm::lookAt(glm::vec3(0, 0, distance), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0)) * glm::mat4(rotation) * translation;
	}
};

struct DragController {
	Transform* transform = nullptr;
	int activeButton = -1;

	double dragX, dragY;
	double wrapX, wrapY;
	double virtualX, virtualY;
	glm::vec3 dragTarget;

	bool unprojectMousePositionToXYPlane(GLFWwindow* window, double x, double y, glm::vec3& result) {
		int iwidth = 0, iheight = 0;
		glfwGetWindowSize(window, &iwidth, &iheight);

		double width = iwidth;
		double height = iheight;

		glm::mat4 projection = transform->getProjectionMatrix(float(width / height));
		glm::mat4 view = transform->getViewMatrix();

		double relX = x/width*2.0 - 1.0;
		double relY = y/height*2.0 - 1.0;

		glm::vec4 clipPos = glm::vec4(float(relX), -float(relY), 0.5f, 1.0f);
		glm::vec4 worldPos = glm::inverse(projection * view) * clipPos;
		worldPos *= 1.0f / worldPos.w;

		glm::vec3 pos = glm::vec3(glm::column(glm::inverse(view), 3));
		glm::vec3 dir = glm::normalize(glm::vec3(worldPos) - pos);
		float t = -pos.z / dir.z;

		result = pos + t * dir;
		return t > 0.0f;
	}

	void onMouseButton(GLFWwindow* window, int button, int action, int mods) {
		if (action == GLFW_PRESS && activeButton == -1) {
			activeButton = button;
			glfwGetCursorPos(window, &dragX, &dragY);
			wrapX = std::numeric_limits<double>::quiet_NaN();
			wrapY = std::numeric_limits<double>::quiet_NaN();
			virtualX = dragX;
			virtualY = dragY;

			glm::vec3 target;
			bool ok = unprojectMousePositionToXYPlane(window, dragX, dragY, target);
			dragTarget = ok ? target : glm::vec3();
		} else if (action == GLFW_RELEASE && activeButton == button) {
			activeButton = -1;
			dragX = 0.0;
			dragY = 0.0;
			wrapX = std::numeric_limits<double>::quiet_NaN();
			wrapY = std::numeric_limits<double>::quiet_NaN();
			virtualX = 0.0;
			virtualY = 0.0;
			dragTarget = glm::vec3();
		}
	}

	void onCursorPos(GLFWwindow* window, double x, double y) {
		if (activeButton == -1) return;

		int iwidth = 0, iheight = 0;
		glfwGetWindowSize(window, &iwidth, &iheight);

		double width = iwidth;
		double height = iheight;

		double deltaX = x-dragX;
		double deltaY = y-dragY;

		if (!std::isnan(wrapX) && !std::isnan(wrapY)) {
			double wrapDeltaX = x-wrapX;
			double wrapDeltaY = y-wrapY;
			if (wrapDeltaX*wrapDeltaX+wrapDeltaY*wrapDeltaY < deltaX*deltaX+deltaY*deltaY) {
				deltaX = wrapDeltaX;
				deltaY = wrapDeltaY;
				wrapX = std::numeric_limits<double>::quiet_NaN();
				wrapY = std::numeric_limits<double>::quiet_NaN();
			}
		}

		dragX = x;
		dragY = y;

		double targetX = x;
		double targetY = y;
		bool changed = false;
		if (targetX < 0) {
			targetX += width - 1;
			changed = true;
		} else if (targetX >= width) {
			targetX -= width - 1;
			changed = true;
		}
		if (targetY < 0) {
			targetY += height - 1;
			changed = true;
		} else if (targetY >= height) {
			targetY -= height - 1;
			changed = true;
		}
		if (changed) {
			glfwSetCursorPos(window, targetX, targetY);
			wrapX = targetX;
			wrapY = targetY;
		}

		if (activeButton == GLFW_MOUSE_BUTTON_2) {
			virtualX += deltaX;
			virtualY += deltaY;

			glm::vec3 target;
			bool ok = unprojectMousePositionToXYPlane(window, virtualX, virtualY, target);
			if (ok) {
				float x = transform->position.x;
				float y = transform->position.y;
				glm::vec3 delta = target - dragTarget;
				transform->position.x = glm::clamp(x + delta.x, -2.0f, 2.0f);
				transform->position.y = glm::clamp(y + delta.y, -2.0f, 2.0f);
			}
		} else if (activeButton == GLFW_MOUSE_BUTTON_3) {
			// Turntable rotation.
			double size = glm::min(width, height);
			glm::mat3 rx = glm::rotate(float(deltaX / size * glm::pi<double>()), glm::vec3(0, 0, 1));
			glm::mat3 ry = glm::rotate(float(deltaY / size * glm::pi<double>()), glm::vec3(1, 0, 0));
			transform->rotation = ry * transform->rotation * rx;
		} else {
			// Trackball rotation.
			double size = glm::min(width, height);
			glm::mat3 rx = glm::rotate(float(deltaX / size * glm::pi<double>()), glm::vec3(0, 1, 0));
			glm::mat3 ry = glm::rotate(float(deltaY / size * glm::pi<double>()), glm::vec3(1, 0, 0));
			transform->rotation = ry * rx * transform->rotation;
		}
	}

	void onScroll(GLFWwindow* window, double xOffset, double yOffset) {
		float factor = glm::clamp(1.0-float(yOffset)/10.0, 0.1, 1.9);
		transform->distance = glm::clamp(transform->distance * factor, 0.010f, 10.000f);
	}
};

namespace {
	FT_Library library;

	Transform transform;
	DragController dragController;

	// Empty VAO used when the vertex shader has no input and only uses gl_VertexID,
	// because OpenGL still requires a non-zero VAO to be bound for the draw call.
	GLuint emptyVAO;

	std::unique_ptr<ShaderCatalog> shaderCatalog;
	std::shared_ptr<ShaderCatalog::Entry> backgroundShader;
	std::shared_ptr<ShaderCatalog::Entry> fontShader;

	std::unique_ptr<Font> font;
}

static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
	dragController.onMouseButton(window, button, action, mods);
}

static void cursorPosCallback(GLFWwindow* window, double x, double y) {
	dragController.onCursorPos(window, x, y);
}

static void scrollCallback(GLFWwindow* window, double xOffset, double yOffset) {
	dragController.onScroll(window, xOffset, yOffset);
}

static void dropCallback(GLFWwindow* window, int pathCount, const char* paths[]) {
	if (pathCount == 0) return;

	std::string filename = paths[0];

	std::string error;
	FT_Face face = Font::loadFace(library, filename, error);
	if (error != "") {
		std::cerr << "[font] failed to load " << filename << ": " << error << std::endl;
	} else {
		font = std::make_unique<Font>(face);
	}
}

int main(int argc, char* argv[]) {
	if (!glfwInit()) {
		std::cerr << "ERROR: failed to initialize GLFW" << std::endl;
		return 1;
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
	glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE);

	GLFWwindow* window = glfwCreateWindow(1600, 900, "GPU Font Rendering Demo", nullptr, nullptr);
	if (!window) {
		std::cerr << "ERROR: failed to create GLFW window" << std::endl;
		glfwTerminate();
		return 1;
	}

	glfwMakeContextCurrent(window);

	if (!gladLoadGLLoader((GLADloadproc) glfwGetProcAddress)) {
		std::cerr << "ERROR: failed to initialize OpenGL context" << std::endl;
		glfwTerminate();
		return 1;
	}

	{
		FT_Error error = FT_Init_FreeType(&library);
		if (error) {
			std::cerr << "ERROR: failed to initialize FreeType" << std::endl;
			glfwTerminate();
			return 1;
		}
	}

	dragController.transform = &transform;
	glfwSetMouseButtonCallback(window, mouseButtonCallback);
	glfwSetCursorPosCallback(window, cursorPosCallback);
	glfwSetScrollCallback(window, scrollCallback);
	glfwSetDropCallback(window, dropCallback);

	glGenVertexArrays(1, &emptyVAO);

	shaderCatalog = std::make_unique<ShaderCatalog>("shaders");
	backgroundShader = shaderCatalog->get("background");
	fontShader = shaderCatalog->get("font");

	{
		std::string filename = "fonts/SourceSerifPro-Regular.otf";

		std::string error;
		FT_Face face = Font::loadFace(library, filename, error);
		if (error != "") {
			std::cerr << "[font] failed to load " << filename << ": " << error << std::endl;
		} else {
			font = std::make_unique<Font>(face);
		}
	}

	while(!glfwWindowShouldClose(window)) {
		shaderCatalog->update();

		glfwPollEvents();

		int width, height;
		glfwGetFramebufferSize(window, &width, &height);
		glViewport(0, 0, width, height);

		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		GLuint location;

		glm::mat4 projection = transform.getProjectionMatrix((float)width / height);
		glm::mat4 view = transform.getViewMatrix();
		glm::mat4 model = glm::mat4(1.0f);

		{ // Draw background.
			GLuint program = backgroundShader->getProgram();
			glUseProgram(program);
			glBindVertexArray(emptyVAO);
			glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
			glBindVertexArray(0);
			glUseProgram(0);
		}

		// Uses premultiplied-alpha.
		glEnable(GL_BLEND);
		glBlendEquation(GL_FUNC_ADD);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

		if (font) {
			GLuint program = fontShader->getProgram();
			glUseProgram(program);

			font->program = program;
			font->dilation = 0.1f;
			font->worldSize = 0.2f;
			font->drawSetup();

			location = glGetUniformLocation(program, "projection");
			glUniformMatrix4fv(location, 1, false, glm::value_ptr(projection));
			location = glGetUniformLocation(program, "view");
			glUniformMatrix4fv(location, 1, false, glm::value_ptr(view));
			location = glGetUniformLocation(program, "model");
			glUniformMatrix4fv(location, 1, false, glm::value_ptr(model));

			location = glGetUniformLocation(program, "color");
			glUniform4f(location, 1.0f, 1.0f, 1.0f, 1.0f);

			font->draw(0, 0, "Hello, world!");
			glUseProgram(0);
		}

		glDisable(GL_BLEND);

		glfwSwapBuffers(window);
	}

	glfwTerminate();
	return 0;
}
