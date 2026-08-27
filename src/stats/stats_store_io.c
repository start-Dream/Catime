/**
 * @file stats_store_io.c
 * @brief stats.ini parsing and atomic write-back for daily statistics.
 *
 * The file is rewritten via a temp file + rename under a named cross-process
 * mutex so independent Catime instances cannot corrupt each other's data.
 */

#include "stats_internal.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define STATS_TEMP_SUFFIX ".tmp"
#define STATS_MAX_LINE 256
#define STATS_WRITE_MUTEX_NAME L"CatimeStatsWriteMutex"
#define STATS_WRITE_LOCK_TIMEOUT_MS 2000

static void BuildTempPath(const char* statsPath, char* out, size_t outSize) {
    snprintf(out, outSize, "%s%s", statsPath, STATS_TEMP_SUFFIX);
}

/* ============================================================================
 * Parsing
 * ============================================================================ */

typedef struct {
    const char* key;
    size_t offset;
} StatsFieldMap;

#define STATS_FIELD(Struct, Field) \
    { #Field, offsetof(Struct, Field) }

static const StatsFieldMap STATS_FIELDS[] = {
    STATS_FIELD(StatsDayRecord, focus_seconds),
    STATS_FIELD(StatsDayRecord, work_seconds),
    STATS_FIELD(StatsDayRecord, study_seconds),
    STATS_FIELD(StatsDayRecord, rest_seconds),
    STATS_FIELD(StatsDayRecord, work_count),
    STATS_FIELD(StatsDayRecord, study_count),
    STATS_FIELD(StatsDayRecord, rest_count),
    STATS_FIELD(StatsDayRecord, countdown_seconds),
    STATS_FIELD(StatsDayRecord, countdown_completed),
    STATS_FIELD(StatsDayRecord, countup_seconds),
    STATS_FIELD(StatsDayRecord, countup_sessions),
    STATS_FIELD(StatsDayRecord, pomodoro_work_seconds),
    STATS_FIELD(StatsDayRecord, pomodoro_rounds),
    STATS_FIELD(StatsDayRecord, completed_sessions),
    STATS_FIELD(StatsDayRecord, cancelled_sessions),
    STATS_FIELD(StatsDayRecord, longest_session_seconds),
};

static int64_t ParseInt64Value(const char* value) {
    if (!value || !*value) return 0;
    char* end = NULL;
    long long parsed = strtoll(value, &end, 10);
    if (end == value || parsed < 0) return 0;
    return (int64_t)parsed;
}

static int ParseIntValue(const char* value) {
    if (!value || !*value) return 0;
    char* end = NULL;
    long parsed = strtol(value, &end, 10);
    if (end == value || parsed < 0 || parsed > INT_MAX) return 0;
    return (int)parsed;
}

static void ApplyField(StatsDayRecord* record, const char* key,
                       const char* value) {
    if (!record || !key || !value) return;
    for (size_t i = 0; i < sizeof(STATS_FIELDS) / sizeof(STATS_FIELDS[0]); ++i) {
        if (strcmp(key, STATS_FIELDS[i].key) != 0) continue;
        void* field = (char*)record + STATS_FIELDS[i].offset;
        if (STATS_FIELDS[i].offset == offsetof(StatsDayRecord, focus_seconds) ||
            STATS_FIELDS[i].offset == offsetof(StatsDayRecord, work_seconds) ||
            STATS_FIELDS[i].offset == offsetof(StatsDayRecord, study_seconds) ||
            STATS_FIELDS[i].offset == offsetof(StatsDayRecord, rest_seconds) ||
            STATS_FIELDS[i].offset == offsetof(StatsDayRecord, countdown_seconds) ||
            STATS_FIELDS[i].offset == offsetof(StatsDayRecord, countup_seconds) ||
            STATS_FIELDS[i].offset == offsetof(StatsDayRecord, pomodoro_work_seconds) ||
            STATS_FIELDS[i].offset == offsetof(StatsDayRecord, longest_session_seconds)) {
            *(int64_t*)field = ParseInt64Value(value);
        } else {
            *(int*)field = ParseIntValue(value);
        }
        return;
    }
}

static BOOL ReadLine(char* buffer, size_t bufferSize, FILE* file) {
    return fgets(buffer, (int)bufferSize, file) != NULL;
}

static void LoadFromFile(const char* path) {
    FILE* file = fopen(path, "r");
    if (!file) return;

    char line[STATS_MAX_LINE];
    StatsDayRecord* current = NULL;
    while (ReadLine(line, sizeof(line), file)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0) continue;

        if (line[0] == '[') {
            char* close = strchr(line, ']');
            if (!close) {
                current = NULL;
                continue;
            }
            *close = '\0';
            const char* section = line + 1;
            current = (strlen(section) == 10)
                ? StatsStore_EnsureDate(section) : NULL;
            continue;
        }

        char* equals = strchr(line, '=');
        if (!equals || !current) continue;
        *equals = '\0';
        ApplyField(current, line, equals + 1);
    }
    fclose(file);
}

