/**
 * @file ui.h
 * @brief RetroAchievements 3DS - Phase 5: UI Engine & Rendering (citro2d/citro3d)
 */

#pragma once

#include "config.h"
#include "network.h"
#include "retro_api.h"

#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>
#include <stddef.h>

#define UI_SCREEN_TOP_W     400
#define UI_SCREEN_TOP_H     240
#define UI_SCREEN_BOT_W     320
#define UI_SCREEN_BOT_H     240

#define UI_ITEM_HEIGHT      42.0f
#define UI_SCROLLBAR_WIDTH  4.0f

// UI Color Palette (RGBA8)
#define UI_CLR_BG            C2D_Color32(0x1B, 0x1B, 0x22, 0xFF)
#define UI_CLR_BANNER        C2D_Color32(0x23, 0x25, 0x30, 0xFF)
#define UI_CLR_CARD          C2D_Color32(0x28, 0x2A, 0x36, 0xFF)
#define UI_CLR_CARD_SEL      C2D_Color32(0x3B, 0x3E, 0x52, 0xFF)
#define UI_CLR_GOLD          C2D_Color32(0xD4, 0xAF, 0x37, 0xFF)
#define UI_CLR_GOLD_DIM      C2D_Color32(0x8A, 0x72, 0x24, 0xFF)
#define UI_CLR_TEXT_WHITE    C2D_Color32(0xF8, 0xF8, 0xF2, 0xFF)
#define UI_CLR_TEXT_DIM      C2D_Color32(0x9A, 0x9B, 0xA2, 0xFF)
#define UI_CLR_GREEN         C2D_Color32(0x50, 0xFA, 0x7B, 0xFF)
#define UI_CLR_RED           C2D_Color32(0xFF, 0x55, 0x55, 0xFF)
#define UI_CLR_BLUE          C2D_Color32(0x8B, 0xE9, 0xFD, 0xFF)
#define UI_CLR_BORDER        C2D_Color32(0x44, 0x47, 0x5A, 0xFF)
#define UI_CLR_PROGRESS_BG   C2D_Color32(0x18, 0x18, 0x1E, 0xFF)

/**
 * @brief View State Modes
 */
typedef enum {
    UI_VIEW_UNCONFIGURED = 0,
    UI_VIEW_SUMMARY,
    UI_VIEW_GAME_PROGRESS
} ui_view_mode_t;

/**
 * @brief Operational UI states
 */
typedef enum {
    UI_STATE_IDLE = 0,
    UI_STATE_BUSY_FETCHING,
    UI_STATE_ERROR
} ui_state_t;

/**
 * @brief Initialize citro3d, citro2d, dual-screen targets, and text buffers.
 * @return True on success, false on error.
 */
bool ui_init(void);

/**
 * @brief Destroy citro2d text buffers, release render targets, and teardown engines.
 */
void ui_cleanup(void);

/**
 * @brief Clear dynamic text buffer for the next frame. Call before drawing frame passes.
 */
void ui_frame_begin(void);

/**
 * @brief Render formatted text with bounded truncation.
 */
void ui_draw_text(float x, float y, float scale, u32 color, const char* text);

/**
 * @brief Render multiline wrapped text within a specified width boundary.
 */
void ui_draw_text_wrapped(float x, float y, float scale, float max_width, u32 color, const char* text);

/**
 * @brief Draw Top Screen layout (Profile, stats, selected item detail card).
 */
void ui_render_top_screen(
    ui_view_mode_t view_mode,
    ui_state_t state,
    const app_config_t* cfg,
    const ra_user_summary_t* summary,
    bool summary_loaded,
    const ra_game_progress_t* progress,
    bool progress_loaded,
    int selected_index,
    const char* status_message
);

/**
 * @brief Draw Bottom Screen layout (Scrollable list view with item culling).
 */
void ui_render_bottom_screen(
    ui_view_mode_t view_mode,
    ui_state_t state,
    const app_config_t* cfg,
    const ra_user_summary_t* summary,
    bool summary_loaded,
    const ra_game_progress_t* progress,
    bool progress_loaded,
    float scroll_y,
    int selected_index,
    const char* status_message
);

/**
 * @brief Get citro3d render targets
 */
C3D_RenderTarget* ui_get_top_target(void);
C3D_RenderTarget* ui_get_bottom_target(void);

