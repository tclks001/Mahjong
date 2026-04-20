#include <iostream>
#include <vector>
#define _USE_MATH_DEFINES
#include <cmath>
// 第三方库头文件
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/vector_angle.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <PxPhysicsAPI.h>


// 命名空间缩写，写起来更爽快
using namespace physx;
using namespace glm;

// =========================================================
// 全局变量区：把控整个程序的脉络
// =========================================================

// 窗口与渲染
GLFWwindow* gWindow = nullptr;
const char* WINDOW_TITLE = "PhysX 5 + OpenGL + ImGui Bouncing Ball";
const int WINDOW_WIDTH = 1280;
const int WINDOW_HEIGHT = 720;

// 着色器程序 ID
GLuint gShaderProgram = 0;

// 物理引擎核心组件
PxPhysics* gPhysics = nullptr;
PxDefaultAllocator gAllocator;
PxDefaultErrorCallback gErrorCallback;
PxFoundation* gFoundation = nullptr;
PxScene* gScene = nullptr;
PxMaterial* gMaterial = nullptr;

// PVD (PhysX Visual Debugger) 连接管理器
PxPvd* gPvd = nullptr;

// 物理刚体：平面（地面）和球体
PxRigidStatic* gGroundPlane = nullptr;
PxRigidDynamic* gBall = nullptr;

// 球体的图形缓冲对象 (OpenGL VAO/VBO)
GLuint gBallVAO = 0;
GLuint gBallVBO = 0;
GLuint gBallEBO = 0;
std::vector<float> gBallVertices;
std::vector<unsigned int> gBallIndices;

// 平面和球体的变换矩阵 (用于渲染)
mat4 gGroundModelMatrix = mat4(1.0f);
mat4 gBallModelMatrix = mat4(1.0f);

// 视图/投影矩阵
mat4 gViewMatrix;
mat4 gProjMatrix;

// ImGui 交互控制变量
bool gIsSimulating = true;
float gBallRadius = 1.0f;
vec3 gBallStartPos = vec3(0.0f, 10.0f, 0.0f); // 球体初始高度
float gPhysicsTimeStep = 1.0f / 60.0f; // 固定时间步长

// =========================================================
// 函数声明：让代码结构更清晰
// =========================================================
bool initWindowAndOpenGL();
void initImGui();
void initPhysX();
void createGroundPlane();
void createBall(const vec3& position, float radius);
void resetSimulation();
void updatePhysics(float dt);
void renderGround();
void renderBall();
void renderUI();
void cleanup();

// 工具函数：读取着色器源码并编译链接
GLuint compileShaders();
// 工具函数：生成一个圆滑的球体网格数据
void generateSphereMesh(float radius, int sectorCount, int stackCount);

// GLFW 错误回调
void glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

// =========================================================
// 主函数：程序的入口
// =========================================================
int main() {
    if (!initWindowAndOpenGL()) {
        std::cerr << "Failed to initialize Window or OpenGL!\n";
        return -1;
    }
    initImGui();
    initPhysX();
    createGroundPlane();
    createBall(gBallStartPos, gBallRadius);

    gShaderProgram = compileShaders();
    glUseProgram(gShaderProgram);

    // 设置透视投影矩阵
    gProjMatrix = perspective(radians(45.0f), (float)WINDOW_WIDTH / WINDOW_HEIGHT, 0.1f, 100.0f);
    // 设置观察矩阵 (摄像机)
    gViewMatrix = lookAt(vec3(15.0f, 15.0f, 15.0f), vec3(0.0f, 5.0f, 0.0f), vec3(0.0f, 1.0f, 0.0f));

    // 传递固定的视图和投影矩阵到着色器
    GLuint viewLoc = glGetUniformLocation(gShaderProgram, "u_View");
    GLuint projLoc = glGetUniformLocation(gShaderProgram, "u_Proj");
    glUniformMatrix4fv(viewLoc, 1, GL_FALSE, &gViewMatrix[0][0]);
    glUniformMatrix4fv(projLoc, 1, GL_FALSE, &gProjMatrix[0][0]);

    // 启用深度测试，让3D遮挡关系正确显示
    glEnable(GL_DEPTH_TEST);

    // =========================================================
    // 主渲染与物理循环
    // =========================================================
    while (!glfwWindowShouldClose(gWindow)) {
        // 1. 轮询事件
        glfwPollEvents();

        // 2. 更新物理世界 (如果模拟开启)
        if (gIsSimulating) {
            updatePhysics(gPhysicsTimeStep);
        }

        // 3. 清除屏幕缓冲区
        glClearColor(0.2f, 0.25f, 0.3f, 1.0f); // 舒服的深蓝灰色背景
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // 4. 渲染 3D 物体
        renderGround();
        renderBall();

        // 5. 渲染 ImGui UI
        renderUI();

        // 6. 双缓冲交换
        glfwSwapBuffers(gWindow);
    }

    cleanup();
    return 0;
}

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

// =========================================================
// 初始化 ImGui
// =========================================================
void initImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(gWindow, true);
    ImGui_ImplOpenGL3_Init("#version 130"); // 匹配 GLSL 1.30 for OpenGL 3.3 Core
}

