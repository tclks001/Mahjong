#pragma once
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#define _USE_MATH_DEFINES
#include <cmath>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/vector_angle.hpp>

#include "physics.h"
#include "ui.h"


// 窗口与渲染
inline GLFWwindow* gWindow = nullptr;
inline const char* WINDOW_TITLE = "PhysX 5 + OpenGL + ImGui Bouncing Ball";
inline const int WINDOW_WIDTH = 1280;
inline const int WINDOW_HEIGHT = 720;

// 着色器程序 ID
inline GLuint gShaderProgram = 0;

// 球体的图形缓冲对象 (OpenGL VAO/VBO)
inline GLuint gBallVAO = 0;
inline GLuint gBallVBO = 0;
inline GLuint gBallEBO = 0;
inline std::vector<float> gBallVertices;
inline std::vector<unsigned int> gBallIndices;

// 视图/投影矩阵
inline glm::mat4 gViewMatrix;
inline glm::mat4 gProjMatrix;


bool initWindowAndOpenGL();
void glfw_error_callback(int error, const char* description);
void renderGround();
void renderBall();
// 工具函数：读取着色器源码并编译链接
GLuint compileShaders();
// 工具函数：生成一个圆滑的球体网格数据
void generateSphereMesh(float radius, int sectorCount, int stackCount);
void cleanup();