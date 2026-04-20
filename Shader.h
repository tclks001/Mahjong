#pragma once
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <unordered_map>

#include <GL/glew.h>

#include "Texture2D.h"


class Shader {
private:
	std::unordered_map<const GLchar*, GLint> location_cache;
	GLint getLocation(const GLchar* name);
public:
	GLuint program; // program id
	Shader(const GLchar* vertexPath, const GLchar* fragmentPath);
	~Shader();
	void Use() const;
	template<typename... Args>
	void UniformFloat(const GLchar* name, Args... args) {
		static_assert(sizeof...(args) >= 1 && sizeof...(args) <= 4,
			"UniformFloat supports 1 to 4 float arguments");
		GLint location = getLocation(name);
		if constexpr (sizeof...(args) == 1) {
			glUniform1f(location, args...);
		}
		else if constexpr (sizeof...(args) == 2) {
			glUniform2f(location, args...);
		}
		else if constexpr (sizeof...(args) == 3) {
			glUniform3f(location, args...);
		}
		else if constexpr (sizeof...(args) == 4) {
			glUniform4f(location, args...);
		}
	}
	void UniformMat4(const GLchar* name, const GLfloat* val);
	void UseTexture(int unit, const GLchar* name, const Texture2D& texture);
};
