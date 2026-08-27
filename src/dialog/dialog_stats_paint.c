/**
 * @file dialog_stats_paint.c
 * @brief Owner-drawn painting for the statistics dialog content surface.
 */

#include "dialog_stats_internal.h"
#include "dialog/dialog_modern.h"
#include "language.h"
#include "utils/localized_duration.h"
#include "../resource/resource.h"
#include <time.h>
#include <stdio.h>
#include <wchar.h>

#define STATS_CHART_DAYS 7
#define STATS_MARGIN 16
#define STATS_CARD_GAP 12

typedef struct {
    const wchar_t* labelKey;
    const wchar_t* value;
} StatsMetric;

static void GetDateOffsetLabel(int offsetDays, wchar_t* out, size_t outCount) {
    time_t now = time(NULL);
    time_t shifted = now - (time_t)offsetDays * 86400;
    struct tm local = {0};
    if (localtime_s(&local, &shifted) != 0) {
        out[0] = L'\0';
        return;
    }
    _snwprintf_s(out, outCount, _TRUNCATE, L"%d/%d",
                 local.tm_mon + 1, local.tm_mday);
}

static void PaintMetrics(HDC hdc, const DialogModernPalette* palette,
                         UINT dpi, const RECT* area,
                         const StatsMetric* metrics, int metricCount) {
    int columns = 2;
    int rows = (metricCount + columns - 1) / columns;
    int gap = DialogModern_Scale(dpi, STATS_CARD_GAP);
    int cardWidth = (area->right - area->left - gap) / columns;
    int cardHeight = (area->bottom - area->top - gap) / rows;

    HFONT labelFont = DialogModern_CreateFont(dpi, 11, FW_NORMAL);
    HFONT valueFont = DialogModern_CreateFont(dpi, 20, FW_SEMIBOLD);
    if (!labelFont || !valueFont) {
        if (labelFont) DeleteObject(labelFont);
        if (valueFont) DeleteObject(valueFont);
        return;
    }

    for (int i = 0; i < metricCount; ++i) {
        int col = i % columns;
        int row = i / columns;
        RECT card;
        card.left = area->left + col * (cardWidth + gap);
        card.top = area->top + row * (cardHeight + gap);
        card.right = card.left + cardWidth;
        card.bottom = card.top + cardHeight;

        DialogModern_DrawRoundedRect(hdc, &card,
                                     DialogModern_Scale(dpi, 10),
                                     palette->surface, palette->border, 1);

        RECT labelRect = card;
        labelRect.left += DialogModern_Scale(dpi, 12);
        labelRect.top += DialogModern_Scale(dpi, 10);
        labelRect.right -= DialogModern_Scale(dpi, 8);
        labelRect.bottom = labelRect.top + DialogModern_Scale(dpi, 18);
        DialogModern_DrawText(hdc, labelFont, palette->mutedText,
                              &labelRect,
                              GetLocalizedString(NULL, metrics[i].labelKey),
                              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT valueRect = card;
        valueRect.left += DialogModern_Scale(dpi, 12);
        valueRect.top += DialogModern_Scale(dpi, 30);
        valueRect.right -= DialogModern_Scale(dpi, 8);
        valueRect.bottom -= DialogModern_Scale(dpi, 8);
        DialogModern_DrawText(hdc, valueFont, palette->text,
                              &valueRect, metrics[i].value,
                              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    DeleteObject(labelFont);
    DeleteObject(valueFont);
}

static void PaintChart(HDC hdc, const DialogModernPalette* palette,
                       UINT dpi, const RECT* area) {
    int64_t values[STATS_CHART_DAYS] = {0};
    int count = Stats_GetRecentDays(values, STATS_CHART_DAYS);
    if (count <= 0) return;

    HFONT smallFont = DialogModern_CreateFont(dpi, 10, FW_NORMAL);
    if (!smallFont) return;

    RECT plot = *area;
    plot.top += DialogModern_Scale(dpi, 18);
    plot.bottom -= DialogModern_Scale(dpi, 20);

    int64_t maxValue = 1;
    for (int i = 0; i < count; ++i) {
        if (values[i] > maxValue) maxValue = values[i];
    }

    int gap = DialogModern_Scale(dpi, 6);
    int barWidth = (plot.right - plot.left - gap * (count - 1)) / count;
    if (barWidth < 4) barWidth = 4;
    int plotHeight = plot.bottom - plot.top;
    if (plotHeight < 10) plotHeight = 10;

    for (int i = 0; i < count; ++i) {
        int x = plot.left + i * (barWidth + gap);
        int64_t value = values[i];
        int barHeight = (int)((value * plotHeight) / maxValue);
        if (barHeight > 0 && barHeight < 2) barHeight = 2;

        RECT bar;
        bar.left = x;
        bar.right = x + barWidth;
        bar.bottom = plot.bottom;
        bar.top = plot.bottom - barHeight;
        if (barHeight > 0) {
            DialogModern_DrawRoundedRect(hdc, &bar,
                                         DialogModern_Scale(dpi, 4),
                                         palette->accent, palette->accent, 0);
        }

        wchar_t valueText[48];
        if (value > 0) {
            if (value >= 3600) {
                _snwprintf_s(valueText, _countof(valueText), _TRUNCATE,
                             L"%dh", (int)(value / 3600));
            } else if (value >= 60) {
                _snwprintf_s(valueText, _countof(valueText), _TRUNCATE,
                             L"%dm", (int)(value / 60));
            } else {
                _snwprintf_s(valueText, _countof(valueText), _TRUNCATE,
                             L"%ds", (int)value);
            }
            RECT valueRect;
            valueRect.left = x;
            valueRect.right = x + barWidth;
            valueRect.top = plot.top;
            valueRect.bottom = plot.top + DialogModern_Scale(dpi, 16);
            DialogModern_DrawText(hdc, smallFont, palette->mutedText,
                                  &valueRect, valueText,
                                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        wchar_t dateText[16];
        GetDateOffsetLabel(count - 1 - i, dateText, _countof(dateText));
        RECT dateRect;
        dateRect.left = x;
        dateRect.right = x + barWidth;
        dateRect.top = plot.bottom;
        dateRect.bottom = area->bottom;
        DialogModern_DrawText(hdc, smallFont, palette->mutedText,
                              &dateRect, dateText,
                              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    DeleteObject(smallFont);
}

static void PaintCategoryStrip(HDC hdc, const DialogModernPalette* palette,
                           UINT dpi, const RECT* area, const StatsAggregate* agg) {
    if (!hdc || !area || !agg) return;
    const wchar_t* labelKeys[3] = { L"Work", L"Study", L"Rest" };
    const int64_t values[3] = { agg->work_seconds, agg->study_seconds, agg->rest_seconds };
    const int counts[3] = { agg->work_count, agg->study_count, agg->rest_count };
    int gap = DialogModern_Scale(dpi, 8);
    int cardWidth = (area->right - area->left - gap * 2) / 3;
    int cardHeight = area->bottom - area->top;
    if (cardWidth < 40 || cardHeight < 20) return;

    HFONT labelFont = DialogModern_CreateFont(dpi, 10, FW_NORMAL);
    HFONT valueFont = DialogModern_CreateFont(dpi, 15, FW_SEMIBOLD);
    HFONT countFont = DialogModern_CreateFont(dpi, 9, FW_NORMAL);
    if (!labelFont || !valueFont || !countFont) {
        if (labelFont) DeleteObject(labelFont);
        if (valueFont) DeleteObject(valueFont);
        if (countFont) DeleteObject(countFont);
        return;
    }

    for (int i = 0; i < 3; ++i) {
        RECT card;
        card.left = area->left + i * (cardWidth + gap);
        card.top = area->top;
        card.right = card.left + cardWidth;
        card.bottom = area->bottom;
        DialogModern_DrawRoundedRect(hdc, &card, DialogModern_Scale(dpi, 8),
                                     palette->surface, palette->border, 1);

        RECT labelRect = card;
        labelRect.left += DialogModern_Scale(dpi, 10);
        labelRect.top += DialogModern_Scale(dpi, 6);
        labelRect.right -= DialogModern_Scale(dpi, 6);
        labelRect.bottom = labelRect.top + DialogModern_Scale(dpi, 13);
        DialogModern_DrawText(hdc, labelFont, palette->mutedText, &labelRect,
                              GetLocalizedString(NULL, labelKeys[i]),
                              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        wchar_t valueText[64] = L"0";
        LocalizedDuration_Format((int)values[i], valueText, _countof(valueText));
        RECT valueRect = card;
        valueRect.left += DialogModern_Scale(dpi, 10);
        valueRect.top += DialogModern_Scale(dpi, 20);
        valueRect.right -= DialogModern_Scale(dpi, 6);
        valueRect.bottom = valueRect.top + DialogModern_Scale(dpi, 20);
        DialogModern_DrawText(hdc, valueFont, palette->text, &valueRect, valueText,
                              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        wchar_t countText[48];
        _snwprintf_s(countText, _countof(countText), _TRUNCATE,
                     GetLocalizedString(NULL, L"Count: %d"), counts[i]);
        RECT countRect = card;
        countRect.left += DialogModern_Scale(dpi, 10);
        countRect.right -= DialogModern_Scale(dpi, 6);
        countRect.bottom -= DialogModern_Scale(dpi, 4);
        countRect.top = countRect.bottom - DialogModern_Scale(dpi, 12);
        DialogModern_DrawText(hdc, countFont, palette->mutedText,
                              &countRect, countText,
                              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    DeleteObject(labelFont);
    DeleteObject(valueFont);
    DeleteObject(countFont);
}

void PaintStatsContent(HWND hwndDlg, const DRAWITEMSTRUCT* item) {
    if (!item || item->CtlID != IDC_STATS_CONTENT) return;

    StatsDialogData* data = (StatsDialogData*)
        GetWindowLongPtrW(hwndDlg, GWLP_USERDATA);
    DialogModernPalette palette;
    DialogModern_CopyPalette(hwndDlg, &palette);
    UINT dpi = DialogModern_GetDpi(hwndDlg);

    HBRUSH background = CreateSolidBrush(palette.background);
    FillRect(item->hDC, &item->rcItem, background);
    DeleteObject(background);

    StatsAggregate agg = {0};
    Stats_GetPeriod(data ? data->period : STATS_PERIOD_TODAY, &agg);

    wchar_t focusText[128] = L"0";
    LocalizedDuration_Format((int)agg.focus_seconds, focusText,
                             _countof(focusText));
    wchar_t completedText[32];
    _snwprintf_s(completedText, _countof(completedText), _TRUNCATE,
                 L"%d", agg.completed_sessions);
    wchar_t roundsText[32];
    _snwprintf_s(roundsText, _countof(roundsText), _TRUNCATE,
                 L"%d", agg.pomodoro_rounds);
    wchar_t longestText[128] = L"0";
    LocalizedDuration_Format((int)agg.longest_session_seconds, longestText,
                             _countof(longestText));

    StatsMetric metrics[] = {
        {L"Focus Time", focusText},
        {L"Completed", completedText},
        {L"Pomodoro Rounds", roundsText},
        {L"Longest Session", longestText},
    };

    RECT content = item->rcItem;
    int margin = DialogModern_Scale(dpi, STATS_MARGIN);
    RECT cardsArea;
    cardsArea.left = content.left + margin;
    cardsArea.top = content.top + DialogModern_Scale(dpi, 8);
    cardsArea.right = content.right - margin;
    cardsArea.bottom = cardsArea.top + DialogModern_Scale(dpi, 150);
    PaintMetrics(item->hDC, &palette, dpi, &cardsArea, metrics,
                 (int)(sizeof(metrics) / sizeof(metrics[0])));

    int catGap = DialogModern_Scale(dpi, 8);
    RECT catArea;
    catArea.left = content.left + margin;
    catArea.top = cardsArea.bottom + catGap;
    catArea.right = content.right - margin;
    catArea.bottom = catArea.top + DialogModern_Scale(dpi, 74);
    PaintCategoryStrip(item->hDC, &palette, dpi, &catArea, &agg);

    RECT chartArea;
    chartArea.left = content.left + margin;
    chartArea.top = catArea.bottom + catGap;
    chartArea.right = content.right - margin;
    chartArea.bottom = content.bottom - DialogModern_Scale(dpi, 12);
    PaintChart(item->hDC, &palette, dpi, &chartArea);
}
