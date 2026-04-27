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

const float M_PI = std::acos(-1);
const float H = diameter * 1.2f;
const float REST_DENSITY = 1000.0F;
const float MASS = 1.0f;

vector<float> density(particleCount);
vector<float> lambda(particleCount);
vector<vec4> initialPositions(particleCount);

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

//// 辅助函数：处理粒子与容器壁的碰撞，将粒子推回容器内，并施加容器壁的摩擦力
//void resolveBoundaryCollision(vec4& pos, vec3&vel) {
//    //vec3 tanVel = vel;
//    // X 轴边界
//    if (pos.x > halfX - particleRadius) {
//        pos.x = halfX - particleRadius;
//        //vel.x = std::min(vel.x, 0.0f);
//        //tanVel.x = 0;
//    }
//    else if (pos.x < -halfX + particleRadius) {
//        pos.x = -halfX + particleRadius;
//        //vel.x = std::max(vel.x, 0.0f);
//        //tanVel.x = 0;
//    }
//    // Y 轴边界
//    if (pos.y > halfY - particleRadius) {
//        pos.y = halfY - particleRadius;
//        //vel.y = std::min(vel.y, 0.0f);
//        //tanVel.y = 0;
//    }
//    else if (pos.y < -halfY + particleRadius) {
//        pos.y = -halfY + particleRadius;
//        //vel.y = std::max(vel.y, 0.0f);
//        //tanVel.y = 0;
//    }
//    // Z 轴边界
//    if (pos.z > halfZ - particleRadius) {
//        pos.z = halfZ - particleRadius;
//        //vel.z = std::min(vel.z, 0.0f);
//        //tanVel.z = 0;
//    }
//    else if (pos.z < -halfZ + particleRadius) {
//        pos.z = -halfZ + particleRadius;
//        //vel.z = std::max(vel.z, 0.0f);
//        //tanVel.z = 0;
//    }
//
//    //if (glm::length(tanVel) < min_tangent_dis_velocity) {
//    //    vel -= tanVel;
//    //}
//    //else {
//    //    vel -= min_tangent_dis_velocity * glm::normalize(tanVel);
//    //}
//}
//
float W_poly6(float r, float h) {
    if (r >= h) {
        return 0.0f;
    }
    float diff = h * h - r * r;
    return 315.0f / (64.0f * M_PI * pow(h, 9.0f) * diff * diff * diff);
}
//
//float computeDensity(int i) {
//    float density = 0.0f;
//    int key_i = hashGridCell(particlePositions[i]);
//
//    for (auto ncell : hashNeighbor(key_i)) {
//        for (int j : grid[ncell]) {
//            float dist = distance(particlePositions[i], particlePositions[j]);
//            density += MASS * W_poly6(dist, H);
//        }
//    }
//
//    return density;
//}
//
vec3 grad_W_spiky(vec3 dir, float r, float h) {
    if (r >= h || r < 1e-6f) {
        return vec3(0.0f);
    }
    float coeff = -45.0f / (M_PI * pow(h, 6.0f));
    float diff = h - r;
    return coeff * diff * diff * (dir / r);
}
//
//float computeLambda(int i, float density_i) {
//    float C_i = density_i / REST_DENSITY - 1.0f;
//
//    float grad_sum = 0.0f;
//    int key_i = hashGridCell(particlePositions[i]);
//
//    for (auto ncell : hashNeighbor(key_i)) {
//        for (int j : grid[ncell]) {
//            vec3 dir = particlePositions[i] - particlePositions[j];
//            float r = length(dir);
//            vec3 grad = grad_W_spiky(dir, r, H);
//            grad_sum += dot(grad, grad);
//        }
//    }
//    return -C_i / (grad_sum + 1e-4f);
//}
//
//vec3 computeDeltaP(int i, float lambda_i) {
//    vec3 delta_p = vec3(0.0f);
//    int key_i = hashGridCell(particlePositions[i]);
//
//    for (auto ncell : hashNeighbor(key_i)) {
//        for (int j : grid[ncell]) {
//            if (i == j) {
//                continue;
//            }
//            vec3 dir = particlePositions[i] - particlePositions[j];
//            float r = length(dir);
//
//            float lambda_j = lambda[j];
//            vec3 grad = grad_W_spiky(dir, r, H);
//
//            delta_p += (lambda_i + lambda_j) * grad;
//        }
//    }
//    return delta_p / REST_DENSITY;
//}

