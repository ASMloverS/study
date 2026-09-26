# 公路车迭代设计：齿比与踏频全手动双控（软腾上限）

- 日期：2026-09-27
- 状态：已确认（两轮问答）
- 基线：`docs/superpowers/specs/2026-09-27-cycling-cadence-control-design.md`（修订其 §2/§3 自动变速与地形默认）

## 1. 目标

齿比与踏频目标均为玩家直接控制量（例：锁定 52×11 且踏频目标 120 → 持续有功率输出至 ~71km/h）。取消玩家自动变速与地形默认切换。

## 2. 控制模型

| 键 | 语义 |
|---|---|
| Q / E | 齿比 −1 / +1（变轻/变重，一次性事件，sim 钳 0..11；低速只选档） |
| W/S 或 ↑/↓ | 踏频目标 ±5rpm（一次性事件，sim 钳 60–150，初始 110） |
| 1~4 | 功率档（不变） |
| A/D 或 ←/→ | 横移（不变）；M 静音（不变） |

- 玩家 `RiderState.cog` 与 `cadTarget` 均为持久状态，由一次性事件（`cogDelta`/`cadDelta`）驱动；无地形状态、无自动变速、无偏移清零。
- AI 完全不变：自动变速目标恒 95rpm，不受软腾规则影响（比赛平衡不动）。

## 3. 软腾上限（soft-pedal）

```
actualCadence = speed / (ratio × 2.096) × 60          # 恒为推导值
玩家且 actualCadence ≥ cadTarget → effort = 0（滑行：无推进、无消耗）
否则 effort = targetPower(功率档)                      # 消耗跟 effort（既有模型）
power = effort × cadenceEfficiency(actualCadence)      # 效率曲线 [80,125] 不变
```

- 语义：踏频目标=腿部转速上限。低于目标全力踩；达到目标软腾滑行（下坡/齿比过轻/功率过剩时生效）。
- 过渡在目标 ±~2rpm 内形成不可见极限环（每 tick 加速 ~1.7rpm），确定性不受影响。

## 4. 数值预检

- 52×11 @120rpm = 19.8 m/s（71 km/h）；52×16 @110rpm = 12.5 m/s（默认巡航功率 300W 平路平衡 10.95 m/s ≈ 96rpm < 110，默认体验不受软腾影响）
- 下坡默认 52×16：超过 12.5 m/s 即软腾滑行（省体力）；升档+提高目标可继续踩
- 不换挡的 reckless（cog 6 恒定）平路被 110rpm 上限压在 12.5 m/s——"会换挡"重新成为速度上限管理维度

## 5. 影响面与测试

- `types`：RiderCommand + `cogDelta`；RiderState `cadOffset`/`cadTerrain` → `cadTarget`
- `input`：+Q/E 事件；`race`：玩家 cog/cadTarget 手动路径替换自动变速与地形块；软腾乘入 effort
- `game.ts`：HUD target 读 `cadTarget`；`index.html` 帮助行更新
- 测试：input +Q/E 用例；race 删地形/自动变速用例、新增手动齿比钳位与软腾（功率 0、体力不耗）用例；reckless cmd 形态更新（其 <500s 完赛契约保持）

## 6. 验收标准

1. Q/E 即时换档、W/S 即时调目标，HUD 实际/目标联动。
2. 实际踏频达到目标后不再耗体力（HUD 体力条走平）且速度自然衰减；降档/降目标可重新发力。
3. AI 行为与上一迭代完全一致（全场完赛回归不变）。
4. 全部单测通过；确定性保持。
