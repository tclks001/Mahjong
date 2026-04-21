// 物理模块
// 负责物理计算
// 给主程序（即UI程序）提供设置接口和调用updatePhysics(dt)接口
// 给渲染程序提供获取粒子数组接口
// 直接对粒子使用数组结构体实现更加紧凑的内存布局
#include <unordered_set>
#include "physics.h"
using namespace std;
using namespace glm;

void initPhysX() {
	particlePositions.resize(particleCount);         // 粒子位置
	particleColors.resize(particleCount);            // 预设颜色，初始化后不修改
	particleVelocities.resize(particleCount);        // 同时也是粒子强度位置在灰度图上的移动速度标量
	particleColorMapPositions.resize(particleCount); // 粒子颜色强度映射在柏林噪声灰度图的位置
	particleForces.resize(particleCount);            // 粒子受力
	// 均匀地把粒子初始化到容器底部
	// 长6cm，宽3.6cm，厚0.2cm
	// 粒子半径0.01cm，初始放置间隔0.04cm
	float d = 0.04f;
	// 从下往上放
	int i = 0;
	for (float z = -containerHeight / 2.0f + d; z <= containerHeight / 2.0f - d && i < particleCount; z += d) {
		for (float x = -containerWidth / 2.0f + d; x <= containerWidth / 2.0f - d && i < particleCount; x += d) {
			for (float y = -containerDepth / 2.0f + d; y <= containerDepth / 2.0f - d && i < particleCount; y += d) {
				particlePositions[i] = vec4(x, y, z ,1.0f);
				i++;
			}
		}
	}
}

// =========================================================
// 计算等效重力
// 利用姿态四元数将世界重力 (0, 0, -g) 旋转到容器本地坐标系
// 返回: vec3 方向=等效重力方向, 模长=重力大小
// =========================================================
vec3 computeEffectiveGravity(const float gMagnitude) {
    // 世界坐标系中重力方向: 沿 -Z 轴向下
    const vec3 worldGravity(0.0f, 0.0f, -gMagnitude);

    // 四元数的共轭 = 逆旋转, 将世界坐标向量变换到容器本地坐标系
    // v_local = q_conj * v_world * q   等价于   mat3(q_conj) * v_world
    vec3 localGravity = conjugate(gObjectOrientation) * worldGravity;

    return localGravity;
}


// 辅助函数：计算空间哈希的网格坐标
glm::ivec3 getGridCell(const glm::vec3& pos, float cellSize) {
    return glm::ivec3(glm::floor(pos / cellSize));
}

// 辅助函数：将网格坐标转换为哈希键（64位整数）
uint64_t hashGridCell(const glm::ivec3& cell) {
    // 使用大质数混合各分量，避免冲突
    return (uint64_t(cell.x) * 73856093) ^
        (uint64_t(cell.y) * 19349663) ^
        (uint64_t(cell.z) * 83492791);
}

// 辅助函数：处理两个粒子之间的碰撞，通过位置微调使其分离到恰好相切
void resolveParticleCollision(glm::vec3& pos1, glm::vec3& pos2, float radius) {
    glm::vec3 delta = pos1 - pos2;
    float dist = glm::length(delta);
    float minDist = 2.0f * radius;
    if (dist < minDist && dist > 1e-6f) {
        float depth = minDist - dist;
        glm::vec3 dir = delta / dist;
        glm::vec3 correction = dir * (depth * 0.5f);
        pos1 += correction;
        pos2 -= correction;
    }
}

// 辅助函数：处理粒子与容器壁的碰撞，将粒子推回容器内
void resolveBoundaryCollision(glm::vec3& pos, float radius,
    float halfX, float halfY, float halfZ) {
    // X 轴边界
    if (pos.x > halfX - radius) pos.x = halfX - radius;
    else if (pos.x < -halfX + radius) pos.x = -halfX + radius;
    // Y 轴边界
    if (pos.y > halfY - radius) pos.y = halfY - radius;
    else if (pos.y < -halfY + radius) pos.y = -halfY + radius;
    // Z 轴边界
    if (pos.z > halfZ - radius) pos.z = halfZ - radius;
    else if (pos.z < -halfZ + radius) pos.z = -halfZ + radius;
}

