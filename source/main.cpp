#include <fstream>
#include <iostream>
#include <string>

#include <defer.hpp>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <ft2build.h>
#include FT_FREETYPE_H

namespace {
	FT_Library library;

	GLuint quadVAO;

	GLuint backgroundProgram;
}

std::string readFile(std::string filename, std::string& error) {
	std::ifstream stream(filename, std::ios::binary);
	if (!stream) { error = "failed to open: " + filename; return ""; }

	stream.seekg(0, std::istream::end);
	size_t size = stream.tellg();
	stream.seekg(0, std::istream::beg);

	std::string result = std::string(size, 0);
	stream.read(&result[0], size);
	if (!stream) { error = "failed to read: " + filename; return ""; }

	return result;
}

GLuint compileShader(std::string name, std::string& error) {
	std::string vertexData = readFile("shaders/" + name + ".vert", error);
	if (error != "") return 0;

	std::string fragmentData = readFile("shaders/" + name + ".frag", error);
	if (error != "") return 0;

	const char* vertexSource = vertexData.c_str();
	GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
	defer { glDeleteShader(vertexShader); };
	glShaderSource(vertexShader, 1, &vertexSource, nullptr);
	glCompileShader(vertexShader);

	const char* fragmentSource = fragmentData.c_str();
	GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
	defer { glDeleteShader(fragmentShader); };
	glShaderSource(fragmentShader, 1, &fragmentSource, nullptr);
	glCompileShader(fragmentShader);

	GLuint program = glCreateProgram();
	glAttachShader(program, vertexShader);
	glAttachShader(program, fragmentShader);
	glLinkProgram(program);

	GLint success = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &success);
	if (!success) {
		char log [1024];
		GLsizei length = 0;
		glGetProgramInfoLog(program, sizeof(log), &length, log);
		glDeleteProgram(program);
		error = "failed to compile program " + name + ":\n\n" + log;
		return 0;
	}

	return program;
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

	{
		std::string error;
		backgroundProgram = compileShader("background", error);
		if (error != "") std::cerr << "ERROR: " << error << std::endl;
	}

	while(!glfwWindowShouldClose(window)) {
		glfwPollEvents();

		int width, height;
		glfwGetFramebufferSize(window, &width, &height);
		glViewport(0, 0, width, height);

		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		glUseProgram(backgroundProgram);
		glBindVertexArray(quadVAO);

		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		glBindVertexArray(0);
		glUseProgram(0);

		glfwSwapBuffers(window);
	}

	glfwTerminate();
	return 0;
}
