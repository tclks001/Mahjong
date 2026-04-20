// 主程序
// 负责初始化和运行游戏UI

// 标准库头文件
#include <iostream>
#include <vector>

// 第三方库头文件
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

// 头文件
#include "graphics.h"
#include "physics.h"

using namespace glm;

// =========================================================
// 函数声明
// =========================================================
void initImGui();
void renderUI();
void cleanup();

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

    // ImGui::GetDrawData(): 获取ImGui渲染的数据
    // ImGui_ImplOpenGL3_RenderDrawData: 调用图形后端渲染数据
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
