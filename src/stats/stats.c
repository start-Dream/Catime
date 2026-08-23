/**
 * @file stats.c
 * @brief Public statistics API: lifecycle, recording wrappers, and queries.
 *
 * All functions are expected to run on the UI thread (timer events, menu
 * commands, and the statistics dialog all execute there), so no locking is
 * required. Persistence is throttled to at most one write per 30 seconds
 * unless a session is finalized or the application shuts down.
 */

#include "stats_internal.h"
#include "log.h"
#include <string.h>

static BOOL g_initialized = FALSE;
static BOOL g_dirty = FALSE;
static ULONGLONG g_last_flush_tick = 0;

static ULONGLONG NowTickMs(void) {
    return GetTickCount64();
}

static void MarkDirty(void) {
    g_dirty = TRUE;
}

void Stats_Initialize(void) {
    if (g_initialized) return;
    g_initialized = TRUE;

    char path[MAX_PATH] = {0};
    if (!StatsStore_GetPath(path, sizeof(path))) {
        LOG_WARNING("Stats: could not resolve stats file path");
        return;
    }
    StatsStore_Load(path);
    StatsRecorder_Reset();
    g_dirty = FALSE;
    LOG_INFO("Stats: loaded daily statistics from %s", path);
}

void Stats_Flush(void) {
    if (!g_initialized || !g_dirty) return;
    char path[MAX_PATH] = {0};
    if (!StatsStore_GetPath(path, sizeof(path))) return;
    if (StatsStore_Save(path)) {
        g_dirty = FALSE;
        g_last_flush_tick = NowTickMs();
    }
}

void Stats_Shutdown(void) {
    if (!g_initialized) return;
    /* Public wrapper marks dirty and flushes, so a still-open session's
     * accumulated seconds survive application exit. */
    Stats_OnSessionEnded();
    StatsRecorder_Reset();
    g_initialized = FALSE;
}

void Stats_OnTimerTick(StatsMode mode, BOOL active, int elapsedSec) {
    if (!g_initialized) return;
    StatsRecorder_OnTimerTick(mode, active, elapsedSec);

    /* Periodic durability: write at most every 30 seconds while ticking. */
    ULONGLONG now = NowTickMs();
    if (g_dirty && now - g_last_flush_tick >= STATS_FLUSH_INTERVAL_MS) {
        Stats_Flush();
    }
}

void Stats_OnCountdownCompleted(void) {
    if (!g_initialized) return;
    MarkDirty();
    StatsRecorder_OnCountdownCompleted();
    Stats_Flush();
}

void Stats_OnPomodoroPhaseCompleted(int completedIndex) {
    if (!g_initialized) return;
    MarkDirty();
    StatsRecorder_OnPomodoroPhaseCompleted(completedIndex);
    Stats_Flush();
}

void Stats_OnSessionEnded(void) {
    if (!g_initialized) return;
    MarkDirty();
    StatsRecorder_OnSessionEnded();
    Stats_Flush();
}
BOOL Stats_GetPeriod(StatsPeriod period, StatsAggregate* out) {
    if (!g_initialized) return FALSE;
    return StatsStore_GetPeriod(period, out);
}

int Stats_GetRecentDays(int64_t* out, int count) {
    if (!g_initialized) return 0;
    return StatsStore_GetRecentFocusDays(out, count);
}