// ==================== 新增：解析边界处理函数 ====================

// 计算粒子到边界平面的距离（带符号）
float distanceToBoundary(const vec3& pos, const vec3& normal, float boundaryPos) {
    // normal 是边界法向（指向流体内部），boundaryPos 是边界位置
    // 返回正距离表示粒子在边界内，负距离表示在边界外
    return (boundaryPos - dot(pos, normal)) * sign(dot(normal, vec3(1, 0, 0))); // 简化处理
}

// ==================== 解析边界处理函数 (修正版) ====================
//
// 核心修正:
//   1. 边界距离计算: 从边界平面到粒子的距离 (正=内部, 负=外部)
//   2. 密度贡献: 使用正确的 Poly6 一维积分公式
//   3. 梯度贡献: 使用正确的 Spiky 一维积分公式
//   4. 最终硬边界裁剪: PBD后强制粒子留在容器内
//
// 物理假设:
//   - 边界由虚粒子构成, 虚粒子密度 = REST_DENSITY
//   - 虚粒子沿边界法向均匀分布, 距边界距离为 d
//   - 一维积分沿法向方向, 从边界面向内积分到核半径 h

// Poly6 核: W(r,h) = 315/(64πh^9) * (h²-r²)³
// 一维积分(沿法向, 从d到h): I_ρ(d) = ∫_d^h W(s,h) ds
// 解析解: I_ρ(d) = 315/(64πh^9) * [ (h²-d²)³*(3d²+2h²) ] / (15d)  (d>0)
// 简化数值稳定版本: 使用高斯-勒让德数值积分
float boundaryDensityContribution(float d, float h) {
    if (d >= h || d < 0.0f) return 0.0f;

    // 使用 4 点高斯-勒让德数值积分 (沿法向从 d 到 h)
    // 变换: s = (h-d)/2 * t + (h+d)/2,  t∈[-1,1]
    const float w[4] = {0.3478548451f, 0.6521451549f, 0.6521451549f, 0.3478548451f};
    const float t[4] = {-0.8611363116f, -0.3399810436f, 0.3399810436f, 0.8611363116f};

    float sum = 0.0f;
    float halfRange = (h - d) * 0.5f;
    float mid = (h + d) * 0.5f;

    for (int k = 0; k < 4; ++k) {
        float s = halfRange * t[k] + mid;
        float r2 = s * s;  // 沿法向, r = s
        float h2 = h * h;
        float diff = h2 - r2;
        float w_poly6 = (315.0f / (64.0f * M_PI * pow(h, 9.0f))) * diff * diff * diff;
        sum += w[k] * w_poly6;
    }

    return sum * halfRange;
}

// Spiky 核梯度: ∇W(r,h) = -45/(πh^6) * (h-r)² * (r/|r|)
// 一维积分(沿法向, 从d到h): I_∇ρ(d) = ∫_d^h |∇W(s,h)| ds
// 解析解: I_∇ρ(d) = 45/(πh^6) * [ (h-d)³/3 ]
float boundaryGradientContribution(float d, float h) {
    if (d >= h || d < 0.0f) return 0.0f;

    float hd = h - d;
    return (45.0f / (M_PI * pow(h, 6.0f))) * (hd * hd * hd / 3.0f);
}

