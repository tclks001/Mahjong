// 主程序
// 负责初始化和运行游戏UI

// 标准库头文件
#include <iostream>
#include <vector>

// 第三方库头文件
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

// 头文件
#include "graphics.h"
#include "physics.h"

using namespace glm;

constexpr float M_PI = 3.1415926f;

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

    gShaderProgram = compileShaders();
    glUseProgram(gShaderProgram);

    // 设置透视投影矩阵
    gProjMatrix = perspective(radians(45.0f), (float)WINDOW_WIDTH / WINDOW_HEIGHT, 0.1f, 100.0f);
    // 设置观察矩阵 (摄像机: y=10 朝向-Y观察, up=+Z — 与 render() 一致)
    gViewMatrix = lookAt(vec3(0.0f, 10.0f, 0.01f), vec3(0.0f, 0.0f, 0.0f), vec3(0.0f, 0.0f, 1.0f));

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
        if (isSimulating) {
            updatePhysics(physicsTimeStep);
        }

        // 3. 清除屏幕缓冲区
        glClearColor(0.2f, 0.25f, 0.3f, 1.0f); // 舒服的深蓝灰色背景
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // 4. 渲染 3D 物体
        render();

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

// ---------- 旋转控制器内部状态 ----------
static int  rotDragState = 0;       // 0=无拖动, 1=区域内(俯仰/偏航), 2=边缘旋钮(滚转)
static ImVec2 rotDragLastMouse;    // 上一帧鼠标位置 (用于增量计算)

