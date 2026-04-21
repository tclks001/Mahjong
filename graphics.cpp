// 渲染模块
// 负责图形渲染
// 给主程序提供绘制函数，主程序每帧调用一次
// 从物理模块中获取粒子状态并绘制

#include "graphics.h"
#include <iostream>

using namespace glm;

// =========================================================
// 初始化窗口和 OpenGL 上下文
// =========================================================
bool initWindowAndOpenGL() {
    glfwSetErrorCallback(glfw_error_callback);

    if (!glfwInit()) {
        std::cerr << "GLFW initialization failed!\n";
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    gWindow = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_TITLE, nullptr, nullptr);
    if (!gWindow) {
        std::cerr << "Failed to create GLFW window!\n";
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(gWindow);
    glfwSwapInterval(1); // 开启垂直同步 (V-Sync)

    // 初始化 GLEW
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "GLEW initialization failed!\n";
        return false;
    }

    std::cout << "Window and OpenGL initialized successfully.\n";
    std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << "\n";
    return true;
}

// GLFW 错误回调
void glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}


// =========================================================
// 工具函数实现：编译着色器
// GLSL 4.30, 使用SSBO传递粒子位置数据, 点精灵渲染
// =========================================================

// ---- 粒子渲染: 顶点着色器 (通过 SSBO 获取粒子位置) ----
const char* particleVertexShaderSource = R"(
#version 430 core

// 注意: 使用 gl_VertexID 索引SSBO, 不需要顶点属性输入

layout (std140, binding = 0) buffer ParticlePositions {
    vec4 positions[];
};

uniform mat4 u_Model;
uniform mat4 u_View;
uniform mat4 u_Proj;

out vec3 FragPos;

void main() {
    // 从SSBO按顶点ID读取粒子位置 (xyz), w分量忽略
    vec4 pos = positions[gl_VertexID];
    // 应用模型矩阵(四元数旋转) -> 视图 -> 投影
    vec4 worldPos = u_Model * vec4(pos.xyz, 1.0);
    FragPos = worldPos.xyz;
    gl_Position = u_Proj * u_View * worldPos;
    // 点精灵大小固定
    gl_PointSize = 4.0;
}
)";

// ---- 粒子渲染: 片元着色器 (纯白点精灵) ----
const char* particleFragmentShaderSource = R"(
#version 430 core

in vec3 FragPos;
out vec4 FragColor;

void main() {
    // 圆形点精灵: 距离中心 > 半径则丢弃
    vec2 coord = gl_PointCoord - vec2(0.5);
    if (dot(coord, coord) > 0.25) discard;
    FragColor = vec4(1.0f, 1.0f, 1.0f, 1.0f); // 纯白
}
)";

// ---- 容器线框: 顶点着色器 ----
const char* containerVertexShaderSource = R"(
#version 430 core

layout (location = 0) in vec3 aPos;

uniform mat4 u_Model;
uniform mat4 u_View;
uniform mat4 u_Proj;

void main() {
    gl_Position = u_Proj * u_View * u_Model * vec4(aPos, 1.0);
}
)";

// ---- 容器线框: 片元着色器 (高对比度亮色线框) ----
const char* containerFragmentShaderSource = R"(
#version 430 core

out vec4 FragColor;

void main() {
    FragColor = vec4(0.4f, 0.7f, 1.0f, 1.0f); // 高对比度浅蓝
}
)";


// =========================================================
// 渲染资源 (全局持久, 跨帧复用)
// =========================================================
static GLuint gParticleSSBO = 0;          // 粒子位置 SSBO
static GLuint gContainerVAO = 0;          // 容器线框 VAO
static GLuint gContainerVBO = 0;          // 容器线框 VBO (边)
static GLuint gContainerProgram = 0;      // 容器线框着色器程序
static bool   gRenderInitialized = false; // 初始化标志

GLuint compileShaders() {
    GLuint particleProgram = glCreateProgram();
    GLuint containerProgram = glCreateProgram();

    // ====== 编译粒子着色器 ======
    GLuint pVS = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(pVS, 1, &particleVertexShaderSource, NULL);
    glCompileShader(pVS);

    GLuint pFS = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(pFS, 1, &particleFragmentShaderSource, NULL);
    glCompileShader(pFS);

    glAttachShader(particleProgram, pVS);
    glAttachShader(particleProgram, pFS);
    glLinkProgram(particleProgram);

    glDeleteShader(pVS);
    glDeleteShader(pFS);

    // ====== 编译容器线框着色器 ======
    GLuint cVS = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(cVS, 1, &containerVertexShaderSource, NULL);
    glCompileShader(cVS);

    GLuint cFS = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(cFS, 1, &containerFragmentShaderSource, NULL);
    glCompileShader(cFS);

    glAttachShader(containerProgram, cVS);
    glAttachShader(containerProgram, cFS);
    glLinkProgram(containerProgram);

    glDeleteShader(cVS);
    glDeleteShader(cFS);

    gShaderProgram = particleProgram; // 主程序ID用于粒子渲染
    gContainerProgram = containerProgram; // 保存容器线框着色器程序

    return particleProgram;
}

