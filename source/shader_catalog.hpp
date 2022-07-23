#pragma once

#include <string>
#include <memory>

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
