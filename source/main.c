/**
 * @file main.c
 * @brief RetroAchievements 3DS - Phase 7: Navigation & State Management
 */

#include "async_api.h"
#include "badge.h"
#include "config.h"
#include "network.h"
#include "retro_api.h"
#include "ui.h"

#include <3ds.h>
#include <3ds/applets/swkbd.h>
#include <ctype.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

// Subsystem lifecycle tracking flags
static bool s_gfx_initialized       = false;
static bool s_romfs_initialized     = false;
static bool s_cfgu_initialized      = false;
static bool s_ui_initialized        = false;
static bool s_badge_initialized     = false;
static bool s_net_initialized       = false;
static bool s_async_initialized     = false;

static app_config_t        s_config;
static config_status_t     s_config_status;

static ra_user_summary_t   s_user_summary;
static bool                s_user_summary_loaded  = false;

static ra_game_progress_t  s_game_progress;
static bool                s_game_progress_loaded = false;

// Global Game Search State
static char                 s_search_query[64]               = {0};
static search_result_item_t s_search_results[MAX_SEARCH_RESULTS];
static size_t               s_search_results_count           = 0;

// Draft Credential Setup State
static char                 s_draft_username[CONFIG_USERNAME_MAX_LEN] = {0};
static char                 s_draft_api_key[CONFIG_API_KEY_MAX_LEN]   = {0};

// Settings & Cache Management State
static settings_modal_t     s_settings_modal                 = SETTINGS_MODAL_NONE;
static float                s_cache_size_mb                  = 0.0f;

// Application State Machine
static app_state_t         s_app_state            = APP_STATE_INIT;
static app_state_t         s_prev_state           = APP_STATE_USER_SUMMARY;
static bool                s_is_loading           = false;
static uint32_t            s_frame_counter        = 0;
static char                s_status_msg[128]      = {0};

static float               s_scroll_y             = 0.0f;
static int                 s_selected_index       = 0;

// Input tracking for smooth touch and button repeating
static bool                s_touch_active         = false;
static touchPosition       s_touch_start          = {0, 0};
static float               s_touch_scroll_start   = 0.0f;
static bool                s_touch_dragged        = false;
static int                 s_key_repeat_timer     = 0;

