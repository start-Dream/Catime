/**
 * @file dialog_stats_internal.h
 * @brief Shared declarations for the statistics dialog implementation.
 */

#ifndef DIALOG_STATS_INTERNAL_H
#define DIALOG_STATS_INTERNAL_H

#include "dialog/dialog_stats.h"
#include "stats/stats.h"
#include <windows.h>

typedef struct {
    StatsPeriod period;
} StatsDialogData;

/** Paint the owner-drawn metrics/chart surface for the statistics dialog. */
void PaintStatsContent(HWND hwndDlg, const DRAWITEMSTRUCT* item);

#endif /* DIALOG_STATS_INTERNAL_H */