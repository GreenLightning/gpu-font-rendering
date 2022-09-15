#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <defer.hpp>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "glm.hpp"

#include "shader_catalog.hpp"

#include "font.cpp"

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
	enum class Action {
		NONE,
		TRANSLATE,
		ROTATE_TURNTABLE,
		ROTATE_TRACKBALL
	};

	Transform* transform = nullptr;
	int activeButton = -1;
	Action activeAction = Action::NONE;

	double dragX, dragY;
	double wrapX, wrapY;
	double virtualX, virtualY;
	glm::vec3 dragTarget;

	void reset() {
		// Reset transform.
		*transform = Transform{};

		// Cancel active action, if any.
		activeButton = -1;
		activeAction = Action::NONE;
	}

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

			if (mods & GLFW_MOD_CONTROL) {
				activeAction = Action::TRANSLATE;
			} else {
				if (activeButton == GLFW_MOUSE_BUTTON_2) {
					activeAction = Action::TRANSLATE;
				} else if (activeButton == GLFW_MOUSE_BUTTON_3) {
					activeAction = Action::ROTATE_TURNTABLE;
				} else {
					activeAction = Action::ROTATE_TRACKBALL;
				}
			}

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
			activeAction = Action::NONE;
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
		if (activeAction == Action::NONE) return;

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

		if (activeAction == Action::TRANSLATE) {
			virtualX += deltaX;
			virtualY += deltaY;

			glm::vec3 target;
			bool ok = unprojectMousePositionToXYPlane(window, virtualX, virtualY, target);
			if (ok) {
				float x = transform->position.x;
				float y = transform->position.y;
				glm::vec3 delta = target - dragTarget;
				transform->position.x = glm::clamp(x + delta.x, -4.0f, 4.0f);
				transform->position.y = glm::clamp(y + delta.y, -4.0f, 4.0f);
			}
		} else if (activeAction == Action::ROTATE_TURNTABLE) {
			double size = glm::min(width, height);
			glm::mat3 rx = glm::rotate(float(deltaX / size * glm::pi<double>()), glm::vec3(0, 0, 1));
			glm::mat3 ry = glm::rotate(float(deltaY / size * glm::pi<double>()), glm::vec3(1, 0, 0));
			transform->rotation = ry * transform->rotation * rx;
		} else if (activeAction == Action::ROTATE_TRACKBALL) {
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

	std::unique_ptr<Font> mainFont;
	std::unique_ptr<Font> helpFont;

	constexpr float helpFontBaseSize = 20.0f;

	int antiAliasingWindowSize = 1;
	bool enableSuperSamplingAntiAliasing = true;
	bool enableControlPointsVisualization = false;

	bool showHelp = true;

	Font::BoundingBox bb;
	std::string mainText = 
R"DONE(In the center of Fedora, that gray stone metropolis, stands a metal building
with a crystal globe in every room. Looking into each globe, you see a blue
city, the model of a different Fedora. These are the forms the city could have
taken if, for one reason or another, it had not become what we see today. In
every age someone, looking at Fedora as it was, imagined a way of making it the
ideal city, but while he constructed his miniature model, Fedora was already no
longer the same as before, and what had been until yesterday a possible future
became only a toy in a glass globe.

The building with the globes is now Fedora's museum: every inhabitant visits it,
chooses the city that corresponds to his desires, contemplates it, imagining his
reflection in the medusa pond that would have collected the waters of the canal
(if it had not been dried up), the view from the high canopied box along the
avenue reserved for elephants (now banished from the city), the fun of sliding
down the spiral, twisting minaret (which never found a pedestal from which to
rise).

On the map of your empire, O Great Khan, there must be room both for the big,
stone Fedora and the little Fedoras in glass globes. Not because they are all
equally real, but because they are only assumptions. The one contains what is
accepted as necessary when it is not yet so; the others, what is imagined as
possible and, a moment later, is possible no longer.

[from Invisible Cities by Italo Calvino])DONE";

}

static std::unique_ptr<Font> loadFont(const std::string& filename, float worldSize = 1.0f, bool hinting = false) {
	std::string error;
	FT_Face face = Font::loadFace(library, filename, error);
	if (error != "") {
		std::cerr << "[font] failed to load " << filename << ": " << error << std::endl;
		return std::unique_ptr<Font>{};
	}

	return std::make_unique<Font>(face, worldSize, hinting);
}

static void tryUpdateMainFont(const std::string& filename) {
	auto font = loadFont(filename, 0.05f);
	if (!font) return;

	font->dilation = 0.1f;

	font->prepareGlyphsForText(mainText);

	mainFont = std::move(font);
	bb = mainFont->measure(0, 0, mainText);
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

static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
	if (action != GLFW_PRESS) return;
	switch (key) {
		case GLFW_KEY_R:
			dragController.reset();
			break;

		case GLFW_KEY_C:
			enableControlPointsVisualization = !enableControlPointsVisualization;
			break;

		case GLFW_KEY_A:
			enableSuperSamplingAntiAliasing = !enableSuperSamplingAntiAliasing;
			break;

		case GLFW_KEY_0:
			antiAliasingWindowSize = 0;
			break;

		case GLFW_KEY_1:
			antiAliasingWindowSize = 1;
			break;

		case GLFW_KEY_2:
			antiAliasingWindowSize = 20;
			break;

		case GLFW_KEY_3:
			antiAliasingWindowSize = 40;
			break;

		case GLFW_KEY_S:
			antiAliasingWindowSize = 1;
			enableSuperSamplingAntiAliasing = true;
			break;

		case GLFW_KEY_H:
			showHelp = !showHelp;
			break;
	}
}

