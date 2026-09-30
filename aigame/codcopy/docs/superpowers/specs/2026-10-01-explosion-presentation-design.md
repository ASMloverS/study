# M17 爆炸演出层升级设计：空袭/集束显眼化

日期：2026-10-01
状态：已与用户确认（方案二）

## 背景与根因

M16 修复引爆高度后，空袭/集束的 blast 事件链路、位置、伤害均已正确（159/159 测试绿），但用户反馈"场景内没有明显显眼和大范围的特效"。核实演出层现状：

- **视觉**（`effects.ts` `explosion()`）：火球 2 个方块粒子峰值 ~5m、**0.35s 即逝**；烟雾 14 颗 ~1.2m、1.3s；碎屑 12 颗。无闪光光源（仅枪口灯）、无冲击波、无地面焦痕、无余烬。
- **镜头**：震动仅 18m 内、幅度 0.45、衰减快（×6dt）。
- **音频**（`audio.ts` `'explode'`）：单次 0.65s 噪声 + 3 个低频砰；3D 距离模型激进（refDistance 4 / rolloff 1.4 / maxDistance 80，30m 外增益 ≈0.1 且低通压至 500Hz）→ 远处近乎无声；**爆炸不进混响总线**（tailBus 仅本地枪声用）；无任何持续声。
- **incoming**：只有呼啸音/红烟/预警圈，无可见飞机。

结论：需要专门的演出层升级，全部程序化实现（保持零音频/模型资源架构）。

## 已确认的决策记录

| 决策点 | 结论 |
|---|---|
| incoming 可见喷气机 | 包含（程序化几何 + 尾迹，与呼啸音同步） |
| 特效共用 vs 专属 | **共用 `explosion()` 全升级**（手雷/油桶连带变强，带 mag 缩放） |
| 持续余烬 | 连杀专属（手雷/油桶不触发，避免满场噪音） |
| 方案 | 二（演出层升级）；方案三（bloom/体积尘/永久焦黑）范围外 |
| 画质档 | 低档去灯/焦痕/降密度；冲击波与喷气机全档保留 |
| 服务端/协议 | 零改动 |

## A. 视觉：`explosion(pos: Vector3, mag = 1)` 共用升级

| 层 | 参数（mag 为倍率） | 说明 |
|---|---|---|
| 白热核心 | size 1.5×mag、grow 5、life 0.5、additive 0xfff6d0 | 峰值 ~5.2×mag（空袭 mag1.6 ≈ 8.4m） |
| 橙红火球 | size 0.8×mag、grow 9、life 0.4、additive 0xff9a2e | 外层辉光 |
| 冲击波环 | 新增环网格池（8 个，RingGeometry），贴地 y+0.15，scale 1→7×mag、opacity 0.85→0、0.45s、additive 橙白 | 全档启用 |
| 烟柱 | 34×density 颗，size 0.5-0.9、上速 2.2-3.6、life 2.6-4.2、grow 2.5、缓升（gravity -0.6） | 蘑菇状烟云 |
| 余烬火星 | 22×density 颗，size 0.06-0.1、随机上抛 5-9、gravity 12、life 0.9-1.6、additive 橙黄 | |
| 碎屑/烟雾底层 | 保留现有 `debris()`（×mag 数量） | |
| 闪光光源 | 专用爆炸灯池 3 个 PointLight（0xffa050、distance 22、intensity 峰值 55×mag、0.4s 指数衰减、y+1.5），中/高档 | 枪口灯池外新增 |
| 地面焦痕 | 大号焦黑圆片（半径 2.4×mag、opacity 0.82→0、12s 线性渐隐、池 12 循环、y+0.02），中/高档 | |
| 粒子池 | 320→1024 ×2（additive/alpha，InstancedMesh 单 draw call） | 防 5-8 连爆耗尽 |

密度 `density` 沿用现有分档（low 0.4 / medium 0.7 / high 1）。

## B. 连杀专属余烬（持续表现核心）

- `fx.aftermath(x, z, durMs = 7000)`：登记发射器 `{x, z, until, nextAt}`（池上限 4，FIFO 淘汰），`fx.update(dt, now)` 每帧驱动：每 ~120ms 在落点 1.2m 半径随机点生成火苗（additive 橙、size 0.25、life 0.5）+ 浓烟（alpha 深灰、size 0.7、上速 1.8、life 3、grow 3）——落弹区 6-8s 持续冒烟起火。
- `audio.burningLoop(x, z, durSec = 7)`：3D 定位（refDistance 6 / rolloff 1.2 / maxDistance 60）低通 180Hz 噪声 loop，gain 0.5 → 0 线性渐弱；每 0.3-0.7s 预调度随机噼啪（snap：highpass 1.8kHz、30ms、gain 0.15）。
- 触发与节流：`main.ts` blast 分支中 `cause === 'airstrike' || 'cluster'` 时以首枚爆点触发，5s 节流窗口（防 5-8 枚各触发一份）。
- 手雷/油桶（mag=1）不触发余烬。

