/**
 * @file main.c
 * @brief RetroAchievements 3DS - Phase 5: UI Engine & Rendering
 */

#include "config.h"
#include "network.h"
#include "retro_api.h"
#include "ui.h"
#include "badge.h"

#include <3ds.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Subsystem lifecycle tracking flags
static bool s_gfx_initialized   = false;
static bool s_romfs_initialized = false;
static bool s_cfgu_initialized  = false;
static bool s_ui_initialized    = false;
static bool s_badge_initialized = false;
static bool s_net_initialized   = false;

static app_config_t        s_config;
static config_status_t     s_config_status;

static ra_user_summary_t   s_user_summary;
static bool                s_user_summary_loaded = false;

static ra_game_progress_t  s_game_progress;
static bool                s_game_progress_loaded = false;

static ui_view_mode_t      s_view_mode       = UI_VIEW_SUMMARY;
static ui_state_t          s_ui_state        = UI_STATE_IDLE;
static char                s_status_msg[128] = {0};

static float               s_scroll_y        = 0.0f;
static int                 s_selected_index  = 0;

static void init_subsystems(void)
{
    Result rc;

    // 1. Graphics display controller
    gfxInitDefault();
    s_gfx_initialized = true;

    // 2. RomFS (embedded read-only assets & certs)
    rc = romfsInit();
    if (R_SUCCEEDED(rc))
    {
        s_romfs_initialized = true;
    }

    // 3. System configuration service
    rc = cfguInit();
    if (R_SUCCEEDED(rc))
    {
        s_cfgu_initialized = true;
    }

    // 4. Citro3D / Citro2D Dual-Screen Graphics Pipeline
    if (ui_init())
    {
        s_ui_initialized = true;

        // 5. Badge Texture & Background Download Subsystem
        if (badge_subsystem_init())
        {
            s_badge_initialized = true;
        }
    }

    // 6. Network subsystem
    net_result_t net_rc = network_init();
    if (net_rc == NET_OK)
    {
        s_net_initialized = true;
    }

    // 7. Application configuration & SDMC storage
    s_config_status = config_load(&s_config);
    if (s_config_status == CONFIG_CREATED_TEMPLATE)
    {
        snprintf(s_status_msg, sizeof(s_status_msg), "Template created at sdmc:/3ds/retroachievements/config.ini");
    }
    else if (!s_config.is_configured)
    {
        snprintf(s_status_msg, sizeof(s_status_msg), "Credentials unconfigured. Edit config.ini.");
    }
}

static void cleanup_subsystems(void)
{
    // Clean up domain models
    if (s_game_progress_loaded)
    {
        ra_game_progress_free(&s_game_progress);
        s_game_progress_loaded = false;
    }

    // Securely wipe sensitive in-memory credentials before teardown
    config_secure_wipe(&s_config);

    // 1. Badge Subsystem (Stop worker thread and free linear memory textures)
    if (s_badge_initialized)
    {
        badge_subsystem_cleanup();
        s_badge_initialized = false;
    }

    // 2. Network subsystem
    if (s_net_initialized)
    {
        network_cleanup();
        s_net_initialized = false;
    }

    // 3. Citro2D / Citro3D UI pipeline
    if (s_ui_initialized)
    {
        ui_cleanup();
        s_ui_initialized = false;
    }

    // 3. CFGU service
    if (s_cfgu_initialized)
    {
        cfguExit();
        s_cfgu_initialized = false;
    }

    // 4. RomFS
    if (s_romfs_initialized)
    {
        romfsExit();
        s_romfs_initialized = false;
    }

    // 5. Graphics
    if (s_gfx_initialized)
    {
        gfxExit();
        s_gfx_initialized = false;
    }
}

