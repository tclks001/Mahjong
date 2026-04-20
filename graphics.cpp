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

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

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
// 渲染地面
// =========================================================
void renderGround() {
    GLuint modelLoc = glGetUniformLocation(gShaderProgram, "u_Model");
    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, &gGroundModelMatrix[0][0]);

    // 绘制一个四边形作为地面
    glBegin(GL_QUADS);
    glVertex3f(-1.0f, 0.0f, -1.0f);
    glVertex3f(1.0f, 0.0f, -1.0f);
    glVertex3f(1.0f, 0.0f, 1.0f);
    glVertex3f(-1.0f, 0.0f, 1.0f);
    glEnd();
}

// =========================================================
// 渲染球体：从 PhysX 获取最新位置并同步到 OpenGL
// =========================================================
void renderBall() {
    // 1. 从 PhysX 获取球体的当前位置和旋转
    PxTransform pose = gBall->getGlobalPose();
    PxVec3 pos = pose.p;
    PxQuat quat = pose.q;

    // 2. 将 PhysX 的变换数据转换为 GLM 的矩阵
    mat4 translation = translate(mat4(1.0f), vec3(pos.x, pos.y, pos.z));
    mat4 rotation = toMat4(glm::quat(quat.w, quat.x, quat.y, quat.z)); // GLM 可以直接从四元数构造矩阵

    gBallModelMatrix = translation * rotation;

    // 3. 传递给着色器并绘制
    GLuint modelLoc = glGetUniformLocation(gShaderProgram, "u_Model");
    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, &gBallModelMatrix[0][0]);

    glBindVertexArray(gBallVAO);
    glDrawElements(GL_TRIANGLES, (GLsizei)gBallIndices.size(), GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

// =========================================================
// 工具函数实现：编译着色器
// =========================================================
GLuint compileShaders() {
    // 极度精简的顶点着色器：只负责将顶点位置传递并投影
    const char* vertexShaderSource = R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        
        uniform mat4 u_Model;
        uniform mat4 u_View;
        uniform mat4 u_Proj;
        
        void main() {
            gl_Position = u_Proj * u_View * u_Model * vec4(aPos, 1.0);
        }
    )";

    // 片段着色器：给物体一个简单的颜色
    const char* fragmentShaderSource = R"(
        #version 330 core
        out vec4 FragColor;
        
        void main() {
            FragColor = vec4(0.8f, 0.2f, 0.2f, 1.0f); // 红色球体
        }
    )";

    // 编译顶点着色器
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);

    // 编译片段着色器
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);

    // 链接着色器程序
    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    // 删除不再需要的着色器对象
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return shaderProgram;
}

// =========================================================
// 工具函数实现：生成球体网格 (UV Sphere)
// =========================================================
void generateSphereMesh(float radius, int sectorCount, int stackCount) {
    gBallVertices.clear();
    gBallIndices.clear();

    float x, y, z, xy;                              // vertex position
    //float s, t;                                     // texCoord
    float sectorStep = 2 * (float)M_PI / sectorCount;
    float stackStep = (float)M_PI / stackCount;
    float sectorAngle, stackAngle;

    for (int i = 0; i <= stackCount; ++i) {
        stackAngle = (float)M_PI / 2 - i * stackStep;        // [pi/2, -pi/2]
        xy = radius * cosf(stackAngle);             // r * cos(u)
        z = radius * sinf(stackAngle);              // r * sin(u)

        for (int j = 0; j <= sectorCount; ++j) {
            sectorAngle = j * sectorStep;           // [0, 2pi]

            x = xy * cosf(sectorAngle);             // r * cos(u) * cos(v)
            y = xy * sinf(sectorAngle);             // r * cos(u) * sin(v)

            gBallVertices.push_back(x);
            gBallVertices.push_back(y);
            gBallVertices.push_back(z);
        }
    }

    int k1, k2;
    for (int i = 0; i < stackCount; ++i) {
        k1 = i * (sectorCount + 1);
        k2 = k1 + sectorCount + 1;
        for (int j = 0; j < sectorCount; ++j, ++k1, ++k2) {
            if (i != 0) {
                gBallIndices.push_back(k1);
                gBallIndices.push_back(k2);
                gBallIndices.push_back(k1 + 1);
            }
            if (i != (stackCount - 1)) {
                gBallIndices.push_back(k1 + 1);
                gBallIndices.push_back(k2);
                gBallIndices.push_back(k2 + 1);
            }
        }
    }

    glGenVertexArrays(1, &gBallVAO);
    glGenBuffers(1, &gBallVBO);
    glGenBuffers(1, &gBallEBO);

    glBindVertexArray(gBallVAO);

    glBindBuffer(GL_ARRAY_BUFFER, gBallVBO);
    glBufferData(GL_ARRAY_BUFFER, gBallVertices.size() * sizeof(float), gBallVertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gBallEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, gBallIndices.size() * sizeof(unsigned int), gBallIndices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}






// =========================================================
// 清理所有分配的资源
// =========================================================
void cleanup() {
    // 逆序释放资源是个好习惯
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glDeleteProgram(gShaderProgram);
    glDeleteVertexArrays(1, &gBallVAO);
    glDeleteBuffers(1, &gBallVBO);
    glDeleteBuffers(1, &gBallEBO);

    if (gScene) gScene->release();
    if (gPhysics) gPhysics->release();
    if (gPvd) gPvd->release();
    if (gFoundation) gFoundation->release();

    glfwDestroyWindow(gWindow);
    glfwTerminate();
}
