/**
 * @file stats_store_aggregate.c
 * @brief Period and recent-days aggregation over daily statistics records.
 */

#include "stats_internal.h"
#include <string.h>

static void AggregateRecord(const StatsDayRecord* rec, StatsAggregate* out) {
    if (!rec || !out) return;
    out->focus_seconds += rec->focus_seconds;
    out->countdown_completed += rec->countdown_completed;
    out->countup_seconds += rec->countup_seconds;
    out->countup_sessions += rec->countup_sessions;
    out->pomodoro_rounds += rec->pomodoro_rounds;
    out->completed_sessions += rec->completed_sessions;
    out->cancelled_sessions += rec->cancelled_sessions;
    if (rec->longest_session_seconds > out->longest_session_seconds) {
        out->longest_session_seconds = rec->longest_session_seconds;
    }
}

BOOL StatsStore_GetPeriod(StatsPeriod period, StatsAggregate* out) {
    if (!out) return FALSE;
    memset(out, 0, sizeof(*out));
    if (period < STATS_PERIOD_TODAY || period > STATS_PERIOD_TOTAL) {
        return FALSE;
    }

    char today[STATS_DATE_LEN] = {0};
    if (!StatsFormatToday(today, sizeof(today))) return FALSE;

    char monthPrefix[8] = {0};
    memcpy(monthPrefix, today, 7);
    monthPrefix[7] = '\0';

    char weekStart[STATS_DATE_LEN] = {0};
    {
        time_t now = time(NULL);
        struct tm local = {0};
        if (localtime_s(&local, &now) != 0) return FALSE;
        int daysSinceMonday = (local.tm_wday + 6) % 7;
        if (!StatsStore_FormatDateOffset(daysSinceMonday, weekStart,
                                         sizeof(weekStart))) {
            return FALSE;
        }
    }

    int count = StatsStore_GetCount();
    for (int i = 0; i < count; ++i) {
        const StatsDayRecord* rec = StatsStore_GetRecord(i);
        switch (period) {
            case STATS_PERIOD_TODAY:
                if (strcmp(rec->date, today) == 0) AggregateRecord(rec, out);
                break;
            case STATS_PERIOD_WEEK:
                if (strcmp(rec->date, weekStart) >= 0 &&
                    strcmp(rec->date, today) <= 0) {
                    AggregateRecord(rec, out);
                }
                break;
            case STATS_PERIOD_MONTH:
                if (strncmp(rec->date, monthPrefix, 7) == 0) {
                    AggregateRecord(rec, out);
                }
                break;
            case STATS_PERIOD_TOTAL:
            default:
                AggregateRecord(rec, out);
                break;
        }
    }
    return TRUE;
}

int StatsStore_GetRecentFocusDays(int64_t* out, int count) {
    if (!out || count <= 0 || count > 31) return 0;
    for (int i = 0; i < count; ++i) {
        int64_t focus = 0;
        int offset = count - 1 - i; /* oldest first; today has offset 0 */
        char date[STATS_DATE_LEN] = {0};
        if (StatsStore_FormatDateOffset(offset, date, sizeof(date))) {
            const StatsDayRecord* rec = StatsStore_FindDate(date);
            if (rec) focus = rec->focus_seconds;
        }
        out[i] = focus;
    }
    return count;
}