/**
 * @file ui.c
 * @brief SDL2-based user interface rendering for AetherBlock.
 *
 * Implements all drawing routines for the main host list, profile selection,
 * confirm dialog, network test results, toast notifications, and the hint
 * bar. Also handles SDL/TTF initialization and teardown.
 */

#include "ui.h"
#include <stdio.h>
#include <string.h>

/* ─── Color palette ─── */
static const SDL_Color COL_BG            = { 20,  20,  30, 255};
static const SDL_Color COL_HEADER_BG     = { 14,  14,  24, 255};
static const SDL_Color COL_CURSOR_BG     = { 35,  65, 125, 220};
static const SDL_Color COL_BLOCKED       = { 50, 190,  70, 255}; /* Green = desired state */
static const SDL_Color COL_ALLOWED       = {230, 130,  40, 255}; /* Orange = unprotected */
static const SDL_Color COL_COMMENT       = { 90, 120, 170, 255};
static const SDL_Color COL_TEXT          = {235, 235, 245, 255};
static const SDL_Color COL_TEXT_DIM      = {120, 120, 145, 255};
static const SDL_Color COL_TEXT_HINT     = {155, 155, 175, 255};
static const SDL_Color COL_ACCENT        = { 70, 135, 220, 255};
static const SDL_Color COL_ACCENT_DIM    = { 45,  85, 145, 255};
static const SDL_Color COL_WARN          = {230, 185,  40, 255};
static const SDL_Color COL_SEPARATOR     = { 38,  38,  55, 255};
static const SDL_Color COL_HINT_BAR_BG   = { 18,  18,  28, 255};
static const SDL_Color COL_PROFILE_BG    = { 28,  28,  42, 255};
static const SDL_Color COL_ROW_ALT       = { 24,  24,  36, 255};
static const SDL_Color COL_SCROLLBAR     = { 45,  45,  65, 255};
static const SDL_Color COL_SCROLL_THUMB  = { 70, 135, 220, 140};
static const SDL_Color COL_TOGGLE_ON_BG  = { 50, 160,  60, 255}; /* Blocked = protected */
static const SDL_Color COL_TOGGLE_OFF_BG = { 90,  70,  50, 255}; /* Allowed = unprotected */
static const SDL_Color COL_TOGGLE_KNOB   = {240, 240, 250, 255};
static const SDL_Color COL_CAT_PILL_BG   = { 40,  40,  60, 255};
static const SDL_Color COL_TOAST_BG      = { 30,  30,  48, 240};

/* Test result colors */
static const SDL_Color COL_TEST_BLOCKED  = { 50, 190,  70, 255}; /* Green = good, blocked as expected */
static const SDL_Color COL_TEST_REACH    = {230,  55,  55, 255}; /* Red = reachable, not blocked */
static const SDL_Color COL_TEST_PENDING  = {100, 100, 120, 255};
static const SDL_Color COL_TEST_RUNNING  = {230, 185,  40, 255};

/* ─── Drawing primitives ─── */

