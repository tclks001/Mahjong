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

vector<vector<int>> grid(indiceCount + 1, {});

const float M_PI = std::acos(-1);
const float H = diameter * 1.2f;
const float REST_DENSITY = 1000.0F;
const float MASS = 1.0f;

vector<float> density(particleCount);
vector<float> lambda(particleCount);
vector<vec4> initialPositions(particleCount);

struct VirtualNeighborInfo {
    int rx; // x轴反射方向：-1（左边界）、0（无反射）、1（右边界）
    int ry;
    int rz;
};

VirtualNeighborInfo virtualNeighborTable[27];

// 初始化表（在容器尺寸确定后调用一次）
void initVirtualNeighborTable() {
    for (int i = 0; i < 27; ++i) {
        int dx = i / 9 - 1;           // -1,0,1
        int dy = (i % 9) / 3 - 1;     // -1,0,1
        int dz = i % 3 - 1;           // -1,0,1
        virtualNeighborTable[i].rx = dx;
        virtualNeighborTable[i].ry = dy;
        virtualNeighborTable[i].rz = dz;
    }
}

void clampParticle(vec4& pos) {
    pos = glm::clamp(pos,
        vec4(-halfX + particleRadius,
             -halfY + particleRadius,
             -halfZ + particleRadius,
              0.0f),
        vec4( halfX - particleRadius,
              halfY - particleRadius,
              halfZ - particleRadius,
              1.0f));
}

// 获取虚粒子位置 (保留用于其他可能的用途)
vec3 getVirtualParticlePosition(const vec3& pos, int idx) {
    const VirtualNeighborInfo& info = virtualNeighborTable[idx];
    float x = info.rx == 0 ? pos.x : 2 * info.rx * halfX - pos.x;
    float y = info.ry == 0 ? pos.y : 2 * info.ry * halfY - pos.y;
    float z = info.rz == 0 ? pos.z : 2 * info.rz * halfZ - pos.z;
    return vec3(x, y, z);
}

int hashGridCell(const glm::vec4& pos) {
    int x = glm::clamp((int)((pos.x + halfX) / cellSize), 0, widthIndice - 1);
    int y = glm::clamp((int)((pos.y + halfY) / cellSize), 0, depthIndice - 1);
    int z = glm::clamp((int)((pos.z + halfZ) / cellSize), 0, heightIndice - 1);
    return (x * depthIndice + y) * heightIndice + z;
}

// 哈希邻居索引
// 
vector<int> hashNeighbor(int cell) {
    vector<int> res(27, indiceCount); // 初始化为indiceCount，表示越界
    // 反解xyz
    int z = cell % heightIndice;
    cell /= heightIndice;
    int y = cell % depthIndice;
    cell /= depthIndice;
    int x = cell;
    int i = 0;
    for (int nx = x - 1; nx <= x + 1; ++nx) {
        for (int ny = y - 1; ny <= y + 1; ++ny) {
            for (int nz = z - 1; nz <= z + 1; ++nz) {
                if (nx >= 0 && nx < widthIndice &&
                    ny >= 0 && ny < depthIndice &&
                    nz >= 0 && nz < heightIndice) {
                    res[i] = (nx * depthIndice + ny) * heightIndice + nz;
                }
                ++i;
            }
        }
    }
    return res;
}


