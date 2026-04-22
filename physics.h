#pragma once
#include <vector>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/vector_angle.hpp>
#include <glm/gtc/random.hpp>

// Position和Color统一用vec4便于SSBO按16字节对齐
inline std::vector<glm::vec4> particlePositions;         // 粒子位置
inline std::vector<glm::vec4> particleColors;            // 预设颜色，初始化后不修改
inline std::vector<glm::vec3> particleVelocities;        // 同时也是粒子强度位置在灰度图上的移动速度标量
inline std::vector<glm::vec2> particleColorMapPositions; // 粒子颜色强度映射在柏林噪声灰度图的位置
inline std::vector<glm::vec3> particleForces;            // 粒子受力

inline bool isSimulating = true;
inline float physicsTimeStep = 1.0f / 60.0f;

// 全局物体姿态四元数 (初始为单位四元数，无旋转)
inline glm::quat gObjectOrientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

// 旋转控制器交互状态
//inline float gPitch = 0.0f;   // 俯仰角 (绕X轴)
//inline float gYaw   = 0.0f;   // 偏航角 (绕Y轴)
//inline float gRoll  = 0.0f;   // 滚转角 (绕Z轴)
constexpr inline float particleMaxSpeed = 2.4f;
constexpr inline int particleCount = 500;

constexpr inline float containerWidth = 3.6f;
constexpr inline float containerHeight = 6.0f;
constexpr inline float containerDepth = 0.2f;

constexpr inline float particleRadius = 0.01f;

constexpr inline float min_tangent_dis_velocity = 0.0005f;

constexpr float diameter = 2.0f * particleRadius;
constexpr float cellSize = 2.0f * diameter;          // 网格单元大小为直径的2倍

constexpr int widthIndice = containerWidth / cellSize; // x
constexpr int heightIndice = containerHeight / cellSize; // z
constexpr int depthIndice = containerDepth / cellSize; // y

constexpr int indiceCount = widthIndice * heightIndice * depthIndice;

const float halfX = containerWidth * 0.5f;
const float halfY = containerDepth * 0.5f;
const float halfZ = containerHeight * 0.5f;
//const int numParticles = particleCount;


void initPhysX();
void updatePhysics(float timeStep);
//glm::vec3 computeEffectiveGravity(const float gMagnitude = 9.8f);
//void updatePhysics(float dt);