static void setColor(SDL_Renderer *r, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

static void fillRect(SDL_Renderer *r, int x, int y, int w, int h, SDL_Color c) {
    setColor(r, c);
    SDL_Rect rect = {x, y, w, h};
    SDL_RenderFillRect(r, &rect);
}

static void drawRoundedRect(SDL_Renderer *r, int x, int y, int w, int h, int radius, SDL_Color c) {
    setColor(r, c);
    SDL_Rect body = {x + radius, y, w - 2 * radius, h};
    SDL_RenderFillRect(r, &body);
    SDL_Rect left_strip = {x, y + radius, radius, h - 2 * radius};
    SDL_RenderFillRect(r, &left_strip);
    SDL_Rect right_strip = {x + w - radius, y + radius, radius, h - 2 * radius};
    SDL_RenderFillRect(r, &right_strip);

    for (int cy2 = -radius; cy2 <= radius; cy2++) {
        for (int cx2 = -radius; cx2 <= radius; cx2++) {
            if (cx2 * cx2 + cy2 * cy2 <= radius * radius) {
                if (cx2 <= 0 && cy2 <= 0)
                    SDL_RenderDrawPoint(r, x + radius + cx2, y + radius + cy2);
                if (cx2 >= 0 && cy2 <= 0)
                    SDL_RenderDrawPoint(r, x + w - radius - 1 + cx2, y + radius + cy2);
                if (cx2 <= 0 && cy2 >= 0)
                    SDL_RenderDrawPoint(r, x + radius + cx2, y + h - radius - 1 + cy2);
                if (cx2 >= 0 && cy2 >= 0)
                    SDL_RenderDrawPoint(r, x + w - radius - 1 + cx2, y + h - radius - 1 + cy2);
            }
        }
    }
}

static void drawText(SDL_Renderer *renderer, TTF_Font *font, const char *text,
                     int x, int y, SDL_Color color) {
    if (!text || !text[0]) return;
    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, text, color);
    if (!surf) return;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
    if (tex) {
        SDL_Rect dst = {x, y, surf->w, surf->h};
        SDL_RenderCopy(renderer, tex, NULL, &dst);
        SDL_DestroyTexture(tex);
    }
    SDL_FreeSurface(surf);
}

static void drawTextRight(SDL_Renderer *renderer, TTF_Font *font, const char *text,
                          int right_x, int y, SDL_Color color) {
    if (!text || !text[0]) return;
    int w = 0, h = 0;
    TTF_SizeUTF8(font, text, &w, &h);
    drawText(renderer, font, text, right_x - w, y, color);
}

static int textWidth(TTF_Font *font, const char *text) {
    int w = 0, h = 0;
    TTF_SizeUTF8(font, text, &w, &h);
    return w;
}

static void drawDot(SDL_Renderer *renderer, int cx, int cy, int radius, SDL_Color color) {
    setColor(renderer, color);
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx * dx + dy * dy <= radius * radius) {
                SDL_RenderDrawPoint(renderer, cx + dx, cy + dy);
            }
        }
    }
}

/* ─── Toggle switch widget ─── */
static void drawToggle(SDL_Renderer *r, int x, int y, bool on) {
    int w = 36, h = 18;
    SDL_Color bg = on ? COL_TOGGLE_ON_BG : COL_TOGGLE_OFF_BG;
    drawRoundedRect(r, x, y, w, h, h / 2, bg);

    int knob_r = 6;
    int knob_x = on ? (x + w - knob_r - 5) : (x + knob_r + 5);
    int knob_y = y + h / 2;
    drawDot(r, knob_x, knob_y, knob_r, COL_TOGGLE_KNOB);
}

/* ─── Category pill badge ─── */
static void drawCategoryPill(SDL_Renderer *r, TTF_Font *font, const char *text,
                             int right_x, int y) {
    int tw = textWidth(font, text);
    int pw = tw + 16;
    int ph = 20;
    int px = right_x - pw;
    drawRoundedRect(r, px, y, pw, ph, 4, COL_CAT_PILL_BG);
    drawText(r, font, text, px + 8, y + 2, COL_TEXT_DIM);
}

/* ─── Button hint pill ─── */
static void drawButtonHint(SDL_Renderer *r, TTF_Font *font, const char *btn,
                           const char *label, int x, int y) {
    int bw = textWidth(font, btn);
    int pill_w = bw + 14;
    int pill_h = 22;
    drawRoundedRect(r, x, y - 1, pill_w, pill_h, 5, COL_SEPARATOR);
    drawText(r, font, btn, x + 7, y + 1, COL_ACCENT);
    drawText(r, font, label, x + pill_w + 8, y + 1, COL_TEXT_HINT);
}

