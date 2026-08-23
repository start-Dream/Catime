/**
 * @file dialog_stats.c
 * @brief Modeless statistics dialog with period switching and a 7-day chart.
 *
 * The shared DialogModern subsystem renders the dialog chrome (title, surface,
 * close button). The metrics and the chart are painted by
 * dialog_stats_paint.c into the owner-drawn static control IDC_STATS_CONTENT.
 */

#include "dialog_stats_internal.h"
#include "dialog/dialog_common.h"
#include "dialog/dialog_modern.h"
#include "dialog/dialog_registry.h"
#include "language.h"
#include "../resource/resource.h"
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#define STATS_REFRESH_TIMER_ID 1
#define STATS_REFRESH_INTERVAL_MS 1000

static const wchar_t* StatsPeriodLabelKey(StatsPeriod period) {
    switch (period) {
        case STATS_PERIOD_WEEK:
            return L"This Week";
        case STATS_PERIOD_MONTH:
            return L"This Month";
        case STATS_PERIOD_TOTAL:
            return L"Total";
        case STATS_PERIOD_TODAY:
        default:
            return L"Today";
    }
}

static StatsPeriod StatsPeriodNext(StatsPeriod period) {
    return (StatsPeriod)(((int)period + 1) % (STATS_PERIOD_TOTAL + 1));
}

static StatsPeriod StatsPeriodPrev(StatsPeriod period) {
    int value = (int)period - 1;
    if (value < 0) value = STATS_PERIOD_TOTAL;
    return (StatsPeriod)value;
}

static void UpdatePeriodButton(HWND hwndDlg, StatsPeriod period) {
    wchar_t label[96];
    _snwprintf_s(label, _countof(label), _TRUNCATE, L"%s  \x25B8",
                 GetLocalizedString(NULL, StatsPeriodLabelKey(period)));
    SetDlgItemTextW(hwndDlg, IDC_STATS_PERIOD, label);
}

static void RefreshContent(HWND hwndDlg) {
    HWND content = GetDlgItem(hwndDlg, IDC_STATS_CONTENT);
    if (content) InvalidateRect(content, NULL, TRUE);
}

static void CleanupStatsDialog(HWND hwndDlg) {
    KillTimer(hwndDlg, STATS_REFRESH_TIMER_ID);
    StatsDialogData* data = (StatsDialogData*)
        GetWindowLongPtrW(hwndDlg, GWLP_USERDATA);
    if (data) {
        SetWindowLongPtrW(hwndDlg, GWLP_USERDATA, 0);
        free(data);
    }
    Dialog_UnregisterInstanceForWindow(DIALOG_INSTANCE_STATS, hwndDlg);
}

static void CloseStatsDialog(HWND hwndDlg) {
    CleanupStatsDialog(hwndDlg);
    DestroyWindow(hwndDlg);
}

INT_PTR CALLBACK StatsDialogProc(HWND hwndDlg, UINT msg, WPARAM wParam,
                                 LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG: {
            Dialog_InitializeInstance(DIALOG_INSTANCE_STATS, hwndDlg);
            StatsDialogData* data = (StatsDialogData*)calloc(1, sizeof(*data));
            if (!data) {
                Dialog_UnregisterInstanceForWindow(DIALOG_INSTANCE_STATS,
                                                   hwndDlg);
                DestroyWindow(hwndDlg);
                return TRUE;
            }
            data->period = STATS_PERIOD_TODAY;
            SetWindowLongPtrW(hwndDlg, GWLP_USERDATA, (LONG_PTR)data);

            SetWindowTextW(hwndDlg,
                           GetLocalizedString(NULL, L"Statistics"));
            UpdatePeriodButton(hwndDlg, data->period);
            Dialog_CenterOnPrimaryScreen(hwndDlg);
            SetTimer(hwndDlg, STATS_REFRESH_TIMER_ID,
                     STATS_REFRESH_INTERVAL_MS, NULL);
            return FALSE;
        }

        case WM_TIMER:
            if (wParam == STATS_REFRESH_TIMER_ID) {
                RefreshContent(hwndDlg);
                return TRUE;
            }
            break;

        case WM_DRAWITEM: {
            const DRAWITEMSTRUCT* item = (const DRAWITEMSTRUCT*)lParam;
            if (item && item->CtlType == ODT_STATIC &&
                item->CtlID == IDC_STATS_CONTENT) {
                PaintStatsContent(hwndDlg, item);
                return TRUE;
            }
            break;
        }

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_STATS_PERIOD: {
                    StatsDialogData* data = (StatsDialogData*)
                        GetWindowLongPtrW(hwndDlg, GWLP_USERDATA);
                    if (data) {
                        data->period = StatsPeriodNext(data->period);
                        UpdatePeriodButton(hwndDlg, data->period);
                        RefreshContent(hwndDlg);
                    }
                    return TRUE;
                }
                case IDOK:
                case IDCANCEL:
                    CloseStatsDialog(hwndDlg);
                    return TRUE;
            }
            break;

        case WM_KEYDOWN:
            if (wParam == VK_LEFT || wParam == VK_RIGHT) {
                StatsDialogData* data = (StatsDialogData*)
                    GetWindowLongPtrW(hwndDlg, GWLP_USERDATA);
                if (data) {
                    data->period = (wParam == VK_RIGHT)
                        ? StatsPeriodNext(data->period)
                        : StatsPeriodPrev(data->period);
                    UpdatePeriodButton(hwndDlg, data->period);
                    RefreshContent(hwndDlg);
                }
                return TRUE;
            }
            break;

        case WM_CLOSE:
            CloseStatsDialog(hwndDlg);
            return TRUE;

        case WM_DESTROY:
            CleanupStatsDialog(hwndDlg);
            return TRUE;
    }
    return FALSE;
}

void ShowStatsDialog(HWND hwndParent) {
    if (Dialog_IsOpen(DIALOG_INSTANCE_STATS)) {
        HWND existing = Dialog_GetInstance(DIALOG_INSTANCE_STATS);
        if (existing) {
            SetForegroundWindow(existing);
        }
        return;
    }

    HWND hwndDlg = CreateDialogW(
        GetModuleHandle(NULL),
        MAKEINTRESOURCE(IDD_STATS_DIALOG),
        hwndParent,
        StatsDialogProc);
    if (hwndDlg) {
        DialogModern_ShowPaintedWindow(hwndDlg, SW_SHOW);
        SetForegroundWindow(hwndDlg);
    }
}