/* ============================================================================
 * Writing
 * ============================================================================ */

static HANDLE GetStatsWriteMutex(void) {
    static HANDLE s_mutex = NULL;
    if (s_mutex) return s_mutex;
    HANDLE created = CreateMutexW(NULL, FALSE, STATS_WRITE_MUTEX_NAME);
    if (!created) return NULL;
    if (InterlockedCompareExchangePointer(
            (PVOID volatile*)&s_mutex, created, NULL) != NULL) {
        CloseHandle(created);
    }
    return s_mutex;
}

static BOOL WriteRecordsToFile(FILE* file) {
    if (!file) return FALSE;
    for (int i = 0; i < StatsStore_GetCount(); ++i) {
        const StatsDayRecord* rec = StatsStore_GetRecord(i);
        if (fprintf(file, "[%s]\n", rec->date) < 0) return FALSE;
        if (fprintf(file, "FOCUS_SECONDS=%lld\n", (long long)rec->focus_seconds) < 0) return FALSE;
        if (fprintf(file, "WORK_SECONDS=%lld\n", (long long)rec->work_seconds) < 0) return FALSE;
        if (fprintf(file, "STUDY_SECONDS=%lld\n", (long long)rec->study_seconds) < 0) return FALSE;
        if (fprintf(file, "REST_SECONDS=%lld\n", (long long)rec->rest_seconds) < 0) return FALSE;
        if (fprintf(file, "WORK_COUNT=%d\n", rec->work_count) < 0) return FALSE;
        if (fprintf(file, "STUDY_COUNT=%d\n", rec->study_count) < 0) return FALSE;
        if (fprintf(file, "REST_COUNT=%d\n", rec->rest_count) < 0) return FALSE;
        if (fprintf(file, "COUNTDOWN_SECONDS=%lld\n", (long long)rec->countdown_seconds) < 0) return FALSE;
        if (fprintf(file, "COUNTDOWN_COMPLETED=%d\n", rec->countdown_completed) < 0) return FALSE;
        if (fprintf(file, "COUNTUP_SECONDS=%lld\n", (long long)rec->countup_seconds) < 0) return FALSE;
        if (fprintf(file, "COUNTUP_SESSIONS=%d\n", rec->countup_sessions) < 0) return FALSE;
        if (fprintf(file, "POMODORO_WORK_SECONDS=%lld\n", (long long)rec->pomodoro_work_seconds) < 0) return FALSE;
        if (fprintf(file, "POMODORO_ROUNDS=%d\n", rec->pomodoro_rounds) < 0) return FALSE;
        if (fprintf(file, "COMPLETED_SESSIONS=%d\n", rec->completed_sessions) < 0) return FALSE;
        if (fprintf(file, "CANCELLED_SESSIONS=%d\n", rec->cancelled_sessions) < 0) return FALSE;
        if (fprintf(file, "LONGEST_SESSION_SECONDS=%lld\n", (long long)rec->longest_session_seconds) < 0) return FALSE;
    }
    return TRUE;
}

static BOOL ReplaceFileWithTemp(const char* statsPath, const char* tempPath) {
    wchar_t wStats[MAX_PATH] = {0};
    wchar_t wTemp[MAX_PATH] = {0};
    if (MultiByteToWideChar(CP_UTF8, 0, statsPath, -1, wStats, MAX_PATH) <= 0 ||
        MultiByteToWideChar(CP_UTF8, 0, tempPath, -1, wTemp, MAX_PATH) <= 0) {
        return FALSE;
    }
    return MoveFileExW(wTemp, wStats,
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

BOOL StatsStore_Save(const char* path) {
    if (!path || !path[0]) return FALSE;

    HANDLE mutex = GetStatsWriteMutex();
    if (!mutex) return FALSE;
    DWORD waitResult = WaitForSingleObject(mutex, STATS_WRITE_LOCK_TIMEOUT_MS);
    if (waitResult != WAIT_OBJECT_0 && waitResult != WAIT_ABANDONED) {
        LOG_WARNING("Stats: timed out waiting for stats write mutex");
        return FALSE;
    }

    char tempPath[MAX_PATH] = {0};
    BuildTempPath(path, tempPath, sizeof(tempPath));

    FILE* file = fopen(tempPath, "wb");
    BOOL ok = FALSE;
    if (file) {
        ok = WriteRecordsToFile(file);
        if (fclose(file) != 0) ok = FALSE;
    }
    if (ok) ok = ReplaceFileWithTemp(path, tempPath);
    if (!ok) {
        wchar_t wTemp[MAX_PATH] = {0};
        if (MultiByteToWideChar(CP_UTF8, 0, tempPath, -1,
                                wTemp, MAX_PATH) > 0) {
            DeleteFileW(wTemp);
        }
        LOG_WARNING("Stats: failed to write %s", path);
    }

    ReleaseMutex(mutex);
    return ok;
}

void StatsStore_Load(const char* path) {
    StatsStore_Reset();
    if (!path || !path[0]) return;
    LoadFromFile(path);
    StatsStore_Prune();
}