## C. 可见喷气机（incoming）

- `fx.jet(x, z, yaw)`：程序化机型 group（机身 5.6×0.5×0.6 + 双后掠翼 + 尾翼，`toonMat(0x2a2e34)`），y=14，沿航线方向 `(-sin yaw, -cos yaw)` 从 target-70m 至 target+70m 以 90m/s 掠过（全程 ~1.56s，与 `jetFlyby` 音效 1.6s 对齐）；结束移除并 dispose。池 1。
- 尾迹：掠过期间每 ~30ms 于双翼尖生成 alpha 白粒子（size 0.5、life 1.8、grow 2）形成两条烟带。
- 接线：`main.ts` `streakUse` 分支（tier 2/3）调用；全画质档启用（单 mesh 成本可忽略）。
- 航迹数学抽为纯函数 `jetPath(x, z, yaw, tSec)` 便于单测。

## D. 音效升级

- **爆炸分层**（`emit` `'explode'` case 接收 mag）：
  - 裂响 `snap(2200Hz, 0.05s, 0.3×mag)`
  - 轰鸣 `burst(850Hz→, 0.75s, 0.6×mag)`
  - 低频 `thump(52→~35Hz, 0.9s, 0.55×mag)` + `thump(90Hz, 0.3s, 0.3×mag)`（延迟 0.05s）
  - **接入混响总线**：轰鸣层额外 send 至 `tailBus`（gain 0.5×mag）→ 1.8s 卷积滚雷尾
- **远距可闻**：`playAt` 对 `'explode'` 使用专属距离模型——refDistance 10 / rolloff 1.0 / maxDistance 200 / 低通 `max(240, min(8000, 8000 - dist×55))`（全图可闻沉闷轰鸣）。
- `playAt(kind, x, y, z, mag = 1)` 增加可选 mag；手雷/油桶调用传 1。
- 主通道压缩机（-12dB / ratio 6）已存在，多爆叠加防削波 ✓。

## E. 屏幕反馈与 mag 接线（main.ts blast 分支）

- **mag 取值**：`cause === 'airstrike' || 'cluster'` → `mag = 1.6`（视觉 + 音效 + 余烬触发）；`'grenade' | 'barrel'` → `mag = 1`（无余烬）。`fx.explosion(p, mag)` 与 `audio.playAt('explode', x, y, z, mag)` 同源传参。
- 震动：连杀爆炸半径 18→45m、幅度 0.45→0.75（`shake = max(shake, 0.75×(1-dist/45))`）；衰减维持全局 ×6dt；手雷/油桶维持现状。
- 近距闪光：新增 `#blastflash` overlay（index.html + style.css：橙白 radial、`mix-blend-mode: screen`、pointer-events none），25m 内强度 `clamp(1-dist/28)×0.9`，120ms 渐隐；由 `hud.blastFlash(a)` 驱动。

## F. 画质档

| 档 | 爆炸灯 | 焦痕 | 粒子密度 | 冲击波/喷气机 |
|---|---|---|---|---|
| low | ✗ | ✗ | 0.4 | ✓ |
| medium | ✓（池 3） | ✓ | 0.7 | ✓ |
| high | ✓ | ✓ | 1.0 | ✓ |

## G. 改动面与测试

**改动文件**：`client/src/render/effects.ts`（主体：explosion 重制/环池/灯池/焦痕池/aftermath/jet）、`client/src/audio.ts`（explode 分层/专属距离模型/burningLoop）、`client/src/main.ts`（接线：mag 传参/余烬节流/喷气机/震动/blastFlash）、`client/src/hud.ts`（blastFlash 方法）、`client/index.html` + `client/src/style.css`（#blastflash）。服务端与协议零改动。

**测试**（vitest node 环境，沿用 viewmodel/audio-params 纯逻辑模式）：
- `explosion(pos, mag)` 粒子数量区间断言 + mag 缩放断言（additive 最大初始 size ≈ 1.5×mag）
- 冲击波环/焦痕池循环复用断言
- `aftermath` 发射器登记/到期清理（`update(dt, now)` 注入 now）
- `jetPath(x, z, yaw, t)` 三个采样点断言（起点后方 70m / 半程过顶 / 终点前方 70m）
- 燃烧声节流窗口纯函数断言
- audio-params 增补爆炸分层参数完整性
- 交付门槛：`npm run typecheck` + `npx vitest run` + `npm run build -w client` 全绿 + 手动试玩清单（低/中/高档各过一遍：空袭线爆、集束散布、手雷对比、远距可闻、余烬持续、喷气机掠过）

## 范围外

- 服务端与协议改动
- bloom 泵动 / 体积尘 / 永久地形焦黑（方案三）
- bot 呼叫空袭的演出
- 手雷/油桶的持续余烬与专属轰鸣（共用升级仅即时演出层）
