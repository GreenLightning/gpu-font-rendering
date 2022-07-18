#include <iostream>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <ft2build.h>
#include FT_FREETYPE_H

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

	FT_Library library;
	FT_Error error = FT_Init_FreeType(&library);
	if (error) {
		std::cerr << "ERROR: failed to initialize FreeType" << std::endl;
		glfwTerminate();
		return 1;
	}

	while(!glfwWindowShouldClose(window)) {
		glfwPollEvents();

		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		glfwSwapBuffers(window);
	}

	glfwTerminate();
	return 0;
}
