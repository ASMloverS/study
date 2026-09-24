# Codcopy 技术架构文档

- 版本：v1.1
- 日期：2026-09-24
- 状态：已确认；含 M5 迭代设计（标注 [M5] 的条目为该迭代新增，待实施）

## 1. 总体架构

**权威服务器（authoritative server）模型**：所有游戏逻辑（移动、射击、伤害、掩体、AI）在服务器模拟并判定；客户端只负责渲染、输入采集、本地玩家预测与表现层。

```
┌─────────────── 单机模式 ────────────────┐   ┌─────────────── 联机模式 ────────────────┐
│  Client (Three.js)                      │   │  Client (Three.js)                      │
│    │ LocalTransport（同进程函数直调）      │   │    │ WebSocketTransport（ws, JSON）      │
│    ▼                                    │   │    ▼                                   │
│  Headless GameServer（内嵌实例）          │   │  Node GameServer（独立进程，多房间）      │
│    - 30Hz tick  - AI  - 权威判定          │   │    - 30Hz tick  - AI  - 权威判定         │
└─────────────────────────────────────────┘   └─────────────────────────────────────────┘
                     两模式共用：shared 协议/常量/地图/碰撞/导航 + server 全部游戏逻辑
```

关键决策：

- **单机 = 同进程内嵌 headless 服务器**，通过 `Transport` 抽象隔离消息通道，单机 / 联机复用 100% 游戏逻辑，杜绝双份实现漂移。
- **不引入物理引擎**。所有掩体为 AABB，自研：圆柱-AABB 碰撞（移动解算）、射线-AABB（slab 法，射击与视线）、2D grid + A*（AI 导航）。确定性强、代码量小、双端复用。

## 2. Monorepo 结构

```
codcopy/
├─ package.json              # npm workspaces: ["shared", "server", "client"]
├─ docs/                     # 本文档集
├─ shared/                   # 双端复用（零 DOM / 零 Node 依赖）
│  ├─ src/
│  │  ├─ protocol.ts         # C2S/S2C 消息类型定义
│  │  ├─ constants.ts        # tick、移动、对局规则等常量
│  │  ├─ weapons.ts          # 武器数据表
│  │  ├─ sim/
│  │  │  └─ movement.ts      # 移动解算器（走/跳/蹲/冲刺/滑铲；双端同一函数）
│  │  ├─ map/
│  │  │  ├─ schema.ts        # 地图数据类型
│  │  │  └─ warehouse.json   # 「工业区仓库」地图数据（tools/genmap.mjs 生成）
│  │  ├─ physics/
│  │  │  ├─ aabb.ts          # AABB / 身体碰撞 / 轴分离移动
│  │  │  └─ raycast.ts       # slab 射线检测（最近命中）
│  │  ├─ nav/
│  │  ├─ grid.ts          # 0.5m 导航网格烘焙（含动态更新）
│  │  └─ astar.ts         # A* 寻路 + 路径平滑（string pulling）
│  └─ test/                  # vitest 单测（碰撞/射线/移动）
├─ server/
│  └─ src/
│     ├─ index.ts            # 包导出（不含 net.ts，避免客户端打包引入 Node 依赖）
│     ├─ net.ts              # ws 服务器入口：连接管理/AI换真人/断线补位/over重置 + 同端口静态托管 client/dist
│     ├─ headless.ts         # 内嵌服务器（tick 驱动 + 快照/事件广播）
│     ├─ transport/
│     │  ├─ local.ts         # Transport 抽象 + 本地直连对
│     ├─ game/
│     │  ├─ world.ts         # Room：tick/移动/射击判定/伤害/死亡重生/快照/掩体集成
│     │  ├─ rng.ts           # mulberry32 种子随机（确定性）
│     │  ├─ destructible.ts  # 可破坏掩体状态、油桶 AOE 数值、连锁半径
│     │  └─ dynamicCover.ts  # 感应滑门/周期升降平台状态机
│     ├─ ai/
│     │  ├─ controller.ts     # AI 控制器：行为树 + A* 跟随 + peek/换弹/选枪
│     │  ├─ perception.ts      # 视觉（FOV+LOS）/ 听觉（声音事件）/ 记忆衰减
│     │  └─ behaviorTree.ts    # 行为树框架（Selector/Sequence/Cond/Act）
└─ client/
   └─ src/
      ├─ index.html / main.ts # main：固定步长循环 + 消息编排 + 会话生命周期
      ├─ input.ts             # 键鼠采样 / 指针锁定 / 切枪 / 设置
      ├─ predict.ts           # 本地玩家预测 + 服务器和解（回滚重放）
      ├─ hud.ts               # DOM HUD：准星扩散/计分板/结算/伤害方向/狙击镜
      ├─ audio.ts             # WebAudio 合成 + PannerNode 空间化
      ├─ render/
      │  ├─ scene.ts          # Three.js 场景/光照/雾
      │  ├─ mapView.ts        # 掩体渲染：静态按色合批 merge；可破坏/动态独立对象
      │  ├─ actors.ts         # 远端玩家：拟人分节模型 + 程序化动画（走/跑/蹲/滑/倒地）[M5]
      │  ├─ effects.ts        # 曳光（高速弹道）/枪口焰/抛壳/弹孔贴花/分材质命中/血雾 [M5]
      │  ├─ viewmodel.ts      # 高精度武器视图模型 + 双手 + 真铁瞄 [M5]
      │  └─ minimap.ts        # 俯视小地图 canvas + 开枪暴露红点
      └─ transport.ts         # Session 抽象：LocalSession / NetSession（ws + RTT + 断线回调）
```