void initRenderResources() {
    if (gRenderInitialized) return;

    // ----- 创建粒子位置 SSBO -----
    glGenBuffers(1, &gParticleSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, gParticleSSBO);
    // 预分配空间 (5000个vec4 * 16字节 = 80KB)
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 particleCount * sizeof(glm::vec4),
                 nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, gParticleSSBO); // binding = 0
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // ----- 构建容器线框顶点 (12条边的线框盒子) -----
    // 容器中心在原点:
    //   X: [-W/2, W/2], Y: [-H/2, H/2], Z: [-D/2, D/2]
    const float hx = containerWidth / 2.0f;
    const float hy = containerDepth / 2.0f;
    const float hz = containerHeight / 2.0f;

    // 8个角点
    glm::vec3 corners[8] = {
        {-hx, -hy, -hz}, {hx, -hy, -hz},  // 底面: 0-1
        {hx, -hy,  hz},  {-hx, -hy,  hz},  //        2-3
        {-hx,  hy, -hz}, {hx,  hy, -hz},  // 顶面: 4-5
        {hx,  hy,  hz},  {-hx,  hy,  hz},  //        6-7
    };

    // 12条边 (每条边2个顶点 = 24个顶点)
    int edgeIndices[24] = {
        0,1, 1,2, 2,3, 3,0,   // 底面4边
        4,5, 5,6, 6,7, 7,4,   // 顶面4边
        0,4, 1,5, 2,6, 3,7    // 4条竖边
    };

    GLfloat containerVertices[24 * 3];
    for (int i = 0; i < 24; ++i) {
        const glm::vec3& c = corners[edgeIndices[i]];
        containerVertices[i * 3 + 0] = c.x;
        containerVertices[i * 3 + 1] = c.y;
        containerVertices[i * 3 + 2] = c.z;
    }

    glGenVertexArrays(1, &gContainerVAO);
    glGenBuffers(1, &gContainerVBO);
    glBindVertexArray(gContainerVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gContainerVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(containerVertices), containerVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(GLfloat), (void*)0);
    glBindVertexArray(0);

    gRenderInitialized = true;
}

void render() {
    // 延迟初始化 (确保在OpenGL上下文就绪后调用)
    initRenderResources();

    // ====== 更新粒子位置到 SSBO ======
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, gParticleSSBO);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                    particleCount * sizeof(glm::vec4),
                    particlePositions.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // ====== 构建模型矩阵 (从四元数) ======
    glm::mat4 modelMatrix = mat4_cast(gObjectOrientation);

    // ====== 摄像机: 固定在 y=10, 朝向 -Y 观察, up=+Z ======
    glm::mat4 viewMatrix = lookAt(vec3(0.0f, 10.0f, 0.01f),
                                  vec3(0.0f, 0.0f, 0.0f),
                                  vec3(0.0f, 0.0f, 1.0f));

    // ====== 渲染粒子 (点精灵) ======
    glEnable(GL_PROGRAM_POINT_SIZE); // 启用着色器控制的点大小
    glUseProgram(gShaderProgram);
    glUniformMatrix4fv(glGetUniformLocation(gShaderProgram, "u_Model"), 1, GL_FALSE, &modelMatrix[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(gShaderProgram, "u_View"), 1, GL_FALSE, &viewMatrix[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(gShaderProgram, "u_Proj"), 1, GL_FALSE, &gProjMatrix[0][0]);

    // Core Profile 要求绑定 VAO (即使是空VAO)
    GLuint dummyVAO = 0;
    glGenVertexArrays(1, &dummyVAO);
    glBindVertexArray(dummyVAO);
    glDrawArrays(GL_POINTS, 0, particleCount);

    // ====== 渲染容器线框 (使用专用着色器程序) ======
    glUseProgram(gContainerProgram);
    glUniformMatrix4fv(glGetUniformLocation(gContainerProgram, "u_Model"), 1, GL_FALSE, &modelMatrix[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(gContainerProgram, "u_View"), 1, GL_FALSE, &viewMatrix[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(gContainerProgram, "u_Proj"), 1, GL_FALSE, &gProjMatrix[0][0]);

    glBindVertexArray(gContainerVAO);
    glLineWidth(1.5f);
    glDrawArrays(GL_LINES, 0, 24);  // 12条边 x 2顶点
    glBindVertexArray(0);
    glDisable(GL_PROGRAM_POINT_SIZE);

    // 清理临时VAO
    if (dummyVAO != 0) {
        GLuint vaoToDelete = dummyVAO;
        glDeleteVertexArrays(1, &vaoToDelete);
    }
}