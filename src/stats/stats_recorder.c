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

static StatsCategory g_pending_countdown_category = STATS_CATEGORY_WORK;
static BOOL g_countdown_category_explicit = FALSE;

static BOOL StatsModeCountsFocus(StatsMode mode) {
    return mode == STATS_MODE_COUNTDOWN ||
           mode == STATS_MODE_COUNTUP ||
           mode == STATS_MODE_POMODORO_WORK;
}

static StatsCategory StatsCategoryForMode(StatsMode mode) {
    if (mode == STATS_MODE_POMODORO_WORK) return STATS_CATEGORY_STUDY;
    if (mode == STATS_MODE_POMODORO_BREAK) return STATS_CATEGORY_REST;
    return g_pending_countdown_category;
}

void StatsRecorder_SetCountdownCategory(StatsCategory category) {
    g_countdown_category_explicit = TRUE;
    g_pending_countdown_category = (category >= STATS_CATEGORY_WORK &&
                                    category < STATS_CATEGORY_COUNT)
        ? category : STATS_CATEGORY_WORK;
}

void StatsRecorder_OnCountdownStarting(int seconds) {
    if (!g_countdown_category_explicit) {
        g_pending_countdown_category = PomodoroTimeIsStudy(seconds)
            ? STATS_CATEGORY_STUDY
            : STATS_CATEGORY_REST;
    }
    /* Consume an explicit dialog choice so the next countdown re-derives. */
    g_countdown_category_explicit = FALSE;
}

void StatsRecorder_Reset(void) {
    memset(&g_session, 0, sizeof(g_session));
    g_session.last_elapsed_sec = -1;
}

static void BeginSession(StatsMode mode) {
    g_session.open = TRUE;
    g_session.mode = mode;
    g_session.category = StatsCategoryForMode(mode);
    g_session.active_seconds = 0;
    g_session.last_elapsed_sec = -1;
    g_session.planned_seconds = 0;
}

static void FinalizeSession(BOOL completed) {
    if (!g_session.open) return;

    StatsDayRecord* day = StatsStore_EnsureToday();
    int64_t active = g_session.active_seconds;
    if (day && active > 0) {
        switch (g_session.category) {
            case STATS_CATEGORY_WORK:
                day->work_seconds += active;
                day->work_count++;
                break;
            case STATS_CATEGORY_STUDY:
                day->study_seconds += active;
                day->study_count++;
                break;
            case STATS_CATEGORY_REST:
                day->rest_seconds += active;
                day->rest_count++;
                break;
            default:
                break;
        }
    }
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

void StatsRecorder_OnPomodoroPhaseCompleted(int phaseSeconds) {
    StatsMode mode = PomodoroTimeIsStudy(phaseSeconds)
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

