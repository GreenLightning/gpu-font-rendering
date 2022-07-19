#pragma once

#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <defer.hpp>

#include <glad/glad.h>

#include <efsw/efsw.hpp>

class ShaderCatalog {
public:
	class Entry {
		friend class ShaderCatalog;

		GLuint id = 0;

	public:
		Entry(GLuint id) : id(id) {}
		GLuint getProgram() { return id; }
	};

private:
	// UpdateList keeps track of which entries need to be updated.
	// The actual update is slightly delayed to avoid reading a partially written file.
	// It is threadsafe to allow safe communication with the asynchronous file watcher callback.
	class UpdateList {
		std::mutex mutex;
		std::unordered_map<std::string, std::chrono::steady_clock::time_point> updates;

	public:
		void requestUpdate(const std::string& name) {
			using namespace std::chrono_literals;
			std::lock_guard<std::mutex> guard(mutex);
			updates[name] = std::chrono::steady_clock::now() + 50ms;
		}

		std::vector<std::string> collectDueUpdates() {
			std::lock_guard<std::mutex> guard(mutex);
			std::vector<std::string> result;
			auto now = std::chrono::steady_clock::now();
			for (auto it = updates.begin(); it != updates.end(); ) {
				if (it->second < now) {
					result.push_back(it->first);
					it = updates.erase(it);
				} else {
					++it;
				}
			}
			return result;
		}
	};

	class FileListener : public efsw::FileWatchListener {
		UpdateList* list;

	public:
		FileListener(UpdateList* list) : list(list) {}

		void handleFileAction(efsw::WatchID watchid, const std::string& dir, const std::string& filename, efsw::Action action, std::string oldFilename) override {
			auto index = filename.rfind('.');
			std::string basename = filename.substr(0, index);
			list->requestUpdate(basename);
		}
	};

private:
	std::string dir;
	std::unordered_map<std::string, std::shared_ptr<Entry>> entries;

	UpdateList list;
	efsw::FileWatcher watcher;
	FileListener listener;

public:
	ShaderCatalog(const std::string& dir) : dir(dir), listener(&list) {
		watcher.addWatch(dir, &listener, /* recursive = */ false);
		watcher.watch();
	}

private:
	std::string readFile(const std::string& filename, std::string& error) {
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

	GLuint compile(const std::string& name, std::string& error) {
		std::string vertexData = readFile(dir + "/" + name + ".vert", error);
		if (error != "") return 0;

		std::string fragmentData = readFile(dir + "/" + name + ".frag", error);
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

public:
	std::shared_ptr<Entry> get(const std::string& name) {
		auto it = entries.find(name);
		if (it != entries.end()) return it->second;

		std::string error;
		GLuint id = compile(name, error);
		if (error != "") {
			std::cerr << "[shader] " << error << std::endl;
		}

		auto entry = std::make_shared<Entry>(id);
		entries[name] = entry;
		return entry;
	}

	void update() {
		std::vector<std::string> updates = list.collectDueUpdates();
		for (const std::string& name : updates) {
			auto it = entries.find(name);
			if (it == entries.end()) continue;

			std::string error;
			GLuint id = compile(name, error);
			if (error != "") {
				std::cerr << "[shader] " << error << std::endl;
			} else {
				std::cerr << "[shader] reloaded " << name << std::endl;
				glDeleteProgram(it->second->id);
				it->second->id = id;
			}
		}
	}
};
