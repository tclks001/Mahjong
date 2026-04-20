#include <iostream>
#include <vector>

// 头文件
#include "graphics.h"
#include "physics.h"
#include "ui.h"

using namespace glm;

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



