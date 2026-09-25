# Codcopy

浏览器端 COD 风格第一人称射击游戏：FFA 个人混战（8 人局，真人不足自动补 AI），连杀奖励（UAV / 精准空袭 / 集束炸弹）、手雷 / 闪光战术装备、近战，鲜艳卡通渲染画风（3 阶 toon + 工业波普纯色 + MSAA 4x）的工业仓库地图，含可破坏掩体（木箱 / 爆炸油桶）、感应滑门与升降平台。

- 客户端：TypeScript + Vite + Three.js
- 服务器：Node.js + ws（权威服务器，30Hz tick / 15Hz 快照）
- 单机与联机共用同一套游戏逻辑（单机 = 浏览器内嵌 headless 服务器）

## 环境要求

- Node.js ≥ 20
- 桌面 Chrome / Edge 最新版

## 快速开始

```bash
npm install
npm run dev          # 打开 http://localhost:5173
```

菜单选择「单机」直接开打；操作：WASD 移动、Shift 冲刺、Space 跳跃、Ctrl 蹲/滑铲（冲刺中按蹲）、左键射击、右键开镜、R 换弹、1/2 或滚轮/Q 切枪（双武器 loadout 制，武器库共 7 把，死亡等待界面可换装）、V 近战、G 手雷（按住烹煮）、E 闪光、4/5/6 激活连杀奖励、狙击开镜时 Shift 屏息、Tab 计分板、Esc 暂停菜单（单机真暂停）。

- 支持手柄（Xbox 标准布局，PS 按标识映射；与键鼠自动切换，可调灵敏度/死区/震动）
- 可选辅助瞄准（粘滞+轻吸附；键鼠默认关、手柄默认开，设置可选档位）
- 设置（灵敏度/画质/亮度/敌人描边颜色等）自动保存，刷新不丢

## 联机

方式一（推荐，单进程同端口提供页面 + 联机）：

```bash
npm run serve        # 构建客户端并启动服务器（默认 8080 端口）
```

局域网其他设备访问 `http://<你的IP>:8080`，菜单选「联机」、服务器地址留空即可。**首个加入的真人自动成为房主**：对局中按 Esc 打开「房间设置」可实时调整 AI 数量/难度（立即生效），击杀上限/时长下局生效；房主退出自动移交。

方式二（开发时分启）：

```bash
npm start -w server                # 或 npm run dev -w server（热重载）
npm run dev                        # 另开终端，客户端菜单选「联机」填 ws://localhost:8080
```

服务器环境变量：

| 变量 | 默认 | 说明 |
|---|---|---|
| `PORT` | `8080` | HTTP / WebSocket 端口 |
| `BOTS` | `7` | 房间初始 AI 数量（真人加入自动替换 AI，离线自动补位） |
| `DIFFICULTY` | `mixed` | AI 难度：`mixed` / `easy` / `normal` / `hard` |
| `KILL_LIMIT` | `30` | 胜利击杀上限（单机菜单也可调） |
| `MATCH_MINUTES` | `10` | 对局时长（分钟，单机菜单也可调） |

PowerShell 示例：`$env:PORT=9000; $env:BOTS=5; npm start -w server`

## 测试与检查

```bash
npm test             # vitest 全部测试（物理/AI/对局/装备/连杀/近战/ws 联机/掩体）
npm run typecheck    # 三个包 tsc --noEmit
npm run build        # typecheck + 客户端生产构建（client/dist）
```

修改地图布局：编辑 `shared/tools/genmap.mjs` 后运行 `node shared/tools/genmap.mjs` 重新生成；调整武器/手感数值见 `shared/src/weapons.ts` 与 `shared/src/constants.ts`。

## 项目结构

```
shared/   协议、常量、武器表、AABB 碰撞/射线、移动解算、导航（A*）、地图数据
server/   Room 权威逻辑、AI（行为树 + 感知）、可破坏/动态掩体、ws 服务器与静态托管
client/   Three.js 渲染、预测与和解、HUD/小地图/音效、Session（单机/联机）
docs/     DESIGN.md（游戏设计）/ ARCHITECTURE.md（技术架构）/ ROADMAP.md（里程碑）
```
