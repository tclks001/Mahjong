#pragma once
#include <PxPhysicsAPI.h>
#include <iostream>
#include "graphics.h"

using namespace physx;

// 物理引擎核心组件
inline PxPhysics* gPhysics = nullptr;
inline PxDefaultAllocator gAllocator;
inline PxDefaultErrorCallback gErrorCallback;
inline PxFoundation* gFoundation = nullptr;
inline PxScene* gScene = nullptr;
inline PxMaterial* gMaterial = nullptr;

// PVD (PhysX Visual Debugger) 连接管理器
inline PxPvd* gPvd = nullptr;

// 物理刚体：平面（地面）和球体
inline PxRigidStatic* gGroundPlane = nullptr;
inline PxRigidDynamic* gBall = nullptr;

// ImGui 交互控制变量
inline bool gIsSimulating = true;
inline float gBallRadius = 1.0f;
inline glm::vec3 gBallStartPos = glm::vec3(0.0f, 10.0f, 0.0f); // 球体初始高度
inline float gPhysicsTimeStep = 1.0f / 60.0f; // 固定时间步长


// 平面和球体的变换矩阵 (用于渲染)
inline glm::mat4 gGroundModelMatrix = glm::mat4(1.0f);
inline glm::mat4 gBallModelMatrix = glm::mat4(1.0f);


void initPhysX();
void createGroundPlane();
void createBall(const glm::vec3& position, float radius);
void resetSimulation();
void updatePhysics(float dt);
