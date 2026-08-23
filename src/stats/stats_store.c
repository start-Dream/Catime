/**
 * @file stats_store.c
 * @brief In-memory daily statistics records: dates, lookup, and retention.
 *
 * stats.ini lives next to config.ini and stores one section per calendar day.
 * Records are kept in a small sorted array (<= 366 days) so the recorder,
 * the INI writer, and the aggregation layer can share one source of truth.
 */

#include "stats_internal.h"
#include "config.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STATS_FILE_NAME "stats.ini"

static StatsDayRecord g_records[STATS_MAX_DAYS];
static int g_record_count = 0;

/* ============================================================================
 * Path helper
 * ============================================================================ */

BOOL StatsStore_GetPath(char* out, size_t outSize) {
    if (!out || outSize == 0) return FALSE;
    out[0] = '\0';
    char configPath[MAX_PATH] = {0};
    GetConfigPath(configPath, sizeof(configPath));
    if (!configPath[0]) return FALSE;

    char* slash = strrchr(configPath, '\\');
    if (!slash) slash = strrchr(configPath, '/');
    size_t dirLen = slash ? (size_t)(slash - configPath)
                          : strlen(configPath);
    if (dirLen + sizeof(STATS_FILE_NAME) + 1 > outSize) return FALSE;
    if (dirLen > 0) {
        memcpy(out, configPath, dirLen);
        out[dirLen] = '\\';
        out[dirLen + 1] = '\0';
    }
    strncat(out, STATS_FILE_NAME, outSize - strlen(out) - 1);
    return out[0] != '\0';
}

/* ============================================================================
 * Date helpers
 * ============================================================================ */

BOOL StatsFormatToday(char* out, size_t outSize) {
    if (!out || outSize < STATS_DATE_LEN) return FALSE;
    time_t now = time(NULL);
    struct tm local = {0};
    if (localtime_s(&local, &now) != 0) return FALSE;
    snprintf(out, outSize, "%04d-%02d-%02d",
             local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
    return out[0] != '\0';
}

BOOL StatsStore_FormatDateOffset(int offsetDays, char* out, size_t outSize) {
    if (!out || outSize < STATS_DATE_LEN) return FALSE;
    time_t now = time(NULL);
    time_t shifted = now - (time_t)offsetDays * 86400;
    struct tm local = {0};
    if (localtime_s(&local, &shifted) != 0) return FALSE;
    snprintf(out, outSize, "%04d-%02d-%02d",
             local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
    return out[0] != '\0';
}

static BOOL IsValidDateString(const char* date) {
    if (!date || strlen(date) != 10) return FALSE;
    for (int i = 0; i < 10; ++i) {
        if (i == 4 || i == 7) {
            if (date[i] != '-') return FALSE;
        } else if (date[i] < '0' || date[i] > '9') {
            return FALSE;
        }
    }
    return TRUE;
}

/* ============================================================================
 * Record management
 * ============================================================================ */

static int CompareDate(const void* left, const void* right) {
    return strcmp(((const StatsDayRecord*)left)->date,
                  ((const StatsDayRecord*)right)->date);
}

StatsDayRecord* StatsStore_FindDate(const char* date) {
    if (!IsValidDateString(date) || g_record_count <= 0) return NULL;
    StatsDayRecord key = {0};
    strncpy(key.date, date, sizeof(key.date) - 1);
    key.date[sizeof(key.date) - 1] = '\0';
    return (StatsDayRecord*)bsearch(
        &key, g_records, (size_t)g_record_count,
        sizeof(StatsDayRecord), CompareDate);
}

StatsDayRecord* StatsStore_EnsureDate(const char* date) {
    if (!IsValidDateString(date)) return NULL;
    StatsDayRecord* existing = StatsStore_FindDate(date);
    if (existing) return existing;
    if (g_record_count >= STATS_MAX_DAYS) return NULL;

    StatsDayRecord record = {0};
    strncpy(record.date, date, sizeof(record.date) - 1);
    record.date[sizeof(record.date) - 1] = '\0';
    g_records[g_record_count++] = record;
    qsort(g_records, (size_t)g_record_count,
          sizeof(StatsDayRecord), CompareDate);
    return StatsStore_FindDate(date);
}

StatsDayRecord* StatsStore_EnsureToday(void) {
    char today[STATS_DATE_LEN] = {0};
    if (!StatsFormatToday(today, sizeof(today))) return NULL;
    return StatsStore_EnsureDate(today);
}

void StatsStore_Reset(void) {
    g_record_count = 0;
    memset(g_records, 0, sizeof(g_records));
}

int StatsStore_GetCount(void) {
    return g_record_count;
}

const StatsDayRecord* StatsStore_GetRecord(int index) {
    if (index < 0 || index >= g_record_count) return NULL;
    return &g_records[index];
}

/* ============================================================================
 * Retention
 * ============================================================================ */

void StatsStore_Prune(void) {
    char cutoff[STATS_DATE_LEN] = {0};
    if (!StatsStore_FormatDateOffset(STATS_RETENTION_DAYS,
                                     cutoff, sizeof(cutoff))) {
        return;
    }
    int keep = 0;
    for (int i = 0; i < g_record_count; ++i) {
        if (strcmp(g_records[i].date, cutoff) >= 0) {
            if (keep != i) g_records[keep] = g_records[i];
            keep++;
        }
    }
    g_record_count = keep;
}