// 处理所有边界对粒子的贡献
void applyBoundaryContributions(int i, float& density, float& grad_sum, vec3& delta_p, float lambda_i) {
    vec3 pos = particlePositions[i];
    const float h = H;

    // 六个边界平面 (法向指向流体外部, 用于计算到边界的距离)
    struct Boundary {
        vec3 normal;    // 法向 (指向外部)
        float d0;       // 边界位置 (平面方程: dot(normal, p) = d0)
    };

    Boundary boundaries[6] = {
        {vec3( 1, 0, 0),  halfX},   // +X 面
        {vec3(-1, 0, 0), -halfX},   // -X 面
        {vec3( 0, 1, 0),  halfY},   // +Y 面
        {vec3( 0,-1, 0), -halfY},   // -Y 面
        {vec3( 0, 0, 1),  halfZ},   // +Z 面
        {vec3( 0, 0,-1), -halfZ}    // -Z 面
    };

    for (int b = 0; b < 6; ++b) {
        const Boundary& bound = boundaries[b];

        // 粒子到边界平面的有符号距离
        // 正 = 在内部 (dot(normal,p) < d0 对于指向外部的法向)
        float signedDist = bound.d0 - dot(bound.normal, pos);

        // 只处理在核函数范围内的边界 (粒子靠近该边界)
        if (signedDist > 0.0f && signedDist < h) {
            // 1. 密度贡献: 虚粒子密度 = REST_DENSITY
            float bd = boundaryDensityContribution(signedDist, h);
            density += REST_DENSITY * bd;

            // 2. 梯度贡献: 用于 lambda 计算的分母
            // 边界梯度大小 = |∇W_boundary| = REST_DENSITY * I_∇ρ(d)
            float bg = boundaryGradientContribution(signedDist, h);
            float gradMag = REST_DENSITY * bg; //
            grad_sum += gradMag * gradMag;

            // 3. 位置修正: 边界将粒子推回内部
            // 修正方向 = 边界法向的反方向 (指向内部)
            vec3 inwardDir = -bound.normal;
            // PBD 约束: delta_p += (lambda_i / REST_DENSITY) * grad_C
            // 这里 grad_C 近似为边界梯度方向 * 大小
            delta_p += (lambda_i * gradMag) * inwardDir;
        }
    }
}

// 硬边界处理: 强制粒子留在容器内, 并修正速度防止下一帧再次穿透
// 核心逻辑:
// 硬边界处理: 强制粒子留在容器内, 并将法向速度置零(沿边界滑动)
// 核心逻辑:
//   1. 位置钳制: 将越界粒子推回边界内 (留 particleRadius 余量)
//   2. 法向速度归零: 粒子到达边界时将该方向速度置零, 保留切向分量(滑动)
void clampParticleToBoundary(int i) {
    float& x = particlePositions[i].x;
    float& y = particlePositions[i].y;
    float& z = particlePositions[i].z;
    vec3& vel = particleVelocities[i];
    float margin = particleRadius;

    // X 轴边界
    if (x > halfX - margin) {
        x = halfX - margin;
        vel.x = 0.0f;
    } else if (x < -halfX + margin) {
        x = -halfX + margin;
        vel.x = 0.0f;
    }

    // Y 轴边界
    if (y > halfY - margin) {
        y = halfY - margin;
        vel.y = 0.0f;
    } else if (y < -halfY + margin) {
        y = -halfY + margin;
        vel.y = 0.0f;
    }

    // Z 轴边界
    if (z > halfZ - margin) {
        z = halfZ - margin;
        vel.z = 0.0f;
    } else if (z < -halfZ + margin) {
        z = -halfZ + margin;
        vel.z = 0.0f;
    }
}

// ==================== 修改后的原有函数 ====================

float computeDensity(int i) {
    float density = 0.0f;
    int key_i = hashGridCell(particlePositions[i]);

    // 粒子间贡献
    for (auto ncell : hashNeighbor(key_i)) {
        for (int j : grid[ncell]) {
            float dist = distance(particlePositions[i], particlePositions[j]);
            density += MASS * W_poly6(dist, H);
        }
    }

    // 边界贡献（新增）
    float dummy_grad_sum = 0.0f;
    vec3 dummy_delta_p = vec3(0.0f);
    float dummy_lambda = 0.0f;
    applyBoundaryContributions(i, density, dummy_grad_sum, dummy_delta_p, dummy_lambda);

    return density;
}

