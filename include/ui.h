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

// Cyberpunk Glassmorphic Color Palette (RGBA8)
#define UI_CLR_CYBER_BG         C2D_Color32(0x08, 0x08, 0x0C, 0xFF) // #08080C Obsidian background
#define UI_CLR_CYBER_PANEL      C2D_Color32(0x0E, 0x11, 0x1A, 0xEA) // Frosted glass dark base
#define UI_CLR_CYBER_PANEL_SEL  C2D_Color32(0x14, 0x1A, 0x2C, 0xF2) // Selected glass dark base
#define UI_CLR_CYBER_BANNER     C2D_Color32(0x0A, 0x0C, 0x14, 0xF8) // Cyber header/footer banner
#define UI_CLR_CYBER_CYAN       C2D_Color32(0x00, 0xFF, 0xFF, 0xFF) // #00FFFF Neon Cyan
#define UI_CLR_CYBER_PINK       C2D_Color32(0xFF, 0x00, 0x7F, 0xFF) // #FF007F Cyber Pink / Magenta
#define UI_CLR_CYBER_ORANGE     C2D_Color32(0xFF, 0x55, 0x00, 0xFF) // #FF5500 Cyber Neon Orange
#define UI_CLR_CYBER_AMBER      C2D_Color32(0xFF, 0xAA, 0x00, 0xFF) // #FFAA00 Amber Gold
#define UI_CLR_CYBER_GREEN      C2D_Color32(0x00, 0xFF, 0x88, 0xFF) // #00FF88 Neon Emerald Green
#define UI_CLR_CYBER_RED        C2D_Color32(0xFF, 0x33, 0x44, 0xFF) // #FF3344 Neon Crimson Red
#define UI_CLR_CYBER_BORDER     C2D_Color32(0x1B, 0x25, 0x3C, 0xD0) // Glass panel boundary
#define UI_CLR_CYBER_BORDER_HI  C2D_Color32(0x00, 0xE5, 0xFF, 0x80) // Cyan glass border highlight
#define UI_CLR_CYBER_WHITE      C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF) // Glowing white
#define UI_CLR_CYBER_TEXT_DIM   C2D_Color32(0x82, 0x90, 0xAC, 0xFF) // Cool muted slate cyan
#define UI_CLR_CYBER_LABEL      C2D_Color32(0x55, 0xC4, 0xFF, 0xFF) // Cyber blue labels
#define UI_CLR_CYBER_PROGRESS   C2D_Color32(0x0A, 0x0E, 0x18, 0xFF) // Progress track
#define UI_CLR_CYBER_SHEEN      C2D_Color32(0xFF, 0xFF, 0xFF, 0x30) // Glass specular sheen
#define UI_CLR_CYBER_GRID       C2D_Color32(0x14, 0x1C, 0x2E, 0x30) // Subtle background grid lines

// Backwards-compatible aliases
#define UI_CLR_BG            UI_CLR_CYBER_BG
#define UI_CLR_BANNER        UI_CLR_CYBER_BANNER
#define UI_CLR_CARD          UI_CLR_CYBER_PANEL
#define UI_CLR_CARD_SEL      UI_CLR_CYBER_PANEL_SEL
#define UI_CLR_GOLD          UI_CLR_CYBER_AMBER
#define UI_CLR_GOLD_DIM      C2D_Color32(0x9E, 0x6E, 0x10, 0xFF)
#define UI_CLR_TEXT_WHITE    UI_CLR_CYBER_WHITE
#define UI_CLR_TEXT_DIM      UI_CLR_CYBER_TEXT_DIM
#define UI_CLR_GREEN         UI_CLR_CYBER_GREEN
#define UI_CLR_RED           UI_CLR_CYBER_RED
#define UI_CLR_BLUE          UI_CLR_CYBER_CYAN
#define UI_CLR_BORDER        UI_CLR_CYBER_BORDER
#define UI_CLR_PROGRESS_BG   UI_CLR_CYBER_PROGRESS

#define MAX_SEARCH_RESULTS  32

typedef struct {
    uint32_t game_id;
    char     title[128];
    char     console_name[64];
    char     image_icon[64];
} search_result_item_t;

/**
 * @brief Top-Level Application State Machine
 */
typedef enum {
    APP_STATE_INIT = 0,
    APP_STATE_SETUP_CREDENTIALS,
    APP_STATE_USER_SUMMARY,
    APP_STATE_GAME_DETAIL,
    APP_STATE_SEARCH,
    APP_STATE_SETTINGS,
    APP_STATE_ERROR
} app_state_t;

#define STATE_SETUP_CREDENTIALS APP_STATE_SETUP_CREDENTIALS
#define STATE_USER_SUMMARY      APP_STATE_USER_SUMMARY
#define STATE_SETTINGS          APP_STATE_SETTINGS

/**
 * @brief Settings Maintenance Confirmation Modal types
 */
typedef enum {
    SETTINGS_MODAL_NONE = 0,
    SETTINGS_MODAL_RESET_USER_DATA,
    SETTINGS_MODAL_CLEAR_CACHE
} settings_modal_t;

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
 * @brief Render formatted text with explicit z-depth.
 */
void ui_draw_text_depth(float x, float y, float depth, float scale, u32 color, const char* text);

/**
 * @brief Render multiline wrapped text within a specified width boundary.
 */
void ui_draw_text_wrapped(float x, float y, float scale, float max_width, u32 color, const char* text);

/**
 * @brief Render 2D vector magnifying glass icon using primitives.
 */
void ui_draw_search_icon(float x, float y, u32 color);

/**
 * @brief Render 2D vector circular reload/sync icon using primitives.
 */
void ui_draw_reload_icon(float x, float y, u32 color);

/**
 * @brief Render 2D vector gear/cog settings icon using primitives.
 */
void ui_draw_settings_icon(float x, float y, u32 color);

/**
 * @brief Render 2D vector back/return arrow icon using primitives.
 */
void ui_draw_back_icon(float x, float y, u32 color);

/**
 * @brief Render simulated glassmorphic panel with specular glare and animated cyber neon border.
 */
void ui_draw_glass_panel(float x, float y, float z, float w, float h, u32 accent_color, bool is_selected, float pulse);

/**
 * @brief Draw Top Screen layout (Profile, stats, selected item detail card, setup, settings).
 */
void ui_render_top_screen(
    app_state_t state,
    bool is_loading,
    const app_config_t* cfg,
    const ra_user_summary_t* summary,
    bool summary_loaded,
    const ra_game_progress_t* progress,
    bool progress_loaded,
    int selected_index,
    const char* status_message,
    const char* search_query,
    const search_result_item_t* search_results,
    size_t search_results_count,
    const char* draft_username,
    const char* draft_api_key,
    float cache_size_mb,
    uint32_t frame_count
);

/**
 * @brief Draw Bottom Screen layout (Scrollable list view, item culling, touch targets, setup buttons, settings).
 */
void ui_render_bottom_screen(
    app_state_t state,
    bool is_loading,
    const app_config_t* cfg,
    const ra_user_summary_t* summary,
    bool summary_loaded,
    const ra_game_progress_t* progress,
    bool progress_loaded,
    float scroll_y,
    int selected_index,
    const char* status_message,
    const char* search_query,
    const search_result_item_t* search_results,
    size_t search_results_count,
    const char* draft_username,
    const char* draft_api_key,
    settings_modal_t settings_modal,
    uint32_t frame_count
);

/**
 * @brief Get citro3d render targets
 */
C3D_RenderTarget* ui_get_top_target(void);
C3D_RenderTarget* ui_get_bottom_target(void);

