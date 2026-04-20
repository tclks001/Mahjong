#pragma once
// glm
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
//#include <glm/gtc/random.hpp>
#include <glm/gtx/vector_angle.hpp>
// GLFW
#include <GLFW/glfw3.h>

class Camera {
private:
	const int screenWidth;
	const int screenHeight;
	glm::vec3 pos;
	glm::vec3 front;
	glm::vec3 up;
	GLfloat aspect;

public:
	Camera();
	void move(GLfloat x, GLfloat z);
	void look(GLfloat x, GLfloat y);
	void scale(GLfloat x);
	glm::mat4 view() const;
	glm::mat4 projection() const;
};