static void dropCallback(GLFWwindow* window, int pathCount, const char* paths[]) {
	if (pathCount == 0) return;
	tryUpdateMainFont(paths[0]);
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
	glfwSetKeyCallback(window, keyCallback);
	glfwSetDropCallback(window, dropCallback);

	glGenVertexArrays(1, &emptyVAO);

	shaderCatalog = std::make_unique<ShaderCatalog>("shaders");
	backgroundShader = shaderCatalog->get("background");
	fontShader = shaderCatalog->get("font");

	tryUpdateMainFont("fonts/SourceSerifPro-Regular.otf");

	{
		float xscale, yscale;
		glfwGetWindowContentScale(window, &xscale, &yscale);
		float worldSize = std::ceil(helpFontBaseSize * yscale);
		helpFont = loadFont("fonts/SourceSansPro-Semibold.otf", worldSize, true);
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
			GLuint program = backgroundShader->program;
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

		if (mainFont) {
			GLuint program = fontShader->program;
			glUseProgram(program);

			mainFont->program = program;
			mainFont->drawSetup();

			location = glGetUniformLocation(program, "projection");
			glUniformMatrix4fv(location, 1, false, glm::value_ptr(projection));
			location = glGetUniformLocation(program, "view");
			glUniformMatrix4fv(location, 1, false, glm::value_ptr(view));
			location = glGetUniformLocation(program, "model");
			glUniformMatrix4fv(location, 1, false, glm::value_ptr(model));

			location = glGetUniformLocation(program, "color");
			glUniform4f(location, 1.0f, 1.0f, 1.0f, 1.0f);

			location = glGetUniformLocation(program, "antiAliasingWindowSize");
			glUniform1f(location, (float) antiAliasingWindowSize);
			location = glGetUniformLocation(program, "enableSuperSamplingAntiAliasing");
			glUniform1i(location, enableSuperSamplingAntiAliasing);
			location = glGetUniformLocation(program, "enableControlPointsVisualization");
			glUniform1i(location, enableControlPointsVisualization);

			float cx = 0.5f * (bb.minX + bb.maxX);
			float cy = 0.5f * (bb.minY + bb.maxY);
			mainFont->draw(-cx, -cy, mainText);
			glUseProgram(0);
		}

		if (helpFont && showHelp) {
			GLuint program = fontShader->program;
			glUseProgram(program);

			helpFont->program = program;
			helpFont->drawSetup();

			glm::mat4 projection = glm::ortho(0.0f, (float) width, 0.0f, (float) height, -1.0f, 1.0f);
			glm::mat4 view = glm::mat4(1.0f);
			glm::mat4 model = glm::mat4(1.0f);

			location = glGetUniformLocation(program, "projection");
			glUniformMatrix4fv(location, 1, false, glm::value_ptr(projection));
			location = glGetUniformLocation(program, "view");
			glUniformMatrix4fv(location, 1, false, glm::value_ptr(view));
			location = glGetUniformLocation(program, "model");
			glUniformMatrix4fv(location, 1, false, glm::value_ptr(model));

			location = glGetUniformLocation(program, "color");
			float r = 200, g = 35, b = 220, a = 0.8;
			glUniform4f(location, r * a / 255.0f, g * a / 255.0f, b * a / 255.0f, a);

			location = glGetUniformLocation(program, "antiAliasingWindowSize");
			glUniform1f(location, 1.0f);
			location = glGetUniformLocation(program, "enableSuperSamplingAntiAliasing");
			glUniform1i(location, true);
			location = glGetUniformLocation(program, "enableControlPointsVisualization");
			glUniform1i(location, false);

			std::stringstream stream;
			stream << "Drag and drop a .ttf or .otf file to change the font\n";
			stream << "\n";
			stream << "right drag (or CTRL drag) - move\n";
			stream << "left drag - trackball rotate\n";
			stream << "middle drag - turntable rotate\n";
			stream << "scroll wheel - zoom\n";
			stream << "\n";
			stream << "0, 1, 2, 3 - change anti-aliasing window size: " << antiAliasingWindowSize << " pixel" << ((antiAliasingWindowSize != 1) ? "s" : "") << "\n";
			stream << glfwGetKeyName(GLFW_KEY_A, 0) << " - " << (enableSuperSamplingAntiAliasing ? "disable" : "enable") << " 2D anti-aliasing\n";
			stream << "(using another ray along the y-axis)\n";
			stream << glfwGetKeyName(GLFW_KEY_S, 0) << " - reset anti-aliasing settings\n";
			stream << glfwGetKeyName(GLFW_KEY_C, 0) << " - " << (enableControlPointsVisualization ? "disable" : "enable") << " control points\n";
			stream << glfwGetKeyName(GLFW_KEY_R, 0) << " - reset view\n";
			stream << glfwGetKeyName(GLFW_KEY_H, 0) << " - toggle help\n";

			std::string helpText = stream.str();
			helpFont->prepareGlyphsForText(helpText);

			float xscale, yscale;
			glfwGetWindowContentScale(window, &xscale, &yscale);
			helpFont->setWorldSize(std::ceil(helpFontBaseSize * yscale));

			auto bb = helpFont->measure(0, 0, helpText);
			helpFont->draw(10 - bb.minX, height - 10 - bb.maxY, helpText);
			glUseProgram(0);
		}

		glDisable(GL_BLEND);

		glfwSwapBuffers(window);
	}

	glfwTerminate();
	return 0;
}
