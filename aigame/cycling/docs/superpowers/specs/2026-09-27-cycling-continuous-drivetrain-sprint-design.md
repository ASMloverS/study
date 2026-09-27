# 公路车迭代设计：齿比/踏频连续化 + 冲刺踏频无上限

- 日期：2026-09-27
- 状态：已确认（brainstorming 产出，两轮问答）
- 基线：`docs/superpowers/specs/2026-09-27-cycling-manual-drivetrain-design.md`（手动 12 档齿比 + 踏频目标双控）

## 1. 目标

1. 飞轮齿比从 12 个离散档改为**连续齿数 [10, 36]T 任意切换**。
2. 踏频目标范围改为 **[80, 150] 任意切换**（下限 60→80，步进 ±5 不变）。
3. **顶级冲刺（功率档 4，gear 3）激活期间踏频无上限**：目标钳位解除 + 效率曲线全程无条件满效。

## 2. 控制模型

| 键 | 语义 |
|---|---|
| Q / E | 齿比变轻 / 变重：单击 ±1T，按住连发 6T/s（无延迟）；sim 钳 [10, 36] |
| W/S 或 ↑/↓ | 踏频目标 ±5rpm（单击），按住连发 30rpm/s；sim 钳 [80, 150]，冲刺档时上限 ∞ |
| 1~4 | 功率档（不变）；A/D 或 ←/→ 横移（不变）；M 静音（不变） |

- 输入模型从纯一次性事件改为**脉冲 + 持续积分**：keydown（!repeat）记脉冲并入 held 集合；`command()` 消费时按 held × 距上次消费时长积分（performance.now() 差，单次上限 100ms 防切页跳变）。
- 单击极短按键 ≈ 脉冲 ±1T + 微量积分（约 1–1.5T），体验可接受；跨全程 36→10 约 4.3s，踏频 80→150 约 2.3s。
- `RiderCommand.cogDelta`/`cadDelta` 语义放宽为任意浮点增量（脉冲 + 积分之和），sim 侧统一钳位。确定性：sim 只要求同 command 序列同结果，输入层时间源在边界外。

## 3. 传动模型（连续齿数）

- `RiderState.cog` 语义从飞轮索引改为**齿数浮点**，默认 16（对应原索引 6 的 52×16）。
- `gearRatio(cog) = 52 / cog`；`cadence(speed, cog) = speed × 60 × cog / (52 × wheelCirc)`（随齿数单调增）。
- Q=变轻（齿数 +），E=变重（齿数 −），与既有"索引 +1 变重"的方向语义保持一致。

## 4. 冲刺踏频无上限

激活条件：**玩家功率档 4（cmd.gear === 3）**。AI 的 gear 3 不触发。

- **目标钳位解除**：冲刺期间 `cadTarget` 上限 150 → ∞（下限 80 不变）；调节公式 `clamp(cadTarget + cadDelta, 80, gear===3 ? Infinity : max(150, cadTarget))`。
- **切出冲刺不回钳**：cadTarget > 150 的值保留；非冲刺期间调节上限为 `max(150, 当前值)`——上调不会意外回落，下调平滑递减，回到 ≤150 后恢复正常钳位。
- **效率全程无条件满效**：冲刺期间 `cadenceEfficiency(cad, sprint=true)` 恒返 1（任何踏频，含 <80 与 >150）。
- **soft-pedal 不变**：实际踏频 ≥ cadTarget 仍滑行（effort=0）——下坡持续输出需主动调高目标，作为玩法深度保留。
- 非冲刺（档 1–3）效率曲线完全不变：满效 [80,125]，线性衰减 50–80 / 125–150 → 0.55，≤50 / ≥150 钳 0.55。

## 5. AI 不变

- 目标恒 95rpm、功率档策略、软腾规则均不动。
- `aiShift` 从 12 档遍历改为**解析解**：理想齿数 `= clamp(target × 52 × wheelCirc / (speed × 60), 10, 36)`，speed 近 0（<0.5 m/s）保持当前齿；滞回 3rpm 保留防抖。行为与 12 档遍历近似等价（全场比赛成绩会有细微漂移，回归实测记录）。

## 6. 数值预检

- 52×10 顶档：@120rpm = 78 km/h，@150rpm = 98 km/h，@180rpm = 118 km/h（风阻自然兜底：平路 750W 平衡速度约 54 km/h）。
- `aiShift` 解析解换算：10.95 m/s @95 → 15.76T（滞回保持 16T）；5.4 m/s @95 → 32.0T；20 m/s @95 → 8.6T → clamp 10T。
- 冲刺满效 + 40000 池 + 25%×2 补给：reckless（全程 gear 3）比现行更强，但耗尽后 105W 爬行机制不变，<500s 完赛契约预估仍成立——实测翻转即上报，不盲目调参。

## 7. 影响面与测试

- `params.ts`：删 `cassette`/`defaultCog`；增 `cogMin/cogMax/cogStep(1)/cogHoldRate(6)/cadHoldRate(30)/defaultCogTeeth(16)`；`cadTargetMin 60→80`；效率曲线常量不动。
- `drivetrain.ts`：齿数语义、`cadenceEfficiency(cad, sprint?)`、`aiShift` 解析化。
- `race.ts`：cog 齿数钳位 [10,36]；cadTarget 新钳位与冲刺 ∞；玩家 gear 3 传 sprint 标志；AI 解析换挡。
- `input.ts`：脉冲 + held 积分模型重写。
- `game.ts`/`index.html`：HUD 档位行 `52×${Math.round(cog)} · 实际/目标rpm · 档名`（冲刺目标可 >150）；帮助行更新。
- 测试：drivetrain（齿数语义 + sprint 效率 + 解析 aiShift 数值）、race（cog/cadTarget 钳位、冲刺 >150 可调且切出保留、冲刺 power=effort、reckless <500s 契约、全场完赛区间、确定性）、input（脉冲/积分/steer 不回归）。

## 8. 验收标准

1. Q/E 单击微调、按住快速跨范围；HUD 齿比整数显示且与速度/踏频联动。
2. W/S 在 [80,150] 调目标；档 4 下可调 >150，切出档 4 后目标保留。
3. 档 4 期间任意踏频效率恒 1（HUD 功率不再随高踏频跳水）；非档 4 曲线与现行完全一致。
4. AI 行为与成绩近似现行（回归区间实测记录）；全场完赛、reckless <500s、确定性测试全过。
5. 全部单测通过；`npm run typecheck`、`npm run build` 全绿。
