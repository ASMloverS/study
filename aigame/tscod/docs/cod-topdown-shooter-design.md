# COD 俯视角射击游戏 — 完整设计文档

> 项目代号：**TSCOD (Top-Down Shadow COD)**
> 日期：2026-03-31
> 状态：已批准

---

## 一、项目概览

| 维度 | 决策 |
|------|------|
| 游戏类型 | 俯视角（Top-Down）射击游戏，**纯 90° 顶视角** |
| 核心体验 | 单人剧情战役为主，参考 COD: Modern Warfare 系列 |
| 美术风格 | 16px 现代高清像素风（类似 Hotline Miami 品质，非复古 NES 风格） |
| 项目规模 | 中型：10 个关卡，12+ 武器，7 种敌人 |
| 架构 | 自建微型 ECS（Entity-Component-System） |
| 运行平台 | 浏览器（Web） |
| 弹道模型 | **Hitscan（即时射线）为主**，狙击枪/火箭筒等特殊武器使用弹丸飞行 |
| 生命恢复 | COD 式「呼吸回血」：受伤后 5 秒未再受击自动恢复，无医疗包 |
| 目标帧率 | 60 FPS（最低 30 FPS 可玩） |
| 目标分辨率 | 最低 1280×720，推荐 1920×1080，支持窗口/全屏切换 |
| 语言 | 中文为主，数据结构预留 i18n 扩展 |

---

## 二、技术栈

| 层级 | 技术选型 | 说明 |
|------|----------|------|
| 语言 | TypeScript 5.x (strict mode) | 全项目严格类型 |
| 构建 | Vite 5.x | HMR 快速迭代 |
| 渲染 | PixiJS 8.x (WebGL2) | 高性能 2D 渲染 |
| 物理/碰撞 | Matter.js | 子弹碰撞 + 爆炸范围 + 掩体穿透 |
| 地图 | Tiled Map Editor → JSON 导出 | 可视化地图编辑 |
| ECS | 自建微型 ECS（~300 行核心） | 避免外部依赖，完全可控 |
| 音频 | Howler.js | 音效 + 音乐 + 空间音频 |
| 输入 | 键鼠 + 手柄（Gamepad API） | 支持按键重映射 |
| 测试 | Vitest | 单元测试 + 集成测试 |

---

## 三、架构设计

### 3.1 ECS 核心模型

```typescript
type Entity = number;

// 组件 = 纯数据结构（无逻辑），按职责分组
// --- 通用 ---
interface PositionComponent { x: number; y: number; angle: number; }
interface VelocityComponent { vx: number; vy: number; }
interface SpriteComponent { container: PIXI.Container; spriteName: string; animState: string; direction: number; }

// --- 战斗 ---
interface HealthComponent { current: number; max: number; armor: number; regenDelay: number; regenTimer: number; }
interface WeaponComponent { slots: WeaponSlot[]; activeSlot: number; }
interface WeaponSlot { weaponId: string; ammo: number; reserve: number; lastFire: number; }
interface ProjectileComponent { damage: number; speed: number; direction: number; owner: Entity; maxRange: number; traveled: number; }
interface HitboxComponent { width: number; height: number; bodyParts: BodyPart[]; }

// --- AI ---
interface AIStateComponent { state: 'patrol' | 'alert' | 'chase' | 'cover' | 'flank' | 'investigate'; target: Entity | null; difficulty: Difficulty; }
interface PatrolComponent { waypoints: { x: number; y: number }[]; currentWaypoint: number; }
interface PerceptionComponent { lastKnownTargetPos: { x: number; y: number } | null; alertLevel: number; hearingEvents: HearingEvent[]; }
interface CoverUserComponent { currentCover: Entity | null; coverState: 'none' | 'moving_to' | 'in_cover' | 'peeking'; }

// --- 玩家/友军 ---
interface PlayerControlComponent { inputMode: 'keyboard' | 'gamepad'; }
interface AllyComponent { leader: Entity; formationOffset: { x: number; y: number }; commandState: 'follow' | 'hold' | 'advance'; }

// --- 潜行 ---
interface StealthComponent { noiseLevel: number; visibility: number; isCrouching: boolean; }
interface DetectionComponent { detectionLevel: number; maxDetection: number; detecting: Entity | null; }

// --- 环境 ---
interface DestructibleComponent { health: number; destroyedSprite: string; blocksPathWhenIntact: boolean; }
interface InteractiveComponent { type: 'door' | 'switch' | 'explosive' | 'pickup'; state: string; onInteract: string; }
interface TriggerComponent { shape: 'rect' | 'circle'; bounds: number[]; event: string; once: boolean; triggered: boolean; }

// 系统 = 每帧执行的逻辑单元
abstract class System {
  abstract update(world: World, dt: number): void;
}
```

### 3.2 渲染管线

```
游戏相机 (固定俯视 / 纯顶视)
    ├── 地图层 (Tiled 底图 → PixiJS Tilemap)
    ├── 实体层 (角色/敌人/道具 → PixiJS Sprite/Container)
    ├── 特效层 (弹道/爆炸/血迹 → PixiJS ParticleContainer)
    ├── UI 层 (HUD → PixiJS 固定层，不随相机移动)
    └── 过场层 (屏幕遮罩/对话框 → PixiJS Graphics)
```

### 3.3 系统列表