/* ─── Title bar ─── */
static void drawTitleBar(UIState *ui, HostsFile *hf) {
    /* Background with subtle gradient effect via two strips */
    fillRect(ui->renderer, 0, 0, SCREEN_WIDTH, TITLE_BAR_HEIGHT, COL_HEADER_BG);
    fillRect(ui->renderer, 0, TITLE_BAR_HEIGHT - 1, SCREEN_WIDTH, 1, COL_ACCENT_DIM);

    /* App name */
    drawText(ui->renderer, ui->font_title, APP_NAME, 30, 12, COL_ACCENT);

    /* Version badge */
    int name_w = textWidth(ui->font_title, APP_NAME);
    char ver[32];
    snprintf(ver, sizeof(ver), "v%s", APP_VERSION);
    int vw = textWidth(ui->font_small, ver);
    int vx = 30 + name_w + 12;
    drawRoundedRect(ui->renderer, vx, 18, vw + 12, 20, 4, COL_ACCENT_DIM);
    drawText(ui->renderer, ui->font_small, ver, vx + 6, 20, COL_TEXT);

    /* File path + dirty indicator */
    int info_y = 44;
    int ix = 32;
    if (hf->dirty) {
        drawDot(ui->renderer, ix + 4, info_y + 8, 4, COL_WARN);
        drawText(ui->renderer, ui->font_small, "Modified", ix + 14, info_y, COL_WARN);
        ix += 14 + textWidth(ui->font_small, "Modified") + 12;
    }
    drawText(ui->renderer, ui->font_small, hf->active_path, ix, info_y, COL_TEXT_DIM);

    /* Blocked count - right side */
    int active = 0;
    int total = 0;
    for (int i = 0; i < hf->entry_count; i++) {
        if (!hf->entries[i].is_comment && !hf->entries[i].is_blank) {
            total++;
            if (hf->entries[i].enabled) active++;
        }
    }

    /* Blocked/Total counter */
    char counts[64];
    snprintf(counts, sizeof(counts), "%d / %d blocked", active, total);
    int cw = textWidth(ui->font_normal, counts);

    /* Background pill for counter */
    drawRoundedRect(ui->renderer, SCREEN_WIDTH - cw - 50, 20, cw + 20, 28, 6, COL_ACCENT_DIM);
    drawText(ui->renderer, ui->font_normal, counts, SCREEN_WIDTH - cw - 40, 23, COL_TEXT);
}

/* ─── Scrollbar ─── */
static void drawScrollbar(UIState *ui, HostsFile *hf, int list_y, int list_h) {
    if (hf->entry_count <= 0) return;

    int max_visible = (LIST_BOTTOM_Y - LIST_TOP_Y - 8) / ROW_HEIGHT;
    if (hf->entry_count <= max_visible) return;

    int bar_x = SCREEN_WIDTH - 10;
    int bar_w = 4;

    fillRect(ui->renderer, bar_x, list_y + 4, bar_w, list_h - 8, COL_SCROLLBAR);

    int thumb_h = (max_visible * (list_h - 8)) / hf->entry_count;
    if (thumb_h < 24) thumb_h = 24;
    int thumb_y = list_y + 4 + (ui->scroll_offset * (list_h - 8 - thumb_h)) / (hf->entry_count - max_visible);
    drawRoundedRect(ui->renderer, bar_x - 1, thumb_y, bar_w + 2, thumb_h, 3, COL_SCROLL_THUMB);
}

/* ─── Hint bar ─── */
static void drawHintBar(UIState *ui) {
    int y = SCREEN_HEIGHT - HINT_BAR_HEIGHT;
    fillRect(ui->renderer, 0, y, SCREEN_WIDTH, HINT_BAR_HEIGHT, COL_HINT_BAR_BG);
    fillRect(ui->renderer, 0, y, SCREEN_WIDTH, 1, COL_SEPARATOR);

    int ty = y + (HINT_BAR_HEIGHT - 20) / 2 + 1;

    typedef struct { const char *btn; const char *label; } HintDef;
    HintDef hints[] = {
        {"A",               "Toggle"},
        {"Y",               "Save & Reload"},
        {"X",               "Profiles"},
        {"L",               "Test Servers"},
        {"\xe2\x88\x92",    "Seed Defaults"},
        {"+",               "Quit"},
    };
    int count = sizeof(hints) / sizeof(hints[0]);

    /* Calculate total width for centering */
    int total_w = 0;
    int gap = 28;
    for (int i = 0; i < count; i++) {
        int bw = textWidth(ui->font_small, hints[i].btn);
        int lw = textWidth(ui->font_small, hints[i].label);
        total_w += (bw + 14) + 8 + lw;
        if (i < count - 1) total_w += gap;
    }

    int tx = (SCREEN_WIDTH - total_w) / 2;
    for (int i = 0; i < count; i++) {
        int bw = textWidth(ui->font_small, hints[i].btn);
        int pill_w = bw + 14;
        drawButtonHint(ui->renderer, ui->font_small, hints[i].btn, hints[i].label, tx, ty);
        int lw = textWidth(ui->font_small, hints[i].label);
        tx += pill_w + 8 + lw + gap;
    }
}

