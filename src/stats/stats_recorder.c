/**
 * @file stats_recorder.c
 * @brief Session tracking and per-day aggregation for timer statistics.
 *
 * A "session" is one continuous timer run. It is started lazily by the first
 * active main-timer tick, stays open across pauses (paused time is not
 * accumulated), and is finalized when the timer completes, is reset, switches
 * mode, or the application exits.
 */

#include "stats_internal.h"
#include <string.h>

static StatsSession g_session = {0};

static BOOL StatsModeCountsFocus(StatsMode mode) {
    return mode == STATS_MODE_COUNTDOWN ||
           mode == STATS_MODE_COUNTUP ||
           mode == STATS_MODE_POMODORO_WORK;
}

void StatsRecorder_Reset(void) {
    memset(&g_session, 0, sizeof(g_session));
    g_session.last_elapsed_sec = -1;
}

static void BeginSession(StatsMode mode) {
    g_session.open = TRUE;
    g_session.mode = mode;
    g_session.active_seconds = 0;
    g_session.last_elapsed_sec = -1;
    g_session.planned_seconds = 0;
}

static void FinalizeSession(BOOL completed) {
    if (!g_session.open) return;

    StatsDayRecord* day = StatsStore_EnsureToday();
    int64_t active = g_session.active_seconds;
    if (day && StatsModeCountsFocus(g_session.mode)) {
        switch (g_session.mode) {
            case STATS_MODE_COUNTDOWN:
                day->focus_seconds += active;
                day->countdown_seconds += active;
                if (completed) {
                    day->countdown_completed++;
                    day->completed_sessions++;
                } else if (active > 0) {
                    day->cancelled_sessions++;
                }
                break;
            case STATS_MODE_COUNTUP:
                day->focus_seconds += active;
                day->countup_seconds += active;
                if (active > 0) {
                    day->countup_sessions++;
                }
                break;
            case STATS_MODE_POMODORO_WORK:
                day->focus_seconds += active;
                day->pomodoro_work_seconds += active;
                if (completed) {
                    day->pomodoro_rounds++;
                    day->completed_sessions++;
                } else if (active > 0) {
                    day->cancelled_sessions++;
                }
                break;
            default:
                break;
        }
        if (active > day->longest_session_seconds) {
            day->longest_session_seconds = active;
        }
    }

    g_session.open = FALSE;
    g_session.active_seconds = 0;
    g_session.last_elapsed_sec = -1;
    g_session.planned_seconds = 0;
}

void StatsRecorder_OnTimerTick(StatsMode mode, BOOL active, int elapsedSec) {
    if (mode == STATS_MODE_NONE) {
        FinalizeSession(FALSE);
        return;
    }
    if (g_session.open && g_session.mode != mode) {
        FinalizeSession(FALSE);
    }
    if (!active) {
        g_session.last_elapsed_sec = -1;
        return;
    }
    if (!g_session.open) {
        BeginSession(mode);
    }
    if (g_session.mode != mode) {
        /* Session was just (re)started; establish the baseline only. */
        g_session.last_elapsed_sec = elapsedSec;
        return;
    }
    if (!StatsModeCountsFocus(g_session.mode)) {
        /* Break phases are tracked but never counted as focus time. */
        g_session.last_elapsed_sec = elapsedSec;
        return;
    }
    if (g_session.last_elapsed_sec >= 0 &&
        elapsedSec >= g_session.last_elapsed_sec) {
        int delta = elapsedSec - g_session.last_elapsed_sec;
        if (delta > 0 && delta <= STATS_MAX_TICK_DELTA_SEC) {
            g_session.active_seconds += delta;
        }
    }
    g_session.last_elapsed_sec = elapsedSec;
}

void StatsRecorder_OnCountdownCompleted(void) {
    FinalizeSession(TRUE);
}

void StatsRecorder_OnPomodoroPhaseCompleted(int completedIndex) {
    StatsMode mode = (completedIndex >= 0 && (completedIndex % 2) == 0)
        ? STATS_MODE_POMODORO_WORK
        : STATS_MODE_POMODORO_BREAK;
    if (!g_session.open || g_session.mode != mode) {
        FinalizeSession(FALSE);
        BeginSession(mode);
    }
    FinalizeSession(TRUE);
}

void StatsRecorder_OnSessionEnded(void) {
    FinalizeSession(FALSE);
}

