# 项目说明
不借助集成游戏引擎，搭建一个简易的流沙麻将小游戏<br>

# 游戏玩法
在扁形瓶（长6cm，宽3.7cm，厚0.2cm）中填满流沙油（略微粘稠的液体）<br>
再填入若干流沙粒子（直径0.02cm）。<br>
通过摇晃、倒置瓶子，观察流沙的沉降、堆积和随液体流动的视觉效果。<br>
还可以增加亮片功能，即体积稍大、具有特定形状的薄片状刚体，重点突出其反光特性。<br>

# 模块划分
模块采用单向依赖+回调通知的架构，UI作为控制入口，物理模块独立模拟，渲染模块仅负责可视化。<br>

```mermaid
graph TD
    UI[UI模块 ImGui] -->|鼠标/按钮事件<br>（重力方向/容器姿态）| PM[物理模块 PhysX]
    UI -->|渲染参数| RM[渲染模块 OpenGL]
    PM -->|每帧粒子位置/速度/颜色| RM
    RM -->|帧缓冲| UI[UI叠加显示]
```

## 物理模块（physx5）
- 职责：维护粒子（闪粉）的物理状态，处理与容器边界的碰撞、粒子间接触（堆积）、流体阻尼（粘稠液体效果），响应重力方向变化。
- 依赖：仅依赖UI模块获取当前重力方向，输出粒子位置数组供渲染模块读取。
- 交互方式：提供`update(deltaTime)`推进模拟，提供`getParticleData`返回只读粒子数据指针。

## 渲染模块（Opengl+glfw+glew+glm+stb_image）
- 职责：管理着色器、顶点缓冲，绘制容器边框和所有粒子，接收UI传递的相机视角参数。
- 依赖：每帧从物理模块获取粒子位置，转换为GPU缓冲后绘制。
- 交互方式：暴露`render()`方法，由UI模块的主循环调用。

## UI模块（ImGui）
- 职责：接收用户输入，调节模拟参数，控制模拟启停。
- 依赖：调用物理模拟的接口更新重力方向、容器变换矩阵；调用渲染模块的接口更新相机视角。
- 交互方式：通过ImGui窗口嵌入OpenGL视口，不直接持有渲染数据。

# 数据流动模型

## 各个模块之间的数据流动
```mermaid
sequenceDiagram
    participant UI as UI模块
    participant PM as 物理模块
    participant RM as 渲染模块
    loop 每一帧
        UI->>UI: 处理ImGui事件（按钮/拖拽）
        UI->>PM: 设置重力方向/容器旋转
        UI->>PM: 调用 update(dt)
        PM->>PM: 计算粒子受力（重力+阻尼+浮力+碰撞）
        PM->>PM: 更新粒子位置/速度（欧拉积分）
        PM->>PM: 处理粒子间堆积（简单斥力或PhysX接触）
        PM-->>RM: 暴露粒子位置数组（std::vector<glm::vec3>）
        UI->>RM: 调用 render()
        RM->>RM: 更新VBO/VAO
        RM->>RM: 绘制粒子（GL_POINTS或实例化四边形）
        RM->>UI: 提交帧缓冲
        UI->>UI: 渲染ImGui控件（叠加）
    end
```
## 物理模块内的数据流动
```mermaid
flowchart TD
    Input[输入：deltaTime, gravity] --> Step1

    subgraph Step1 [对每个粒子施力]
        A1[重力 gravity * mass]
        A2[流体阻力 -k_drag * velocity]
        A3[浮力 -density_fluid * volume * gravity]
    end

    Step1 --> Step2[积分更新速度与位置]
    Step2 --> Step3[将粒子变换到容器局部坐标]
    Step3 --> Step4[与6个平面碰撞响应<br>（恢复系数 + 摩擦）]
    Step4 --> Step5[粒子间简单堆积<br>（空间哈希优化邻近搜索）]
    Step5 --> Step6[施加排斥冲量（距离 < 2倍半径）]
    Step6 --> Output[输出：更新后的 Particle 数组]

    Input2[重力向量 gravity<br>（可动态改变方向）] -.-> Step1
    Input3[容器变换矩阵 containerTransform] -.-> Step3
```
## 渲染模块内的数据流动
```mermaid
flowchart TD
    subgraph 每帧循环
        A[从物理模块获取粒子位置数组] --> B[将位置、颜色分别上传到两个 VBO]
        B --> C[顶点着色器：将粒子位置从世界坐标转换到裁剪坐标<br>（提供 MVP 矩阵）]
        C --> D[片元着色器：绘制圆形或方形点]
        D --> E{是否为<br>大刚体薄片？}
        E -->|是| F[额外绘制纹理四边形（支持高光效果）]
        E -->|否| G[直接输出片元颜色]
        F --> H[输出到帧缓冲]
        G --> H
    end
```
# 开发流程

> 20260419<br>
> 项目构思，环境搭建，测试基础窗口功能。<br>
> 在NuGet里直接安装glfw、glew、glm、stb_image。<br>
> PhysX5在GitHub下载源码，并在StaticDebug模式下编译。<br>
> 注意设定头文件路径、库文件路径，尤其注意设定路径时的项目模式应该保持一致为StaticDebug。<br>
> ImGui在GitHub下载源码，并将需要用到的文件拷贝到项目中。<br>

> 20260420<br>
> 构建物理模型。<br>
> 完全没有必要使用SPH（光滑粒子流体动力学）方法。<br>
> 只把粒子当做质点，实现粒子间的法向斥力和切向摩擦力。<br>
> 把流体当做场，因为容器非常扁（厚度仅长宽的1/20）<br>
> 用单层纳维 - 斯托克斯方程计算流体的流动速度场。<br>
> 用拖曳力模型把速度场施加到粒子上。<br>
> 只考虑单向耦合（忽略粒子对流体的影响）<br>
> $v_{new} = v_{old} + \alpha * (v_{fluid} - v_{old})$<br>
> 不用光照模拟，那太吃性能。<br>
> 用柏林噪声闪烁贴图，各个粒子的发光参数在2D柏林噪声图上随机平滑移动。<br>
> 对噪声值取exp，只在一定阈值以上时高光，实现闪粉的闪烁效果。<br>
> 再用HDR + Bloom（泛光）增强闪烁的视觉效果。<br>