#include "ui.h"

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
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}