float computeLambda(int i, float density_i) {
    float C_i = density_i / REST_DENSITY - 1.0f;

    float grad_sum = 0.0f;
    int key_i = hashGridCell(particlePositions[i]);

    // 粒子间梯度贡献
    for (auto ncell : hashNeighbor(key_i)) {
        for (int j : grid[ncell]) {
            vec3 dir = particlePositions[i] - particlePositions[j];
            float r = length(dir);
            vec3 grad = grad_W_spiky(dir, r, H);
            grad_sum += dot(grad, grad);
        }
    }

    // 边界梯度贡献（新增）
    float dummy_density = 0.0f;
    vec3 dummy_delta_p = vec3(0.0f);
    float dummy_lambda = 0.0f;
    applyBoundaryContributions(i, dummy_density, grad_sum, dummy_delta_p, dummy_lambda);

    return -C_i / (grad_sum + 1e-4f);
}

vec3 computeDeltaP(int i, float lambda_i) {
    vec3 delta_p = vec3(0.0f);
    int key_i = hashGridCell(particlePositions[i]);

    // 粒子间位置修正贡献
    for (auto ncell : hashNeighbor(key_i)) {
        for (int j : grid[ncell]) {
            if (i == j) continue;

            vec3 dir = particlePositions[i] - particlePositions[j];
            float r = length(dir);
            float lambda_j = lambda[j];
            vec3 grad = grad_W_spiky(dir, r, H);

            delta_p += (lambda_i + lambda_j) * grad;
        }
    }

    // 边界位置修正贡献（新增）
    float dummy_density = 0.0f;
    float dummy_grad_sum = 0.0f;
    applyBoundaryContributions(i, dummy_density, dummy_grad_sum, delta_p, lambda_i);

    return delta_p / REST_DENSITY;
}

// 主函数：更新粒子系统的物理模拟
void updatePhysics(float dt) {
    // 保存这一帧开始时的初始位置（用于最后反推速度）
    initialPositions = particlePositions;

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

    // 步骤4 & 5：PBD迭代 (Solvers)
    // 迭代次数: 建议 3-5 次以获得稳定约束
    const int iterations = 4;
    for (int iter = 0; iter < iterations; ++iter) {
        // ----- 构建空间哈希网格 -----
        for (int i = 0; i < indiceCount; ++i) {
            grid[i].clear();
        }
        for (int i = 0; i < particleCount; ++i) {
            int key = hashGridCell(particlePositions[i]);
            grid[key].push_back(i);
        }

        // ----- 计算密度 (包含边界虚粒子贡献) -----
        for (int i = 0; i < particleCount; ++i) {
            density[i] = computeDensity(i);
        }

        // ----- 计算 lambda (包含边界梯度贡献) -----
        for (int i = 0; i < particleCount; ++i) {
            lambda[i] = computeLambda(i, density[i]);
        }

        // ----- 应用位置修正 (包含边界位置修正) -----
        for (int i = 0; i < particleCount; ++i) {
            particlePositions[i] += vec4(computeDeltaP(i, lambda[i]), 0.0f);
        }
    }

    // 步骤7：硬边界裁剪 (安全网, 防止数值误差导致穿透)
    for (int i = 0; i < particleCount; ++i) {
        clampParticleToBoundary(i);
    }

    // 步骤6：根据最终位置与初始位置的变化反推速度
    // 关键修复: 必须保护 clampParticleToBoundary 已归零的法向速度分量
    // 否则反推速度会重新引入向外的法向分量, 导致下一帧穿透
    for (int i = 0; i < particleCount; ++i) {
        vec3 posNow(particlePositions[i]);
        vec3 posBefore(initialPositions[i]);

        // 对每个轴独立处理: 只在未被边界钳制的方向上使用反推速度
        float margin = particleRadius;

        if (posNow.x <= halfX - margin && posNow.x >= -halfX + margin) {
            particleVelocities[i].x = (posNow.x - posBefore.x) / dt;
        }
        if (posNow.y <= halfY - margin && posNow.y >= -halfY + margin) {
            particleVelocities[i].y = (posNow.y - posBefore.y) / dt;
        }
        if (posNow.z <= halfZ - margin && posNow.z >= -halfZ + margin) {
            particleVelocities[i].z = (posNow.z - posBefore.z) / dt;
        }

        // 速度限幅
        float speed = length(particleVelocities[i]);
        if (speed > particleMaxSpeed) {
            particleVelocities[i] *= particleMaxSpeed / speed;
        }
    }
}