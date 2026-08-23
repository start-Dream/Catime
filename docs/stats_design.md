# Catime 计时统计功能设计方案

> 日期：2026-08-19
> 范围：为 Catime（纯 C / Win32 倒计时·番茄钟）增加计时统计能力
> 目标：给出可落地的模块划分、数据模型、录制时机、UI 与里程碑

---

## 1. 需求概述

自动记录用户使用 **倒计时 / 正计时 / 番茄钟** 产生的计时数据，并按 **今日 / 本周 / 本月 / 累计** 维度展示：

- 专注 / 计时总时长（活跃秒数）
- 倒计时完成次数
- 正计时累计时长与次数
- 番茄钟完成工作轮数与工作时长
- 单次最长专注
- 最近 7 天柱状图（M3）

**v1 不包含**：

- 不统计"软件后台运行时长"（只统计计时器真正在走的活跃时间）
- 不做云端同步 / 多设备
- 不做应用使用分布统计

---

## 2. 关键概念

### 2.1 计时会话（Session）

一次"计时器从开始到结束（完成 / 取消 / 切换）"的过程。

| 字段 | 说明 |
|---|---|
| date | 会话所属日期 YYYY-MM-DD（按开始时间） |
| start_time / end_time | Unix 时间戳 |
| mode | COUNTDOWN / COUNTUP / POMODORO_WORK / POMODORO_BREAK |
| planned_seconds | 计划时长（倒计时 / 番茄钟阶段），正计时为 0 |
| active_seconds | 实际"走表"秒数（排除暂停、排除系统挂起） |
| status | COMPLETED / CANCELLED / ONGOING |

### 2.2 活跃时长（active_seconds）定义

- 仅在计时器**非暂停**且**非"显示当前时间"模式**下累计；
- 系统睡眠期间不计入（沿用现有 `Timer_OnSystemSuspend / Timer_OnSystemResume` 的基线修正）；
- 暂停（`CLOCK_IS_PAUSED`）期间不计入。

### 2.3 每日聚合（DailyAggregate）

按天汇总，供快速展示与查询：

| 键 | 含义 |
|---|---|
| FOCUS_SECONDS | 当日总活跃秒（倒计时 + 正计时 + 番茄钟工作） |
| COUNTDOWN_SECONDS / COUNTDOWN_COMPLETED | 倒计时活跃秒 / 完成次数 |
| COUNTUP_SECONDS / COUNTUP_SESSIONS | 正计时累计秒 / 次数 |
| POMODORO_WORK_SECONDS / POMODORO_ROUNDS | 番茄钟工作秒 / 完成工作轮数 |
| COMPLETED_SESSIONS / CANCELLED_SESSIONS | 完成 / 取消会话数 |
| LONGEST_SESSION_SECONDS | 当日单次最长活跃秒 |

---

## 3. 存储方案对比

### 方案 A：`stats.ini` 按天分区（推荐主存储）

- 路径：`%LOCALAPPDATA%\Catime\stats.ini`（与 config.ini 同目录）
- 格式：`[2026-08-19]` 分区 + 2.3 的键
- 复用现有 `config_ini_api.h` 的 `WriteIniMultipleAtomic`（原子写）与 `CatimeConfigWriteMutex`（跨进程互斥）
- 优点：与项目风格一致、零新依赖、体量有界（一年约 365 个分区，几十 KB）、读取快
- 缺点：丢失逐会话明细；写入频率需节流

### 方案 B：`stats.csv` 逐会话追加日志（可选增强）

- 路径：`%LOCALAPPDATA%\Catime\stats.csv`
- 每个会话完成 / 取消追加一行；崩溃安全（最后一行残缺可丢弃）
- 优点：完整历史、可导出、支持未来"会话明细"视图
- 缺点：文件无限增长（需保留策略，如打开时清理 >365 天）、需要约百行解析器

**结论：v1 以方案 A 为主；M3 若做导出 / 明细视图再叠加方案 B。**

---

## 4. 模块划分（新增文件，CMake 已自动 GLOB `src/*.c`）