## 3. 技术选型

| 层 | 选型 | 理由 |
|---|---|---|
| 语言 | TypeScript 全栈 | 双端复用类型与逻辑 |
| 客户端构建 | Vite | 快、零配置 |
| 渲染 | Three.js | 成熟 Web3D，Low-poly 无需美术管线 |
| 通信 | ws (WebSocket) + JSON | 8 人局带宽足够；协议类型先行，后续可换二进制 |
| 测试 | vitest（shared 优先） | 碰撞 / 射线 / 寻路 / 行为树为纯逻辑，可测性高 |
| 物理 | 自研（见上） | 确定性 + 复用 |
| 包管理 | npm workspaces | 无额外工具链 |

## 4. 游戏循环

### 4.1 服务器（固定步长）

```ts
tick(dt = 1/30):
  1. 收集输入：每玩家（真人 / AI controller）按序号取输入
  2. movement.solve(players, colliders)      // 权威移动
  3. dynamicCover.update(dt)                 // 滑门/平台
  4. combat.resolve(shots)                   // 射线判定/伤害/死亡
  5. destructible.update(dt)                 // 油桶连锁/木箱
  6. ai.update(dt)                           // 感知+行为树→生成下 tick 输入
  7. respawn / 对局规则推进（击杀数/计时）
  8. 每 2 tick 广播一次快照（15Hz）+ 即时事件
```

- 固定 `dt` 保证确定性与可测性；AI 决策频率按难度分档（5/8/12Hz），在 `ai.update` 内节流。

### 4.2 客户端（可变帧率）

```ts
frame(rAF):
  1. 采样键鼠 → 生成输入指令（带序号 seq 与本地时间戳）
  2. 本地预测：立即应用移动/射击到本地影子状态（predict.ts）
  3. 发送输入（单机直调 / 联机 ws）
  4. 渲染：本地玩家用预测态；远端实体用插值态（interpolate.ts）
  5. 收到快照 → 和解（见 5.2）；收到事件 → 特效/音效/killsfeed
```

## 5. 网络同步

### 5.1 基本参数

| 参数 | 值 |
|---|---|
| 服务器逻辑 tick | 30Hz（dt = 1/30） |
| 快照广播 | 15Hz（每 2 tick） |
| 远端插值缓冲 | ~100ms（2 个快照间隔 + 抖动余量） |
| 时钟同步 | 客户端启动时 ping × 5 取最小 RTT 估算偏移（仅用于事件表现排序，不影响判定） |

### 5.2 本地玩家：预测 + 和解

1. 输入指令带自增 `seq`，客户端对本地玩家影子状态立即执行 `shared/movement`（与服务器同一函数）。
2. 快照含「确认到某玩家最后的输入序号 `lastSeq` + 权威状态」。
3. 和解：权威态与预测态差异超阈值 → 回滚到权威态 → 重放 `lastSeq+1 .. 当前` 的未确认输入 → 表现层平滑（位置误差按 10%/帧 收敛）。
4. **客户端开火预测（仅视觉）[M5]**：扣扳机本地立即播放枪口焰 / 枪模后坐 / 曳光 / 音效 / 后坐力视角反馈（零 ½RTT 迟滞）；服务器 `shot`/`hit` 事件到达后按输入 seq 去重（自身已表现过的枪口 / 曳光效果不再重播）；命中标记与伤害以服务器 `hit` 事件为唯一权威，弹匣数以快照为权威。

### 5.3 远端实体：快照插值

- 快照含全体玩家位姿（位置 / 朝向 / 姿态 / 武器 / 动画态）与掩体关键状态（可破坏 HP 档位、动态掩体位置）。
- 客户端在两个快照间按渲染时间戳插值；爆头 / 死亡等瞬发用事件通道。

### 5.4 服务端回滚（lag compensation）[M5]