// =========================================================
// 初始化 PhysX 5 物理引擎
// =========================================================
void initPhysX() {
    // 1. 创建 Foundation (PhysX 的根对象)
    gFoundation = PxCreateFoundation(PX_PHYSICS_VERSION, gAllocator, gErrorCallback);
    if (!gFoundation) {
        std::cerr << "PxCreateFoundation failed!\n";
        return;
    }

    // 2. 创建 PVD (PhysX Visual Debugger) 连接，方便用官方调试工具连入查看
    gPvd = PxCreatePvd(*gFoundation);
    PxPvdTransport* transport = PxDefaultPvdSocketTransportCreate("127.0.0.1", 5425, 10);
    gPvd->connect(*transport, PxPvdInstrumentationFlag::eALL);

    // 3. 创建 Physics SDK 实例
    gPhysics = PxCreatePhysics(PX_PHYSICS_VERSION, *gFoundation, PxTolerancesScale(), true, gPvd);
    if (!gPhysics) {
        std::cerr << "PxCreatePhysics failed!\n";
        return;
    }

    // 4. 创建 Scene 场景描述
    PxSceneDesc sceneDesc(gPhysics->getTolerancesScale());
    sceneDesc.gravity = PxVec3(0.0f, -9.81f, 0.0f); // 设置重力向下

    // 使用默认的 CPU 调度器
    PxDefaultCpuDispatcher* dispatcher = PxDefaultCpuDispatcherCreate(2);
    sceneDesc.cpuDispatcher = dispatcher;
    sceneDesc.filterShader = PxDefaultSimulationFilterShader; // 默认碰撞过滤着色器

    // 5. 创建 Scene
    gScene = gPhysics->createScene(sceneDesc);
    if (!gScene) {
        std::cerr << "createScene failed!\n";
        return;
    }

    // 6. 创建默认物理材质 (静态摩擦系数, 动态摩擦系数, 弹性恢复系数)
    gMaterial = gPhysics->createMaterial(0.5f, 0.5f, 0.6f); // 弹性设为0.6，球会有很好的反弹效果

    std::cout << "PhysX 5 initialized successfully.\n";
}

// =========================================================
// 创建地面平面
// =========================================================
void createGroundPlane() {
    // 使用 PxCreatePlane 辅助函数快速创建一个无限大的平面
    gGroundPlane = PxCreatePlane(*gPhysics, PxPlane(PxVec3(0, 1, 0), 0), *gMaterial);
    gScene->addActor(*gGroundPlane);

    // 对应 OpenGL 的地面模型矩阵：铺开一个大平面
    gGroundModelMatrix = scale(mat4(1.0f), vec3(20.0f, 0.01f, 20.0f));
}

// =========================================================
// 创建弹跳球体
// =========================================================
void createBall(const vec3& position, float radius) {
    // 1. 创建动态刚体描述
    PxTransform transform(PxVec3(position.x, position.y, position.z));
    gBall = gPhysics->createRigidDynamic(transform);

    // 2. 设置球体几何形状
    PxSphereGeometry geometry(radius);

    // 3. 计算球体的质量属性 (密度默认设为 1)
    PxRigidBodyExt::updateMassAndInertia(*gBall, 1.0f);

    // 4. 将形状附加到刚体上
    PxRigidActorExt::createExclusiveShape(*gBall, geometry, *gMaterial);

    // 5. 将刚体加入场景
    gScene->addActor(*gBall);

    // 6. 生成球体的 OpenGL 渲染网格 (低多边形风格，看起来有质感)
    generateSphereMesh(radius, 24, 16);
}

// =========================================================
// 重置物理模拟：把球拿回手里，重新扔下去
// =========================================================
void resetSimulation() {
    if (gBall) {
        gBall->setGlobalPose(PxTransform(PxVec3(gBallStartPos.x, gBallStartPos.y, gBallStartPos.z)));
        gBall->setLinearVelocity(PxVec3(0)); // 清零速度
        gBall->setAngularVelocity(PxVec3(0));
    }
}

// =========================================================
// 更新物理模拟
// =========================================================
void updatePhysics(float dt) {
    if (gScene) {
        gScene->simulate(dt);
        gScene->fetchResults(true); // 等待当前帧模拟完成
    }
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
    glDrawElements(GL_TRIANGLES, gBallIndices.size(), GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

// =========================================================
// 渲染 ImGui 用户界面
// =========================================================
void renderUI() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // 创建一个控制面板窗口
    ImGui::Begin("Physics Controls");

    ImGui::Text("Welcome to the PhysX 5 Sandbox!");
    ImGui::Separator();

    // 模拟开关
    if (ImGui::Checkbox("Simulating", &gIsSimulating)) {
        // 可以在这里处理状态切换的逻辑
    }

    // 重置按钮
    if (ImGui::Button("Reset Ball")) {
        resetSimulation();
    }

    ImGui::Spacing();
    ImGui::Text("Ball Settings:");

    // 调整球体半径
    if (ImGui::SliderFloat("Radius", &gBallRadius, 0.5f, 3.0f)) {
        // 动态修改物理形状和渲染网格 (简单粗暴的重置方式)
        gScene->removeActor(*gBall);
        gBall->release();
        createBall(gBallStartPos, gBallRadius);
    }

    ImGui::Text("Application Average: %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
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
    float s, t;                                     // texCoord
    float sectorStep = 2 * M_PI / sectorCount;
    float stackStep = M_PI / stackCount;
    float sectorAngle, stackAngle;

    for (int i = 0; i <= stackCount; ++i) {
        stackAngle = M_PI / 2 - i * stackStep;        // [pi/2, -pi/2]
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