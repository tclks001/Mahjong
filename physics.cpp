// 物理模块
// 负责物理计算
// 给主程序（即UI程序）提供设置接口和调用updatePhysics(dt)接口
// 给渲染程序提供获取粒子数组接口
// 直接对粒子使用数组结构体实现更加紧凑的内存布局
#include <unordered_set>
#include <algorithm>
#include "physics.h"
using namespace std;
using namespace glm;

vector<vector<int>> grid(indiceCount, {});
vector<vec3> push_force(particleCount);

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
vec3 computeEffectiveGravity(const float gMagnitude = 9.8f) {
    // 世界坐标系中重力方向: 沿 -Z 轴向下
    const vec3 worldGravity(0.0f, 0.0f, -gMagnitude);

    // 四元数的共轭 = 逆旋转, 将世界坐标向量变换到容器本地坐标系
    // v_local = q_conj * v_world * q   等价于   mat3(q_conj) * v_world
    vec3 localGravity = conjugate(gObjectOrientation) * worldGravity;

    return localGravity;
}


//// 辅助函数：计算空间哈希的网格坐标
//glm::ivec3 getGridCell(const glm::vec3& pos, float cellSize) {
//    return glm::ivec3(glm::floor(pos / cellSize));
//}
//
//// 辅助函数：将网格坐标转换为哈希键（64位整数）
//uint64_t hashGridCell(const glm::ivec3& cell) {
//    // 使用大质数混合各分量，避免冲突
//    return (uint64_t(cell.x) * 73856093) ^
//        (uint64_t(cell.y) * 19349663) ^
//        (uint64_t(cell.z) * 83492791);
//}

int hashGridCell(const glm::vec4& pos) {
    int x = glm::clamp((int)((pos.x + halfX) / cellSize), 0, widthIndice - 1);
    int y = glm::clamp((int)((pos.y + halfY) / cellSize), 0, depthIndice - 1);
    int z = glm::clamp((int)((pos.z + halfZ) / cellSize), 0, heightIndice - 1);
    return (x * depthIndice + y) * heightIndice + z;
}

vector<int> hashNeighbor(int cell) {
    vector<int> res{};
    // 反解xyz
    int z = cell % heightIndice;
    cell /= heightIndice;
    int y = cell % depthIndice;
    cell /= depthIndice;
    int x = cell;
    for (int nx = std::max(0, x - 1);nx <= std::min(widthIndice - 1, x + 1);++nx){
        for (int ny = std::max(0, y - 1);ny <= std::min(depthIndice - 1, y + 1);++ny) {
            for (int nz = std::max(0, z - 1);nz <= std::min(heightIndice - 1, z + 1);++nz) {
                res.push_back((nx * depthIndice + ny) * heightIndice + nz);
            }
        }
    }
    return res;
}

// 辅助函数：处理两个粒子之间的碰撞，通过位置微调使其分离到恰好相切
void resolveParticleCollision(const vec4& pos1, const vec4& pos2, vec3& force1, vec3& force2) {
    vec3 delta = pos1 - pos2;
    float dist = length(delta);
    if (dist < diameter && dist > 1e-6f) {
        float depth = diameter - dist;
        vec3 dir = delta / dist;
        vec3 correction = dir * (depth * 0.5f);
        force1 += correction;
        force2 -= correction;
    }
}

// 辅助函数：处理两个粒子之间的切向相对速度，模拟摩擦力
void resolveParticleFriction(glm::vec3& vel1, glm::vec3& vel2, glm::vec3 normal) {
    if (glm::length(normal) < 1e-6f) {
        normal = glm::sphericalRand(1.0f);
    }
    else {
        normal = glm::normalize(normal);
    }
    glm::vec3 norVel1 = normal * glm::dot(vel1, normal);
    glm::vec3 norVel2 = normal * glm::dot(vel2, normal);
    glm::vec3 tanVel1 = vel1 - norVel1;
    glm::vec3 tanVel2 = vel2 - norVel2;
    
    glm::vec3 tanDisV = tanVel1 - tanVel2;
    glm::vec3 tanAveV = (tanVel1 + tanVel2) * 0.5f;

    if (glm::length(tanDisV) <= min_tangent_dis_velocity) {
        vel1 = norVel1 + tanAveV;
        vel2 = norVel2 + tanAveV;
    }
    else {
        glm::vec3 friction_velocity = min_tangent_dis_velocity * tanDisV;
        vel1 -= friction_velocity;
        vel2 += friction_velocity;
    }
}

