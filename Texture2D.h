#pragma once
#include <GL/glew.h>
#include <iostream>
// stb_image

 #include "stb_image.h"

class Texture2D {
public:
	int width;
	int height;
	GLuint tex;
	GLenum format;
	Texture2D(const GLchar* texPath);
	void Bind() const;
};