| 系统 | 职责 |
|------|------|
| `InputSystem` | 键鼠 + 手柄输入 → 生成 Intent 事件，支持按键重映射 |
| `MovementSystem` | 处理移动速度/方向更新，读取 VelocityComponent |
| `PhysicsSystem` | Matter.js 集成：角色 vs 墙壁碰撞、物理刚体同步 |
| `AimSystem` | 鼠标/右摇杆 → 武器朝向，计算瞄准角度 |
| `CombatSystem` | 射击决策（能否开火/弹药检查）、伤害判定（命中部位/距离衰减/护甲计算） |
| `BallisticSystem` | 弹道数学：hitscan 射线检测 + 弹丸飞行物理，调用 `weapons/Ballistic.ts` |
| `ProjectileSystem` | 弹丸实体（手雷/RPG/狙击）的飞行更新、碰撞回调、爆炸处理 |
| `AISystem` | 驱动行为树 tick、更新感知数据、执行 AI 决策 |
| `HealthSystem` | 生命值/护甲/呼吸回血计时器/死亡处理 |
| `WeaponSystem` | 武器切换/装弹动画/弹药管理/后坐力 |
| `ParticleSystem` | 爆炸/烟雾/弹壳/枪口火焰特效（对象池管理） |
| `AnimationSystem` | 精灵动画帧切换/8 方向判断/动画状态机 |
| `CameraSystem` | 相机跟随玩家/震动/缩放/边界限制 |
| `MapSystem` | Tiled 地图加载/瓦片按 Chunk 渲染/视口裁剪 |
| `CoverSystem` | 掩体点查询/射线遮挡检测/掩体状态管理 |
| `StealthSystem` | 噪声计算/侦测等级/警报传播/消音武器判定 |
| `TriggerSystem` | 关卡触发器区域检测/脚本事件分发 |
| `ObjectiveSystem` | 任务目标追踪/完成判定/UI 通知 |
| `DialogueSystem` | 对话框/字幕显示/语音触发 |
| `SoundSystem` | 音效播放/音乐淡入淡出/空间音频衰减 |
| `HUDSystem` | 准星/血条/弹药/小地图/任务提示/伤害方向指示 |
| `SceneSystem` | 场景切换/加载过渡/转场动画 |
| `SaveSystem` | IndexedDB 存档/检查点/读档恢复 ECS 快照 |
| `CutsceneSystem` | 过场动画/镜头脚本/屏幕遮罩 |
| `AllySystem` | 友军 AI（简化行为树）/跟随队形/指令响应 |
| `EnvironmentSystem` | 天气（雨/雾）/昼夜光照/环境粒子（灰尘） |
| `TutorialSystem` | 教学引导：高亮区域/强制操作/提示文本/进度门控 |
| `PickupSystem` | 地面武器/弹药拾取/武器替换/交互提示 |
| `BossSystem` | Boss 多阶段管理/弱点标记/特殊攻击模式/血条显示 |

### 3.4 模块依赖规则

单向依赖，无循环引用：

```
core/ ← components/ ← systems/ ← campaign/
                        ↑
              ai/ ←-----┘
              maps/ ←---┘
              weapons/ ←┘
```

上层模块不依赖下层，同层之间通过 `EventManager` 解耦通信。

---

## 四、项目目录结构

```
tscod/
├── public/
│   ├── assets/
│   │   ├── sprites/           # 角色精灵图 (PNG)
│   │   ├── tiles/             # 瓦片贴图 (PNG)
│   │   ├── weapons/           # 武器图标/动画帧
│   │   ├── effects/           # 爆炸/粒子精灵
│   │   ├── ui/                # UI 元素素材
│   │   ├── audio/
│   │   │   ├── sfx/           # 音效 (.mp3/.ogg)
│   │   │   ├── music/         # 背景音乐
│   │   │   └── voice/         # 语音/对白
│   │   └── fonts/             # 像素字体
│   └── maps/
│       ├── mission_01.tmx     # Tiled 地图文件
│       ├── mission_02.tmx
│       └── .../
├── src/
│   ├── core/                  # 核心框架
│   │   ├── ecs/
│   │   │   ├── World.ts       # ECS 世界：实体管理
│   │   │   ├── Entity.ts      # 实体创建/销毁
│   │   │   ├── Component.ts   # 组件注册/查询
│   │   │   ├── System.ts      # 系统基类/调度器
│   │   │   └── Query.ts       # 组件查询（按需匹配）
│   │   ├── Game.ts            # 游戏主循环入口
│   │   ├── Time.ts            # 帧率/时间管理
│   │   ├── Input.ts           # 输入管理（键鼠 + 手柄）
│   │   ├── EventManager.ts    # 全局事件总线
│   │   ├── AssetManager.ts    # 资源预加载/缓存
│   │   ├── SceneManager.ts    # 场景状态机
│   │   └── Config.ts          # 全局常量/配置
│   ├── components/            # 所有组件定义
│   │   ├── Position.ts
│   │   ├── Health.ts
│   │   ├── Weapon.ts
│   │   ├── AIState.ts
│   │   ├── Sprite.ts
│   │   ├── Physics.ts
│   │   ├── Projectile.ts
│   │   ├── Trigger.ts
│   │   └── .../
│   ├── systems/               # 所有系统实现
│   │   ├── InputSystem.ts
│   │   ├── MovementSystem.ts
│   │   ├── CombatSystem.ts
│   │   ├── AISystem.ts
│   │   ├── .../
│   │   └── ui/
│   │       ├── HUDSystem.ts
│   │       └── MenuSystem.ts
│   ├── ai/                    # AI 子系统
│   │   ├── BehaviorTree.ts    # 行为树框架
│   │   ├── nodes/             # 行为树节点
│   │   │   ├── Selector.ts
│   │   │   ├── Sequence.ts
│   │   │   ├── Condition.ts
│   │   │   └── Action.ts
│   │   ├── behaviors/         # 具体 AI 行为
│   │   │   ├── Patrol.ts
│   │   │   ├── Engage.ts
│   │   │   ├── TakeCover.ts
│   │   │   ├── Flank.ts
│   │   │   └── GrenadeThrow.ts
│   │   ├── perception/        # AI 感知系统
│   │   │   ├── Vision.ts      # 视野锥/射线检测
│   │   │   └── Hearing.ts     # 声音感知
│   │   └── NavMesh.ts         # 导航网格/A*寻路
│   ├── maps/                  # 地图相关
│   │   ├── TiledLoader.ts     # TMX/TSX 解析
│   │   ├── MapRenderer.ts     # 瓦片渲染
│   │   ├── CollisionMap.ts    # 碰撞层提取
│   │   └── TriggerZone.ts     # 触发区域
│   ├── weapons/               # 武器定义
│   │   ├── WeaponRegistry.ts  # 武器注册表
│   │   ├── Ballistic.ts       # 弹道计算
│   │   └── data/              # 武器数据
│   │       ├── assault_rifles.ts
│   │       ├── smgs.ts
│   │       ├── shotguns.ts
│   │       ├── snipers.ts
│   │       └── explosives.ts
│   ├── campaign/              # 剧情战役
│   │   ├── StoryManager.ts    # 剧情进度管理
│   │   ├── DialogueManager.ts # 对话系统
│   │   ├── ObjectiveManager.ts# 任务目标
│   │   ├── CutsceneEngine.ts  # 过场动画引擎
│   │   ├── ScriptEngine.ts    # 关卡脚本（触发器/事件）
│   │   └── missions/          # 关卡脚本定义
│   │       ├── mission_01.ts
│   │       ├── mission_02.ts
│   │       └── .../
│   ├── ui/                    # UI 组件
│   │   ├── HUD.ts             # 战斗 HUD
│   │   ├── MainMenu.ts        # 主菜单
│   │   ├── PauseMenu.ts       # 暂停菜单
│   │   ├── LoadoutScreen.ts   # 装备选择界面
│   │   ├── BriefingScreen.ts  # 任务简报
│   │   └── DeathScreen.ts     # 阵亡/重生界面
│   ├── audio/                 # 音频管理
│   │   ├── AudioSystem.ts     # 音效/音乐控制
│   │   └── SpatialAudio.ts    # 空间音效
│   ├── save/                  # 存档系统
│   │   ├── SaveManager.ts
│   │   └── Checkpoint.ts
│   └── utils/                 # 工具函数
│       ├── math.ts            # 向量/矩阵/随机
│       ├── pool.ts            # 对象池（子弹/粒子）
│       ├── debug.ts           # 调试工具
│       └── constants.ts       # 全局常量
├── tools/                     # 开发工具
│   ├── sprite-packer/         # 精灵图打包脚本
│   └── map-validator/         # 地图数据验证
├── index.html
├── vite.config.ts
├── tsconfig.json
└── package.json
```