// 辅助函数：处理粒子与容器壁的碰撞，将粒子推回容器内，并施加容器壁的摩擦力
void resolveBoundaryCollision(vec4& pos, vec3&vel, vec3& force) {
    vec3 tanVel = vel;
    // X 轴边界
    if (pos.x > halfX - particleRadius) {
        pos.x = halfX - particleRadius;
        force += vec3(halfX - particleRadius - pos.x, 0.0f, 0.0f);
        tanVel.x = 0;
    }
    else if (pos.x < -halfX + particleRadius) {
        pos.x = -halfX + particleRadius;
        force += vec3(halfX - particleRadius + pos.x, 0.0f, 0.0f);
        tanVel.x = 0;
    }
    // Y 轴边界
    if (pos.y > halfY - particleRadius) {
        pos.y = halfY - particleRadius;
        force += vec3(0.0f, halfY - particleRadius - pos.y, 0.0f);
        tanVel.y = 0;
    }
    else if (pos.y < -halfY + particleRadius) {
        pos.y = -halfY + particleRadius;
        force += vec3(0.0f, halfY - particleRadius + pos.y, 0.0f);
        tanVel.y = 0;
    }
    // Z 轴边界
    if (pos.z > halfZ - particleRadius) {
        pos.z = halfZ - particleRadius;
        force += vec3(0.0f, 0.0f, halfZ - particleRadius - pos.z);
        tanVel.z = 0;
    }
    else if (pos.z < -halfZ + particleRadius) {
        pos.z = -halfZ + particleRadius;
        force += vec3(0.0f, 0.0f, halfZ - particleRadius + pos.z);
        tanVel.z = 0;
    }

    if (glm::length(tanVel) < min_tangent_dis_velocity) {
        vel -= tanVel;
    }
    else {
        vel -= min_tangent_dis_velocity * glm::normalize(tanVel);
    }
}

