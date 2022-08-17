#pragma once

#include <string>
#include <memory>

// A shader catalog loads and compiles shaders from a directory. Vertex and
// fragment shaders are matched based on their filename (e.g. example.vert and
// example.frag are loaded and linked together to form the "example" program).
// Whenever a shader file changes on disk, the corresponding program is
// recompiled and relinked.
class ShaderCatalog {
public:
	struct Entry {
		unsigned int program;

		Entry() : program(0) {}
		Entry(unsigned int program) : program(program) {}
	};

	ShaderCatalog(const std::string& dir);
	~ShaderCatalog();

	std::shared_ptr<Entry> get(const std::string& name);
	void update();

private:
	class Impl;
	std::unique_ptr<Impl> impl;
};
