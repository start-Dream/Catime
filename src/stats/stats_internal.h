/**
 * @file stats_internal.h
 * @brief Internal shared declarations for the statistics module.
 */

#ifndef CATIME_STATS_INTERNAL_H
#define CATIME_STATS_INTERNAL_H

#include "stats/stats.h"
#include "pomodoro.h"
#include <time.h>

#define STATS_DATE_LEN 11        /* "YYYY-MM-DD" + NUL */
#define STATS_MAX_DAYS 366       /* today + 365 days retention */
#define STATS_RETENTION_DAYS 365
#define STATS_FLUSH_INTERVAL_MS 30000
#define STATS_MAX_TICK_DELTA_SEC 5  /* guard against implausible per-tick jumps */

/** Per-day aggregate record persisted in stats.ini. */
typedef struct {
    char date[STATS_DATE_LEN];
    int64_t focus_seconds;
    int64_t work_seconds;
    int64_t study_seconds;
    int64_t rest_seconds;
    int work_count;
    int study_count;
    int rest_count;
    int64_t countdown_seconds;
    int countdown_completed;
    int64_t countup_seconds;
    int countup_sessions;
    int64_t pomodoro_work_seconds;
    int pomodoro_rounds;
    int completed_sessions;
    int cancelled_sessions;
    int64_t longest_session_seconds;
} StatsDayRecord;

/** In-memory session being tracked. */
typedef struct {
    BOOL open;
    StatsMode mode;
    StatsCategory category;
    int64_t active_seconds;
    int last_elapsed_sec;
    int planned_seconds;
} StatsSession;

/* store.c */
BOOL StatsStore_GetPath(char* out, size_t outSize);
void StatsStore_Load(const char* path);
BOOL StatsStore_Save(const char* path);
StatsDayRecord* StatsStore_EnsureToday(void);
StatsDayRecord* StatsStore_EnsureDate(const char* date);
StatsDayRecord* StatsStore_FindDate(const char* date);
void StatsStore_Reset(void);
int StatsStore_GetCount(void);
const StatsDayRecord* StatsStore_GetRecord(int index);
void StatsStore_Prune(void);
BOOL StatsStore_FormatDateOffset(int offsetDays, char* out, size_t outSize);
BOOL StatsStore_GetPeriod(StatsPeriod period, StatsAggregate* out);
int StatsStore_GetRecentFocusDays(int64_t* out, int count);

/* recorder.c */
void StatsRecorder_Reset(void);
void StatsRecorder_OnTimerTick(StatsMode mode, BOOL active, int elapsedSec);
void StatsRecorder_SetCountdownCategory(StatsCategory category);
void StatsRecorder_OnCountdownStarting(int seconds);
void StatsRecorder_OnCountdownCompleted(void);
void StatsRecorder_OnPomodoroPhaseCompleted(int phaseSeconds);
void StatsRecorder_OnSessionEnded(void);

/* shared helper: format today's date into buffer */
BOOL StatsFormatToday(char* out, size_t outSize);

#endif /* CATIME_STATS_INTERNAL_H */

