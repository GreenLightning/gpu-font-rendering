#include <iostream>
#include <string>

#include <defer.hpp>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "shader_catalog.hpp"

namespace {
	FT_Library library;

	GLuint quadVAO;

	std::unique_ptr<ShaderCatalog> shaderCatalog;
	std::shared_ptr<ShaderCatalog::Entry> backgroundShader;
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

	glGenVertexArrays(1, &quadVAO);

	shaderCatalog = std::make_unique<ShaderCatalog>("shaders");
	backgroundShader = shaderCatalog->get("background");

	while(!glfwWindowShouldClose(window)) {
		shaderCatalog->update();

		glfwPollEvents();

		int width, height;
		glfwGetFramebufferSize(window, &width, &height);
		glViewport(0, 0, width, height);

		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		GLuint program = backgroundShader->getProgram();
		glUseProgram(program);
		glBindVertexArray(quadVAO);

		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		glBindVertexArray(0);
		glUseProgram(0);

		glfwSwapBuffers(window);
	}

	glfwTerminate();
	return 0;
}
