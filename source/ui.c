/**
 * @file ui.c
 * @brief RetroAchievements 3DS - Phase 5: UI Engine & Rendering Implementation
 */

#include "ui.h"
#include "badge.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static C3D_RenderTarget* s_top_target    = NULL;
static C3D_RenderTarget* s_bottom_target = NULL;
static C2D_TextBuf       s_text_buf      = NULL;

bool ui_init(void)
{
    // Initialize Citro3D and Citro2D
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE))
    {
        return false;
    }

    if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS))
    {
        C3D_Fini();
        return false;
    }

    C2D_Prepare();

    // Create dual-screen render targets (Top left eye, Bottom)
    s_top_target    = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    s_bottom_target = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    if (s_top_target == NULL || s_bottom_target == NULL)
    {
        ui_cleanup();
        return false;
    }

    // Allocate dynamic text buffer for glyph parsing (4096 glyphs per frame)
    s_text_buf = C2D_TextBufNew(4096);
    if (s_text_buf == NULL)
    {
        ui_cleanup();
        return false;
    }

    return true;
}

void ui_cleanup(void)
{
    if (s_text_buf != NULL)
    {
        C2D_TextBufDelete(s_text_buf);
        s_text_buf = NULL;
    }

    s_top_target    = NULL;
    s_bottom_target = NULL;

    C2D_Fini();
    C3D_Fini();
}

void ui_frame_begin(void)
{
    if (s_text_buf != NULL)
    {
        C2D_TextBufClear(s_text_buf);
    }
}

C3D_RenderTarget* ui_get_top_target(void)
{
    return s_top_target;
}

C3D_RenderTarget* ui_get_bottom_target(void)
{
    return s_bottom_target;
}

void ui_draw_text(float x, float y, float scale, u32 color, const char* text)
{
    if (text == NULL || text[0] == '\0' || s_text_buf == NULL)
    {
        return;
    }

    C2D_Text c2d_txt;
    if (C2D_TextParse(&c2d_txt, s_text_buf, text) != NULL)
    {
        C2D_TextOptimize(&c2d_txt);
        C2D_DrawText(&c2d_txt, C2D_WithColor, x, y, 0.5f, scale, scale, color);
    }
}

void ui_draw_text_wrapped(float x, float y, float scale, float max_width, u32 color, const char* text)
{
    if (text == NULL || text[0] == '\0' || s_text_buf == NULL)
    {
        return;
    }

    C2D_Text c2d_txt;
    if (C2D_TextParse(&c2d_txt, s_text_buf, text) != NULL)
    {
        C2D_TextOptimize(&c2d_txt);
        C2D_DrawText(&c2d_txt, C2D_WithColor | C2D_WordWrap, x, y, 0.5f, scale, scale, color, max_width);
    }
}