---

## 五、关卡设计与剧情

### 5.1 故事背景：「暗影行动 - Shadow Protocol」

**世界观：** 近未来（2028 年），虚构的现代战争背景。玩家扮演特种部队「幽灵小队」成员 Cpt. Alex Novak，在全球多个热点地区执行反恐任务。

**叙事风格：** 类似 COD: Modern Warfare 系列的紧张电影化叙事，穿插任务简报、战场对话和过场动画。

### 5.2 关卡列表

| # | 任务代号 | 场景 | 核心玩法 | 预估时长 |
|---|---------|------|---------|---------|
| 1 | **First Light** | 军事基地训练场 | 教学关：移动/射击/换弹/掩体 | 20min |
| 2 | **Desert Storm** | 中东城镇废墟 | 室内外混合战斗，学习 AI 掩体行为 | 30min |
| 3 | **Black Market** | 东欧港口仓库 | 潜行 + 突袭交替，暗杀/潜行机制 | 35min |
| 4 | **Nightfall** | 雨夜城市街道 | 夜视仪/闪光弹，低可见度战斗 | 35min |
| 5 | **Intercept** | 移动的货运火车 | 线性推进，限时突入，动态场景 | 30min |
| 6 | **Compound** | 恐怖分子山区营地 | 大型开放区域，多路线进攻 | 40min |
| 7 | **Extraction** | 使馆围攻防守 | 防守波次战，保护 VIP，塔防元素 | 35min |
| 8 | **Infiltration** | 敌方地下实验室 | 潜行为主，激光陷阱，警报机制 | 35min |
| 9 | **Endgame** | 核设施控制中心 | 时间压力，解谜 + 战斗混合 | 40min |
| 10 | **Final Stand** | 最终 Boss 战 - 武装直升机 | 史诗级 Boss 战，多阶段 | 30min |

**总游戏时长：约 5-8 小时。**

### 5.3 关卡脚本系统

每个关卡是一个 TypeScript 模块，通过 `ScriptEngine` 定义触发器和事件：

```typescript
// missions/mission_02.ts
export default defineMission({
  map: 'desert_town',
  playerSpawn: { x: 100, y: 800 },
  loadout: ['m4a1', 'beretta', 'frag_x2'],

  objectives: [
    { type: 'reach', target: 'checkpoint_alpha', description: '到达集合点' },
    { type: 'eliminate', count: 15, description: '清除城镇守军' },
    { type: 'destroy', targets: ['ammo_cache_1', 'ammo_cache_2'], description: '摧毁弹药库' },
    { type: 'extract', zone: 'helipad', description: '撤离到直升机' },
  ],

  triggers: [
    { zone: 'alley_entrance', event: 'spawnAmbush', once: true },
    { zone: 'building_roof', event: 'sniperReveal', once: true },
    { event: 'allCachesDestroyed', action: 'triggerReinforcements' },
  ],

  spawnWaves: [
    { trigger: 'mission_start', enemies: ['soldier_x5', 'rpg_x1'], delay: 0 },
    { trigger: 'checkpoint_alpha', enemies: ['soldier_x8', 'shotgun_x2'], delay: 3000 },
    { trigger: 'triggerReinforcements', enemies: ['heavy_x2', 'soldier_x6'], delay: 5000 },
  ],
});
```

---

## 六、地图设计

### 6.1 Tiled 集成策略

**地图分层（从下到上）：**

| 层名 | 类型 | 用途 |
|------|------|------|
| `terrain` | Tile Layer | 地面纹理（沙地/水泥/草地） |
| `decoration` | Tile Layer | 地面装饰（血迹/弹孔/碎片） |
| `collision` | Object Layer | 碰撞多边形（墙壁/掩体/不可通行区） |
| `cover` | Object Layer | 掩体标记（AI 用于判断遮蔽位置） |
| `spawn` | Object Layer | 实体出生点（玩家/敌人/道具） |
| `triggers` | Object Layer | 触发区域（事件/脚本/音效） |
| `navigation` | Object Layer | 导航网格（AI 寻路用） |
| `structures` | Tile Layer | 建筑/墙壁上层（可遮挡/半透明） |