```
include/stats/stats.h          // 公共 API 与数据结构
src/stats/stats_internal.h     // 内部状态
src/stats/stats_recorder.c     // 会话开始/结束/累计（SRWLOCK 保护）
src/stats/stats_storage.c      // stats.ini 读写（复用 INI 层 + 跨进程互斥）
src/stats/stats_aggregate.c    // 今日/本周/本月/累计/近7天聚合
src/stats/stats_format.c       // 时长格式化（HH:MM:SS / Xh Ym / X天）
```

公共 API 草案：

```c
/* 会话生命周期 */
void Stats_OnSessionStarted(StatsMode mode, int plannedSeconds);
void Stats_OnSessionCancelled(void);
void Stats_OnSessionCompleted(StatsMode mode, int plannedSeconds);
void Stats_OnPomodoroWorkCompleted(int plannedSeconds);

/* 活跃秒累计（主循环调用，节流在模块内部） */
void Stats_AddActiveSeconds(int deltaSeconds);

/* 查询 */
BOOL Stats_GetToday(StatsDay* out);
BOOL Stats_GetPeriod(StatsPeriod period, StatsDay* out);  /* 今日/本周/本月/累计 */
BOOL Stats_GetRecentDays(StatsDay days[7], int* count);   /* 近 7 天 */

/* 生命周期 */
void Stats_Initialize(void);  /* 启动加载今日 */
void Stats_Flush(void);       /* 定时 + 关键事件 + 退出 */
void Stats_Shutdown(void);
```

---

## 5. 录制时机（Hook 点）

以 **主循环累计 + 关键事件落定** 为原则，避免在十几个入口重复打点：

1. **活跃秒累计（核心）**：`src/timer/timer_events_main.c` 的 `HandleMainTimer` 每 tick 已算出 `currentElapsedSec`，用"与上一秒的差值"累加活跃秒（仅当非暂停、非显示时间模式、计时器在走）。这是唯一可信来源，自动覆盖所有启动路径。
2. **会话开始**：统一在 `SwitchTimerMode` / `ResetTimerWithInterval` 记录"新模式 + 计划秒数"，开启当前会话上下文（覆盖 `StartCountdownWithTime`、`StartCountUp`、`StartPomodoroTimer`、`StartDefaultCountDown`、`HandleQuickCountdown` 等入口）。
3. **完成**：`TimerEvents_HandleCountdownCompletion`（普通倒计时）与 `TimerEvents_HandlePomodoroCompletion`（工作阶段）→ 落定 COMPLETED。
4. **取消 / 切换**：`CmdCountdownReset` / `CmdCountUpReset` / `CmdPomodoroReset` / `RestartCurrentTimer` / 模式切换 / 退出 → 落定 CANCELLED。
5. **暂停**：`TogglePauseTimer` 无需额外打点（暂停时主循环不再累计活跃秒）。
6. **落盘时机**：计时运行期间每 30s 一次 + 每个会话落定 + `WM_DESTROY` / `MainTimer_Cleanup` 时 `Stats_Flush()`；写文件前加 `CatimeConfigWriteMutex`（或独立 `CatimeStatsWriteMutex`）避免多实例竞争。

---

## 6. UI 设计

### 6.1 入口

- 托盘**右键菜单**：在"帮助"子菜单上方新增 **"统计 / Statistics"** 菜单项（新增 `CLOCK_IDM_STATS`，id 分配在 `resource/resource_ui_ids.h` 空闲区，如 5150）。
- 托盘**左键菜单**（计时管理）也可加一项。

### 6.2 对话框

- 复用 `DialogModern` 体系：新增 `resource/stats_dialog.rc`，`DialogInstanceType` 增加 `DIALOG_INSTANCE_STATS`。
- 顶部：时间段切换（今日 / 本周 / 本月 / 累计）。
- 主体：4 个指标（专注时长、完成次数、番茄轮数、单次最长）。
- 底部：近 7 天柱状图（纯 GDI 绘制，风格对齐 `DialogModern_DrawRoundedRect`）。
- 主题：随 `DialogModern_ResolvePalette` 深浅色自动适配。

### 6.3 资源与 i18n