/* ─── Main list ─── */
static void drawMainList(UIState *ui, HostsFile *hf) {
    int y = LIST_TOP_Y + 6;
    int visible = 0;
    int max_visible = (LIST_BOTTOM_Y - LIST_TOP_Y - 8) / ROW_HEIGHT;
    int row_index = 0;

    for (int i = ui->scroll_offset; i < hf->entry_count && visible < max_visible; i++) {
        HostEntry *e = &hf->entries[i];

        if (e->is_blank) {
            y += 10;
            visible++;
            continue;
        }

        /* Alternating row background */
        if (!e->is_comment && i != ui->cursor_index) {
            if (row_index % 2 == 1) {
                fillRect(ui->renderer, 8, y - 2, SCREEN_WIDTH - 28, ROW_HEIGHT, COL_ROW_ALT);
            }
        }

        /* Cursor highlight with rounded rect */
        if (i == ui->cursor_index) {
            drawRoundedRect(ui->renderer, 8, y - 3, SCREEN_WIDTH - 28, ROW_HEIGHT + 2, 6, COL_CURSOR_BG);
        }

        if (e->is_comment) {
            /* Category section header */
            fillRect(ui->renderer, 20, y + ROW_HEIGHT - 6, SCREEN_WIDTH - 52, 1, COL_SEPARATOR);
            drawText(ui->renderer, ui->font_small, e->raw_line, 22, y + 4, COL_COMMENT);
        } else {
            /* Toggle switch */
            drawToggle(ui->renderer, 22, y + (ROW_HEIGHT - 18) / 2, e->enabled);

            /* Hostname */
            drawText(ui->renderer, ui->font_normal, e->hostname, 72, y + (ROW_HEIGHT - 22) / 2, COL_TEXT);

            /* Status label */
            const char *status = e->enabled ? "BLOCKED" : "ALLOWED";
            SDL_Color status_col = e->enabled ? COL_BLOCKED : COL_ALLOWED;
            int sx = 72 + textWidth(ui->font_normal, e->hostname) + 14;
            drawText(ui->renderer, ui->font_small, status, sx, y + (ROW_HEIGHT - 16) / 2 + 2, status_col);

            /* Category pill on right */
            const char *cat = categoryName(e->category);
            drawCategoryPill(ui->renderer, ui->font_small, cat, SCREEN_WIDTH - 24, y + (ROW_HEIGHT - 20) / 2);
        }

        if (!e->is_blank) row_index++;
        y += ROW_HEIGHT;
        visible++;
    }

    /* Empty state */
    if (hf->entry_count == 0) {
        const char *empty = "No host entries found. Press \xe2\x88\x92 to seed defaults.";
        int ew = textWidth(ui->font_normal, empty);
        drawText(ui->renderer, ui->font_normal, empty,
                 (SCREEN_WIDTH - ew) / 2, (SCREEN_HEIGHT - 30) / 2, COL_TEXT_DIM);
    }

    /* Scrollbar */
    int list_h = LIST_BOTTOM_Y - LIST_TOP_Y;
    drawScrollbar(ui, hf, LIST_TOP_Y, list_h);
}