### 6.2 地图尺寸与规格

- 基础瓦片尺寸：**16×16 像素**
- 地图尺寸：**200×150 到 320×240 瓦片**（3200×2400 到 5120×3840 像素世界坐标）
- 相机视口：根据屏幕自适应，固定 3x 像素缩放（1080p 下视口约 640×360 游戏像素）
- 视口裁剪：仅渲染相机视口 ±1 屏范围内的瓦片和实体

### 6.3 关键地图特性

1. **视线遮挡（Fog of War）：** 墙壁/建筑遮挡玩家视野，使用射线检测 + 渲染遮罩
2. **可破坏环境：** 部分掩体可被爆炸摧毁（预设破坏状态）
3. **垂直层次：** 建筑内部/屋顶可通过楼梯/梯子切换楼层（逻辑层切换）
4. **掩体系统：** Tiled 中标记掩体点和朝向，AI 和玩家共用

### 6.4 瓦片集规划

```
tilesets/
├── terrain/          # 地面：沙/水泥/草地/泥土/水
├── walls/            # 墙壁：混凝土/砖/金属/玻璃窗
├── props/            # 道具：箱子/桶/车辆/路障
├── cover_objects/    # 掩体：沙袋/低墙/车辆残骸
├── interiors/        # 室内：桌椅/电脑/管道
├── destruction/      # 破坏状态：破损墙/碎玻璃/弹孔
└── special/          # 特殊：火车/直升机/爆炸物
```

---

## 七、战斗与武器系统

### 7.1 武器分类（12+ 种）

| 类别 | 武器 | 伤害 | 射速 | 弹匣 | 特性 |
|------|------|------|------|------|------|
| 突击步枪 | M4A1, AK-47 | 中 | 高 | 30 | 全自动，中距离 |
| 冲锋枪 | MP5, P90 | 低 | 极高 | 25/50 | 高射速，近战优势 |
| 霰弹枪 | SPAS-12 | 高(近距离) | 低 | 8 | 散射，近距离毁灭 |
| 狙击枪 | Intervention | 极高 | 极低 | 5 | 远距离，穿透 |
| 手枪 | Beretta, Deagle | 低/中 | 中 | 15/7 | 副武器 |
| 投掷物 | 破片手雷/闪光弹/烟雾弹 | - | - | - | AOE 效果 |
| 重武器 | RPG-7 | 极高 | 极低 | 1 | 爆炸范围伤害 |

### 7.2 武器数据定义

```typescript
// weapons/data/assault_rifles.ts
export const M4A1: WeaponDef = {
  id: 'm4a1',
  name: 'M4A1 Carbine',
  type: 'assault_rifle',
  slot: 'primary',
  damage: 35,
  fireRate: 700,          // RPM
  reloadTime: 2200,       // ms
  magSize: 30,
  maxAmmo: 210,           // 备弹
  range: 800,             // 像素
  spread: 3,              // 度
  recoil: 2.5,
  penetration: 0.3,       // 穿墙概率
  moveSpeedMultiplier: 0.85,
  switchTime: 400,
  sounds: { fire: 'm4_fire', reload: 'm4_reload', empty: 'weapon_empty' },
  sprite: 'weapons/m4a1',
  muzzleFlash: 'effects/muzzle_rifle',
};
```

### 7.3 伤害模型

- **基础伤害 × 距离衰减 × 命中部位倍率**（头 2.0 / 身 1.0 / 腿 0.7）
- **护甲系统**：护甲吸收 50% 伤害直到耗尽
- **穿透**：部分武器可穿透薄墙/掩体，伤害递减
- **爆炸**：中心点全伤害，边缘线性衰减

---

## 八、AI 系统

### 8.1 架构：行为树 + 感知系统 + 导航网格

```
AIController (每帧执行)
    ├── PerceptionSystem (感知更新)
    │   ├── Vision: 120° 锥形视野, 400px 距离, 射线遮挡检测
    │   └── Hearing: 声音事件范围感知 (射击声 600px, 脚步 200px)
    ├── BehaviorTree (决策)
    │   ├── Selector (优先级选择)
    │   │   ├── Sequence: 【受重伤】→ 撤退找掩护
    │   │   ├── Sequence: 【发现敌人】→ 选择战术
    │   │   │   ├── 【近距离】→ 冲锋/霰弹枪
    │   │   │   ├── 【有掩体】→ 进入掩体射击
    │   │   │   ├── 【侧翼可绕】→ 包抄
    │   │   │   └── 【无可掩护】→ 后退射击
    │   │   ├── Sequence: 【听到声音】→ 调查
    │   │   ├── Sequence: 【巡逻路径】→ 沿路巡逻
    │   │   └── Action: 待机
    │   └──
    └── NavMesh (寻路)
        └── A* 算法 + 局部避障
```

### 8.2 敌人类型与行为配置

| 类型 | 血量 | 武器 | AI 特点 |
|------|------|------|---------|
| 普通士兵 | 100 | 步枪/冲锋枪 | 基础巡逻-发现-射击-掩护循环 |
| 精锐士兵 | 150 | 步枪 + 手雷 | 会包抄/投弹/协同进攻 |
| 狙击手 | 80 | 狙击枪 | 远距离架枪，视野广，不主动冲锋 |
| 重装兵 | 300 | 轻机枪 | 缓慢移动，高伤害，会压制射击 |
| RPG 兵 | 100 | RPG | 远距离爆炸攻击，低射速 |
| 盾兵 | 200+盾 | 手枪 | 正面免疫，需绕后或爆炸 |
| Boss | 500+ | 多武器 | 多阶段行为模式，特殊攻击 |

### 8.3 AI 难度分级

```typescript
enum Difficulty { EASY, REGULAR, HARDENED, VETERAN }

const DIFFICULTY_CONFIG = {
  [Difficulty.EASY]:     { aimAccuracy: 0.3, reactionTime: 1500, aiHealthMul: 0.7, grenades: false },
  [Difficulty.REGULAR]:  { aimAccuracy: 0.5, reactionTime: 800,  aiHealthMul: 1.0, grenades: true },
  [Difficulty.HARDENED]: { aimAccuracy: 0.7, reactionTime: 400,  aiHealthMul: 1.3, grenades: true },
  [Difficulty.VETERAN]:  { aimAccuracy: 0.9, reactionTime: 200,  aiHealthMul: 1.5, grenades: true, flank: true },
};
```