// ============================================================
// 统一邻居遍历函数 (核心边界投影逻辑)
//
// [Bug4修复] 回调新增 isBoundary 参数:
//   isBoundary=false: 内部网格中的真实粒子
//   isBoundary=true:  边界虚粒子(镜像反射)
//   computeDensity/computeLambda 应只使用内部邻居(isBoundary=false)
//   computeDeltaP 需要同时处理内部和边界邻居
//
// 将粒子 i 的 3x3x3 邻域网格遍历封装为独立函数:
//   - 内部网格单元: 直接返回该单元内的所有真实粒子索引
//   - 越界(边界)网格单元: 通过坐标映射找到容器内"正对"的真实网格单元,
//     遍历其中的所有真实粒子, 每个粒子的位置经镜像反射后作为虚粒子
//
// 投影对应关系:
//   - 正对单元 (如 +X 方向越界 -> 投影到 -X 边界内的正对网格)
//   - 斜向单元 (如 +X+Y 越界 -> 投影到 -X-Y 角落的正对网格)
//   - 坐标映射严格维护格子与粒子坐标的对应关系
//
// 回调参数说明:
//   j           -> 真实粒子在 particlePositions 中的索引
//   virtPos     -> 粒子位置 (对于内部邻居, =particlePositions[j])
//                 (对于边界邻居, =mirror(particlePositions[j]))
//   isBoundary  -> 是否为边界虚粒子
// ============================================================
template<typename Func>
void forEachNeighbor(int i, const Func& callback) {
    vec3 pos_i = vec3(particlePositions[i]);
    int key_i = hashGridCell(particlePositions[i]);
    vector<int> ncells = hashNeighbor(key_i);

    for (int k = 0; k < 27; ++k) {
        int ncell = ncells[k];

        if (ncell != indiceCount) {
            // ===== 内部网格单元: 真实粒子 =====
            for (int j : grid[ncell]) {
                callback(j, pos_i, vec3(particlePositions[j]), false);
            }
        }
        else {
            // ===== 越界(边界)网格单元: 虚粒子 =====

            // 反解当前粒子所在网格坐标
            int cz = key_i % heightIndice;
            int cy = (key_i / heightIndice) % depthIndice;
            int cx = key_i / (heightIndice * depthIndice);

            // 计算邻居网格坐标
            const VirtualNeighborInfo& info = virtualNeighborTable[k];
            int nx = cx + info.rx;
            int ny = cy + info.ry;
            int nz = cz + info.rz;

            // 将越界坐标镜像映射到容器内的正对网格
            auto reflect = [](int v, int maxSize) -> int {
                if (v < 0) return -(v + 1);
                if (v >= maxSize) return 2 * maxSize - 1 - v;
                return v;
            };

            int mx = reflect(nx, widthIndice);
            int my = reflect(ny, depthIndice);
            int mz = reflect(nz, heightIndice);

            if (mx < 0 || mx >= widthIndice || my < 0 || my >= depthIndice || mz < 0 || mz >= heightIndice) {
                continue;
            }

            int mirrorCell = (mx * depthIndice + my) * heightIndice + mz;

            for (int j : grid[mirrorCell]) {
                vec3 realPos_j = vec3(particlePositions[j]);
                vec3 virtPos_j = getVirtualParticlePosition(realPos_j, k);
                callback(j, pos_i, virtPos_j, true);  // isBoundary=true
            }
        }
    }
}