- 服务器按 tick 保存全体玩家位置历史（环形缓冲，约 267ms / 8 tick 窗口）。
- 开火判定时：将所有潜在受击者回溯至「当前 tick − 攻击者 ½RTT 对应 tick 数」时刻的位置再做射线求交；掩体（含动态掩体）按当前状态判定，不回溯。
- 攻击者 RTT 来源：`ping` 消息携带客户端实测 RTT（服务器侧 `setRtt`）；RTT 上限 500ms、回溯上限 8 tick（≈250ms，防 ping 欺骗拉大回溯窗口）。
- 单测重点：回溯窗口读写边界、延迟命中判定（构造 100ms 前位置与当前视线的命中）。

### 5.5 协议（v1 摘要，类型定义于 `shared/protocol.ts`）

| 方向 | 消息 | 载荷要点 |
|---|---|---|
| C→S | `join` | 昵称 / 客户端版本 |
| C→S | `input` | seq、移动向量、视角增量、按键位（跳/蹲/冲刺/开镜/射击/换弹/切枪） |
| S→C | `welcome` | 玩家 id、地图数据、对局配置、tick 基准 |
| S→C | `snapshot` | 各玩家权威态 + lastSeq + 掩体状态 + 计分 |
| S→C | `event` | shot（弹道表现）/ hit / kill / explode / coverBreak / door / spawn / gameOver |
| C↔S | `lobby` / `leave` | 房间管理、AI 设置（单机建房） |

- 协议 v1.1 变更 [M5]：`PlayerSnap` 增加 `rs`（reserve 备弹）与 `rl`（换弹剩余秒数）字段，快照体积预算复测通过（< 6.2KB 断言维持）；`ping` 消息增加可选 `rtt`（客户端实测回传，用于服务端回滚）；其余消息结构不变。

## 6. 共享库（shared）

- **纯逻辑、零平台依赖**：不 import DOM / Node API，保证浏览器与服务器通用。
- **确定性要求**：移动解算、射线判定只使用地图 JSON + 输入 + 固定 dt，禁止 `Math.random()`（AI 用注入的种子随机源），为将来回放 / 校验留路。
- **单测重点**：AABB 移动解算（转角 / 台阶 / 蹲起卡位）、射线最近命中与穿透忽略、A* 寻路（绕障 / 动态掩体更新）、油桶连锁引爆顺序。

## 7. 服务器设计

- **Room**：一局对战的完整容器（最多 8 席位）。创建时可配 AI 数量 / 难度；真人加入时按策略替换 AI（保持总数 8）。
- **AI Controller**：实现与真人相同的「输入源」接口，每 tick 由行为树产出输入指令，走同一条 `movement/combat` 管线。
- **事件总线（听觉）**：射击 / 爆炸 / 脚步在服务器内以半径广播给 AI 感知模块（不占用网络）。
- **输入校验**：限频（输入 ≤ 60Hz）、视角增量 / 移动向量限幅，防基础作弊。

## 8. 客户端设计

- **Transport 抽象**：`send(msg)` / `onMessage(cb)`，`local.ts`（单机直调）与 `websocket.ts`（联机）同接口，上层无感知。
- **渲染分层**：静态地图（一次性构建 + 合批）/ 动态掩体（独立对象）/ 玩家 / 粒子，控制 draw call。
- **动画 / 特效系统 [M5]**：程序化动画（姿态驱动，无骨骼资源）+ 分层特效（枪口 / 弹道 / 命中 / 环境）；弹孔贴花等长生命周期效果走对象池（32 循环复用）；命中材质由客户端对照地图数据推断，不新增协议字段。
- **画质档 [M5]**：低 / 中 / 高——粒子密度、阴影开关与质量、特效层数（枪口点光 / 弹孔贴花等），纯客户端设置。
- **音频**：WebAudio 合成器 + PannerNode；事件驱动（shot/hit/explode 事件 → 对应音效 + 空间定位）。
- **死亡回放**：kill 事件含击杀者 id，客户端 3s 内将相机挂到击杀者插值态视角。

## 9. 性能与风险

| 风险 | 对策 |
|---|---|
| 快照体积（8 人 × 15Hz） | 全量 JSON 初版即可（估算 < 8KB/s）；超限再增量 / 二进制 |
| 预测回滚抖动 | 表现层误差平滑收敛；移动逻辑保持确定性减少分叉 |
| 动态掩体同步卡顿 | 快照内含门 / 平台位置，客户端插值；事件做即时反馈 |
| AI 寻路开销 | 0.5m 网格 96×96，A* 单次 < 1ms；决策分档节流 |
| 单机内存 | 内嵌 server 与客户端同进程，共享地图数据引用 |
| 回滚窗口被滥用 [M5] | ½RTT 上限 250ms；仅回溯受击者位置，掩体静态不回溯 |
| 开火预测与权威不一致 [M5] | 预测仅视觉层（弹道 / 枪口 / 音效），伤害与弹匣以服务器为准，视觉按 seq 去重 |