---

## 九、美术规范

### 9.1 像素风格参数

| 参数 | 规格 |
|------|------|
| 瓦片大小 | 16×16 px |
| 角色尺寸 | 16×24 px（站立）/ 20×16 px（卧倒） |
| 武器大小 | 12-24 px（根据武器类型） |
| 动画帧率 | 8-12 FPS（像素风不需要 60fps） |
| 调色板 | 限制 48-64 色主调色板保持一致性 |
| 像素密度 | 内部分辨率放大 2-4x 显示 |

### 9.2 角色精灵图布局

```
每个角色方向: 8方向 (N, NE, E, SE, S, SW, W, NW)
每个方向动画: idle(4帧), walk(6帧), run(6帧), shoot(4帧), reload(6帧), death(8帧), hurt(2帧) = 7 种动画
总帧数: 8方向 × (4+6+6+4+6+8+2) = 8 × 36 = 288帧/角色
```

### 9.3 视觉特效

- **弹道轨迹**：短线条 + 发光末端（3-5 帧消失）
- **枪口闪光**：4 帧闪烁动画
- **爆炸**：16 帧爆炸精灵 + 烟雾粒子 + 屏幕震动
- **血迹**：地面持久贴花，随机变体
- **弹壳**：抛壳粒子，碰撞反弹
- **环境**：雨滴/灰尘/烟雾粒子持续播放

### 9.4 素材来源策略

1. **OpenGameArt.org** — 搜索 "modern military top-down pixel"
2. **itch.io** — 大量免费/付费像素素材包
3. **Kenney.nl** — 高质量免费素材
4. **自建素材** — 使用 Aseprite 绘制关键角色和武器，确保风格统一

---

## 十、UI/UX 设计

### 10.1 界面层级

```
MainMenu
├── New Game → DifficultySelect → BriefingScreen → Gameplay
├── Continue → Gameplay (读档)
├── Loadout → 武器自定义界面
├── Options → 音频/画面/控制/手柄设置
└── Credits

Gameplay (HUD 叠加)
├── HUD (固定层)
│   ├── 准星 (屏幕中心)
│   ├── 血条 + 护甲条 (左下)
│   ├── 武器信息 + 弹药 (右下)
│   ├── 小地图 (右上)
│   ├── 任务目标 (左上)
│   ├── 击杀提示 (屏幕上方居中)
│   └── 手雷数量 (血条旁)
├── DialogueBox (底部弹出)
├── PauseMenu (暂停叠加)
└── DeathScreen → Retry / LoadCheckpoint
```

### 10.2 HUD 设计要点

- **准星**：十字准星，射击时扩散表示精度下降，命中时闪红
- **小地图**：俯视简化地图，显示玩家位置/朝向，已探索区域，敌人红点（被发现时）
- **伤害指示器**：屏幕边缘红色闪光 + 受伤方向
- **击杀反馈**：命中标记（X 图标），击杀图标（头骨），连杀提示

### 10.3 UI 像素风格

- UI 元素使用相同 16px 像素风格
- 按钮和面板使用深色半透明底 + 像素边框
- 字体使用像素字体（如 Press Start 2P 或 VT323）
- 颜色方案：军绿 + 橙色高亮 + 白色文字

---

## 十一、输入系统

### 11.1 支持的输入方式

| 输入方式 | 映射 |
|---------|------|
| 键盘 + 鼠标 | WASD 移动，鼠标瞄准，左键射击，R 换弹，数字键切武器 |
| 手柄 (Gamepad API) | 左摇杆移动，右摇杆瞄准，RT 射击，X 换弹，LB/RB 切武器 |

### 11.2 可重映射

- 所有按键可在 Options 中重新绑定
- 支持多套预设配置（默认/左撇子/自定义）
- 手柄支持死区调节和震动强度设置

---

## 十二、AI Coding 策略

### 12.1 核心原则

**每个文件 < 200 行，每个系统独立可测。**

### 12.2 开发阶段划分

| 阶段 | 内容 | AI Coding 策略 |
|------|------|---------------|
| **P0: 骨架** | ECS 核心 + 渲染循环 + 基础输入 | 一次性生成，~500 行 |
| **P1: 移动** | 角色显示 + 移动 + 碰撞 | 增量：基于 P0 添加系统 |
| **P2: 射击** | 武器 + 弹道 + 伤害 | 增量：武器数据 + 射击系统 |
| **P3: 基础 AI** | 巡逻 + 追击 + 射击 | 增量：行为树框架 + 基础行为 |
| **P4: 地图** | Tiled 加载 + 碰撞地图 + 掩体 | 独立模块 |
| **P5: 高级 AI** | 掩体/包抄/手雷/感知 | 增量：新行为节点 |
| **P6: UI** | HUD + 菜单 + 任务目标 | 独立模块 |
| **P7: 关卡** | 第一个完整关卡 + 触发器脚本 | 集成测试 |
| **P8: 剧情系统** | 对话/过场/存档 | 独立模块 |
| **P9: 打磨** | 特效/音效/平衡/多关卡 | 逐项完善 |

### 12.3 AI Prompt 设计模式

每个系统的生成遵循统一模板：

```
1. 上下文：提供相关接口定义 + 依赖系统
2. 需求：明确的功能规格
3. 约束：文件大小限制 + 性能要求
4. 验证：附带的测试期望
```

### 12.4 对象池模式（性能关键）

```typescript
// utils/pool.ts - 对象池用于子弹/粒子等高频创建销毁的对象
class ObjectPool<T> {
  private pool: T[] = [];
  constructor(private factory: () => T, initialSize: number) { /* ... */ }
  acquire(): T { /* ... */ }
  release(item: T): void { /* ... */ }
}
```

### 12.5 测试策略