static void draw_signal_bars(float x, float y, u8 strength)
{
    for (int i = 0; i < 3; i++)
    {
        u32 bar_clr = (strength > i) ? UI_CLR_GOLD : UI_CLR_BORDER;
        float h = 6.0f + (i * 4.0f);
        C2D_DrawRectSolid(x + (i * 6.0f), y + (14.0f - h), 0.5f, 4.0f, h, bar_clr);
    }
}

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
    uint32_t frame_count
)
{
    if (s_top_target == NULL)
    {
        return;
    }

    C2D_TargetClear(s_top_target, UI_CLR_BG);
    C2D_SceneBegin(s_top_target);

    // Header Banner (y: 0..30)
    C2D_DrawRectSolid(0, 0, 0.1f, UI_SCREEN_TOP_W, 30, UI_CLR_BANNER);
    C2D_DrawRectSolid(0, 29, 0.1f, UI_SCREEN_TOP_W, 2, UI_CLR_GOLD);

    ui_draw_text(12, 6, 0.52f, UI_CLR_GOLD, "RETROACHIEVEMENTS");

    u8 wifi_strength = 0;
    network_get_status(&wifi_strength);
    draw_signal_bars(368, 8, wifi_strength);

    // Animated loading indicator in top header when background API or image is syncing
    if (is_loading)
    {
        float angle = ((float)(frame_count % 60) / 60.0f) * 6.283185f;
        float cx = 348.0f, cy = 15.0f, r = 5.0f;
        C2D_DrawCircleSolid(cx, cy, 0.4f, 2.0f, UI_CLR_BORDER);
        C2D_DrawCircleSolid(cx + cosf(angle) * r, cy + sinf(angle) * r, 0.5f, 2.5f, UI_CLR_GOLD);
    }

    // Card 1: User Profile Summary (y: 36..114, h: 78)
    C2D_DrawRectSolid(10, 36, 0.1f, 380, 78, UI_CLR_CARD);
    C2D_DrawRectSolid(10, 36, 0.2f, 4, 78, summary_loaded ? UI_CLR_GOLD : UI_CLR_BORDER);

    // User Avatar Display
    if (summary_loaded && summary != NULL)
    {
        C2D_Image avatar_img = user_get_avatar(summary->user_pic, summary->username);
        C2D_DrawImageAt(avatar_img, 22.0f, 53.0f, 0.3f, NULL, 44.0f / (float)BADGE_TEX_SIZE, 44.0f / (float)BADGE_TEX_SIZE);

        ui_draw_text(76, 42, 0.55f, UI_CLR_TEXT_WHITE, summary->username);

        char stats_line[96];
        snprintf(stats_line, sizeof(stats_line), "%lu pts  (Softcore: %lu)",
                 (unsigned long)summary->total_points, (unsigned long)summary->total_softcore_points);
        ui_draw_text(76, 62, 0.43f, UI_CLR_GOLD, stats_line);

        char rank_line[64];
        snprintf(rank_line, sizeof(rank_line), "Rank #%lu of %lu",
                 (unsigned long)summary->rank, (unsigned long)summary->total_ranked);
        ui_draw_text(76, 78, 0.40f, UI_CLR_BLUE, rank_line);

        if (summary->rich_presence[0] != '\0')
        {
            char act_line[80];
            snprintf(act_line, sizeof(act_line), "Activity: %.45s", summary->rich_presence);
            ui_draw_text(76, 94, 0.38f, UI_CLR_TEXT_DIM, act_line);
        }
    }
    else
    {
        C2D_DrawCircleSolid(44, 75, 0.3f, 22, UI_CLR_BANNER);
        C2D_DrawCircleSolid(44, 75, 0.4f, 20, summary_loaded ? UI_CLR_GOLD_DIM : UI_CLR_CARD);

        ui_draw_text(76, 48, 0.50f, UI_CLR_TEXT_WHITE, (cfg && cfg->is_configured) ? cfg->username : "Unconfigured User");
        ui_draw_text(76, 68, 0.42f, UI_CLR_TEXT_DIM, "Press [A] to sync profile summary.");
        ui_draw_text(76, 86, 0.38f, UI_CLR_GOLD_DIM, "Edit credentials at sdmc:/3ds/retroachievements/config.ini");
    }

    // Card 2: Context / Detail Card (y: 120..232, h: 112)
    C2D_DrawRectSolid(10, 120, 0.1f, 380, 112, UI_CLR_CARD);

    if (state == APP_STATE_ERROR)
    {
        C2D_DrawRectSolid(10, 120, 0.2f, 4, 112, UI_CLR_RED);
        ui_draw_text(24, 126, 0.52f, UI_CLR_RED, "! SYSTEM NOTICE / ERROR");

        const char* err_text = (status_message && status_message[0]) ? status_message : "An unexpected error occurred.";
        ui_draw_text_wrapped(24, 148, 0.40f, 350.0f, UI_CLR_TEXT_WHITE, err_text);

        ui_draw_text(24, 204, 0.36f, UI_CLR_GOLD, "Use touch controls or buttons on the bottom screen to recover.");
    }
    else if (state == APP_STATE_GAME_DETAIL && progress_loaded && progress != NULL)
    {
        C2D_DrawRectSolid(10, 120, 0.2f, 4, 112, UI_CLR_GREEN);

        if (selected_index >= 0 && selected_index < (int)progress->achievement_count)
        {
            const ra_achievement_t* ach = &progress->achievements[selected_index];

            // 48x48 Badge Texture with border frame on right side of detail card
            C2D_Image badge_img = badge_get_image(ach->badge_name, ach->is_unlocked);
            C2D_DrawRectSolid(329.0f, 127.0f, 0.25f, 50.0f, 50.0f, ach->is_unlocked ? UI_CLR_GOLD_DIM : UI_CLR_BORDER);
            C2D_DrawImageAt(badge_img, 330.0f, 128.0f, 0.3f, NULL, 48.0f / (float)BADGE_TEX_SIZE, 48.0f / (float)BADGE_TEX_SIZE);

            // Achievement Title & Points (wrapped to 298px to ensure no overlap with icon)
            char ach_hdr[128];
            snprintf(ach_hdr, sizeof(ach_hdr), "%s (%u pts)", ach->title, ach->points);
            ui_draw_text_wrapped(22, 126, 0.48f, 298.0f, UI_CLR_TEXT_WHITE, ach_hdr);

            // Hardcore / Status tag
            if (ach->is_hardcore)
            {
                ui_draw_text(22, 144, 0.40f, UI_CLR_GREEN, "[UNLOCKED - HARDCORE]");
            }
            else if (ach->is_unlocked)
            {
                ui_draw_text(22, 144, 0.40f, UI_CLR_BLUE, "[UNLOCKED - SOFTCORE]");
            }
            else
            {
                ui_draw_text(22, 144, 0.40f, UI_CLR_RED, "[LOCKED]");
            }

            // Description (Wrapped text constrained to 298px before icon)
            ui_draw_text_wrapped(22, 160, 0.38f, 298.0f, UI_CLR_TEXT_DIM, ach->description);

            // Unlock timestamp if earned
            if (ach->is_unlocked && ach->date_earned[0] != '\0')
            {
                char date_line[64];
                snprintf(date_line, sizeof(date_line), "Unlocked on: %s", ach->date_earned);
                ui_draw_text(22, 212, 0.35f, UI_CLR_TEXT_DIM, date_line);
            }
        }
        else
        {
            // Game Overview banner & Game Icon
            C2D_Image game_icon = game_get_icon(progress->image_icon);
            C2D_DrawRectSolid(329.0f, 125.0f, 0.25f, 48.0f, 48.0f, UI_CLR_BORDER);
            C2D_DrawImageAt(game_icon, 331.0f, 127.0f, 0.3f, NULL, 44.0f / (float)BADGE_TEX_SIZE, 44.0f / (float)BADGE_TEX_SIZE);

            ui_draw_text_wrapped(22, 126, 0.50f, 300.0f, UI_CLR_TEXT_WHITE, progress->title);

            char console_line[96];
            snprintf(console_line, sizeof(console_line), "Console: %s  (GameID: %lu)",
                     progress->console_name, (unsigned long)progress->game_id);
            ui_draw_text(22, 146, 0.40f, UI_CLR_TEXT_DIM, console_line);

            // Progress bar (constrained to 300px to stop before icon)
            C2D_DrawRectSolid(22, 170, 0.2f, 300.0f, 12.0f, UI_CLR_PROGRESS_BG);
            if (progress->num_achievements > 0)
            {
                float ratio = (float)progress->num_awarded / (float)progress->num_achievements;
                if (ratio > 1.0f) ratio = 1.0f;
                C2D_DrawRectSolid(22, 170, 0.3f, 300.0f * ratio, 12.0f, UI_CLR_GOLD);
            }

            char comp_line[96];
            snprintf(comp_line, sizeof(comp_line), "%lu / %lu Achievements Unlocked (%s)",
                     (unsigned long)progress->num_awarded,
                     (unsigned long)progress->num_achievements,
                     progress->user_completion);
            ui_draw_text(22, 192, 0.40f, UI_CLR_GOLD, comp_line);
        }
    }
    else if (state == APP_STATE_USER_SUMMARY)
    {
        C2D_DrawRectSolid(10, 120, 0.2f, 4, 112, UI_CLR_BLUE);

        if (summary_loaded && summary != NULL && summary->recent_games_count > 0 &&
            selected_index >= 0 && selected_index < (int)summary->recent_games_count)
        {
            const ra_recent_game_t* g = &summary->recent_games[selected_index];
            C2D_Image game_icon = game_get_icon(g->image_icon);
            C2D_DrawRectSolid(329.0f, 125.0f, 0.25f, 48.0f, 48.0f, UI_CLR_BORDER);
            C2D_DrawImageAt(game_icon, 331.0f, 127.0f, 0.3f, NULL, 44.0f / (float)BADGE_TEX_SIZE, 44.0f / (float)BADGE_TEX_SIZE);

            ui_draw_text_wrapped(22, 126, 0.50f, 300.0f, UI_CLR_TEXT_WHITE, g->title);

            char console_line[96];
            snprintf(console_line, sizeof(console_line), "Platform: %s  (GameID: %lu)",
                     g->console_name, (unsigned long)g->game_id);
            ui_draw_text(22, 148, 0.40f, UI_CLR_BLUE, console_line);

            if (g->last_played[0] != '\0')
            {
                char date_str[64];
                snprintf(date_str, sizeof(date_str), "Last played: %s", g->last_played);
                ui_draw_text(22, 168, 0.38f, UI_CLR_TEXT_DIM, date_str);
            }

            ui_draw_text(22, 196, 0.40f, UI_CLR_GOLD, "Press [A] or tap row to view achievements for this game.");
        }
        else
        {
            ui_draw_text(24, 136, 0.50f, UI_CLR_TEXT_WHITE, "Profile Overview");
            ui_draw_text(24, 160, 0.40f, UI_CLR_TEXT_DIM, "Select a recent game from the bottom list to inspect.");
            ui_draw_text(24, 184, 0.38f, UI_CLR_GOLD, "Press [X] or tap [Sync] to fetch data from RetroAchievements.");
        }
    }
    else // APP_STATE_INIT
    {
        C2D_DrawRectSolid(10, 120, 0.2f, 4, 112, UI_CLR_GOLD);
        ui_draw_text(24, 136, 0.50f, UI_CLR_GOLD, "Initializing Application...");
        ui_draw_text(24, 162, 0.40f, UI_CLR_TEXT_DIM, "Checking network connectivity and configuration...");
        ui_draw_text(24, 186, 0.38f, UI_CLR_TEXT_DIM, "Please wait while services initialize.");
    }
}

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
    uint32_t frame_count
)
{
    if (s_bottom_target == NULL)
    {
        return;
    }

    C2D_TargetClear(s_bottom_target, UI_CLR_BG);
    C2D_SceneBegin(s_bottom_target);

    // Header bar (y: 0..26)
    C2D_DrawRectSolid(0, 0, 0.1f, UI_SCREEN_BOT_W, 26, UI_CLR_BANNER);
    C2D_DrawRectSolid(0, 25, 0.1f, UI_SCREEN_BOT_W, 1, UI_CLR_BORDER);

    if (state == APP_STATE_ERROR)
    {
        ui_draw_text(8, 6, 0.45f, UI_CLR_RED, "SYSTEM RECOVERY & ACTIONS");
    }
    else if (state == APP_STATE_GAME_DETAIL)
    {
        // Virtual Back Button on Left: x in [4, 68], y in [2, 23]
        C2D_DrawRectSolid(4, 3, 0.2f, 64, 20, UI_CLR_CARD);
        C2D_DrawRectSolid(4, 3, 0.25f, 64, 20, UI_CLR_BORDER);
        ui_draw_text(10, 5, 0.40f, UI_CLR_GOLD, "< Back");

        char ach_hdr[64];
        snprintf(ach_hdr, sizeof(ach_hdr), "ACHIEVEMENTS (%zu)",
                 (progress_loaded && progress) ? progress->achievement_count : 0);
        ui_draw_text(76, 6, 0.42f, UI_CLR_TEXT_WHITE, ach_hdr);

        // Virtual Sync Button on Right: x in [252, 316], y in [2, 23]
        C2D_DrawRectSolid(252, 3, 0.2f, 64, 20, UI_CLR_CARD);
        C2D_DrawRectSolid(252, 3, 0.25f, 64, 20, UI_CLR_BORDER);
        ui_draw_text(264, 5, 0.38f, UI_CLR_BLUE, "Sync");
    }
    else // APP_STATE_USER_SUMMARY or APP_STATE_INIT
    {
        char games_hdr[64];
        snprintf(games_hdr, sizeof(games_hdr), "RECENT GAMES (%zu)",
                 (summary_loaded && summary) ? summary->recent_games_count : 0);
        ui_draw_text(8, 6, 0.45f, UI_CLR_GOLD, games_hdr);

        // Virtual Sync Button on Right: x in [252, 316], y in [2, 23]
        C2D_DrawRectSolid(252, 3, 0.2f, 64, 20, UI_CLR_CARD);
        C2D_DrawRectSolid(252, 3, 0.25f, 64, 20, UI_CLR_BORDER);
        ui_draw_text(264, 5, 0.38f, UI_CLR_BLUE, "Sync");
    }

    if (state == APP_STATE_ERROR)
    {
        // Interactive touch cards for error recovery
        // Card 1: Retry (y: 40..84)
        C2D_DrawRectSolid(16, 40, 0.1f, 288, 44, UI_CLR_CARD);
        C2D_DrawRectSolid(16, 40, 0.2f, 4, 44, UI_CLR_GOLD);
        ui_draw_text(30, 48, 0.48f, UI_CLR_GOLD, "[A] Retry Connection / Sync");
        ui_draw_text(30, 66, 0.36f, UI_CLR_TEXT_DIM, "Attempt network operation again");

        // Card 2: Return to Profile (y: 94..138)
        C2D_DrawRectSolid(16, 94, 0.1f, 288, 44, UI_CLR_CARD);
        C2D_DrawRectSolid(16, 94, 0.2f, 4, 44, UI_CLR_BLUE);
        ui_draw_text(30, 102, 0.48f, UI_CLR_BLUE, "[B] Return to User Profile");
        ui_draw_text(30, 120, 0.36f, UI_CLR_TEXT_DIM, "Dismiss error screen to profile summary");

        // Card 3: Reload Config (y: 148..192)
        C2D_DrawRectSolid(16, 148, 0.1f, 288, 44, UI_CLR_CARD);
        C2D_DrawRectSolid(16, 148, 0.2f, 4, 44, UI_CLR_GREEN);
        ui_draw_text(30, 156, 0.48f, UI_CLR_GREEN, "[Y] Reload config.ini");
        ui_draw_text(30, 174, 0.36f, UI_CLR_TEXT_DIM, "Reload credentials from SD card");
    }
    else
    {
        // Scroll View Window: y in [28.0f, 214.0f]
        const float view_top = 28.0f;
        const float view_bottom = 214.0f;
        const float view_height = view_bottom - view_top;

        size_t total_items = 0;
        if (state == APP_STATE_GAME_DETAIL && progress_loaded && progress)
        {
            total_items = progress->achievement_count;
        }
        else if (summary_loaded && summary)
        {
            total_items = summary->recent_games_count;
        }

        // List rendering with strict off-screen culling
        if (total_items > 0)
        {
            for (size_t i = 0; i < total_items; i++)
            {
                float item_y = view_top + ((float)i * UI_ITEM_HEIGHT) - scroll_y;

                if ((item_y + UI_ITEM_HEIGHT < view_top) || (item_y > view_bottom))
                {
                    continue;
                }

                bool is_selected = (selected_index == (int)i);
                u32 card_color = is_selected ? UI_CLR_CARD_SEL : UI_CLR_CARD;

                C2D_DrawRectSolid(6, item_y, 0.1f, 304, UI_ITEM_HEIGHT - 2.0f, card_color);

                if (is_selected)
                {
                    C2D_DrawRectSolid(6, item_y, 0.2f, 3, UI_ITEM_HEIGHT - 2.0f, UI_CLR_GOLD);
                }

                if (state == APP_STATE_GAME_DETAIL && progress)
                {
                    const ra_achievement_t* ach = &progress->achievements[i];
                    C2D_Image badge_img = badge_get_image(ach->badge_name, ach->is_unlocked);
                    C2D_DrawImageAt(badge_img, 14.0f, item_y + 4.0f, 0.3f, NULL, 32.0f / (float)BADGE_TEX_SIZE, 32.0f / (float)BADGE_TEX_SIZE);

                    u32 text_clr = ach->is_unlocked ? UI_CLR_TEXT_WHITE : UI_CLR_TEXT_DIM;
                    ui_draw_text(52, item_y + 6.0f, 0.44f, text_clr, ach->title);

                    char pts_str[32];
                    snprintf(pts_str, sizeof(pts_str), "%u pts", ach->points);
                    ui_draw_text(52, item_y + 22.0f, 0.38f, UI_CLR_GOLD, pts_str);

                    if (ach->is_hardcore)
                    {
                        ui_draw_text(230, item_y + 12.0f, 0.38f, UI_CLR_GREEN, "HARDCORE");
                    }
                    else if (ach->is_unlocked)
                    {
                        ui_draw_text(236, item_y + 12.0f, 0.38f, UI_CLR_BLUE, "UNLOCKED");
                    }
                    else
                    {
                        ui_draw_text(250, item_y + 12.0f, 0.38f, UI_CLR_RED, "LOCKED");
                    }
                }
                else if (summary)
                {
                    const ra_recent_game_t* g = &summary->recent_games[i];
                    C2D_Image icon_img = game_get_icon(g->image_icon);
                    C2D_DrawImageAt(icon_img, 14.0f, item_y + 4.0f, 0.3f, NULL, 32.0f / (float)BADGE_TEX_SIZE, 32.0f / (float)BADGE_TEX_SIZE);

                    ui_draw_text(52, item_y + 6.0f, 0.44f, UI_CLR_TEXT_WHITE, g->title);
                    ui_draw_text(52, item_y + 22.0f, 0.38f, UI_CLR_BLUE, g->console_name);

                    if (g->last_played[0] != '\0')
                    {
                        char date_short[20];
                        snprintf(date_short, sizeof(date_short), "%.10s", g->last_played);
                        ui_draw_text(230, item_y + 12.0f, 0.36f, UI_CLR_TEXT_DIM, date_short);
                    }
                }
            }

            // Scrollbar on right edge
            float total_list_height = (float)total_items * UI_ITEM_HEIGHT;
            if (total_list_height > view_height)
            {
                C2D_DrawRectSolid(314, view_top, 0.1f, UI_SCROLLBAR_WIDTH, view_height, UI_CLR_PROGRESS_BG);

                float thumb_ratio = view_height / total_list_height;
                float thumb_h = view_height * thumb_ratio;
                if (thumb_h < 12.0f) thumb_h = 12.0f;

                float max_scroll = total_list_height - view_height;
                float thumb_y = view_top + ((scroll_y / max_scroll) * (view_height - thumb_h));

                C2D_DrawRectSolid(314, thumb_y, 0.2f, UI_SCROLLBAR_WIDTH, thumb_h, UI_CLR_GOLD);
            }
        }
        else
        {
            if (is_loading)
            {
                float angle = ((float)(frame_count % 60) / 60.0f) * 6.283185f;
                float cx = 160.0f, cy = 100.0f, r = 12.0f;
                C2D_DrawCircleSolid(cx, cy, 0.4f, 4.0f, UI_CLR_BORDER);
                C2D_DrawCircleSolid(cx + cosf(angle) * r, cy + sinf(angle) * r, 0.5f, 5.0f, UI_CLR_GOLD);
                ui_draw_text(85, 126, 0.46f, UI_CLR_BLUE, "Loading items from API...");
            }
            else
            {
                ui_draw_text(60, 90, 0.46f, UI_CLR_TEXT_DIM, "No items available to display.");
                ui_draw_text(40, 115, 0.40f, UI_CLR_GOLD, "Press [A] or tap [Sync] to fetch RetroAchievements data.");
            }
        }
    }

    // Bottom Navigation Bar (y: 216..240)
    C2D_DrawRectSolid(0, 216, 0.1f, UI_SCREEN_BOT_W, 24, UI_CLR_BANNER);
    C2D_DrawRectSolid(0, 216, 0.2f, UI_SCREEN_BOT_W, 1, UI_CLR_BORDER);

    if (state == APP_STATE_ERROR)
    {
        ui_draw_text(8, 222, 0.35f, UI_CLR_TEXT_DIM, "[A] Retry  [B] Back  [Y] Reload Config  [START] Exit");
    }
    else if (state == APP_STATE_GAME_DETAIL)
    {
        ui_draw_text(8, 222, 0.35f, UI_CLR_TEXT_DIM, "[D-Pad] Scroll  [A] Inspect  [B] Back  [X] Sync  [START] Exit");
    }
    else if (state == APP_STATE_USER_SUMMARY)
    {
        ui_draw_text(8, 222, 0.35f, UI_CLR_TEXT_DIM, "[D-Pad] Scroll  [A] Select Game  [X] Sync  [START] Exit");
    }
    else
    {
        ui_draw_text(8, 222, 0.35f, UI_CLR_TEXT_DIM, "Loading RetroAchievements for Nintendo 3DS...");
    }
}
