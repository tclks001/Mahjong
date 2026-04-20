// 物理模块
// 负责物理计算
// 给主程序（即UI程序）提供设置重力方向接口和调用update(dt)接口
// 给渲染程序提供获取粒子位置指针数组std::vector<glm::vec3>接口
// 直接对粒子使用数组结构体实现更加紧凑的内存布局

#include "physics.h"

using namespace glm;


// =========================================================
// 初始化 PhysX 5 物理引擎
// =========================================================
void initPhysX() {
    // 1. 创建 Foundation (PhysX 的根对象)
    gFoundation = PxCreateFoundation(PX_PHYSICS_VERSION, gAllocator, gErrorCallback);
    if (!gFoundation) {
        std::cerr << "PxCreateFoundation failed!\n";
        return;
    }

    // 2. 创建 PVD (PhysX Visual Debugger) 连接，方便用官方调试工具连入查看
    gPvd = PxCreatePvd(*gFoundation);
    PxPvdTransport* transport = PxDefaultPvdSocketTransportCreate("127.0.0.1", 5425, 10);
    gPvd->connect(*transport, PxPvdInstrumentationFlag::eALL);

    // 3. 创建 Physics SDK 实例
    gPhysics = PxCreatePhysics(PX_PHYSICS_VERSION, *gFoundation, PxTolerancesScale(), true, gPvd);
    if (!gPhysics) {
        std::cerr << "PxCreatePhysics failed!\n";
        return;
    }

    // 4. 创建 Scene 场景描述
    PxSceneDesc sceneDesc(gPhysics->getTolerancesScale());
    sceneDesc.gravity = PxVec3(0.0f, -9.81f, 0.0f); // 设置重力向下


    // 使用默认的 CPU 调度器
    PxDefaultCpuDispatcher* dispatcher = PxDefaultCpuDispatcherCreate(2);
    sceneDesc.cpuDispatcher = dispatcher;
    sceneDesc.filterShader = PxDefaultSimulationFilterShader; // 默认碰撞过滤着色器

    // 5. 创建 Scene
    gScene = gPhysics->createScene(sceneDesc);
    if (!gScene) {
        std::cerr << "createScene failed!\n";
        return;
    }

    PxPvdSceneClient* pvdClient = gScene->getScenePvdClient();
    if (pvdClient) {
        // 设置需要传输的场景标志（通常全部启用）
        pvdClient->setScenePvdFlags(
            PxPvdSceneFlag::eTRANSMIT_CONSTRAINTS |
            PxPvdSceneFlag::eTRANSMIT_CONTACTS |
            PxPvdSceneFlag::eTRANSMIT_SCENEQUERIES);
    }
    else {
        std::cerr << "Warning: No PVD client for scene!\n";
    }

    // 6. 创建默认物理材质 (静态摩擦系数, 动态摩擦系数, 弹性恢复系数)
    gMaterial = gPhysics->createMaterial(0.5f, 0.5f, 0.6f); // 弹性设为0.6，球会有很好的反弹效果

    std::cout << "PhysX 5 initialized successfully.\n";
}

// =========================================================
// 创建地面平面
// =========================================================
void createGroundPlane() {
    // 使用 PxCreatePlane 辅助函数快速创建一个无限大的平面
    gGroundPlane = PxCreatePlane(*gPhysics, PxPlane(PxVec3(0, 1, 0), 0), *gMaterial);
    gScene->addActor(*gGroundPlane);

    // 对应 OpenGL 的地面模型矩阵：铺开一个大平面
    gGroundModelMatrix = scale(mat4(1.0f), vec3(20.0f, 0.01f, 20.0f));
}

// =========================================================
// 创建弹跳球体
// =========================================================
void createBall(const vec3& position, float radius) {
    // 1. 创建动态刚体描述
    PxTransform transform(PxVec3(position.x, position.y, position.z));
    gBall = gPhysics->createRigidDynamic(transform);

    // 2. 设置球体几何形状
    PxSphereGeometry geometry(radius);

    // 3. 计算球体的质量属性 (密度默认设为 1)
    PxRigidBodyExt::updateMassAndInertia(*gBall, 1.0f);

    // 4. 将形状附加到刚体上
    PxRigidActorExt::createExclusiveShape(*gBall, geometry, *gMaterial);

    // 5. 将刚体加入场景
    gScene->addActor(*gBall);

    // 6. 生成球体的 OpenGL 渲染网格 (低多边形风格，看起来有质感)
    generateSphereMesh(radius, 24, 16);
}

// =========================================================
// 重置物理模拟：把球拿回手里，重新扔下去
// =========================================================
void resetSimulation() {
    if (gBall) {
        gBall->setGlobalPose(PxTransform(PxVec3(gBallStartPos.x, gBallStartPos.y, gBallStartPos.z)));
        gBall->setLinearVelocity(PxVec3(0)); // 清零速度
        gBall->setAngularVelocity(PxVec3(0));
    }
}

// =========================================================
// 更新物理模拟
// =========================================================
void updatePhysics(float dt) {
    if (gScene) {
        gScene->simulate(dt);
        gScene->fetchResults(true); // 等待当前帧模拟完成
    }
}