- **单元测试**：Vitest，覆盖所有 System 的纯逻辑（脱离 PixiJS）
- **集成测试**：创建最小 World，验证系统间交互
- **可视化调试**：内置调试面板（F12），显示碰撞盒/AI 状态/帧率

---

## 十三、性能优化策略

| 优化点 | 策略 |
|--------|------|
| 渲染 | 只渲染相机视口内的实体/瓦片（视口裁剪），PixiJS 自带脏矩形优化 |
| 弹道 | 对象池复用弹丸实体，避免 GC；hitscan 武器无弹丸开销 |
| 粒子 | PixiJS ParticleContainer 批量渲染，单次 draw call |
| AI | 分帧更新（每帧只更新 1/N 的 AI），超出感知范围的 AI 休眠 |
| 物理 (Matter.js) | 仅角色/手雷 vs 墙壁碰撞使用 Matter.js 刚体，禁用重力，简化迭代 |
| 游戏碰撞 (Spatial Hash) | hitscan 射线检测、AOE 范围查询、弹丸 vs 敌人批量检测使用自建空间哈希网格，不经过 Matter.js |
| 地图 | 只加载当前关卡地图，瓦片按区块（Chunk）按需渲染 |
| 音频 | 限制同时播放音效数量（最多 16 通道），超出听觉范围不播放 |

---

## 十四、风险与缓解

| 风险 | 影响 | 缓解策略 |
|------|------|---------|
| 像素素材不足 | 视觉不完整 | 先用彩色占位矩形，后期替换正式素材 |
| AI 行为不自然 | 游戏体验差 | 从简单行为开始，逐步增加复杂度 |
| 性能瓶颈 | 帧率下降 | 早期引入对象池和空间哈希，固定 3x 缩放减少渲染量 |
| 项目范围蔓延 | 永远做不完 | 严格按 P0-P9 阶段交付，每阶段可玩 |
| 物理引擎集成问题 | 碰撞不准确 | Matter.js 仅用于角色碰撞，弹道用自建射线检测 |
| Hitscan + Projectile 混合弹道复杂度 | 两套弹道逻辑难以维护 | BallisticSystem 统一封装两种模式，上层系统不感知区别 |
| 友军 AI 卡路/挡路 | 玩家体验差 | 友军不阻挡玩家移动（碰撞层分离），友军自动避让 |
| 潜行系统平衡 | 过于容易或太难 | 侦测参数可配置，通过测试调优，提供难度分级 |
| 关卡 5 滚动地图技术风险 | 实现复杂 | 备选方案：改为固定火车站台关卡 |
| Boss 战手感不佳 | 结尾体验差 | Boss 血量/攻击模式参数化，易于调整 |

---

## 十五、弹道模型与掩体系统

### 15.1 弹道模型

采用 **Hitscan（即时射线）为主** 的混合弹道模型：

| 武器类型 | 弹道模型 | 说明 |
|---------|---------|------|
| 手枪/步枪/冲锋枪/霰弹枪 | Hitscan | 射击瞬间射线检测命中，无飞行时间，带随机散布 |
| 狙击枪 | Hitscan + 穿透 | 射线可穿透薄墙/1 个敌人，伤害衰减 50% |
| RPG-7 | Projectile | 抛物线飞行，速度 400px/s，碰撞爆炸，爆炸半径 120px |
| 手雷 | Projectile（抛物线） | 玩家投掷，3.5 秒延迟引爆（可预烹 2 秒），爆炸半径 100px |
| 闪光弹 | Projectile | 落地 1 秒后引爆，范围内敌人致盲 3-5 秒 |
| 烟雾弹 | Projectile | 落地后释放烟雾区域，持续 15 秒，遮挡 AI 视野 |

**Hitscan 流程：**

```
玩家射击 → WeaponSystem 检查弹药/射速 → CombatSystem 生成射击事件
→ BallisticSystem 执行射线检测（从枪口到最大射程）
→ Spatial Hash 查询射线路径附近实体
→ 射线-AABB 相交测试 → 取最近命中实体
→ CombatSystem 计算伤害（距离衰减 × 部位倍率 × 护甲）
→ 命中反馈（血液粒子 + 击中音效 + HUD 标记）
```

### 15.2 掩体系统

**交互方式：自动遮挡**，玩家无需按键进入掩体。

- 掩体在世界中是带有碰撞盒的实体，射线检测自然被掩体阻挡
- 玩家移动到掩体后方时，敌人的射线无法击中玩家（掩体完全遮挡直线弹道）
- 玩家可从掩体上方/侧面射击（探头），暴露部分身体可被命中
- AI 使用 `CoverUserComponent` 主动寻路到掩体点并隐藏
- **可破坏掩体**（木质箱/栅栏）：累计受到 200 伤害后摧毁，切换为碎片瓦片，移除碰撞体

### 15.3 玩家操作列表

| 操作 | 键鼠 | 手柄 | 说明 |
|------|------|------|------|
| 移动 | WASD | 左摇杆 | 8 方向移动，速度受武器影响 |
| 瞄准 | 鼠标移动 | 右摇杆 | 全方向瞄准，角色自动面向瞄准方向 |
| 射击 | 左键 | RT | 按住连射（自动武器）/ 单发（半自动） |
| 换弹 | R | X | 装弹动画期间无法射击 |
| 冲刺 | Shift | L3 按下 | 1.5x 移动速度，持续 3 秒，冷却 2 秒 |
| 蹲下 | C | B | 降低移动速度 50%，减少散布 30%，降低噪声 |
| 近战 | V / 鼠标中键 | 右摇杆按下 | 近距离 1 击杀（普通敌人），动画锁定 0.5 秒 |
| 投掷手雷 | G | LB | 抛物线投掷，按住 G 可预烹（最长 2 秒） |
| 切换武器 | 1/2/3 / 滚轮 | 方向键上下 | 主武器/副武器/手雷切换 |
| 拾取武器 | E（靠近时） | Y | 拾取地面武器/弹药，替换当前武器槽 |
| 交互 | E | Y | 开门/按开关/拆除炸弹 |
| 指令友军 | Q / F | 方向键左右 | 「跟随/待命/推进」切换 |
| 暂停 | Esc | Start | 打开暂停菜单 |