static void clamp_scroll_only(size_t total_items)
{
    const float view_h = 214.0f - 28.0f; // 186 px
    const float max_scroll = ((float)total_items * UI_ITEM_HEIGHT) - view_h;

    if (s_scroll_y < 0.0f)
    {
        s_scroll_y = 0.0f;
    }
    else if (max_scroll > 0.0f && s_scroll_y > max_scroll)
    {
        s_scroll_y = max_scroll;
    }
    else if (max_scroll <= 0.0f)
    {
        s_scroll_y = 0.0f;
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

    clamp_scroll_only(total_items);
}

static void trigger_fetch_user_summary(void)
{
    if (!s_config.is_configured)
    {
        s_prev_state = s_app_state;
        s_app_state = APP_STATE_ERROR;
        snprintf(s_status_msg, sizeof(s_status_msg), "Credentials unconfigured. Edit sdmc:/3ds/retroachievements/config.ini");
        return;
    }

    u8 wifi_strength = 0;
    if (!network_get_status(&wifi_strength) || wifi_strength == 0)
    {
        s_prev_state = s_app_state;
        s_app_state = APP_STATE_ERROR;
        snprintf(s_status_msg, sizeof(s_status_msg), "Wi-Fi disconnected. Turn on Wi-Fi and retry.");
        return;
    }

    s_is_loading = true;
    snprintf(s_status_msg, sizeof(s_status_msg), "Syncing profile for %s...", s_config.username);
    async_api_request_user_summary(&s_config);
}

static void trigger_fetch_game_progress(uint32_t game_id)
{
    if (!s_config.is_configured)
    {
        s_prev_state = s_app_state;
        s_app_state = APP_STATE_ERROR;
        snprintf(s_status_msg, sizeof(s_status_msg), "Credentials unconfigured. Edit sdmc:/3ds/retroachievements/config.ini");
        return;
    }

    u8 wifi_strength = 0;
    if (!network_get_status(&wifi_strength) || wifi_strength == 0)
    {
        s_prev_state = s_app_state;
        s_app_state = APP_STATE_ERROR;
        snprintf(s_status_msg, sizeof(s_status_msg), "Wi-Fi disconnected. Turn on Wi-Fi and retry.");
        return;
    }

    s_is_loading = true;
    s_prev_state = s_app_state;
    s_app_state = APP_STATE_GAME_DETAIL;
    s_selected_index = 0;
    s_scroll_y = 0.0f;

    snprintf(s_status_msg, sizeof(s_status_msg), "Fetching achievements for Game ID %lu...", (unsigned long)game_id);
    async_api_request_game_progress(&s_config, game_id);
}

static void execute_search(const char* query)
{
    if (query == NULL || query[0] == '\0')
    {
        s_search_results_count = 0;
        s_selected_index = 0;
        s_scroll_y = 0.0f;
        return;
    }

    s_is_loading = true;
    s_selected_index = 0;
    s_scroll_y = 0.0f;
    s_search_results_count = 0;
    snprintf(s_status_msg, sizeof(s_status_msg), "Searching RetroAchievements for \"%s\"...", query);

    async_api_request_search(&s_config, query);
}

static void launch_search_keyboard(void)
{
    // Suspend background syncing while modal OS keyboard is active
    if (s_is_loading)
    {
        async_api_cancel();
        s_is_loading = false;
    }

    static SwkbdState s_swkbd;
    swkbdInit(&s_swkbd, SWKBD_TYPE_NORMAL, 2, 60);
    swkbdSetHintText(&s_swkbd, "Enter Game ID or Title");
    swkbdSetValidation(&s_swkbd, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);

    char input_buf[64] = {0};
    SwkbdButton button = swkbdInputText(&s_swkbd, input_buf, sizeof(input_buf));

    if (button != SWKBD_BUTTON_NONE && button != SWKBD_BUTTON_LEFT)
    {
        char* start = input_buf;
        while (*start && isspace((unsigned char)*start))
        {
            start++;
        }
        char* end = start + strlen(start);
        while (end > start && isspace((unsigned char)*(end - 1)))
        {
            end--;
        }
        *end = '\0';

        // Sanitize characters
        for (char* p = start; *p; p++)
        {
            if (!isprint((unsigned char)*p))
            {
                *p = ' ';
            }
        }

        if (strlen(start) > 0)
        {
            strncpy(s_search_query, start, sizeof(s_search_query) - 1);
            s_search_query[sizeof(s_search_query) - 1] = '\0';

            if (s_app_state != APP_STATE_SEARCH)
            {
                s_prev_state = s_app_state;
                s_app_state = APP_STATE_SEARCH;
            }

            execute_search(s_search_query);
        }
    }
}

static void launch_username_keyboard(void)
{
    if (s_is_loading)
    {
        return;
    }

    static SwkbdState s_swkbd_user;
    swkbdInit(&s_swkbd_user, SWKBD_TYPE_NORMAL, 2, 32);
    swkbdSetHintText(&s_swkbd_user, "Enter RA Username");
    if (s_draft_username[0] != '\0')
    {
        swkbdSetInitialText(&s_swkbd_user, s_draft_username);
    }
    swkbdSetValidation(&s_swkbd_user, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);

    char input_buf[64] = {0};
    SwkbdButton button = swkbdInputText(&s_swkbd_user, input_buf, sizeof(input_buf));

    // Clear residual touch flags so applet dismiss tap does not trigger UI buttons
    s_touch_active = false;
    s_touch_dragged = false;

    if (button != SWKBD_BUTTON_NONE && button != SWKBD_BUTTON_LEFT)
    {
        char* start = input_buf;
        while (*start && isspace((unsigned char)*start))
        {
            start++;
        }
        char* end = start + strlen(start);
        while (end > start && isspace((unsigned char)*(end - 1)))
        {
            end--;
        }
        *end = '\0';

        if (strlen(start) > 0)
        {
            strncpy(s_draft_username, start, sizeof(s_draft_username) - 1);
            s_draft_username[sizeof(s_draft_username) - 1] = '\0';
        }
    }
}

static void launch_api_key_keyboard(void)
{
    if (s_is_loading)
    {
        return;
    }

    static SwkbdState s_swkbd_key;
    swkbdInit(&s_swkbd_key, SWKBD_TYPE_NORMAL, 2, 64);
    swkbdSetHintText(&s_swkbd_key, "Enter Web API Key");
    if (s_draft_api_key[0] != '\0')
    {
        swkbdSetInitialText(&s_swkbd_key, s_draft_api_key);
    }
    swkbdSetValidation(&s_swkbd_key, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);

    char input_buf[96] = {0};
    SwkbdButton button = swkbdInputText(&s_swkbd_key, input_buf, sizeof(input_buf));

    // Clear residual touch flags so applet dismiss tap does not trigger UI buttons
    s_touch_active = false;
    s_touch_dragged = false;

    if (button != SWKBD_BUTTON_NONE && button != SWKBD_BUTTON_LEFT)
    {
        char* start = input_buf;
        while (*start && isspace((unsigned char)*start))
        {
            start++;
        }
        char* end = start + strlen(start);
        while (end > start && isspace((unsigned char)*(end - 1)))
        {
            end--;
        }
        *end = '\0';

        if (strlen(start) > 0)
        {
            strncpy(s_draft_api_key, start, sizeof(s_draft_api_key) - 1);
            s_draft_api_key[sizeof(s_draft_api_key) - 1] = '\0';
        }
    }
}

static void trigger_save_and_connect(void)
{
    if (s_is_loading)
    {
        return;
    }

    if (!config_validate_username(s_draft_username) || !config_validate_api_key(s_draft_api_key))
    {
        snprintf(s_status_msg, sizeof(s_status_msg), "Please enter both username and Web API key.");
        return;
    }

    // Copy draft credentials into active configuration struct
    strncpy(s_config.username, s_draft_username, sizeof(s_config.username) - 1);
    s_config.username[sizeof(s_config.username) - 1] = '\0';
    strncpy(s_config.web_api_key, s_draft_api_key, sizeof(s_config.web_api_key) - 1);
    s_config.web_api_key[sizeof(s_config.web_api_key) - 1] = '\0';
    s_config.is_configured = true;

    // Atomically write validated credentials to SD card with fflush/fclose
    config_status_t save_rc = config_save(&s_config);
    if (save_rc != CONFIG_OK)
    {
        snprintf(s_status_msg, sizeof(s_status_msg), "Failed to save config.ini to SD (err: %d)", save_rc);
        return;
    }

    // Verify Wi-Fi network connectivity before network request
    u8 wifi_strength = 0;
    if (!network_get_status(&wifi_strength) || wifi_strength == 0)
    {
        snprintf(s_status_msg, sizeof(s_status_msg), "Saved to SD! Wi-Fi disconnected: connect Wi-Fi & retry.");
        return;
    }

    // Trigger live credential verification call (Phase 4 API_GetUserSummary.php)
    s_is_loading = true;
    snprintf(s_status_msg, sizeof(s_status_msg), "Connecting to RetroAchievements to verify credentials...");
    async_api_request_user_summary(&s_config);
}

static void execute_reset_user_data(void)
{
    // Suspend any background network activity immediately
    if (s_is_loading)
    {
        async_api_cancel();
        s_is_loading = false;
    }

    // 1. Delete config.ini from SD card
    remove("sdmc:/3ds/retroachievements/config.ini");

    // 2. Clear in-memory credential structs
    memset(&s_config, 0, sizeof(s_config));
    s_config.is_configured = false;
    memset(s_draft_username, 0, sizeof(s_draft_username));
    memset(s_draft_api_key, 0, sizeof(s_draft_api_key));

    // 3. Clear active profile and game models
    memset(&s_user_summary, 0, sizeof(s_user_summary));
    s_user_summary_loaded = false;
    if (s_game_progress_loaded)
    {
        ra_game_progress_free(&s_game_progress);
        s_game_progress_loaded = false;
    }
    s_search_results_count = 0;
    s_search_query[0] = '\0';
    s_selected_index = 0;
    s_scroll_y = 0.0f;

    // 4. Safely re-initialize state flow back to STATE_SETUP_CREDENTIALS
    s_settings_modal = SETTINGS_MODAL_NONE;
    s_app_state = APP_STATE_SETUP_CREDENTIALS;
    snprintf(s_status_msg, sizeof(s_status_msg), "Credentials reset. Please enter username and API key.");
}

static void execute_clear_cache(void)
{
    // Suspend any background syncing / badge downloading
    if (s_is_loading)
    {
        async_api_cancel();
        s_is_loading = false;
    }

    // 1. Evict in-memory texture badges and linear memory allocations
    badge_cache_clear_memory();

    // 2. Recursively clear filesystem cache files under sdmc:/3ds/retroachievements/cache/
    //    and recreate empty subdirectories (badges, games, users, gamelists)
    config_clear_cache();

    // 3. Dynamically update storage usage display
    s_cache_size_mb = 0.0f;

    // 4. Close confirmation modal
    s_settings_modal = SETTINGS_MODAL_NONE;
    snprintf(s_status_msg, sizeof(s_status_msg), "Cache cleared successfully (0.00 MB).");
}

static void handle_touch_tap(u16 px, u16 py)
{
    // Intercept all touches while confirmation modal is active
    if (s_settings_modal != SETTINGS_MODAL_NONE)
    {
        // Modal Confirm Button: x in [24, 154], y in [154, 194]
        if (px >= 24 && px <= 154 && py >= 154 && py <= 194)
        {
            if (s_settings_modal == SETTINGS_MODAL_RESET_USER_DATA)
            {
                execute_reset_user_data();
            }
            else if (s_settings_modal == SETTINGS_MODAL_CLEAR_CACHE)
            {
                execute_clear_cache();
            }
            return;
        }

        // Modal Cancel Button: x in [166, 296], y in [154, 194]
        if (px >= 166 && px <= 296 && py >= 154 && py <= 194)
        {
            s_settings_modal = SETTINGS_MODAL_NONE;
            return;
        }

        // Touches outside the buttons are swallowed while modal is displayed
        return;
    }

    // Credential Setup Screen Hitboxes
    if (s_app_state == APP_STATE_SETUP_CREDENTIALS)
    {
        // Field 1: [ Set Username ] (y: 32..74, x: 16..304)
        if (py >= 32 && py <= 74 && px >= 16 && px <= 304)
        {
            launch_username_keyboard();
            return;
        }

        // Field 2: [ Set Web API Key ] (y: 80..122, x: 16..304)
        if (py >= 80 && py <= 122 && px >= 16 && px <= 304)
        {
            launch_api_key_keyboard();
            return;
        }

        // Action Button: [ Save & Connect ] (y: 150..206, x: 16..304)
        if (py >= 150 && py <= 206 && px >= 16 && px <= 304)
        {
            if (config_validate_username(s_draft_username) && config_validate_api_key(s_draft_api_key))
            {
                trigger_save_and_connect();
            }
            return;
        }

        return;
    }

    // Settings Screen Hitboxes
    if (s_app_state == APP_STATE_SETTINGS)
    {
        // Virtual Back Button Icon on Left: x in [0, 48], y <= 26
        if (px <= 48 && py <= 26)
        {
            s_app_state = s_prev_state;
            return;
        }

        // Settings Icon Toggle: x in [212, 244], y <= 26
        if (px >= 212 && px <= 244 && py <= 26)
        {
            s_app_state = s_prev_state;
            return;
        }

        // Action 1: [ Reset User Data ] (y: 36..86, x: 16..304)
        if (py >= 36 && py <= 86 && px >= 16 && px <= 304)
        {
            s_settings_modal = SETTINGS_MODAL_RESET_USER_DATA;
            return;
        }

        // Action 2: [ Clear Cache ] (y: 94..144, x: 16..304)
        if (py >= 94 && py <= 144 && px >= 16 && px <= 304)
        {
            s_settings_modal = SETTINGS_MODAL_CLEAR_CACHE;
            return;
        }

        // Action 3: [ Back ] (y: 152..202, x: 16..304)
        if (py >= 152 && py <= 202 && px >= 16 && px <= 304)
        {
            s_app_state = s_prev_state;
            return;
        }

        return;
    }

    // Global Header Bar Hitboxes (y: 0..26)
    if (py <= 26 && s_app_state != APP_STATE_ERROR)
    {
        // 1. Back button icon on left: x in [0, 48] (for GAME_DETAIL or SEARCH)
        if (px <= 48 && (s_app_state == APP_STATE_GAME_DETAIL || s_app_state == APP_STATE_SEARCH))
        {
            if (s_is_loading)
            {
                async_api_cancel();
                s_is_loading = false;
            }
            s_app_state = (s_app_state == APP_STATE_GAME_DETAIL) ? s_prev_state : APP_STATE_USER_SUMMARY;
            s_selected_index = 0;
            s_scroll_y = 0.0f;
            return;
        }

        // 2. Settings Icon button: x in [212, 244]
        if (px >= 212 && px <= 244)
        {
            s_prev_state = s_app_state;
            s_app_state = APP_STATE_SETTINGS;
            config_refresh_cache_size_async();
            return;
        }

        // Note: Dead zone gap at x in [245, 247] prevents accidental dual-triggering

        // 3. Search Icon button: x in [248, 280]
        if (px >= 248 && px <= 280)
        {
            launch_search_keyboard();
            return;
        }

        // Note: Dead zone gap at x in [281, 283] prevents accidental dual-triggering

        // 4. Reload / Sync Icon button: x in [284, 316]
        if (px >= 284 && px <= 316)
        {
            if (s_app_state == APP_STATE_GAME_DETAIL && s_game_progress_loaded)
            {
                trigger_fetch_game_progress(s_game_progress.game_id);
            }
            else
            {
                trigger_fetch_user_summary();
            }
            return;
        }
    }

    if (s_app_state == APP_STATE_ERROR)
    {
        // Touch Hitboxes for Error Recovery screen
        // Button 1: Retry (y: 40..84)
        if (py >= 40 && py <= 84 && px >= 16 && px <= 304)
        {
            if (s_prev_state == APP_STATE_GAME_DETAIL && s_game_progress_loaded)
            {
                trigger_fetch_game_progress(s_game_progress.game_id);
            }
            else
            {
                trigger_fetch_user_summary();
            }
        }
        // Button 2: Return to Profile (y: 94..138)
        else if (py >= 94 && py <= 138 && px >= 16 && px <= 304)
        {
            s_app_state = APP_STATE_USER_SUMMARY;
            snprintf(s_status_msg, sizeof(s_status_msg), "Returned to Profile Overview.");
        }
        // Button 3: Reload Config (y: 148..192)
        else if (py >= 148 && py <= 192 && px >= 16 && px <= 304)
        {
            s_config_status = config_load(&s_config);
            if (config_needs_setup(&s_config))
            {
                s_app_state = APP_STATE_SETUP_CREDENTIALS;
                snprintf(s_status_msg, sizeof(s_status_msg), "Please configure RetroAchievements credentials.");
            }
            else
            {
                snprintf(s_status_msg, sizeof(s_status_msg), "Config reloaded for user: %s", s_config.username);
                s_app_state = APP_STATE_USER_SUMMARY;
                trigger_fetch_user_summary();
            }
        }
        return;
    }

    // Scrollable List Area (y: 28..214)
    size_t current_items = 0;
    if (s_app_state == APP_STATE_GAME_DETAIL && s_game_progress_loaded)
    {
        current_items = s_game_progress.achievement_count;
    }
    else if (s_app_state == APP_STATE_SEARCH)
    {
        current_items = s_search_results_count;
    }
    else if (s_app_state == APP_STATE_USER_SUMMARY && s_user_summary_loaded)
    {
        current_items = s_user_summary.recent_games_count;
    }

    if (py >= 28 && py <= 214 && current_items > 0)
    {
        int row = (int)(( (float)py - 28.0f + s_scroll_y ) / UI_ITEM_HEIGHT);
        if (row >= 0 && row < (int)current_items)
        {
            if (s_app_state == APP_STATE_SEARCH)
            {
                if (s_selected_index == row)
                {
                    uint32_t chosen_game = s_search_results[row].game_id;
                    trigger_fetch_game_progress(chosen_game);
                }
                else
                {
                    s_selected_index = row;
                    clamp_scroll_and_selection(current_items);
                }
            }
            else if (s_app_state == APP_STATE_USER_SUMMARY)
            {
                if (s_selected_index == row)
                {
                    uint32_t chosen_game = s_user_summary.recent_games[row].game_id;
                    trigger_fetch_game_progress(chosen_game);
                }
                else
                {
                    s_selected_index = row;
                    clamp_scroll_and_selection(current_items);
                }
            }
            else if (s_app_state == APP_STATE_GAME_DETAIL)
            {
                s_selected_index = row;
                clamp_scroll_and_selection(current_items);
            }
        }
    }
}

static void init_subsystems(void)
{
    Result rc;

    // 1. Graphics display controller
    gfxInitDefault();
    s_gfx_initialized = true;

    // 2. RomFS (embedded root certificates)
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

        // 5. Badge Texture & Asset Cache Subsystem
        if (badge_subsystem_init())
        {
            s_badge_initialized = true;
        }
    }

    // 6. Network Subsystem
    net_result_t net_rc = network_init();
    if (net_rc == NET_OK)
    {
        s_net_initialized = true;
    }

    // 7. Non-blocking Async API Worker Thread
    if (async_api_init())
    {
        s_async_initialized = true;
    }

    // 8. Application configuration & SDMC storage
    s_config_status = config_load(&s_config);
    if (config_needs_setup(&s_config))
    {
        s_app_state = APP_STATE_SETUP_CREDENTIALS;
        s_config.is_configured = false;
        if (config_validate_username(s_config.username))
        {
            strncpy(s_draft_username, s_config.username, sizeof(s_draft_username) - 1);
            s_draft_username[sizeof(s_draft_username) - 1] = '\0';
        }
        if (config_validate_api_key(s_config.web_api_key))
        {
            strncpy(s_draft_api_key, s_config.web_api_key, sizeof(s_draft_api_key) - 1);
            s_draft_api_key[sizeof(s_draft_api_key) - 1] = '\0';
        }
        snprintf(s_status_msg, sizeof(s_status_msg), "Please configure RetroAchievements credentials.");
    }
    else
    {
        s_app_state = APP_STATE_USER_SUMMARY;
        // Automatically initiate background profile sync on boot
        trigger_fetch_user_summary();
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

    // 1. Stop background async API worker
    if (s_async_initialized)
    {
        async_api_cleanup();
        s_async_initialized = false;
    }

    // 2. Securely wipe sensitive in-memory credentials
    config_secure_wipe(&s_config);
    memset(s_draft_username, 0, sizeof(s_draft_username));
    memset(s_draft_api_key, 0, sizeof(s_draft_api_key));

    // 3. Badge Subsystem (Stop worker thread and free linear memory textures)
    if (s_badge_initialized)
    {
        badge_subsystem_cleanup();
        s_badge_initialized = false;
    }

    // 4. Network Subsystem
    if (s_net_initialized)
    {
        network_cleanup();
        s_net_initialized = false;
    }

    // 5. Citro2D / Citro3D UI pipeline
    if (s_ui_initialized)
    {
        ui_cleanup();
        s_ui_initialized = false;
    }

    // 6. CFGU service
    if (s_cfgu_initialized)
    {
        cfguExit();
        s_cfgu_initialized = false;
    }

    // 7. RomFS
    if (s_romfs_initialized)
    {
        romfsExit();
        s_romfs_initialized = false;
    }

    // 8. Graphics
    if (s_gfx_initialized)
    {
        gfxExit();
        s_gfx_initialized = false;
    }
}

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    init_subsystems();

    while (aptMainLoop())
    {
        s_frame_counter++;

        // 1. Poll Background Async API Worker
        async_req_type_t completed_type = ASYNC_REQ_NONE;
        char err_temp[128] = {0};
        async_api_status_t async_res = async_api_poll(
            &completed_type,
            &s_user_summary,
            &s_game_progress,
            s_search_results,
            &s_search_results_count,
            err_temp,
            sizeof(err_temp)
        );

        if (async_res == ASYNC_STATUS_SUCCESS)
        {
            s_is_loading = false;
            if (completed_type == ASYNC_REQ_USER_SUMMARY)
            {
                s_user_summary_loaded = true;
                s_app_state = APP_STATE_USER_SUMMARY;
                s_selected_index = 0;
                s_scroll_y = 0.0f;
                snprintf(s_status_msg, sizeof(s_status_msg), "Profile updated for %s", s_user_summary.username);
            }
            else if (completed_type == ASYNC_REQ_GAME_PROGRESS)
            {
                s_game_progress_loaded = true;
                s_app_state = APP_STATE_GAME_DETAIL;
                s_selected_index = 0;
                s_scroll_y = 0.0f;
                snprintf(s_status_msg, sizeof(s_status_msg), "Loaded: %s (%zu achievements)",
                         s_game_progress.title, s_game_progress.achievement_count);
            }
            else if (completed_type == ASYNC_REQ_SEARCH)
            {
                s_app_state = APP_STATE_SEARCH;
                s_selected_index = 0;
                s_scroll_y = 0.0f;
                snprintf(s_status_msg, sizeof(s_status_msg), "Found %zu match(es) for \"%s\"",
                         s_search_results_count, s_search_query);
            }
        }
        else if (async_res == ASYNC_STATUS_ERROR)
        {
            s_is_loading = false;
            if (s_app_state == APP_STATE_SETUP_CREDENTIALS)
            {
                // Auth failure during setup: remain in STATE_SETUP_CREDENTIALS and display warning
                if (strstr(err_temp, "credential") || strstr(err_temp, "Key") ||
                    strstr(err_temp, "key") || strstr(err_temp, "auth") ||
                    strstr(err_temp, "Auth") || strstr(err_temp, "401") ||
                    strstr(err_temp, "Invalid") || strstr(err_temp, "invalid"))
                {
                    snprintf(s_status_msg, sizeof(s_status_msg), "Invalid Username or API Key. Please re-enter.");
                }
                else
                {
                    snprintf(s_status_msg, sizeof(s_status_msg), "Verification failed: %s", err_temp);
                }
            }
            else
            {
                if (s_app_state != APP_STATE_SEARCH)
                {
                    s_prev_state = s_app_state;
                    s_app_state = APP_STATE_ERROR;
                }
                strncpy(s_status_msg, err_temp, sizeof(s_status_msg) - 1);
                s_status_msg[sizeof(s_status_msg) - 1] = '\0';
            }
        }
        else if (s_is_loading && s_app_state == APP_STATE_SEARCH)
        {
            char live_status[128] = {0};
            async_api_get_progress_status(live_status, sizeof(live_status));
            if (live_status[0] != '\0')
            {
                strncpy(s_status_msg, live_status, sizeof(s_status_msg) - 1);
                s_status_msg[sizeof(s_status_msg) - 1] = '\0';
            }
        }

        // 2. Poll Input Services
        hidScanInput();
        const u32 kDown = hidKeysDown();
        const u32 kHeld = hidKeysHeld();
        const u32 kUp   = hidKeysUp();

        if (kDown & KEY_START)
        {
            break; // Clean exit to Homebrew Launcher
        }

        size_t current_items = 0;
        if (s_app_state == APP_STATE_GAME_DETAIL && s_game_progress_loaded)
        {
            current_items = s_game_progress.achievement_count;
        }
        else if (s_app_state == APP_STATE_SEARCH)
        {
            current_items = s_search_results_count;
        }
        else if (s_app_state == APP_STATE_USER_SUMMARY && s_user_summary_loaded)
        {
            current_items = s_user_summary.recent_games_count;
        }

        // 3. Touch Screen Input & Gesture Handling
        if (kDown & KEY_TOUCH)
        {
            s_touch_active = true;
            hidTouchRead(&s_touch_start);
            s_touch_scroll_start = s_scroll_y;
            s_touch_dragged = false;
        }
        if (kHeld & KEY_TOUCH)
        {
            if (s_touch_active && s_settings_modal == SETTINGS_MODAL_NONE && s_app_state != APP_STATE_SETTINGS)
            {
                touchPosition cur;
                hidTouchRead(&cur);
                float dy = (float)(s_touch_start.py - cur.py);
                if (fabsf(dy) > 5.0f || s_touch_dragged)
                {
                    s_touch_dragged = true;
                    s_scroll_y = s_touch_scroll_start + dy;
                    clamp_scroll_only(current_items);
                }
            }
        }
        if (kUp & KEY_TOUCH)
        {
            if (s_touch_active)
            {
                s_touch_active = false;
                if (!s_touch_dragged)
                {
                    handle_touch_tap(s_touch_start.px, s_touch_start.py);
                }
            }
        }

        // 4. Gamepad Navigation (D-Pad & Circle Pad with hold acceleration)
        if (s_settings_modal == SETTINGS_MODAL_NONE && s_app_state != APP_STATE_SETTINGS && s_app_state != APP_STATE_SETUP_CREDENTIALS)
        {
            if (kDown & (KEY_DUP | KEY_CPAD_UP))
            {
                if (s_selected_index > 0)
                {
                    s_selected_index--;
                    clamp_scroll_and_selection(current_items);
                }
                s_key_repeat_timer = 0;
            }
            else if (kDown & (KEY_DDOWN | KEY_CPAD_DOWN))
            {
                if (s_selected_index < (int)current_items - 1)
                {
                    s_selected_index++;
                    clamp_scroll_and_selection(current_items);
                }
                s_key_repeat_timer = 0;
            }
            else if (kHeld & (KEY_DUP | KEY_CPAD_UP))
            {
                s_key_repeat_timer++;
                if (s_key_repeat_timer > 14 && (s_key_repeat_timer % 4 == 0))
                {
                    if (s_selected_index > 0)
                    {
                        s_selected_index--;
                        clamp_scroll_and_selection(current_items);
                    }
                }
            }
            else if (kHeld & (KEY_DDOWN | KEY_CPAD_DOWN))
            {
                s_key_repeat_timer++;
                if (s_key_repeat_timer > 14 && (s_key_repeat_timer % 4 == 0))
                {
                    if (s_selected_index < (int)current_items - 1)
                    {
                        s_selected_index++;
                        clamp_scroll_and_selection(current_items);
                    }
                }
            }
            else
            {
                s_key_repeat_timer = 0;
            }
        }

        // 5. Gamepad Action Mappings
        if (s_settings_modal != SETTINGS_MODAL_NONE)
        {
            if (kDown & KEY_A)
            {
                if (s_settings_modal == SETTINGS_MODAL_RESET_USER_DATA)
                {
                    execute_reset_user_data();
                }
                else if (s_settings_modal == SETTINGS_MODAL_CLEAR_CACHE)
                {
                    execute_clear_cache();
                }
            }
            else if (kDown & (KEY_B | KEY_SELECT))
            {
                s_settings_modal = SETTINGS_MODAL_NONE;
            }
        }
        else
        {
            if (kDown & KEY_SELECT)
            {
                if (s_app_state == APP_STATE_SETTINGS)
                {
                    s_app_state = s_prev_state;
                }
                else if (s_app_state != APP_STATE_SETUP_CREDENTIALS)
                {
                    s_prev_state = s_app_state;
                    s_app_state = APP_STATE_SETTINGS;
                    config_refresh_cache_size_async();
                }
            }

            if (kDown & KEY_A)
            {
                if (s_app_state == APP_STATE_SETUP_CREDENTIALS)
                {
                    if (config_validate_username(s_draft_username) && config_validate_api_key(s_draft_api_key))
                    {
                        trigger_save_and_connect();
                    }
                }
                else if (s_app_state == APP_STATE_ERROR)
                {
                    if (s_prev_state == APP_STATE_GAME_DETAIL && s_game_progress_loaded)
                    {
                        trigger_fetch_game_progress(s_game_progress.game_id);
                    }
                    else
                    {
                        trigger_fetch_user_summary();
                    }
                }
                else if (s_app_state == APP_STATE_SEARCH)
                {
                    if (s_search_results_count > 0 && s_selected_index < (int)s_search_results_count)
                    {
                        uint32_t chosen_game = s_search_results[s_selected_index].game_id;
                        trigger_fetch_game_progress(chosen_game);
                    }
                }
                else if (s_app_state == APP_STATE_USER_SUMMARY)
                {
                    if (s_user_summary_loaded && s_user_summary.recent_games_count > 0 &&
                        s_selected_index < (int)s_user_summary.recent_games_count)
                    {
                        uint32_t chosen_game = s_user_summary.recent_games[s_selected_index].game_id;
                        trigger_fetch_game_progress(chosen_game);
                    }
                    else
                    {
                        trigger_fetch_user_summary();
                    }
                }
            }

            if (kDown & KEY_B)
            {
                if (s_app_state == APP_STATE_SETTINGS)
                {
                    s_app_state = s_prev_state;
                }
                else if (s_app_state == APP_STATE_GAME_DETAIL)
                {
                    if (s_is_loading)
                    {
                        async_api_cancel();
                        s_is_loading = false;
                    }
                    s_app_state = s_prev_state;
                    s_selected_index = 0;
                    s_scroll_y = 0.0f;
                }
                else if (s_app_state == APP_STATE_SEARCH)
                {
                    if (s_is_loading)
                    {
                        async_api_cancel();
                        s_is_loading = false;
                    }
                    s_app_state = APP_STATE_USER_SUMMARY;
                    s_selected_index = 0;
                    s_scroll_y = 0.0f;
                }
                else if (s_app_state == APP_STATE_ERROR)
                {
                    s_app_state = APP_STATE_USER_SUMMARY;
                    snprintf(s_status_msg, sizeof(s_status_msg), "Returned to Profile Overview.");
                }
            }

            if (kDown & KEY_X)
            {
                if (s_app_state == APP_STATE_SETUP_CREDENTIALS)
                {
                    launch_username_keyboard();
                }
                else if (s_app_state == APP_STATE_GAME_DETAIL)
                {
                    if (s_game_progress_loaded)
                    {
                        trigger_fetch_game_progress(s_game_progress.game_id);
                    }
                }
                else if (s_app_state != APP_STATE_SETTINGS)
                {
                    trigger_fetch_user_summary();
                }
            }

            if (kDown & KEY_Y)
            {
                if (s_app_state == APP_STATE_SETUP_CREDENTIALS)
                {
                    launch_api_key_keyboard();
                }
                else if (s_app_state == APP_STATE_ERROR)
                {
                    s_config_status = config_load(&s_config);
                    s_cache_size_mb = config_get_cache_size_mb();
                    if (config_needs_setup(&s_config))
                    {
                        s_app_state = APP_STATE_SETUP_CREDENTIALS;
                        snprintf(s_status_msg, sizeof(s_status_msg), "Please configure RetroAchievements credentials.");
                    }
                    else
                    {
                        snprintf(s_status_msg, sizeof(s_status_msg), "Config reloaded for user: %s", s_config.username);
                        s_app_state = APP_STATE_USER_SUMMARY;
                        trigger_fetch_user_summary();
                    }
                }
                else if (s_app_state != APP_STATE_SETTINGS)
                {
                    launch_search_keyboard();
                }
            }
        }

        // 6. Citro3D Dual-Screen Frame Rendering Pass (Fixed 60 FPS VSync)
        if (s_app_state == APP_STATE_SETTINGS)
        {
            s_cache_size_mb = config_get_cache_size_mb();
        }

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        ui_frame_begin();

        ui_render_top_screen(
            s_app_state,
            s_is_loading,
            &s_config,
            &s_user_summary,
            s_user_summary_loaded,
            &s_game_progress,
            s_game_progress_loaded,
            s_selected_index,
            s_status_msg,
            s_search_query,
            s_search_results,
            s_search_results_count,
            s_draft_username,
            s_draft_api_key,
            s_cache_size_mb,
            s_frame_counter
        );

        ui_render_bottom_screen(
            s_app_state,
            s_is_loading,
            &s_config,
            &s_user_summary,
            s_user_summary_loaded,
            &s_game_progress,
            s_game_progress_loaded,
            s_scroll_y,
            s_selected_index,
            s_status_msg,
            s_search_query,
            s_search_results,
            s_search_results_count,
            s_draft_username,
            s_draft_api_key,
            s_settings_modal,
            s_frame_counter
        );

        C3D_FrameEnd(0);
    }

    cleanup_subsystems();
    return 0;
}