static void clamp_scroll_and_selection(size_t total_items)
{
    if (total_items == 0)
    {
        s_selected_index = 0;
        s_scroll_y = 0.0f;
        return;
    }

    if (s_selected_index < 0)
    {
        s_selected_index = 0;
    }
    if (s_selected_index >= (int)total_items)
    {
        s_selected_index = (int)total_items - 1;
    }

    const float view_h = 214.0f - 28.0f; // 186 px
    const float sel_y_top = (float)s_selected_index * UI_ITEM_HEIGHT;
    const float sel_y_bottom = sel_y_top + UI_ITEM_HEIGHT;

    // Keep selected item within view
    if (sel_y_top < s_scroll_y)
    {
        s_scroll_y = sel_y_top;
    }
    else if (sel_y_bottom > s_scroll_y + view_h)
    {
        s_scroll_y = sel_y_bottom - view_h;
    }

    const float max_scroll = ((float)total_items * UI_ITEM_HEIGHT) - view_h;
    if (s_scroll_y < 0.0f)
    {
        s_scroll_y = 0.0f;
    }
    else if (max_scroll > 0.0f && s_scroll_y > max_scroll)
    {
        s_scroll_y = max_scroll;
    }
}

int main(int argc, char* argv[])
{
    init_subsystems();

    while (aptMainLoop())
    {
        hidScanInput();
        const u32 kDown = hidKeysDown();
        const u32 kHeld = hidKeysHeld();

        if (kDown & KEY_START)
        {
            break; // Clean return to hbmenu
        }

        size_t current_items = 0;
        if (s_view_mode == UI_VIEW_GAME_PROGRESS && s_game_progress_loaded)
        {
            current_items = s_game_progress.achievement_count;
        }
        else if (s_view_mode == UI_VIEW_SUMMARY && s_user_summary_loaded)
        {
            current_items = s_user_summary.recent_games_count;
        }

        // Navigation (Up/Down)
        if (kDown & (KEY_DUP | KEY_CPAD_UP))
        {
            if (s_selected_index > 0)
            {
                s_selected_index--;
                clamp_scroll_and_selection(current_items);
            }
        }
        else if (kDown & (KEY_DDOWN | KEY_CPAD_DOWN))
        {
            if (s_selected_index < (int)current_items - 1)
            {
                s_selected_index++;
                clamp_scroll_and_selection(current_items);
            }
        }

        // Smooth scroll handling with Circle Pad when held
        if (kHeld & KEY_CPAD_UP)
        {
            s_scroll_y -= 4.0f;
            clamp_scroll_and_selection(current_items);
        }
        else if (kHeld & KEY_CPAD_DOWN)
        {
            s_scroll_y += 4.0f;
            clamp_scroll_and_selection(current_items);
        }

        // Toggle View Mode (X)
        if (kDown & KEY_X)
        {
            s_view_mode = (s_view_mode == UI_VIEW_SUMMARY) ? UI_VIEW_GAME_PROGRESS : UI_VIEW_SUMMARY;
            s_selected_index = 0;
            s_scroll_y = 0.0f;

            size_t new_items = (s_view_mode == UI_VIEW_GAME_PROGRESS && s_game_progress_loaded)
                ? s_game_progress.achievement_count
                : (s_user_summary_loaded ? s_user_summary.recent_games_count : 0);
            clamp_scroll_and_selection(new_items);
        }

        // Reload config from SD (Y)
        if (kDown & KEY_Y)
        {
            s_config_status = config_load(&s_config);
            if (s_config.is_configured)
            {
                snprintf(s_status_msg, sizeof(s_status_msg), "Config reloaded for user: %s", s_config.username);
            }
            else
            {
                snprintf(s_status_msg, sizeof(s_status_msg), "Credentials unconfigured in config.ini");
            }
        }

        // Action / Sync (A)
        if (kDown & KEY_A)
        {
            if (s_view_mode == UI_VIEW_SUMMARY)
            {
                // If on recent games list and user presses A on a selected game: load its progress!
                if (s_user_summary_loaded && s_user_summary.recent_games_count > 0 &&
                    s_selected_index < (int)s_user_summary.recent_games_count)
                {
                    uint32_t chosen_game = s_user_summary.recent_games[s_selected_index].game_id;
                    s_ui_state = UI_STATE_BUSY_FETCHING;

                    if (s_game_progress_loaded)
                    {
                        ra_game_progress_free(&s_game_progress);
                        s_game_progress_loaded = false;
                    }

                    char err_buf[128] = {0};
                    ra_result_t res = ra_api_get_game_progress(&s_config, chosen_game, &s_game_progress, err_buf, sizeof(err_buf));
                    if (res == RA_OK)
                    {
                        s_game_progress_loaded = true;
                        s_view_mode = UI_VIEW_GAME_PROGRESS;
                        s_selected_index = 0;
                        s_scroll_y = 0.0f;
                        s_ui_state = UI_STATE_IDLE;
                        snprintf(s_status_msg, sizeof(s_status_msg), "Loaded: %s", s_game_progress.title);
                    }
                    else
                    {
                        s_ui_state = UI_STATE_ERROR;
                        snprintf(s_status_msg, sizeof(s_status_msg), "Failed to load game: %s", err_buf);
                    }
                }
                else
                {
                    // Fetch user summary
                    s_ui_state = UI_STATE_BUSY_FETCHING;
                    char err_buf[128] = {0};
                    ra_result_t res = ra_api_get_user_summary(&s_config, NULL, &s_user_summary, err_buf, sizeof(err_buf));
                    if (res == RA_OK)
                    {
                        s_user_summary_loaded = true;
                        s_ui_state = UI_STATE_IDLE;
                        s_selected_index = 0;
                        s_scroll_y = 0.0f;
                        snprintf(s_status_msg, sizeof(s_status_msg), "Profile updated for %s", s_user_summary.username);
                    }
                    else
                    {
                        s_ui_state = UI_STATE_ERROR;
                        snprintf(s_status_msg, sizeof(s_status_msg), "Profile sync error: %s", err_buf);
                    }
                }
            }
            else // UI_VIEW_GAME_PROGRESS
            {
                uint32_t target_game = s_game_progress_loaded ? s_game_progress.game_id :
                    ((s_user_summary_loaded && s_user_summary.recent_games_count > 0) ? s_user_summary.recent_games[0].game_id : 2);

                s_ui_state = UI_STATE_BUSY_FETCHING;
                if (s_game_progress_loaded)
                {
                    ra_game_progress_free(&s_game_progress);
                    s_game_progress_loaded = false;
                }

                char err_buf[128] = {0};
                ra_result_t res = ra_api_get_game_progress(&s_config, target_game, &s_game_progress, err_buf, sizeof(err_buf));
                if (res == RA_OK)
                {
                    s_game_progress_loaded = true;
                    s_ui_state = UI_STATE_IDLE;
                    s_selected_index = 0;
                    s_scroll_y = 0.0f;
                    snprintf(s_status_msg, sizeof(s_status_msg), "Loaded %zu achievements", s_game_progress.achievement_count);
                }
                else
                {
                    s_ui_state = UI_STATE_ERROR;
                    snprintf(s_status_msg, sizeof(s_status_msg), "Game sync error: %s", err_buf);
                }
            }
        }

        // Citro3D Dual-Screen Frame Rendering Pass
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        ui_frame_begin();

        ui_render_top_screen(
            s_view_mode,
            s_ui_state,
            &s_config,
            &s_user_summary,
            s_user_summary_loaded,
            &s_game_progress,
            s_game_progress_loaded,
            s_selected_index,
            s_status_msg
        );

        ui_render_bottom_screen(
            s_view_mode,
            s_ui_state,
            &s_config,
            &s_user_summary,
            s_user_summary_loaded,
            &s_game_progress,
            s_game_progress_loaded,
            s_scroll_y,
            s_selected_index,
            s_status_msg
        );

        C3D_FrameEnd(0);
    }

    cleanup_subsystems();
    return 0;
}
