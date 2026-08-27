/**
 * @file stats.h
 * @brief Timer statistics recording and aggregation API.
 *
 * Records focus/active seconds per day for countdown, count-up and Pomodoro
 * modes and exposes period aggregates (today / week / month / total) plus the
 * recent-7-days series used by the statistics dialog.
 */

#ifndef CATIME_STATS_H
#define CATIME_STATS_H

#include <windows.h>
#include <stdint.h>

/* ============================================================================
 * Types
 * ============================================================================ */

/** Timer mode of the currently tracked session. */
typedef enum {
    STATS_MODE_NONE = 0,
    STATS_MODE_COUNTDOWN,
    STATS_MODE_COUNTUP,
    STATS_MODE_POMODORO_WORK,
    STATS_MODE_POMODORO_BREAK
} StatsMode;


/** User-facing time category a session is grouped by. */
typedef enum {
    STATS_CATEGORY_WORK = 0,   /**< Work */
    STATS_CATEGORY_STUDY,      /**< Study */
    STATS_CATEGORY_REST,       /**< Rest */
    STATS_CATEGORY_COUNT
} StatsCategory;

/** Aggregation period for statistics queries. */
typedef enum {
    STATS_PERIOD_TODAY = 0,
    STATS_PERIOD_WEEK,
    STATS_PERIOD_MONTH,
    STATS_PERIOD_TOTAL
} StatsPeriod;

/** Aggregated statistics over one period. All values are in seconds. */
typedef struct {
    int64_t focus_seconds;        /**< Total active seconds (countdown + count-up + Pomodoro work). */
    int64_t work_seconds;         /**< Active seconds grouped into Work. */
    int64_t study_seconds;        /**< Active seconds grouped into Study. */
    int64_t rest_seconds;         /**< Active seconds grouped into Rest. */
    int work_count;               /**< Active sessions grouped into Work. */
    int study_count;              /**< Active sessions grouped into Study. */
    int rest_count;               /**< Active sessions grouped into Rest. */
    int countdown_completed;      /**< Completed countdown sessions. */
    int64_t countup_seconds;      /**< Count-up active seconds. */
    int countup_sessions;         /**< Ended count-up sessions. */
    int pomodoro_rounds;          /**< Completed Pomodoro work phases. */
    int completed_sessions;       /**< Countdown completions + Pomodoro work rounds. */
    int cancelled_sessions;       /**< Ended-but-not-completed counting sessions. */
    int64_t longest_session_seconds; /**< Longest single session active seconds. */
} StatsAggregate;

/* ============================================================================
 * Lifecycle
 * ============================================================================ */

/** Load persisted daily records (retention 365 days). Call after ReadConfig(). */
void Stats_Initialize(void);

/** End the current session, flush to disk and release resources. */
void Stats_Shutdown(void);

/* ============================================================================
 * Recording (called from timer event paths; UI thread only)
 * ============================================================================ */

/**
 * @brief Feed one main-timer tick into the recorder.
 * @param mode      Current timer mode (STATS_MODE_NONE when no timer is active).
 * @param active    TRUE while the timer is actually counting (not paused).
 * @param elapsedSec Current timer-elapsed seconds (countdown/count-up).
 *
 * Lazily starts a session when the mode becomes active, ends it when the mode
 * changes or becomes STATS_MODE_NONE, and accumulates active seconds.
 */
void Stats_OnTimerTick(StatsMode mode, BOOL active, int elapsedSec);


/**
 * @brief Set the category used for countdown/count-up sessions.
 * @param category One of STATS_CATEGORY_WORK / STUDY / REST. Invalid values
 *                 reset to STATS_CATEGORY_WORK. Defaults to Work.
 *
 * Called by the UI when a count-down/count-up session is about to start; the
 * category is captured when the recorder lazily opens the next session.
 */
void Stats_SetCountdownCategory(StatsCategory category);

/**
 * @brief Apply the countdown category just before a countdown session starts.
 * @param seconds Countdown length in seconds.
 *
 * If the UI selected an explicit category (custom countdown dialog), that
 * choice is kept. Otherwise the category is derived from the duration:
 * phases at or above POMODORO_STUDY_MIN_SECONDS become Study, shorter phases
 * become Rest. Called from the countdown start paths.
 */
void Stats_OnCountdownStarting(int seconds);

/** Mark the current countdown session as completed. */
void Stats_OnCountdownCompleted(void);

/**
 * @brief Mark the current Pomodoro phase as completed.
 * @param phaseSeconds Length in seconds of the completed phase. Phases at or
 *                     above POMODORO_STUDY_MIN_SECONDS count as work, shorter
 *                     phases count as break/rest.
 */
void Stats_OnPomodoroPhaseCompleted(int phaseSeconds);

/** End the current session without completion (reset / mode switch / exit). */
void Stats_OnSessionEnded(void);

/** Write pending in-memory changes to disk (throttled internally). */
void Stats_Flush(void);

/* ============================================================================
 * Query
 * ============================================================================ */

/** Get aggregated statistics for a period. Returns FALSE on invalid input. */
BOOL Stats_GetPeriod(StatsPeriod period, StatsAggregate* out);

/**
 * @brief Fill the recent-N-days focus series (oldest first).
 * @param out    Output buffer of at least @p count int64_t values.
 * @param count  Number of trailing days to return (1..31).
 * @return Number of days written, or 0 on invalid input.
 */
int Stats_GetRecentDays(int64_t* out, int count);

#endif /* CATIME_STATS_H */