void initPhysX() {
	particlePositions.resize(particleCount);         // 粒子位置
	particleColors.resize(particleCount);            // 预设颜色，初始化后不修改
	particleVelocities.resize(particleCount);        // 同时也是粒子强度位置在灰度图上的移动速度标量
	particleColorMapPositions.resize(particleCount); // 粒子颜色强度映射在柏林噪声灰度图的位置
	particleForces.resize(particleCount);            // 粒子受力
    initVirtualNeighborTable();
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


float W_poly6(float r, float h) {
    if (r >= h) {
        return 0.0f;
    }
    float diff = h * h - r * r;
    return 315.0f / (64.0f * M_PI * pow(h, 9.0f) * diff * diff * diff);
}
vec3 grad_W_spiky(vec3 dir, float r, float h) {
    if (r >= h || r < 1e-6f) {
        return vec3(0.0f);
    }
    float coeff = -45.0f / (M_PI * pow(h, 6.0f));
    float diff = h - r;
    return coeff * diff * diff * (dir / r);
}

float computeDensity(int i) {
    float rho = 0.0f;
    vec3 pi = vec3(particlePositions[i]);

    forEachNeighbor(i, [&](int j, const vec3& p_i, const vec3& p_j, bool isBoundary) {
        if (j == i) return;

        float r = length(p_i - p_j);
        if (r >= H) return;

        // 边界虚粒子也应作为 support particle 参与密度估计
        rho += MASS * W_poly6(r, H);
        });

    return rho;
}

float computeLambda(int i, float density_i) {
    float C_i = density_i / REST_DENSITY - 1.0f;
    float grad_sum = 0.0f;

    forEachNeighbor(i, [&](int j, const vec3& p_i, const vec3& p_j, bool isBoundary) {
        if (j == i) return;

        vec3 dir = p_i - p_j;
        float r = length(dir);
        if (r >= H || r < 1e-6f) return;

        vec3 grad = grad_W_spiky(dir, r, H);

        // 边界虚粒子同样应参与梯度分母，否则靠墙时 lambda 会被放大
        grad_sum += dot(grad, grad);
        });

    return -C_i / (grad_sum + 1e-4f);
}

vec3 computeDeltaP(int i, float lambda_i) {
    vec3 delta_p(0.0f);

    forEachNeighbor(i, [&](int j, const vec3& p_i, const vec3& p_j, bool isBoundary) {
        if (j == i) return;

        vec3 dir = p_i - p_j;
        float r = length(dir);
        if (r >= H || r < 1e-6f) return;

        vec3 grad = grad_W_spiky(dir, r, H);

        // 不管是不是 boundary image，压力项都应该来自“源粒子 j”的 lambda
        // 绝不能写成 lambda_i + lambda_i
        float lambda_j_eff = lambda[j];

        delta_p += (lambda_i + lambda_j_eff) * grad;
        });

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
        clampParticle(particlePositions[i]);
    }

    // 步骤4 & 5：PBD迭代 (Solvers)
    // [Bug5修复] 迭代次数从3增至7, 提高不可压缩性收敛
    // PBF要求足够多的迭代才能让密度约束 C_i = rho/rho_0 - 1 -> 0
    // 迭代不足时密度偏差持续存在, 导致每帧都有大的deltaP修正
    const int iterations = 7;
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
        // 关键: 限制每步修正幅度, 防止角落多约束叠加导致瞬移
        const float maxDeltaP = 0.1f; // 单步最大位移 (约为粒子半径的5-10倍)
        for (int i = 0; i < particleCount; ++i) {
            vec3 dp = computeDeltaP(i, lambda[i]);
            float dpLen = length(dp);
            if (dpLen > maxDeltaP) {
                dp *= maxDeltaP / dpLen;
            }
            particlePositions[i] += vec4(dp, 0.0f);
            //clampParticle(particlePositions[i]); // 钳制在容器内部
        }
    }
    // 统一边界钳制：只在所有约束迭代结束后做一次
    for (int i = 0; i < particleCount; ++i) {
        clampParticle(particlePositions[i]);
    }

    // ============================================================
    // 步骤6: 能量守恒速度更新 (修复粒子弹飞/动能异常)
    //
    // Bug1修复(致命): v = Δx/dt 反推公式每帧注入非物理动能
    //   PBD位置修正是纯代数约束投影(不遵循F=ma),
    //   直接将Δx/dt作为速度等价于每帧注入非物理动能
    //   -> 检测异常加速, 限制反推速度幅度
    //
    // Bug2修复(严重): 缺少全局粘性阻尼
    //   数值积分引入能量漂移无法耗散 -> 每帧damping衰减
    //
    // Bug3修复(中等): 缺少XSPH粘性修正
    //   碰撞相对速度无抑制 -> 计算邻域加权平均速度偏差
    // ============================================================

    const float damping       = 0.998f;     // Bug2: 全局阻尼(~0.2%动能耗散/帧)
    const float xsph_viscosity = 0.01f;     // Bug3: XSPH粘性系数

    // 最终网格构建 (用于XSPH邻居查询)
    for (int i = 0; i < indiceCount; ++i) { grid[i].clear(); }
    for (int i = 0; i < particleCount; ++i) {
        int key = hashGridCell(particlePositions[i]);
        grid[key].push_back(i);
    }

    for (int i = 0; i < particleCount; ++i) {
        // --- Bug1修复: 基于位移的速度更新(带异常检测) ---
        vec3 displacement = vec3(particlePositions[i]) - vec3(initialPositions[i]);
        vec3 velFromDisplacement = displacement / dt;

        float oldSpeed = length(particleVelocities[i]);
        float newSpeed = length(velFromDisplacement);

        vec3 newVel;
        // 异常检测: 反推速度 > 2倍旧速度 且 > 1.0 m/s
        if (newSpeed > oldSpeed * 2.0f && newSpeed > 1.0f) {
            float safeSpeed = std::max(oldSpeed, particleMaxSpeed * 0.5f);
            newVel = (newSpeed > 1e-6f)
                ? velFromDisplacement * (safeSpeed / newSpeed)
                : particleVelocities[i];
        } else {
            newVel = velFromDisplacement;
        }

        // --- Bug3修复: XSPH粘性速度修正 ---
        // [Bug4] XSPH只使用内部邻居(边界虚粒子无真实速度)
        vec3 xsph_correction(0.0f);
        forEachNeighbor(i, [&](int j, const vec3& pi, const vec3& pj, bool isBoundary) {
            if (j == i || isBoundary) return;  // 排除自身和边界虚粒子
            float dist = distance(pi, pj);
            if (dist >= H || dist < 1e-6f) return;
            xsph_correction += W_poly6(dist, H) * (particleVelocities[j] - newVel);
        });
        newVel += xsph_viscosity * xsph_correction;

        // --- Bug2修复: 全局粘性阻尼 ---
        newVel *= damping;

        // 边界速度钳制
        float& x = particlePositions[i].x;
        float& y = particlePositions[i].y;
        float& z = particlePositions[i].z;
        float margin = particleRadius;

        if (x > halfX - margin) { x = halfX - margin; newVel.x = std::min(0.0f, newVel.x); }
        else if (x < -halfX + margin) { x = -halfX + margin; newVel.x = std::max(0.0f, newVel.x); }

        if (y > halfY - margin) { y = halfY - margin; newVel.y = std::min(0.0f, newVel.y); }
        else if (y < -halfY + margin) { y = -halfY + margin; newVel.y = std::max(0.0f, newVel.y); }

        if (z > halfZ - margin) { z = halfZ - margin; newVel.z = std::min(0.0f, newVel.z); }
        else if (z < -halfZ + margin) { z = -halfZ + margin; newVel.z = std::max(0.0f, newVel.z); }

        particleVelocities[i] = newVel;

        // 最终速度限幅
        float speed = length(particleVelocities[i]);
        if (speed > particleMaxSpeed) {
            particleVelocities[i] *= particleMaxSpeed / speed;
        }
    }
}