---

## 十六、潜行系统

关卡 3（Black Market）和关卡 8（Infiltration）涉及潜行玩法。

### 16.1 噪声系统

| 动作 | 噪声半径 | 备注 |
|------|---------|------|
| 走路 | 150px | 默认移动 |
| 跑步 | 300px | 冲刺/跑步 |
| 蹲下移动 | 50px | 几乎无声 |
| 射击（无消音） | 600px | 所有附近敌人感知 |
| 射击（消音） | 200px | 仅近距离感知 |
| 近战击杀 | 100px | 安静 |
| 手雷爆炸 | 全地图 | 所有敌人感知 |
| 开门 | 200px | |

### 16.2 侦测系统

每个敌人拥有 `DetectionComponent`：

```typescript
interface DetectionComponent {
  detectionLevel: number;    // 0-100，达到 100 时进入 alert
  detecting: Entity | null;  // 正在侦测的目标
  decayRate: number;         // 每秒衰减速率（侦测等级自动下降）
}
```

- 敌人视野锥内且无遮挡时，侦测等级以 30/秒 速度上升
- 噪声事件导致侦测等级以 15/秒 速度上升
- 侦测等级到 100：敌人进入 `alert` 状态，呼叫周围 400px 内友军
- 脱离视野后侦测等级以 10/秒 速度衰减
- 完全衰减后敌人进入 `investigate`（调查最后已知位置），然后回到 `patrol`

### 16.3 警报等级

| 警报等级 | 行为 | 重置条件 |
|---------|------|---------|
| 绿色（未发现） | 正常巡逻 | — |
| 黄色（调查中） | 前往噪声源/最后已知位置调查 | 调查无果后 10 秒回到绿色 |
| 红色（战斗中） | 全员进入 chase/cover，呼叫增援 | 所有无敌人在视野外超过 15 秒 |

---

## 十七、友军系统

### 17.1 设计原则

COD 战役的灵魂是团队感。每关至少 1-2 名友军 NPC 陪同作战。

### 17.2 友军行为

友军使用**简化版行为树**（比敌人 AI 简单）：

```
AllyBehaviorTree
├── 【生命值低】→ 蹲下呼叫"我受伤了"（不实际死亡，剧情保护）
├── 【收到推进指令】→ 向玩家准星方向推进到最近掩体
├── 【敌人可见】→ 射击（命中率固定 60%，不抢玩家击杀）
├── 【跟随模式】→ 保持 formationOffset 距离跟随玩家
└── 待机
```

- 友军**不可死亡**（剧情保护），生命值归零时蹲下呼救，5 秒后恢复
- 友军击杀不计入玩家击杀数，但会减少敌人血量辅助玩家
- 玩家可通过 Q/F 键下达简单指令：跟随 / 待命 / 推进

### 17.3 友军剧情功能

- 对话/语音呼喊提供战场氛围
- 在特定剧情节点触发门/爆破（玩家无法自行开启）
- 指引玩家前往目标方向

---

## 十八、Boss 系统

### 18.1 设计原则

Boss 战是节奏变化的亮点，需要与普通战斗不同的机制。

### 18.2 Boss 架构

```typescript
interface BossComponent {
  phases: BossPhase[];
  currentPhase: number;
  phaseTransitionHealth: number[];  // 触发阶段转换的血量阈值
  weakPoints: WeakPoint[];
  isInvulnerable: boolean;
}

interface BossPhase {
  behaviorTree: string;        // 每阶段不同的行为树
  attackPatterns: string[];    // 攻击模式列表
  movementSpeed: number;
  specialAttacks: SpecialAttack[];
}
```

### 18.3 关卡 10 Boss 设计：武装直升机

| 阶段 | 血量范围 | 攻击模式 | 弱点/对策 |
|------|---------|---------|----------|
| 阶段 1 | 100%-60% | 机枪扫射（压制玩家） + 悬停移动 | 射击敞开的侧门舱口 |
| 阶段 2 | 60%-30% | 火箭弹齐射（覆盖大面积） + 飞行路线变化 | 用 RPG 击中尾旋翼 |
| 阶段 3 | 30%-0% | 紧急迫降 + 地面士兵增援 + 最后爆炸 | 集中射击驾驶舱 |

Boss 战场地为开阔停机坪，四周分布掩体（逐步被火箭弹摧毁），形成动态难度。

---

## 十九、动态场景与环境系统

### 19.1 动态关卡机制

**关卡 5（Intercept - 移动火车）实现方案：**

- 地图使用**滚动偏移**：背景层以恒定速度向一个方向滚动，模拟火车移动
- 玩家实际在固定区域内活动，但视觉上火车在移动
- 前方定期生成新车厢段（从地图右侧移入），后方车厢移出视口并回收
- 脱离火车范围（掉落）= 即死，触发检查点重载

### 19.2 环境/天气系统

| 环境效果 | 实现方式 | 影响关卡 |
|---------|---------|---------|
| 雨 | ParticleContainer 粒子（斜向线条），地面添加水坑瓦片 | 关卡 4 Nightfall |
| 雾 | 全屏半透明覆盖层 + 可见距离限制（相机远景模糊） | 关卡 6 Compound（清晨雾） |
| 夜视仪 | 覆盖绿色滤镜，提高亮度，敌人显示高亮轮廓 | 关卡 4 Nightfall |
| 闪电 | 随机全屏白色闪烁 + 短暂全图可见 + 雷声延迟 | 关卡 4 Nightfall |
| 烟雾 | 烟雾弹释放烟雾区域（圆形半透明精灵），阻挡 AI 视野射线 | 所有关卡 |

### 19.3 可破坏环境

```typescript
interface DestructibleComponent {
  health: number;
  maxHealth: number;
  intactSprite: string;
  damagedSprite: string;      // health < 50% 时显示
  destroyedSprite: string;    // health = 0 时显示
  blocksPathWhenIntact: boolean;
  providesCover: boolean;     // 是否作为掩体
  dropTable?: DropEntry[];    // 摧毁后掉落物
}
```