/* ─── Toast overlay ─── */
static void drawToast(UIState *ui) {
    if (ui->toast_timer <= 0) return;

    int tw = textWidth(ui->font_normal, ui->toast_msg);
    int pw = tw + 40;
    int ph = 44;
    int px = (SCREEN_WIDTH - pw) / 2;
    int py = SCREEN_HEIGHT - HINT_BAR_HEIGHT - ph - 16;

    /* Fade effect */
    SDL_Color bg = COL_TOAST_BG;
    if (ui->toast_timer < 30) {
        bg.a = (Uint8)(bg.a * ui->toast_timer / 30);
    }

    drawRoundedRect(ui->renderer, px, py, pw, ph, 8, bg);

    /* Accent left strip */
    drawRoundedRect(ui->renderer, px, py, 4, ph, 2, ui->toast_color);

    SDL_Color tc = COL_TEXT;
    if (ui->toast_timer < 30) {
        tc.a = (Uint8)(255 * ui->toast_timer / 30);
    }
    drawText(ui->renderer, ui->font_normal, ui->toast_msg, px + 20, py + 11, tc);
}

/* ─── Profiles screen ─── */
static void drawProfilesScreen(UIState *ui) {
    fillRect(ui->renderer, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COL_BG);

    /* Title */
    fillRect(ui->renderer, 0, 0, SCREEN_WIDTH, TITLE_BAR_HEIGHT, COL_HEADER_BG);
    fillRect(ui->renderer, 0, TITLE_BAR_HEIGHT - 1, SCREEN_WIDTH, 1, COL_ACCENT_DIM);
    drawText(ui->renderer, ui->font_title, "Select Profile", 30, 16, COL_ACCENT);

    /* Subtitle */
    drawText(ui->renderer, ui->font_small, "Choose a preset to quickly configure server blocking",
             30, 46, COL_TEXT_DIM);

    int y = TITLE_BAR_HEIGHT + 24;
    int card_h = 72;
    int card_gap = 12;

    for (int i = 0; i < PROFILE_COUNT; i++) {
        bool selected = (i == ui->profile_cursor);
        SDL_Color bg = selected ? COL_CURSOR_BG : COL_PROFILE_BG;
        int card_x = 30;
        int card_w = SCREEN_WIDTH - 60;

        drawRoundedRect(ui->renderer, card_x, y, card_w, card_h, 8, bg);

        if (selected) {
            /* Left accent bar */
            drawRoundedRect(ui->renderer, card_x, y, 4, card_h, 2, COL_ACCENT);
        }

        /* Profile name */
        drawText(ui->renderer, ui->font_normal, PROFILE_DEFS[i].name, card_x + 20, y + 14, COL_TEXT);

        /* Description */
        drawText(ui->renderer, ui->font_small, PROFILE_DEFS[i].description, card_x + 20, y + 42, COL_TEXT_DIM);

        y += card_h + card_gap;
    }

    /* Hint bar */
    int hy = SCREEN_HEIGHT - HINT_BAR_HEIGHT;
    fillRect(ui->renderer, 0, hy, SCREEN_WIDTH, HINT_BAR_HEIGHT, COL_HINT_BAR_BG);
    fillRect(ui->renderer, 0, hy, SCREEN_WIDTH, 1, COL_SEPARATOR);
    int ty = hy + (HINT_BAR_HEIGHT - 20) / 2 + 1;

    int tx = 30;
    drawButtonHint(ui->renderer, ui->font_small, "A", "Apply", tx, ty);
    tx += textWidth(ui->font_small, "A") + 14 + 8 + textWidth(ui->font_small, "Apply") + 36;
    drawButtonHint(ui->renderer, ui->font_small, "B", "Back", tx, ty);
}

/* ─── Status screen (full-screen message) ─── */
static void drawStatusScreen(UIState *ui) {
    fillRect(ui->renderer, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COL_BG);

    /* Centered card */
    int card_w = 600;
    int card_h = 160;
    int cx = (SCREEN_WIDTH - card_w) / 2;
    int cy = (SCREEN_HEIGHT - card_h) / 2 - 20;

    drawRoundedRect(ui->renderer, cx, cy, card_w, card_h, 10, COL_HEADER_BG);

    int tw = textWidth(ui->font_normal, ui->status_msg);
    int tx = (SCREEN_WIDTH - tw) / 2;
    drawText(ui->renderer, ui->font_normal, ui->status_msg, tx, cy + 50, COL_TEXT);

    const char *hint = "Press any button to continue...";
    int hw = textWidth(ui->font_small, hint);
    drawText(ui->renderer, ui->font_small, hint, (SCREEN_WIDTH - hw) / 2, cy + card_h - 40, COL_TEXT_DIM);
}