// 主函数：更新粒子系统的物理模拟
void updatePhysics(float dt) {
    // 常量定义
    const float diameter = 2.0f * particleRadius;
    const float cellSize = 2.0f * diameter;          // 网格单元大小为直径的2倍
    const float halfX = containerWidth * 0.5f;
    const float halfY = containerDepth * 0.5f;
    const float halfZ = containerHeight * 0.5f;
    const int numParticles = particlePositions.size();

    // 保存这一帧开始时的初始位置（用于最后反推速度）
    std::vector<glm::vec3> initialPositions(numParticles);
    for (int i = 0; i < numParticles; ++i) {
        initialPositions[i] = glm::vec3(particlePositions[i]);
    }

    // 步骤1：获取等效重力方向
    glm::vec3 gravity = computeEffectiveGravity();

    // 步骤2：更新速度（只考虑重力），并限制最大速度
    for (int i = 0; i < numParticles; ++i) {
        particleVelocities[i] += gravity * dt;
        float speed = glm::length(particleVelocities[i]);
        if (speed > particleMaxSpeed) {
            particleVelocities[i] *= particleMaxSpeed / speed;
        }
    }

    // 步骤3：根据速度更新粒子位置（预测位置）
    for (int i = 0; i < numParticles; ++i) {
        glm::vec3 pos = glm::vec3(particlePositions[i]);
        pos += particleVelocities[i] * dt;
        particlePositions[i] = glm::vec4(pos, 0.0f);
    }

    // 步骤4 & 5：PBD迭代，共5轮，每轮包含空间哈希、粒子间碰撞、边界碰撞处理
    const int iterations = 5;
    for (int iter = 0; iter < iterations; ++iter) {
        // ----- 构建空间哈希网格 -----
        std::unordered_map<uint64_t, std::vector<int>> grid;
        for (int i = 0; i < numParticles; ++i) {
            glm::vec3 pos = glm::vec3(particlePositions[i]);
            glm::ivec3 cell = getGridCell(pos, cellSize);
            uint64_t key = hashGridCell(cell);
            grid[key].push_back(i);
        }

        // 用于标记已处理过的粒子对（避免重复），由于我们按 i<j 顺序处理，不需要额外标记

        // ----- 粒子间碰撞处理（只处理 i<j 的对）-----
        for (int i = 0; i < numParticles; ++i) {
            glm::vec3 pos_i = glm::vec3(particlePositions[i]);
            glm::ivec3 cell_i = getGridCell(pos_i, cellSize);

            // 收集相邻网格单元（包括自身）中的所有粒子索引
            std::unordered_set<int> neighborSet;
            for (int dx = -1; dx <= 1; ++dx) {
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dz = -1; dz <= 1; ++dz) {
                        glm::ivec3 neighborCell = cell_i + glm::ivec3(dx, dy, dz);
                        uint64_t key = hashGridCell(neighborCell);
                        auto it = grid.find(key);
                        if (it != grid.end()) {
                            for (int j : it->second) {
                                if (j != i) neighborSet.insert(j);
                            }
                        }
                    }
                }
            }

            // 与邻居中的每个粒子 j > i 进行碰撞解析
            for (int j : neighborSet) {
                if (j > i) {  // 确保每对只处理一次
                    glm::vec3 pos_j = glm::vec3(particlePositions[j]);
                    resolveParticleCollision(pos_i, pos_j, particleRadius);
                    particlePositions[i] = glm::vec4(pos_i, 0.0f);
                    particlePositions[j] = glm::vec4(pos_j, 0.0f);
                }
            }
        }

        // ----- 粒子与容器壁碰撞处理 -----
        for (int i = 0; i < numParticles; ++i) {
            glm::vec3 pos = glm::vec3(particlePositions[i]);
            resolveBoundaryCollision(pos, particleRadius, halfX, halfY, halfZ);
            particlePositions[i] = glm::vec4(pos, 0.0f);
        }
    }

    // 步骤6：根据最终位置与初始位置的变化反推速度，并更新 particleVelocities
    for (int i = 0; i < numParticles; ++i) {
        glm::vec3 finalPos = glm::vec3(particlePositions[i]);
        glm::vec3 initialPos = initialPositions[i];
        particleVelocities[i] = (finalPos - initialPos) / dt;
        // 可选：如果需要限制速度，可在此处添加，但题目未要求
    }
}