- 新增资源：`resource/stats_dialog.rc`、`resource_dialog_ids.h` 中 `CLOCK_IDD_STATS_DIALOG`。
- 新增字符串键（**需同步 10 个语言文件**，en.ini 为模板，`i18n/validate_languages.js` 校验键序一致）：
  - "Statistics" → 统计
  - "Focus Time" → 专注时长
  - "Completed Sessions" → 完成次数
  - "Pomodoro Rounds" → 番茄轮数
  - "Longest Session" → 单次最长
  - "Today" → 今日 / "This Week" → 本周 / "This Month" → 本月 / "Total" → 累计

---

## 7. 里程碑

- **M1 录制引擎**：stats 模块 + 主循环累计 + 会话落定 + stats.ini 读写 + 退出落盘（无 UI，可用日志 / 单测验证）。
- **M2 统计对话框**：菜单入口 + 只读展示今日 / 周期聚合。
- **M3 增强**：近 7 天柱状图、导出 CSV（方案 B）、托盘 tooltip 显示今日专注时长、可选"取消会话不计入"开关。

---

## 8. 风险与注意

1. **多实例**：Catime 支持多开（独立多实例窗口）。两实例写同一 stats.ini 会互相覆盖 → 复用 / 新增跨进程 Mutex 包住写操作，读取以"最近写入"为准。
2. **写放大**：主循环 20ms tick，绝不能每 tick 写盘 → 内存累计 + 30s / 事件 / 退出落盘。
3. **状态机耦合**：不要新增大量全局状态；会话上下文集中在 stats 模块内部，Hook 只放主循环 + 完成 + 切换几个点。
4. **与 config watcher 隔离**：stats.ini 为独立文件，不会被 config watcher 当作配置热加载；统计数据不要混入 config.ini（避免写放大与配置冲突）。
5. **系统挂起**：睡眠期间不计活跃秒；沿用现有 `Timer_OnSystemSuspend/Resume` 基线修正，主循环差值法天然处理。
6. **数据迁移 / 首启**：stats.ini 不存在即视为全 0，无需迁移。
7. **时长精度**：按"秒"粒度统计（与 `elapsed_time` 一致），避免毫秒级写放大。

---

## 9. 已确认的决策（2026-08-19）

- **Q1**：取消 / 重置的会话，已累计的活跃秒**计入**"专注时长"，但**不计入**"完成次数"（单独记入 CANCELLED_SESSIONS）。
- **Q2**：正计时会话在 **重置 / 切换模式 / 退出** 时结束（无自然完成）。
- **Q3**：统计数据**保留 365 天**（启动加载时清理更早记录）。
- **Q4**：**不做导出 / 会话明细**，v1 只做方案 A（stats.ini 按天分区聚合）。

## 10. 实现状态（M1 + M2 已完成编码）

- 新增模块：`include/stats/stats.h`、`src/stats/stats_store.c`、`src/stats/stats_store_io.c`、`src/stats/stats_store_aggregate.c`、`src/stats/stats_recorder.c`、`src/stats/stats.c`；对话框拆分为 `src/dialog/dialog_stats.c` 与 `src/dialog/dialog_stats_paint.c`（满足项目单文件 300 行限制）。
- Hook 点：
  - 主循环 `HandleMainTimer`（`src/timer/timer_events_main.c`）按 tick 累计活跃秒并懒启动/切换会话；
  - 倒计时完成 `TimerEvents_HandleCountdownCompletion`；
  - 番茄阶段完成 `TimerEvents_HandlePomodoroCompletion`（偶索引=工作轮，奇索引=休息，休息不计专注）；
  - 重置命令 `CmdCountdownReset` / `CmdCountUpReset` / `CmdPomodoroReset` 与 `RestartCurrentTimer` 结束会话；
  - `CleanupResources`（退出）结束会话并落盘。
- 存储：`%LOCALAPPDATA%\Catime\stats.ini`（与 config.ini 同目录），原子写 + 跨进程互斥 `CatimeStatsWriteMutex`，脏标记 + 30s/会话结束/退出落盘。
- UI：托盘左/右键菜单新增"统计"；`IDD_STATS_DIALOG`（780）无模式对话框，复用 DialogModern 现代外观，今日/本周/本月/累计切换、4 指标卡、近 7 天柱状图（owner-drawn 内容区）；i18n 已同步 10 语言。