/* ─── Confirm dialog ─── */
static void drawConfirmScreen(UIState *ui) {
    fillRect(ui->renderer, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COL_BG);

    int card_w = 560;
    int card_h = 260;
    int cx = (SCREEN_WIDTH - card_w) / 2;
    int cy = (SCREEN_HEIGHT - card_h) / 2 - 20;

    drawRoundedRect(ui->renderer, cx, cy, card_w, card_h, 12, COL_HEADER_BG);

    /* Warning icon */
    drawDot(ui->renderer, SCREEN_WIDTH / 2, cy + 45, 16, COL_WARN);
    drawText(ui->renderer, ui->font_large, "!", SCREEN_WIDTH / 2 - 5, cy + 28, COL_HEADER_BG);

    /* Title */
    const char *title = "Unsaved Changes";
    int ttw = textWidth(ui->font_large, title);
    drawText(ui->renderer, ui->font_large, title, (SCREEN_WIDTH - ttw) / 2, cy + 75, COL_WARN);

    /* Message */
    const char *msg = "You have unsaved changes. Quit without saving?";
    int mw = textWidth(ui->font_normal, msg);
    drawText(ui->renderer, ui->font_normal, msg, (SCREEN_WIDTH - mw) / 2, cy + 120, COL_TEXT);

    /* Buttons */
    int btn_y = cy + card_h - 70;
    int btn_w = 140;
    int btn_h = 44;
    int gap = 30;

    /* Quit button */
    int qx = SCREEN_WIDTH / 2 - btn_w - gap / 2;
    drawRoundedRect(ui->renderer, qx, btn_y, btn_w, btn_h, 8, COL_BLOCKED);
    const char *qtext = "A  Quit";
    int qw = textWidth(ui->font_normal, qtext);
    drawText(ui->renderer, ui->font_normal, qtext, qx + (btn_w - qw) / 2, btn_y + (btn_h - 22) / 2, COL_TEXT);

    /* Cancel button */
    int bx = SCREEN_WIDTH / 2 + gap / 2;
    drawRoundedRect(ui->renderer, bx, btn_y, btn_w, btn_h, 8, COL_SEPARATOR);
    const char *btext = "B  Cancel";
    int bw = textWidth(ui->font_normal, btext);
    drawText(ui->renderer, ui->font_normal, btext, bx + (btn_w - bw) / 2, btn_y + (btn_h - 22) / 2, COL_TEXT);
}