void renderUI() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // =========================================================
    // 圆形3D旋转交互区域 (叠加在渲染视窗中央, 背景完全透明)
    // 使用前景 DrawList 确保绘制在最顶层
    //
    // 核心设计: 纯增量四元数操作, 无欧拉角边界限制, 环形连续旋转
    //   区域内拖动: 增量Yaw(世界Y轴) + 增量Pitch(局部X轴), Roll不变
    //   边缘旋钮: 增量Roll(局部Z轴), Yaw/Pitch不变
    // =========================================================
    {
        const float   circleRadius = 120.0f;
        const float   knobRadius = 16.0f;
        const float   knobTrack = circleRadius + knobRadius * 0.6f;
        const ImVec2  gizmoSize(circleRadius * 2.0f + 30.0f, circleRadius * 2.0f + 30.0f);
        const ImVec2  viewportSize((float)WINDOW_WIDTH, (float)WINDOW_HEIGHT);
        const ImVec2  gizmoCenter((viewportSize.x - gizmoSize.x) * 0.5f + gizmoSize.x * 0.5f,
            (viewportSize.y - gizmoSize.y) * 0.5f + gizmoSize.y * 0.5f);

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(viewportSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
        ImGui::Begin("Rotation Gizmo Overlay", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoFocusOnAppearing);

        ImDrawList* dl = ImGui::GetForegroundDrawList();

        // --- 绘制外圈轨道 ---
        dl->AddCircle(gizmoCenter, knobTrack, IM_COL32(80, 80, 90, 255), 64, 2.5f);

        // --- 主圆形区域 ---
        dl->AddCircle(gizmoCenter, circleRadius, IM_COL32(180, 200, 255, 255), 48, 2.8f);

        //// --- 十字参考线 ---
        //dl->AddLine(gizmoCenter - ImVec2(circleRadius - 8, 0), gizmoCenter + ImVec2(circleRadius - 8, 0),
        //    IM_COL32(100, 110, 130, 150), 1.2f);
        //dl->AddLine(gizmoCenter - ImVec2(0, circleRadius - 8), gizmoCenter + ImVec2(0, circleRadius - 8),
        //    IM_COL32(100, 110, 130, 150), 1.2f);

        //// --- 方向指示点: 从当前四元数提取前方向向量并投影到2D ---
        //{
        //    // 容器本地"前"方向 (+Z轴) 经四元数变换后的世界方向
        //    vec3 localForward = gObjectOrientation * vec3(0.0f, 0.0f, 1.0f);
        //    // 投影到 XY 平面用于2D显示
        //    float nx = localForward.x;
        //    float ny = localForward.y;
        //    ImVec2 dirPt = gizmoCenter + ImVec2(nx * (circleRadius * 0.55f), -ny * (circleRadius * 0.55f));
        //    dl->AddCircleFilled(dirPt, 7.0f, IM_COL32(100, 220, 120, 255), 16);
        //    dl->AddCircle(dirPt, 7.0f, IM_COL32(255, 255, 255, 200), 12, 1.5f);
        //}

        // --- 滚转旋钮位置: 从四元数提取局部Z轴滚转角 ---
        {
            // 取容器本地 +X 轴经四元数变换后的方向
            vec3 localRight = gObjectOrientation * vec3(1.0f, 0.0f, 0.0f);
            // 投影到XY平面的角度即为视觉上的滚转角
            float knobAngleRad = atan2(localRight.y, localRight.x);
            ImVec2 knobPos = gizmoCenter + ImVec2(cos(knobAngleRad) * knobTrack,
                sin(knobAngleRad) * knobTrack);

            //// --- 滚转旋钮 ---
            //ImU32 knobColor = (rotDragState == 2)
            //    ? IM_COL32(255, 210, 60, 255)
            //    : IM_COL32(70, 160, 255, 255);
            //dl->AddCircleFilled(knobPos, knobRadius, knobColor, 24);
            //dl->AddCircle(knobPos, knobRadius, IM_COL32(255, 255, 255, 220), 24, 2.0f);

            //// --- 旋钮内刻线 ---
            //float innerR = knobRadius * 0.55f;
            //dl->AddLine(knobPos + ImVec2(-cos(knobAngleRad) * innerR, -sin(knobAngleRad) * innerR),
            //    knobPos + ImVec2(cos(knobAngleRad) * innerR, sin(knobAngleRad) * innerR),
            //    IM_COL32(255, 255, 255, 230), 2.2f);

            // --- 不可见按钮捕获事件 ---
            ImGui::SetCursorScreenPos(ImVec2(
                (viewportSize.x - gizmoSize.x) * 0.5f,
                (viewportSize.y - gizmoSize.y) * 0.5f));
            ImGui::InvisibleButton("##rotation_gizmo", gizmoSize);

            ImGuiIO& io = ImGui::GetIO();
            ImVec2 mousePos = io.MousePos;

            // 判断鼠标悬停区域
            bool isHoveringInside = false;
            bool isHoveringKnob = false;
            {
                float dx = mousePos.x - gizmoCenter.x;
                float dy = mousePos.y - gizmoCenter.y;
                float distCenter = sqrtf(dx * dx + dy * dy);
                isHoveringInside = (distCenter <= circleRadius);
                isHoveringKnob = (distCenter <= knobTrack + knobRadius && distCenter >= knobTrack - knobRadius);
            }

            bool isActive = ImGui::IsItemActive();
            bool justPressed = ImGui::IsItemActivated();

            // 开始拖拽
            if (justPressed && !rotDragState) {
                if (isHoveringKnob) {
                    rotDragState = 2;
                    // 记录起始极角（用于外圈增量）
                    rotDragLastMouse = mousePos;
                }
                else if (isHoveringInside) {
                    rotDragState = 1;
                    rotDragLastMouse = mousePos;
                }
            }

            // 拖拽中：纯增量更新四元数, 无边界限制
            if (isActive && rotDragState != 0) {
                ImVec2 delta(mousePos.x - rotDragLastMouse.x,
                    mousePos.y - rotDragLastMouse.y);

                if (rotDragState == 1) {
                    constexpr float sensitivity = 0.008f;

                    // 横向 → Roll: 绕世界Z轴
                    float deltaRoll = delta.x * sensitivity;
                    glm::quat qRoll = glm::angleAxis(deltaRoll, vec3(0.0f, 0.0f, 1.0f));

                    // 纵向 → Pitch: 绕世界X轴
                    float deltaPitch = -delta.y * sensitivity;  // 上 = 抬头
                    glm::quat qPitch = glm::angleAxis(deltaPitch, vec3(1.0f, 0.0f, 0.0f));

                    // 全部左乘：世界空间旋转
                    gObjectOrientation = normalize(qRoll * qPitch * gObjectOrientation);
                }
                else if (rotDragState == 2) {
                    float prevAngle = atan2f(rotDragLastMouse.y - gizmoCenter.y,
                        rotDragLastMouse.x - gizmoCenter.x);
                    float curAngle = atan2f(mousePos.y - gizmoCenter.y,
                        mousePos.x - gizmoCenter.x);
                    float deltaAngle = curAngle - prevAngle;

                    // 归一化到 [-π, π]
                    while (deltaAngle > M_PI) deltaAngle -= 2.0f * M_PI;
                    while (deltaAngle < -M_PI) deltaAngle += 2.0f * M_PI;

                    // 绕世界Y轴旋转（左乘）
                    glm::quat qYaw = glm::angleAxis(-deltaAngle, vec3(0.0f, 1.0f, 0.0f));
                    gObjectOrientation = normalize(qYaw * gObjectOrientation);
                }

                // 更新帧间参考点 (连续增量模式, 无累积误差)
                rotDragLastMouse = mousePos;
            }

            // 结束拖拽
            if (!isActive && rotDragState) {
                rotDragState = 0;
            }

            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
            ImGui::End();
        }

        // =========================================================
        // 控制面板窗口
        // =========================================================
        ImGui::Begin("Physics Controls");

        ImGui::Text("Welcome to the PhysX 5 Sandbox!");
        ImGui::Separator();

        if (ImGui::Checkbox("Simulating", &isSimulating)) {}

        if (ImGui::Button("Reset")) {
            gObjectOrientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            rotDragState = 0;
            //resetSimulation();
        }

        ImGui::Spacing();

        // --- 显示姿态信息 (从四元数提取欧拉角仅用于UI展示, 不参与计算) ---
        {
            vec3 euler = eulerAngles(gObjectOrientation); // 返回 [X(pitch), Y(yaw), Z(roll)]
            float dp = euler.x, dy = euler.y, dr = euler.z;
            while (dy > M_PI) dy -= 2.0f * M_PI;
            while (dy < -M_PI) dy += 2.0f * M_PI;
            while (dr > M_PI) dr -= 2.0f * M_PI;
            while (dr < -M_PI) dr += 2.0f * M_PI;

            ImGui::Text("Orientation (quat):");
            ImGui::Text("  w=%.3f x=%.3f y=%.3f z=%.3f",
                gObjectOrientation.w, gObjectOrientation.x,
                gObjectOrientation.y, gObjectOrientation.z);
            ImGui::Text("Euler (deg): P=%.1f  Y=%.1f  R=%.1f",
                degrees(dp), degrees(dy), degrees(dr));
        }

        ImGui::Spacing();
        ImGui::Text("Application Average: %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

        ImGui::End();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
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


    glfwDestroyWindow(gWindow);
    glfwTerminate();
}