// 主函数：更新粒子系统的物理模拟
void updatePhysics(float dt) {
    // 保存这一帧开始时的初始位置（用于最后反推速度）
    // std::vector<glm::vec4> initialPositions = particlePositions;

    // 步骤1：获取等效重力方向
    glm::vec3 gravity = computeEffectiveGravity();

    // 步骤2：更新速度（只考虑重力），并限制最大速度
    for (int i = 0; i < particleCount; ++i) {
        particleVelocities[i] += gravity * dt;
        float speed = glm::length(particleVelocities[i]);
        if (speed > particleMaxSpeed) {
            particleVelocities[i] *= particleMaxSpeed / speed;
        }
    }

    // 步骤3：根据速度更新粒子位置（预测位置）
    for (int i = 0; i < particleCount; ++i) {
        particlePositions[i] += vec4(particleVelocities[i], 0.0f) * dt;
    }

    // 步骤4 & 5：PBD迭代，共5轮，每轮包含空间哈希、粒子间碰撞、边界碰撞处理
    const int iterations = 5;
    for (int iter = 0; iter < iterations; ++iter) {
        // ----- 构建空间哈希网格 -----
        //std::unordered_map<uint64_t, std::vector<int>> grid;
        for (int i = 0;i < indiceCount;++i) {
            grid[i].clear();
        }
        for (int i = 0; i < particleCount; ++i) {
            //glm::vec3 pos = glm::vec3(particlePositions[i]);
            //glm::ivec3 cell = getGridCell(pos, cellSize);
            //uint64_t key = hashGridCell(cell);
            int key = hashGridCell(particlePositions[i]);
            grid[key].push_back(i);
        }

        //// ----- 粒子与容器壁碰撞处理 -----
        //for (int i = 0; i < particleCount; ++i) {
        //    vec4& pos = particlePositions[i];
        //    vec3& vel = particleVelocities[i];
        //    vec3& force = push_force[i];
        //    resolveBoundaryCollision(pos, vel, force);
        //    //particlePositions[i] = pos;
        //    //particleVelocities[i] = vel;
        //}

        // 用于标记已处理过的粒子对（避免重复），由于我们按 j<i 顺序处理，不需要额外标记
        for (int i = 0; i < particleCount; ++i) {
            push_force[i] = vec4(0.0f, 0.0f, 0.0f, 0.0f);
        }
        // ----- 粒子间碰撞处理（只处理 j<i 的对）-----
        for (int i = 0; i < particleCount; ++i) {
            vec4& pos_i = particlePositions[i];
            vec3& vel_i = particleVelocities[i];
            vec3& force_i = push_force[i];
            //glm::ivec3 cell_i = getGridCell(pos_i, cellSize);
            int key_i = hashGridCell(particlePositions[i]);
            // 收集相邻网格单元（包括自身）中的所有粒子索引
            //std::unordered_set<int> neighborSet;
            for (auto ncell : hashNeighbor(key_i)) {
                //auto it = grid.find(ncell);
                //if (it != grid.end()) {
                    for (int j : grid[ncell]) {
                        //if (j != i) neighborSet.insert(j);
                        if (j < i) {
                            vec4& pos_j = particlePositions[j];
                            vec3& vel_j = particleVelocities[j];
                            vec3& force_j = push_force[j];
                            resolveParticleCollision(pos_i, pos_j, force_i, force_j);
                            resolveParticleFriction(vel_i, vel_j, pos_i - pos_j);
                        }
                    }
                //}
            }
            resolveBoundaryCollision(pos_i, vel_i, force_i);

            //for (int dx = -1; dx <= 1; ++dx) {
            //    for (int dy = -1; dy <= 1; ++dy) {
            //        for (int dz = -1; dz <= 1; ++dz) {
            //            glm::ivec3 neighborCell = cell_i + glm::ivec3(dx, dy, dz);
            //            uint64_t key = hashGridCell(neighborCell);
            //            auto it = grid.find(key);
            //            if (it != grid.end()) {
            //                for (int j : it->second) {
            //                    if (j != i) neighborSet.insert(j);
            //                }
            //            }
            //        }
            //    }
            //}

            // 与邻居中的每个粒子 j > i 进行碰撞解析
            //for (int j : neighborSet) {
            //    if (j > i) {  // 确保每对只处理一次
            //        glm::vec4& pos_j = particlePositions[j];
            //        glm::vec3& vel_j = particleVelocities[j];
            //        resolveParticleCollision(pos_i, pos_j, particleRadius);
            //        resolveParticleFriction(vel_i, vel_j, pos_i - pos_j);
            //        //particlePositions[i] = pos_i, 1.0f;
            //        //particlePositions[j] = pos_j, 1.0f;
            //        //particleVelocities[i] = vel_i;
            //        //particleVelocities[j] = vel_j;
            //    }
            //}
        }
        for (int i = 0; i < particleCount; ++i) {
            particlePositions[i] += vec4(push_force[i], 0.0f);
        }

        //// ----- 粒子与容器壁碰撞处理 -----
        //for (int i = 0; i < particleCount; ++i) {
        //    glm::vec4& pos = particlePositions[i];
        //    glm::vec3& vel = particleVelocities[i];
        //    resolveBoundaryCollision(pos, vel);
        //    //particlePositions[i] = pos;
        //    //particleVelocities[i] = vel;
        //}


    }

    //// 步骤6：根据最终位置与初始位置的变化反推速度，并更新 particleVelocities
    //for (int i = 0; i < numParticles; ++i) {
    //    glm::vec3 finalPos = glm::vec3(particlePositions[i]);
    //    glm::vec3 initialPos = initialPositions[i];
    //    particleVelocities[i] = (finalPos - initialPos) / dt;
    //    // 可选：如果需要限制速度，可在此处添加，但题目未要求
    //}
}