/* ─── Network Test screen ─── */
static void drawNetTestScreen(UIState *ui) {
    fillRect(ui->renderer, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COL_BG);

    /* Title bar */
    fillRect(ui->renderer, 0, 0, SCREEN_WIDTH, TITLE_BAR_HEIGHT, COL_HEADER_BG);
    fillRect(ui->renderer, 0, TITLE_BAR_HEIGHT - 1, SCREEN_WIDTH, 1, COL_ACCENT_DIM);
    drawText(ui->renderer, ui->font_title, "Server Connectivity Test", 30, 16, COL_ACCENT);

    /* Progress info */
    char progress[64];
    if (ui->net_test.running) {
        snprintf(progress, sizeof(progress), "Testing %d / %d ...",
                 ui->net_test.current + 1, ui->net_test.count);
        drawText(ui->renderer, ui->font_small, progress, 30, 46, COL_WARN);
    } else if (ui->net_test.finished) {
        int blocked = 0, reachable = 0, errors = 0;
        for (int i = 0; i < ui->net_test.count; i++) {
            if (ui->net_test.entries[i].result == TEST_BLOCKED) blocked++;
            else if (ui->net_test.entries[i].result == TEST_REACHABLE) reachable++;
            else if (ui->net_test.entries[i].result == TEST_ERROR) errors++;
        }
        if (errors > 0)
            snprintf(progress, sizeof(progress), "Complete: %d blocked, %d reachable, %d errors", blocked, reachable, errors);
        else
            snprintf(progress, sizeof(progress), "Complete: %d blocked, %d reachable", blocked, reachable);
        drawText(ui->renderer, ui->font_small, progress, 30, 46, COL_TEXT_DIM);
    }

    /* Progress bar */
    if (ui->net_test.count > 0) {
        int bar_x = SCREEN_WIDTH - 280;
        int bar_y = 42;
        int bar_w = 240;
        int bar_h = 14;
        drawRoundedRect(ui->renderer, bar_x, bar_y, bar_w, bar_h, 4, COL_SEPARATOR);

        int fill_w = 0;
        if (ui->net_test.finished) {
            fill_w = bar_w;
        } else {
            fill_w = (ui->net_test.current * bar_w) / ui->net_test.count;
        }
        if (fill_w > 0) {
            drawRoundedRect(ui->renderer, bar_x, bar_y, fill_w, bar_h, 4, COL_ACCENT);
        }
    }

    /* Results list */
    int y = TITLE_BAR_HEIGHT + 12;
    int row_h = 34;
    int max_visible = (LIST_BOTTOM_Y - TITLE_BAR_HEIGHT - 16) / row_h;

    for (int i = ui->net_test_scroll; i < ui->net_test.count && (i - ui->net_test_scroll) < max_visible; i++) {
        NetTestEntry *e = &ui->net_test.entries[i];
        int row_y = y + (i - ui->net_test_scroll) * row_h;

        /* Alternating rows */
        if (i % 2 == 1) {
            fillRect(ui->renderer, 8, row_y - 2, SCREEN_WIDTH - 16, row_h, COL_ROW_ALT);
        }

        /* Status indicator dot */
        SDL_Color dot_col;
        const char *status_text;
        switch (e->result) {
        case TEST_BLOCKED:
            dot_col = COL_TEST_BLOCKED;
            status_text = "BLOCKED";
            break;
        case TEST_REACHABLE:
            dot_col = COL_TEST_REACH;
            status_text = "REACHABLE";
            break;
        case TEST_RUNNING:
            dot_col = COL_TEST_RUNNING;
            status_text = "TESTING...";
            break;
        case TEST_ERROR:
            dot_col = COL_WARN;
            status_text = "ERROR";
            break;
        default:
            dot_col = COL_TEST_PENDING;
            status_text = "PENDING";
            break;
        }

        drawDot(ui->renderer, 30, row_y + row_h / 2 - 1, 5, dot_col);

        /* Status text */
        drawText(ui->renderer, ui->font_small, status_text, 44, row_y + (row_h - 16) / 2, dot_col);

        /* Hostname */
        drawText(ui->renderer, ui->font_normal, e->hostname, 160, row_y + (row_h - 22) / 2, COL_TEXT);

        /* Latency */
        if (e->result == TEST_REACHABLE && e->latency_ms >= 0) {
            char lat[32];
            snprintf(lat, sizeof(lat), "%d ms", e->latency_ms);
            drawTextRight(ui->renderer, ui->font_small, lat, SCREEN_WIDTH - 30, row_y + (row_h - 16) / 2, COL_TEXT_DIM);
        } else if (e->result == TEST_BLOCKED) {
            drawTextRight(ui->renderer, ui->font_small, "Timed out", SCREEN_WIDTH - 30, row_y + (row_h - 16) / 2, COL_TEXT_DIM);
        }
    }

    /* Hint bar */
    int hy = SCREEN_HEIGHT - HINT_BAR_HEIGHT;
    fillRect(ui->renderer, 0, hy, SCREEN_WIDTH, HINT_BAR_HEIGHT, COL_HINT_BAR_BG);
    fillRect(ui->renderer, 0, hy, SCREEN_WIDTH, 1, COL_SEPARATOR);
    int ty = hy + (HINT_BAR_HEIGHT - 20) / 2 + 1;

    int tx = 30;
    drawButtonHint(ui->renderer, ui->font_small, "B", "Back", tx, ty);
    if (ui->net_test.finished) {
        tx += textWidth(ui->font_small, "B") + 14 + 8 + textWidth(ui->font_small, "Back") + 36;
        drawButtonHint(ui->renderer, ui->font_small, "A", "Re-test", tx, ty);
    }
}

