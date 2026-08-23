/**
 * @file dialog_stats.h
 * @brief Timer statistics dialog.
 */

#ifndef DIALOG_STATS_H
#define DIALOG_STATS_H

#include <windows.h>

/** Show the modeless statistics dialog (singleton per app instance). */
void ShowStatsDialog(HWND hwndParent);

#endif /* DIALOG_STATS_H */