- 爆炸（手雷/RPG）对范围内可破坏物体造成 200 伤害
- 普通子弹对可破坏物体造成 10 伤害
- 破坏时更新碰撞地图，AI 导航网格同步更新

---

## 二十、教学系统

### 20.1 设计原则

关卡 1（First Light）为教学关，采用**渐进式解锁**方式引导玩家。

### 20.2 教学流程

| 步骤 | 教学内容 | 引导方式 |
|------|---------|---------|
| 1 | 移动（WASD） | 屏幕中央大字提示 + 高亮目标点，到达后解锁射击 |
| 2 | 射击（左键） | 生成静态靶标，强制射击 3 发才能继续 |
| 3 | 换弹（R） | 弹匣打空后弹出换弹提示，等待换弹完成 |
| 4 | 冲刺（Shift） | 计时跑酷段，必须冲刺才能在时间内到达 |
| 5 | 掩体 | 敌人开始射击，必须躲到掩体后存活 5 秒 |
| 6 | 投掷手雷（G） | 靶标在掩体后方，必须用手雷摧毁 |
| 7 | 近战（V） | 近距离敌人冲来，教学近战击杀 |
| 8 | 全技能实战 | 解锁全部操作，5 个敌人小规模战斗 |

### 20.3 提示 UI

- **按键图标**：屏幕底部显示当前可用操作的按键/手柄按钮图标
- **高亮区域**：用闪烁半透明框标记目标位置
- **进度门控**：未完成当前步骤时，后续区域的触发器不激活
- 已完成的教学步骤不再显示提示（存档标记）

---

## 二十一、存档系统

### 21.1 存储方案

- 使用 **IndexedDB** 存储（容量大，支持二进制）
- 每个存档槽为一个独立记录

### 21.2 存档内容

```typescript
interface SaveData {
  version: number;
  missionId: string;
  difficulty: Difficulty;
  timestamp: number;
  checkpoint: {
    playerPosition: { x: number; y: number };
    playerHealth: number;
    playerArmor: number;
    playerLoadout: WeaponSlot[];
    objectivesState: ObjectiveState[];
    triggersTriggered: string[];
    enemiesKilled: string[];       // 已击杀的敌人 ID 列表
    destructiblesDestroyed: string[];
  };
  campaignProgress: {
    missionsCompleted: string[];
    totalPlayTime: number;
    totalKills: number;
    totalDeaths: number;
  };
}
```

### 21.3 存档策略

- **自动存档**：每个检查点触发自动存档（关卡脚本中的触发器）
- **手动存档**：暂停菜单中可手动存档（覆盖当前检查点）
- **关卡完成存档**：每关通关后保存进度，解锁下一关
- **最大存档数**：3 个存档槽
- **读档**：恢复 ECS 快照——重新加载地图、重建实体、恢复组件数据

---

## 二十二、加载与显示

### 22.1 资源加载流程

```
Game Launch
├── Phase 1: 核心资源（~500KB）
│   ├── ECS 核心、PixiJS 初始化
│   ├── 主菜单 UI 素材 + 字体
│   └── 显示主菜单
├── Phase 2: 关卡资源（选择关卡后，~2-5MB）
│   ├── 地图 TMX + 瓦片集
│   ├── 该关卡角色精灵图
│   ├── 武器精灵 + 音效
│   └── 显示加载进度条
└── Phase 3: 进入游戏
```

### 22.2 加载界面

- 显示任务简报文本（滚动显示）+ 加载进度条
- 背景使用深色 + 当前关卡概念图（低分辨率）
- 加载完成后按任意键开始（确保玩家准备好）

### 22.3 显示规格

| 参数 | 规格 |
|------|------|
| 目标帧率 | 60 FPS |
| 最低帧率 | 30 FPS（复杂场景降帧保护） |
| 最低分辨率 | 1280×720 |
| 推荐分辨率 | 1920×1080 |
| 全屏/窗口 | 支持切换（F11 或设置菜单） |
| 像素缩放 | 固定 3x（nearest-neighbor 过滤保持像素锐利） |
| 游戏内部分辨率 | 640×360（1080p ÷ 3），逻辑坐标使用游戏像素 |
| 色深 | 32-bit RGBA |

---

## 二十三、关卡间进度管理

### 23.1 武器/装备规则

- **每关脚本指定初始装备**，玩家使用预设武器开始
- **关卡内可拾取**敌人掉落的武器和弹药
- **无跨关传承**：下一关重新使用脚本指定的装备
- **无解锁系统**：所有武器通过关卡脚本控制，不需要经验解锁

### 23.2 难度选择

- 开始新游戏时选择难度（EASY / REGULAR / HARDENED / VETERAN）
- 难度影响 AI 参数（见 8.3），同时影响：
  - 玩家最大生命值（EASY: 150 / REGULAR: 100 / HARDENED: 80 / VETERAN: 60）
  - 检查点密度（EASY 每 2 分钟 / VETERAN 每 5 分钟）
  - 敌人数量倍率（EASY: 0.7x / VETERAN: 1.3x）
- 可在暂停菜单中降低难度（不可提高）

---

## 二十四、小地图系统

### 24.1 数据来源

- 小地图数据从 Tiled 地图的 `terrain` + `collision` 层提取，生成低分辨率缩略图
- 缩略图在关卡加载时一次性生成并缓存

### 24.2 小地图显示

| 元素 | 显示规则 |
|------|---------|
| 地形轮廓 | 始终显示（灰度） |
| 玩家位置 | 白色三角形 + 朝向线 |
| 友军位置 | 蓝色圆点，始终显示 |
| 已发现敌人 | 红色圆点，仅侦测范围内 |
| 未发现敌人 | 不显示 |
| 任务目标 | 金色闪烁图标 |
| 探索迷雾 | 未探索区域用黑色遮罩覆盖 |

### 24.3 小地图规格

- 尺寸：160×120 像素（右上角）
- 比例：1 游戏像素 = 0.05 小地图像素（全地图概览）
- 边框：军绿色像素边框 + 半透明黑底