/* ─── Init / Destroy / Render ─── */

/**
 * @brief Initialize the SDL2 window, renderer, and fonts.
 * @param ui  UIState structure to populate.
 * @return true on success, false if any SDL/TTF/font initialization fails.
 */
bool uiInit(UIState *ui) {
    memset(ui, 0, sizeof(*ui));

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0)
        return false;

    if (TTF_Init() < 0)
        return false;

    ui->window = SDL_CreateWindow(APP_NAME,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!ui->window) return false;

    ui->renderer = SDL_CreateRenderer(ui->window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ui->renderer) return false;

    SDL_SetRenderDrawBlendMode(ui->renderer, SDL_BLENDMODE_BLEND);
    SDL_RenderSetLogicalSize(ui->renderer, SCREEN_WIDTH, SCREEN_HEIGHT);

    /* Load system shared font via libnx */
    PlFontData font_data;
    Result rc = plGetSharedFontByType(&font_data, PlSharedFontType_Standard);
    if (R_FAILED(rc)) return false;

    SDL_RWops *rw;

    rw = SDL_RWFromMem(font_data.address, font_data.size);
    ui->font_title = TTF_OpenFontRW(rw, 1, 30);

    rw = SDL_RWFromMem(font_data.address, font_data.size);
    ui->font_large = TTF_OpenFontRW(rw, 1, 26);

    rw = SDL_RWFromMem(font_data.address, font_data.size);
    ui->font_normal = TTF_OpenFontRW(rw, 1, 20);

    rw = SDL_RWFromMem(font_data.address, font_data.size);
    ui->font_small = TTF_OpenFontRW(rw, 1, 15);

    if (!ui->font_title || !ui->font_large || !ui->font_normal || !ui->font_small)
        return false;

    return true;
}

/**
 * @brief Tear down the UI, releasing all SDL and TTF resources.
 * @param ui  UIState to clean up.
 */
void uiDestroy(UIState *ui) {
    if (ui->font_small)  TTF_CloseFont(ui->font_small);
    if (ui->font_normal) TTF_CloseFont(ui->font_normal);
    if (ui->font_large)  TTF_CloseFont(ui->font_large);
    if (ui->font_title)  TTF_CloseFont(ui->font_title);
    if (ui->renderer)    SDL_DestroyRenderer(ui->renderer);
    if (ui->window)      SDL_DestroyWindow(ui->window);
    TTF_Quit();
    SDL_Quit();
}

/**
 * @brief Display a toast notification overlay with the given message and color.
 * @param ui     UIState to update.
 * @param msg    Text to display in the toast.
 * @param color  Accent color for the toast strip.
 */
void uiShowToast(UIState *ui, const char *msg, SDL_Color color) {
    strncpy(ui->toast_msg, msg, sizeof(ui->toast_msg) - 1);
    ui->toast_timer = TOAST_FRAMES;
    ui->toast_color = color;
}

/**
 * @brief Render the current frame based on the active screen.
 * @param ui  UIState containing screen state and renderer.
 * @param hf  HostsFile data used by the main list screen.
 */
void uiRender(UIState *ui, HostsFile *hf) {
    setColor(ui->renderer, COL_BG);
    SDL_RenderClear(ui->renderer);

    switch (ui->current_screen) {
    case SCREEN_MAIN_LIST:
        drawTitleBar(ui, hf);
        drawMainList(ui, hf);
        drawHintBar(ui);
        drawToast(ui);
        break;
    case SCREEN_PROFILES:
        drawProfilesScreen(ui);
        break;
    case SCREEN_STATUS:
        drawStatusScreen(ui);
        break;
    case SCREEN_CONFIRM:
        drawConfirmScreen(ui);
        break;
    case SCREEN_NET_TEST:
        drawNetTestScreen(ui);
        break;
    }

    /* Tick toast timer regardless of active screen */
    if (ui->toast_timer > 0)
        ui->toast_timer--;

    SDL_RenderPresent(ui->renderer);
}
