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

void ui_draw_text_depth(float x, float y, float depth, float scale, u32 color, const char* text)
{
    if (text == NULL || text[0] == '\0' || s_text_buf == NULL)
    {
        return;
    }

    C2D_Text c2d_txt;
    if (C2D_TextParse(&c2d_txt, s_text_buf, text) != NULL)
    {
        C2D_TextOptimize(&c2d_txt);
        C2D_DrawText(&c2d_txt, C2D_WithColor, x, y, depth, scale, scale, color);
    }
}

void ui_draw_text(float x, float y, float scale, u32 color, const char* text)
{
    ui_draw_text_depth(x, y, 0.5f, scale, color, text);
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

void ui_draw_search_icon(float x, float y, u32 color)
{
    // x, y is center of icon
    float lens_x = x - 2.0f;
    float lens_y = y - 2.0f;
    C2D_DrawCircleSolid(lens_x, lens_y, 0.65f, 4.8f, color);
    C2D_DrawCircleSolid(lens_x, lens_y, 0.66f, 2.6f, UI_CLR_CARD);

    // Diagonal handle
    C2D_DrawLine(x + 1.2f, y + 1.2f, color, x + 5.8f, y + 5.8f, color, 2.2f, 0.67f);
}

void ui_draw_reload_icon(float x, float y, u32 color)
{
    // x, y is center of icon
    C2D_DrawCircleSolid(x, y, 0.65f, 5.0f, color);
    C2D_DrawCircleSolid(x, y, 0.66f, 2.8f, UI_CLR_CARD);

    // Cutout top-left notch
    C2D_DrawRectSolid(x - 5.5f, y - 5.5f, 0.67f, 5.5f, 4.5f, UI_CLR_CARD);

    // Triangle arrow head pointing clockwise
    C2D_DrawTriangle(
        x - 1.0f, y - 6.5f, color,
        x + 3.5f, y - 4.0f, color,
        x - 1.0f, y - 1.5f, color,
        0.68f
    );
}

void ui_draw_settings_icon(float x, float y, u32 color)
{
    // x, y is center of icon (bounds: 32x22 area centered at 228.0f, 13.0f)
    // Cross bars for gear teeth
    C2D_DrawRectSolid(x - 5.5f, y - 1.5f, 0.65f, 11.0f, 3.0f, color);
    C2D_DrawRectSolid(x - 1.5f, y - 5.5f, 0.65f, 3.0f, 11.0f, color);
    C2D_DrawLine(x - 3.8f, y - 3.8f, color, x + 3.8f, y + 3.8f, color, 3.0f, 0.65f);
    C2D_DrawLine(x - 3.8f, y + 3.8f, color, x + 3.8f, y - 3.8f, color, 3.0f, 0.65f);

    // Gear body
    C2D_DrawCircleSolid(x, y, 0.66f, 4.5f, color);

    // Inner hole cutout
    C2D_DrawCircleSolid(x, y, 0.67f, 2.0f, UI_CLR_CARD);
}

void ui_draw_back_icon(float x, float y, u32 color)
{
    // x, y is center of icon (bounds: 32x22 area centered at 20.0f, 13.0f)
    // Left-pointing arrowhead
    C2D_DrawTriangle(
        x - 5.5f, y,        color,
        x - 0.5f, y - 4.5f, color,
        x - 0.5f, y + 4.5f, color,
        0.66f
    );

    // Horizontal stem line connecting to arrowhead
    C2D_DrawLine(x - 1.5f, y, color, x + 5.0f, y, color, 2.4f, 0.66f);
}

static inline u32 ui_color_lerp(u32 c1, u32 c2, float t)
{
    if (t <= 0.0f) return c1;
    if (t >= 1.0f) return c2;
    u32 r1 = c1 & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = (c1 >> 16) & 0xFF, a1 = (c1 >> 24) & 0xFF;
    u32 r2 = c2 & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = (c2 >> 16) & 0xFF, a2 = (c2 >> 24) & 0xFF;
    u32 r = (u32)((float)r1 + (float)((int)r2 - (int)r1) * t);
    u32 g = (u32)((float)g1 + (float)((int)g2 - (int)g1) * t);
    u32 b = (u32)((float)b1 + (float)((int)b2 - (int)b1) * t);
    u32 a = (u32)((float)a1 + (float)((int)a2 - (int)a1) * t);
    return C2D_Color32(r, g, b, a);
}

static inline u32 ui_color_alpha(u32 c, u8 alpha)
{
    return (c & 0x00FFFFFF) | ((u32)alpha << 24);
}

void ui_draw_glass_panel(float x, float y, float z, float w, float h, u32 accent_color, bool is_selected, float pulse)
{
    // 1. Dark obsidian frosted glass base (semi-transparent high-contrast base)
    u32 base_clr = is_selected ? UI_CLR_CYBER_PANEL_SEL : UI_CLR_CYBER_PANEL;
    C2D_DrawRectSolid(x, y, z, w, h, base_clr);

    // 2. Progressive alpha inner glow (1px inset)
    u8 inner_alpha = is_selected ? (u8)(35 + (u8)(35.0f * pulse)) : 18;
    u32 inner_glow = ui_color_alpha(accent_color, inner_alpha);
    C2D_DrawRectSolid(x + 1.0f, y + 1.0f, z + 0.01f, w - 2.0f, 1.0f, inner_glow);
    C2D_DrawRectSolid(x + 1.0f, y + h - 2.0f, z + 0.01f, w - 2.0f, 1.0f, inner_glow);
    C2D_DrawRectSolid(x + 1.0f, y + 1.0f, z + 0.01f, 1.0f, h - 2.0f, inner_glow);
    C2D_DrawRectSolid(x + w - 2.0f, y + 1.0f, z + 0.01f, 1.0f, h - 2.0f, inner_glow);

    // 3. Cyber neon outer outline / bounding box
    u32 border_clr;
    if (is_selected)
    {
        // Pulsating neon glow between accent color and bright tint
        border_clr = ui_color_lerp(accent_color, UI_CLR_CYBER_WHITE, 0.35f * pulse);
    }
    else
    {
        border_clr = UI_CLR_CYBER_BORDER;
    }
    C2D_DrawRectSolid(x, y, z + 0.02f, w, 1.0f, border_clr);
    C2D_DrawRectSolid(x, y + h - 1.0f, z + 0.02f, w, 1.0f, border_clr);
    C2D_DrawRectSolid(x, y, z + 0.02f, 1.0f, h, border_clr);
    C2D_DrawRectSolid(x + w - 1.0f, y, z + 0.02f, 1.0f, h, border_clr);

    // 4. Specular glass highlight glare (top edge 15-20% white sheen + angled micro-stroke)
    float sheen_w = (w > 48.0f) ? 42.0f : (w - 4.0f);
    C2D_DrawRectSolid(x + 2.0f, y + 1.0f, z + 0.03f, sheen_w, 1.0f, UI_CLR_CYBER_SHEEN);
    C2D_DrawLine(x + 2.0f, y + 4.0f, UI_CLR_CYBER_SHEEN, x + 6.0f, y + 1.0f, C2D_Color32(0xFF, 0xFF, 0xFF, 0x06), 1.0f, z + 0.03f);

    // 5. Left cyber neon accent strip & corner brackets
    float strip_w = is_selected ? 4.0f : 3.0f;
    u32 strip_clr = is_selected ? ui_color_lerp(accent_color, UI_CLR_CYBER_WHITE, 0.25f * pulse) : accent_color;
    C2D_DrawRectSolid(x, y, z + 0.04f, strip_w, h, strip_clr);

    // Corner bracket accents on selected items (classic cyberpunk HUD look)
    if (is_selected)
    {
        C2D_DrawRectSolid(x + w - 6.0f, y, z + 0.04f, 6.0f, 2.0f, strip_clr);
        C2D_DrawRectSolid(x + w - 2.0f, y, z + 0.04f, 2.0f, 6.0f, strip_clr);
    }
}

static void ui_draw_cyber_grid(float screen_w, float screen_h)
{
    for (float gx = 16.0f; gx < screen_w; gx += 24.0f)
    {
        C2D_DrawRectSolid(gx, 0.0f, 0.04f, 1.0f, screen_h, UI_CLR_CYBER_GRID);
    }
    for (float gy = 16.0f; gy < screen_h; gy += 24.0f)
    {
        C2D_DrawRectSolid(0.0f, gy, 0.04f, screen_w, 1.0f, UI_CLR_CYBER_GRID);
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
    const char* search_query,
    const search_result_item_t* search_results,
    size_t search_results_count,
    const char* draft_username,
    const char* draft_api_key,
    float cache_size_mb,
    uint32_t frame_count
)
{
    if (s_top_target == NULL)
    {
        return;
    }

    // Shared frame-based sine wave counter (bounded modulo prevents precision loss)
    float phase = ((float)(frame_count % 180) / 180.0f) * 6.2831853f;
    float pulse = 0.5f + 0.5f * sinf(phase);

    C2D_TargetClear(s_top_target, UI_CLR_CYBER_BG);
    C2D_SceneBegin(s_top_target);

    // Cyberpunk cyberspace backdrop grid
    ui_draw_cyber_grid(UI_SCREEN_TOP_W, UI_SCREEN_TOP_H);

    // Header Banner (y: 0..30)
    C2D_DrawRectSolid(0, 0, 0.1f, UI_SCREEN_TOP_W, 30, UI_CLR_CYBER_BANNER);
    u32 banner_border = ui_color_lerp(UI_CLR_CYBER_CYAN, UI_CLR_CYBER_PINK, 0.35f * pulse);
    C2D_DrawRectSolid(0, 29, 0.12f, UI_SCREEN_TOP_W, 1.5f, banner_border);

    ui_draw_text(12, 6, 0.52f, UI_CLR_CYBER_CYAN, "RETROACHIEVEMENTS");

    u8 wifi_strength = 0;
    network_get_status(&wifi_strength);
    draw_signal_bars(368, 8, wifi_strength);

    // Animated loading indicator in top header when background API or image is syncing
    if (is_loading)
    {
        float angle = ((float)(frame_count % 60) / 60.0f) * 6.283185f;
        float cx = 348.0f, cy = 15.0f, r = 5.0f;
        C2D_DrawCircleSolid(cx, cy, 0.4f, 2.0f, UI_CLR_CYBER_BORDER);
        C2D_DrawCircleSolid(cx + cosf(angle) * r, cy + sinf(angle) * r, 0.5f, 2.5f, UI_CLR_CYBER_CYAN);
    }

    if (state == APP_STATE_SETTINGS)
    {
        // Card 1: Build & Account Info (y: 36..124, h: 88)
        ui_draw_glass_panel(10, 36, 0.1f, 380, 88, UI_CLR_CYBER_CYAN, false, pulse);

        ui_draw_text(22, 42, 0.50f, UI_CLR_CYBER_CYAN, "APPLICATION SETTINGS & SYSTEM");
        ui_draw_text(22, 60, 0.38f, UI_CLR_CYBER_WHITE, "Client: RetroAchievements 3DS (Native CTR)");

        char user_info[96];
        if (cfg && cfg->is_configured && cfg->username[0] != '\0')
        {
            snprintf(user_info, sizeof(user_info), "User: %s  [Configured / Active]", cfg->username);
            ui_draw_text(22, 78, 0.38f, UI_CLR_CYBER_GREEN, user_info);
        }
        else
        {
            ui_draw_text(22, 78, 0.38f, UI_CLR_CYBER_RED, "User: Unconfigured / Not Connected");
        }

        if (cfg && cfg->is_configured && cfg->web_api_key[0] != '\0')
        {
            char masked[33];
            size_t klen = strlen(cfg->web_api_key);
            if (klen > 24) klen = 24;
            for (size_t i = 0; i < klen; i++) masked[i] = '*';
            masked[klen] = '\0';
            char key_info[96];
            snprintf(key_info, sizeof(key_info), "Key:  %s  (%zu chars)", masked, strlen(cfg->web_api_key));
            ui_draw_text(22, 96, 0.36f, UI_CLR_CYBER_TEXT_DIM, key_info);
        }
        else
        {
            ui_draw_text(22, 96, 0.36f, UI_CLR_CYBER_BORDER, "Key:  <Not Configured>");
        }

        // Card 2: SD Storage & Cache Info (y: 130..232, h: 102)
        ui_draw_glass_panel(10, 130, 0.1f, 380, 102, UI_CLR_CYBER_PINK, false, pulse);

        ui_draw_text(22, 136, 0.46f, UI_CLR_CYBER_PINK, "STORAGE & CACHE USAGE");

        char cache_info[96];
        if (cache_size_mb < 0.0f)
        {
            snprintf(cache_info, sizeof(cache_info), "SD Cache Storage: Calculating...");
        }
        else
        {
            snprintf(cache_info, sizeof(cache_info), "SD Cache Storage: %.2f MB", cache_size_mb);
        }
        ui_draw_text(22, 154, 0.42f, UI_CLR_CYBER_AMBER, cache_info);

        ui_draw_text(22, 172, 0.36f, UI_CLR_CYBER_TEXT_DIM, "Cache Path: sdmc:/3ds/retroachievements/cache/");
        ui_draw_text(22, 188, 0.35f, UI_CLR_CYBER_WHITE, "Badges and cached API responses are stored offline.");

        if (status_message && status_message[0] != '\0')
        {
            ui_draw_text(22, 206, 0.36f, UI_CLR_CYBER_AMBER, status_message);
        }
        else
        {
            ui_draw_text(22, 206, 0.35f, UI_CLR_CYBER_TEXT_DIM, "Use touch controls or buttons on the bottom screen.");
        }

        return;
    }

    if (state == APP_STATE_SETUP_CREDENTIALS)
    {
        // Card 1: Setup Instructions (y: 36..124, h: 88)
        ui_draw_glass_panel(10, 36, 0.1f, 380, 88, UI_CLR_CYBER_CYAN, false, pulse);

        ui_draw_text(22, 42, 0.50f, UI_CLR_CYBER_AMBER, "CREDENTIAL SETUP REQUIRED");
        ui_draw_text(22, 60, 0.38f, UI_CLR_CYBER_WHITE, "Account credentials are required to connect and track achievements.");
        ui_draw_text(22, 78, 0.38f, UI_CLR_CYBER_CYAN, "Where to find your Web API Key:");
        ui_draw_text(22, 94, 0.36f, UI_CLR_CYBER_WHITE, "1. Log into retroachievements.org -> Settings -> Keys");
        ui_draw_text(22, 108, 0.35f, UI_CLR_CYBER_TEXT_DIM, "2. Copy 'Web API Key' (NOT your account password)");

        // Card 2: Current Draft Values & Status (y: 130..232, h: 102)
        bool user_ok = config_validate_username(draft_username);
        bool key_ok  = config_validate_api_key(draft_api_key);
        bool has_err = (status_message && (strstr(status_message, "Invalid") ||
                                           strstr(status_message, "failed") ||
                                           strstr(status_message, "Failed") ||
                                           strstr(status_message, "Error") ||
                                           strstr(status_message, "error")));

        u32 card2_accent = has_err ? UI_CLR_CYBER_RED : ((user_ok && key_ok) ? UI_CLR_CYBER_GREEN : UI_CLR_CYBER_CYAN);
        ui_draw_glass_panel(10, 130, 0.1f, 380, 102, card2_accent, false, pulse);

        ui_draw_text(22, 136, 0.46f, UI_CLR_CYBER_WHITE, "CURRENT CREDENTIALS");

        // Username row
        ui_draw_text(22, 154, 0.40f, UI_CLR_CYBER_LABEL, "Username:");
        if (draft_username && draft_username[0] != '\0')
        {
            ui_draw_text(110, 154, 0.40f, user_ok ? UI_CLR_CYBER_AMBER : UI_CLR_CYBER_RED, draft_username);
        }
        else
        {
            ui_draw_text(110, 154, 0.40f, UI_CLR_CYBER_BORDER, "<Not Set>");
        }

        // Web API Key row (Masked with asterisks for privacy)
        ui_draw_text(22, 172, 0.40f, UI_CLR_CYBER_LABEL, "Web API Key:");
        if (draft_api_key && draft_api_key[0] != '\0')
        {
            char masked[33];
            size_t klen = strlen(draft_api_key);
            if (klen > 32) klen = 32;
            for (size_t i = 0; i < klen; i++)
            {
                masked[i] = '*';
            }
            masked[klen] = '\0';

            char key_disp[96];
            if (key_ok)
            {
                snprintf(key_disp, sizeof(key_disp), "%s  (%zu chars) [OK]", masked, strlen(draft_api_key));
                ui_draw_text(110, 172, 0.40f, UI_CLR_CYBER_GREEN, key_disp);
            }
            else
            {
                snprintf(key_disp, sizeof(key_disp), "%s  [Invalid]", masked);
                ui_draw_text(110, 172, 0.40f, UI_CLR_CYBER_RED, key_disp);
            }
        }
        else
        {
            ui_draw_text(110, 172, 0.40f, UI_CLR_CYBER_BORDER, "<Not Set>");
        }

        // Status or instruction line
        if (has_err && status_message)
        {
            ui_draw_text(22, 196, 0.38f, UI_CLR_CYBER_RED, status_message);
        }
        else if (is_loading)
        {
            ui_draw_text(22, 196, 0.38f, UI_CLR_CYBER_CYAN, "Connecting to RetroAchievements to verify credentials...");
        }
        else if (user_ok && key_ok)
        {
            ui_draw_text(22, 196, 0.38f, UI_CLR_CYBER_GREEN, "Ready to save! Tap [Save & Connect] on the bottom screen.");
        }
        else
        {
            ui_draw_text(22, 196, 0.36f, UI_CLR_CYBER_TEXT_DIM, "Tap buttons on the bottom screen using stylus to enter info.");
        }

        return;
    }

    // Card 1: User Profile Summary (y: 36..114, h: 78)
    ui_draw_glass_panel(10, 36, 0.1f, 380, 78, summary_loaded ? UI_CLR_CYBER_AMBER : UI_CLR_CYBER_BORDER, false, pulse);

    // User Avatar Display
    if (summary_loaded && summary != NULL)
    {
        C2D_Image avatar_img = user_get_avatar(summary->user_pic, summary->username);
        C2D_DrawRectSolid(20.0f, 51.0f, 0.28f, 48.0f, 48.0f, UI_CLR_CYBER_BORDER_HI);
        C2D_DrawImageAt(avatar_img, 22.0f, 53.0f, 0.3f, NULL, 44.0f / (float)BADGE_TEX_SIZE, 44.0f / (float)BADGE_TEX_SIZE);

        ui_draw_text(76, 42, 0.55f, UI_CLR_CYBER_WHITE, summary->username);

        char stats_line[96];
        snprintf(stats_line, sizeof(stats_line), "%lu pts  (Softcore: %lu)",
                 (unsigned long)summary->total_points, (unsigned long)summary->total_softcore_points);
        ui_draw_text(76, 62, 0.43f, UI_CLR_CYBER_AMBER, stats_line);

        char rank_line[64];
        snprintf(rank_line, sizeof(rank_line), "Rank #%lu of %lu",
                 (unsigned long)summary->rank, (unsigned long)summary->total_ranked);
        ui_draw_text(76, 78, 0.40f, UI_CLR_CYBER_LABEL, rank_line);

        if (summary->motto[0] != '\0')
        {
            char motto_line[96];
            if (strlen(summary->motto) > 48)
            {
                snprintf(motto_line, sizeof(motto_line), "Motto: %.45s...", summary->motto);
            }
            else
            {
                snprintf(motto_line, sizeof(motto_line), "Motto: %s", summary->motto);
            }
            ui_draw_text(76, 94, 0.38f, UI_CLR_CYBER_TEXT_DIM, motto_line);
        }
        else
        {
            ui_draw_text(76, 94, 0.38f, UI_CLR_CYBER_BORDER, "Motto: None");
        }
    }
    else
    {
        C2D_DrawCircleSolid(44, 75, 0.3f, 22, UI_CLR_CYBER_BANNER);
        C2D_DrawCircleSolid(44, 75, 0.4f, 20, summary_loaded ? UI_CLR_GOLD_DIM : UI_CLR_CYBER_PANEL);

        ui_draw_text(76, 48, 0.50f, UI_CLR_CYBER_WHITE, (cfg && cfg->is_configured) ? cfg->username : "Unconfigured User");
        ui_draw_text(76, 68, 0.42f, UI_CLR_CYBER_TEXT_DIM, "Press [A] to sync profile summary.");
        ui_draw_text(76, 86, 0.38f, UI_CLR_GOLD_DIM, "Edit credentials at sdmc:/3ds/retroachievements/config.ini");
    }

    if (state == APP_STATE_ERROR)
    {
        ui_draw_glass_panel(10, 120, 0.1f, 380, 112, UI_CLR_CYBER_RED, false, pulse);
        ui_draw_text(24, 126, 0.52f, UI_CLR_CYBER_RED, "! SYSTEM NOTICE / ERROR");

        const char* err_text = (status_message && status_message[0]) ? status_message : "An unexpected error occurred.";
        ui_draw_text_wrapped(24, 148, 0.40f, 350.0f, UI_CLR_CYBER_WHITE, err_text);

        ui_draw_text(24, 204, 0.36f, UI_CLR_CYBER_AMBER, "Use touch controls or buttons on the bottom screen to recover.");
    }
    else if (state == APP_STATE_GAME_DETAIL)
    {
        if (progress_loaded && progress != NULL)
        {
            bool has_selected_ach = (selected_index >= 0 &&
                                     selected_index < (int)progress->achievement_count &&
                                     progress->achievements != NULL);
            u32 detail_accent = (has_selected_ach && progress->achievements[selected_index].is_unlocked)
                                ? UI_CLR_CYBER_GREEN : UI_CLR_CYBER_PINK;
            ui_draw_glass_panel(10, 120, 0.1f, 380, 112, detail_accent, false, pulse);

            if (has_selected_ach)
            {
                const ra_achievement_t* ach = &progress->achievements[selected_index];

                // 48x48 Badge Texture with border frame on right side of detail card
                C2D_Image badge_img = badge_get_image(ach->badge_name, ach->is_unlocked);
                C2D_DrawRectSolid(329.0f, 127.0f, 0.25f, 50.0f, 50.0f, ach->is_unlocked ? UI_CLR_CYBER_BORDER_HI : UI_CLR_CYBER_BORDER);
                C2D_DrawImageAt(badge_img, 330.0f, 128.0f, 0.3f, NULL, 48.0f / (float)BADGE_TEX_SIZE, 48.0f / (float)BADGE_TEX_SIZE);

                // Achievement Title & Points (wrapped to 298px to ensure no overlap with icon)
                char ach_hdr[128];
                snprintf(ach_hdr, sizeof(ach_hdr), "%s (%u pts)", ach->title, ach->points);
                ui_draw_text_wrapped(22, 126, 0.48f, 298.0f, UI_CLR_CYBER_WHITE, ach_hdr);

                // Hardcore / Status tag
                if (ach->is_hardcore)
                {
                    ui_draw_text(22, 144, 0.40f, UI_CLR_CYBER_GREEN, "[UNLOCKED - HARDCORE]");
                }
                else if (ach->is_unlocked)
                {
                    ui_draw_text(22, 144, 0.40f, UI_CLR_CYBER_CYAN, "[UNLOCKED - SOFTCORE]");
                }
                else
                {
                    ui_draw_text(22, 144, 0.40f, UI_CLR_CYBER_RED, "[LOCKED]");
                }

                // Description (Wrapped text constrained to 298px before icon)
                ui_draw_text_wrapped(22, 160, 0.38f, 298.0f, UI_CLR_CYBER_TEXT_DIM, ach->description);

                // Unlock timestamp if earned
                if (ach->is_unlocked && ach->date_earned[0] != '\0')
                {
                    char date_line[64];
                    snprintf(date_line, sizeof(date_line), "Unlocked on: %s", ach->date_earned);
                    ui_draw_text(22, 212, 0.35f, UI_CLR_CYBER_LABEL, date_line);
                }
            }
            else
            {
                // Game Overview banner & Game Icon
                C2D_Image game_icon = game_get_icon(progress->image_icon);
                C2D_DrawRectSolid(329.0f, 125.0f, 0.25f, 48.0f, 48.0f, UI_CLR_CYBER_BORDER_HI);
                C2D_DrawImageAt(game_icon, 331.0f, 127.0f, 0.3f, NULL, 44.0f / (float)BADGE_TEX_SIZE, 44.0f / (float)BADGE_TEX_SIZE);

                ui_draw_text_wrapped(22, 126, 0.50f, 300.0f, UI_CLR_CYBER_WHITE, progress->title);

                char console_line[96];
                snprintf(console_line, sizeof(console_line), "Console: %s  (GameID: %lu)",
                         progress->console_name, (unsigned long)progress->game_id);
                ui_draw_text(22, 146, 0.40f, UI_CLR_CYBER_LABEL, console_line);

                // Progress bar (constrained to 300px to stop before icon)
                C2D_DrawRectSolid(22, 170, 0.2f, 300.0f, 12.0f, UI_CLR_CYBER_PROGRESS);
                if (progress->num_achievements > 0)
                {
                    float ratio = (float)progress->num_awarded / (float)progress->num_achievements;
                    if (ratio > 1.0f) ratio = 1.0f;
                    u32 bar_fill = ui_color_lerp(UI_CLR_CYBER_AMBER, UI_CLR_CYBER_CYAN, 0.4f * pulse);
                    C2D_DrawRectSolid(22, 170, 0.3f, 300.0f * ratio, 12.0f, bar_fill);
                }

                char comp_line[96];
                snprintf(comp_line, sizeof(comp_line), "%lu / %lu Achievements Unlocked (%s)",
                         (unsigned long)progress->num_awarded,
                         (unsigned long)progress->num_achievements,
                         progress->user_completion);
                ui_draw_text(22, 192, 0.40f, UI_CLR_CYBER_AMBER, comp_line);
            }
        }
        else
        {
            ui_draw_glass_panel(10, 120, 0.1f, 380, 112, UI_CLR_CYBER_BORDER, false, pulse);
            ui_draw_text(24, 150, 0.48f, UI_CLR_CYBER_CYAN, "Fetching game achievements...");
            ui_draw_text(24, 174, 0.38f, UI_CLR_CYBER_TEXT_DIM, "Communicating with RetroAchievements API");
        }
    }
    else if (state == APP_STATE_SEARCH)
    {
        ui_draw_glass_panel(10, 120, 0.1f, 380, 112, UI_CLR_CYBER_CYAN, false, pulse);

        if (search_results != NULL && search_results_count > 0 &&
            selected_index >= 0 && selected_index < (int)search_results_count)
        {
            const search_result_item_t* item = &search_results[selected_index];
            C2D_Image game_icon = game_get_icon(item->image_icon);
            C2D_DrawRectSolid(329.0f, 125.0f, 0.25f, 48.0f, 48.0f, UI_CLR_CYBER_BORDER_HI);
            C2D_DrawImageAt(game_icon, 331.0f, 127.0f, 0.3f, NULL, 44.0f / (float)BADGE_TEX_SIZE, 44.0f / (float)BADGE_TEX_SIZE);

            ui_draw_text_wrapped(22, 126, 0.50f, 300.0f, UI_CLR_CYBER_WHITE, item->title);

            char console_line[96];
            snprintf(console_line, sizeof(console_line), "Platform: %s  (GameID: %lu)",
                     item->console_name, (unsigned long)item->game_id);
            ui_draw_text(22, 148, 0.40f, UI_CLR_CYBER_LABEL, console_line);

            char match_line[80];
            snprintf(match_line, sizeof(match_line), "Search Query: \"%.30s\"", (search_query && search_query[0]) ? search_query : "*");
            ui_draw_text(22, 168, 0.38f, UI_CLR_CYBER_TEXT_DIM, match_line);

            ui_draw_text(22, 196, 0.40f, UI_CLR_CYBER_AMBER, "Press [A] or tap to inspect game achievements.");
        }
        else
        {
            ui_draw_text(24, 126, 0.52f, UI_CLR_CYBER_CYAN, "GLOBAL GAME SEARCH");

            char query_disp[96];
            snprintf(query_disp, sizeof(query_disp), "Active Query: \"%.40s\"", (search_query && search_query[0]) ? search_query : "<None>");
            ui_draw_text(24, 150, 0.42f, UI_CLR_CYBER_WHITE, query_disp);

            if (is_loading)
            {
                ui_draw_text(24, 172, 0.38f, UI_CLR_CYBER_AMBER, "Searching RetroAchievements database...");
            }
            else
            {
                ui_draw_text(24, 172, 0.38f, UI_CLR_CYBER_TEXT_DIM, "Tap Search icon or press [Y] to enter Game ID / title.");
            }

            ui_draw_text(24, 196, 0.38f, UI_CLR_CYBER_AMBER, "Press [B] or tap [< Back] to return to Profile Overview.");
        }
    }
    else if (state == APP_STATE_USER_SUMMARY)
    {
        ui_draw_glass_panel(10, 120, 0.1f, 380, 112, UI_CLR_CYBER_PINK, false, pulse);

        if (summary_loaded && summary != NULL && summary->recent_games_count > 0 &&
            selected_index >= 0 && selected_index < (int)summary->recent_games_count)
        {
            const ra_recent_game_t* g = &summary->recent_games[selected_index];
            C2D_Image game_icon = game_get_icon(g->image_icon);
            C2D_DrawRectSolid(329.0f, 125.0f, 0.25f, 48.0f, 48.0f, UI_CLR_CYBER_BORDER_HI);
            C2D_DrawImageAt(game_icon, 331.0f, 127.0f, 0.3f, NULL, 44.0f / (float)BADGE_TEX_SIZE, 44.0f / (float)BADGE_TEX_SIZE);

            ui_draw_text_wrapped(22, 126, 0.50f, 300.0f, UI_CLR_CYBER_WHITE, g->title);

            char console_line[96];
            snprintf(console_line, sizeof(console_line), "Platform: %s  (GameID: %lu)",
                     g->console_name, (unsigned long)g->game_id);
            ui_draw_text(22, 148, 0.40f, UI_CLR_CYBER_LABEL, console_line);

            if (g->last_played[0] != '\0')
            {
                char date_str[64];
                snprintf(date_str, sizeof(date_str), "Last played: %s", g->last_played);
                ui_draw_text(22, 168, 0.38f, UI_CLR_CYBER_TEXT_DIM, date_str);
            }

            ui_draw_text(22, 196, 0.40f, UI_CLR_CYBER_AMBER, "Press [A] or tap row to view achievements for this game.");
        }
        else
        {
            ui_draw_text(24, 136, 0.50f, UI_CLR_CYBER_WHITE, "Profile Overview");
            ui_draw_text(24, 160, 0.40f, UI_CLR_CYBER_TEXT_DIM, "Select a recent game from the bottom list to inspect.");
            ui_draw_text(24, 184, 0.38f, UI_CLR_CYBER_AMBER, "Press [X] or tap [Sync] to fetch data from RetroAchievements.");
        }
    }
    else // APP_STATE_INIT
    {
        ui_draw_glass_panel(10, 120, 0.1f, 380, 112, UI_CLR_CYBER_CYAN, false, pulse);
        ui_draw_text(24, 136, 0.50f, UI_CLR_CYBER_AMBER, "Initializing Application...");
        ui_draw_text(24, 162, 0.40f, UI_CLR_CYBER_TEXT_DIM, "Checking network connectivity and configuration...");
        ui_draw_text(24, 186, 0.38f, UI_CLR_CYBER_TEXT_DIM, "Please wait while services initialize.");
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
    const char* search_query,
    const search_result_item_t* search_results,
    size_t search_results_count,
    const char* draft_username,
    const char* draft_api_key,
    settings_modal_t settings_modal,
    uint32_t frame_count
)
{
    if (s_bottom_target == NULL)
    {
        return;
    }

    // Shared frame-based sine wave counter (bounded modulo prevents precision loss)
    float phase = ((float)(frame_count % 180) / 180.0f) * 6.2831853f;
    float pulse = 0.5f + 0.5f * sinf(phase);

    C2D_TargetClear(s_bottom_target, UI_CLR_CYBER_BG);
    C2D_SceneBegin(s_bottom_target);

    // Cyberpunk cyberspace backdrop grid
    ui_draw_cyber_grid(UI_SCREEN_BOT_W, UI_SCREEN_BOT_H);

    // Header bar (y: 0..26)
    C2D_DrawRectSolid(0, 0, 0.6f, UI_SCREEN_BOT_W, 26, UI_CLR_CYBER_BANNER);
    C2D_DrawRectSolid(0, 25, 0.62f, UI_SCREEN_BOT_W, 1.5f, UI_CLR_CYBER_BORDER_HI);

    if (state == APP_STATE_SETUP_CREDENTIALS)
    {
        // Header bar text
        ui_draw_text_depth(8, 6, 0.65f, 0.44f, UI_CLR_CYBER_AMBER, "CREDENTIAL CONFIGURATION");

        bool user_ok = config_validate_username(draft_username);
        bool key_ok  = config_validate_api_key(draft_api_key);
        bool can_save = user_ok && key_ok && !is_loading;

        // Button 1: [ Set Username ] (y: 32..74, h: 42)
        ui_draw_glass_panel(16, 32, 0.1f, 288, 42, user_ok ? UI_CLR_CYBER_GREEN : UI_CLR_CYBER_CYAN, false, pulse);
        ui_draw_text(26, 38, 0.44f, UI_CLR_CYBER_CYAN, "[ Set Username ]");
        if (draft_username && draft_username[0] != '\0')
        {
            ui_draw_text(26, 54, 0.38f, UI_CLR_CYBER_WHITE, draft_username);
        }
        else
        {
            ui_draw_text(26, 54, 0.36f, UI_CLR_CYBER_TEXT_DIM, "Tap to enter your RetroAchievements username");
        }

        // Button 2: [ Set Web API Key ] (y: 80..122, h: 42)
        ui_draw_glass_panel(16, 80, 0.1f, 288, 42, key_ok ? UI_CLR_CYBER_GREEN : UI_CLR_CYBER_AMBER, false, pulse);
        ui_draw_text(26, 86, 0.44f, UI_CLR_CYBER_AMBER, "[ Set Web API Key ]");
        if (draft_api_key && draft_api_key[0] != '\0')
        {
            char masked[33];
            size_t klen = strlen(draft_api_key);
            if (klen > 24) klen = 24;
            for (size_t i = 0; i < klen; i++) masked[i] = '*';
            masked[klen] = '\0';
            char key_str[64];
            snprintf(key_str, sizeof(key_str), "%s (%zu chars)", masked, strlen(draft_api_key));
            ui_draw_text(26, 102, 0.38f, key_ok ? UI_CLR_CYBER_WHITE : UI_CLR_CYBER_RED, key_str);
        }
        else
        {
            ui_draw_text(26, 102, 0.36f, UI_CLR_CYBER_TEXT_DIM, "Tap to enter Web API Key");
        }

        // Info / Prompt area (y: 126..144)
        bool has_err = (status_message && (strstr(status_message, "Invalid") ||
                                            strstr(status_message, "failed") ||
                                            strstr(status_message, "Failed") ||
                                            strstr(status_message, "Error") ||
                                            strstr(status_message, "error")));
        if (has_err && status_message)
        {
            ui_draw_text_wrapped(16, 128, 0.35f, 288.0f, UI_CLR_CYBER_RED, status_message);
        }
        else if (can_save)
        {
            ui_draw_text(16, 130, 0.37f, UI_CLR_CYBER_GREEN, "Credentials valid! Tap below to save & connect.");
        }
        else
        {
            ui_draw_text(16, 130, 0.35f, UI_CLR_CYBER_TEXT_DIM, "Enter your Web API Key from RetroAchievements settings.");
        }

        // Action Button: [ Save & Connect ] (y: 150..206, h: 56)
        ui_draw_glass_panel(16, 150, 0.1f, 288, 56, can_save ? UI_CLR_CYBER_GREEN : UI_CLR_CYBER_BORDER, can_save, pulse);
        if (can_save)
        {
            ui_draw_text(28, 158, 0.50f, UI_CLR_CYBER_GREEN, "[ Save & Connect ]");
            ui_draw_text(28, 180, 0.36f, UI_CLR_CYBER_WHITE, "Save to config.ini and test connection");
        }
        else
        {
            ui_draw_text(28, 158, 0.46f, UI_CLR_CYBER_TEXT_DIM, "[ Save & Connect ] (Disabled)");
            if (is_loading)
            {
                ui_draw_text(28, 180, 0.36f, UI_CLR_CYBER_CYAN, "Connecting to RetroAchievements...");
            }
            else
            {
                ui_draw_text(28, 180, 0.35f, UI_CLR_CYBER_BORDER, "Provide both username and Web API key");
            }
        }

        // Bottom Navigation Bar (y: 216..240)
        C2D_DrawRectSolid(0, 216, 0.6f, UI_SCREEN_BOT_W, 24, UI_CLR_CYBER_BANNER);
        C2D_DrawRectSolid(0, 216, 0.62f, UI_SCREEN_BOT_W, 1.5f, UI_CLR_CYBER_BORDER_HI);
        ui_draw_text_depth(8, 222, 0.65f, 0.35f, UI_CLR_CYBER_WHITE, "[Touch] Select Field  [A] Save & Connect  [START] Exit");

        return;
    }

    if (state == APP_STATE_ERROR)
    {
        ui_draw_text_depth(8, 6, 0.65f, 0.45f, UI_CLR_CYBER_RED, "SYSTEM RECOVERY & ACTIONS");
    }
    else if (state == APP_STATE_SETTINGS)
    {
        // Virtual Back Button on Left: x in [4, 36], y in [2, 24]
        ui_draw_glass_panel(4, 2, 0.63f, 32, 22, UI_CLR_CYBER_BORDER_HI, false, pulse);
        ui_draw_back_icon(20.0f, 13.0f, UI_CLR_CYBER_AMBER);
        ui_draw_text_depth(44, 6, 0.65f, 0.40f, UI_CLR_CYBER_AMBER, "SETTINGS & DATA");
    }
    else if (state == APP_STATE_GAME_DETAIL)
    {
        // Virtual Back Button on Left: x in [4, 36], y in [2, 24]
        ui_draw_glass_panel(4, 2, 0.63f, 32, 22, UI_CLR_CYBER_BORDER_HI, false, pulse);
        ui_draw_back_icon(20.0f, 13.0f, UI_CLR_CYBER_AMBER);

        char ach_hdr[64];
        snprintf(ach_hdr, sizeof(ach_hdr), "ACHIEVEMENTS (%zu)",
                 (progress_loaded && progress) ? progress->achievement_count : 0);
        ui_draw_text_depth(44, 6, 0.65f, 0.40f, UI_CLR_CYBER_WHITE, ach_hdr);
    }
    else if (state == APP_STATE_SEARCH)
    {
        // Virtual Back Button on Left: x in [4, 36], y in [2, 24]
        ui_draw_glass_panel(4, 2, 0.63f, 32, 22, UI_CLR_CYBER_BORDER_HI, false, pulse);
        ui_draw_back_icon(20.0f, 13.0f, UI_CLR_CYBER_AMBER);

        char search_hdr[64];
        snprintf(search_hdr, sizeof(search_hdr), "SEARCH (%zu)", search_results_count);
        ui_draw_text_depth(44, 6, 0.65f, 0.40f, UI_CLR_CYBER_WHITE, search_hdr);
    }
    else // APP_STATE_USER_SUMMARY or APP_STATE_INIT
    {
        char games_hdr[64];
        snprintf(games_hdr, sizeof(games_hdr), "RECENT GAMES (%zu)",
                 (summary_loaded && summary) ? summary->recent_games_count : 0);
        ui_draw_text_depth(8, 6, 0.65f, 0.42f, UI_CLR_CYBER_AMBER, games_hdr);
    }

    if (state != APP_STATE_ERROR)
    {
        // Settings Icon Button: x in [212, 244], y in [2, 24]
        bool is_settings_active = (state == APP_STATE_SETTINGS);
        u32 settings_accent = is_settings_active ? ui_color_lerp(UI_CLR_CYBER_AMBER, UI_CLR_CYBER_WHITE, 0.35f * pulse) : UI_CLR_CYBER_BORDER;
        ui_draw_glass_panel(212, 2, 0.63f, 32, 22, settings_accent, is_settings_active, pulse);
        ui_draw_settings_icon(228.0f, 13.0f, is_settings_active ? UI_CLR_CYBER_AMBER : UI_CLR_CYBER_CYAN);

        // Search Icon Button: x in [248, 280], y in [2, 24]
        bool is_search_active = (state == APP_STATE_SEARCH);
        u32 search_accent = is_search_active ? ui_color_lerp(UI_CLR_CYBER_AMBER, UI_CLR_CYBER_WHITE, 0.35f * pulse) : UI_CLR_CYBER_BORDER;
        ui_draw_glass_panel(248, 2, 0.63f, 32, 22, search_accent, is_search_active, pulse);
        ui_draw_search_icon(264.0f, 13.0f, is_search_active ? UI_CLR_CYBER_AMBER : UI_CLR_CYBER_CYAN);

        // Reload / Sync Icon Button: x in [284, 316], y in [2, 24]
        u32 reload_accent = is_loading ? ui_color_lerp(UI_CLR_CYBER_AMBER, UI_CLR_CYBER_WHITE, 0.4f * pulse) : UI_CLR_CYBER_BORDER;
        ui_draw_glass_panel(284, 2, 0.63f, 32, 22, reload_accent, is_loading, pulse);
        ui_draw_reload_icon(300.0f, 13.0f, is_loading ? UI_CLR_CYBER_AMBER : UI_CLR_CYBER_CYAN);
    }

    if (state == APP_STATE_SETTINGS)
    {
        // Interactive Maintenance Action Cards
        // Card 1: Reset User Data (y: 36..86, h: 50, x: 16..304)
        ui_draw_glass_panel(16, 36, 0.1f, 288, 50, UI_CLR_CYBER_RED, false, pulse);
        ui_draw_text(26, 42, 0.44f, UI_CLR_CYBER_RED, "[ Reset User Data ]");
        ui_draw_text(26, 62, 0.36f, UI_CLR_CYBER_WHITE, "Wipe credentials & return to Setup screen");

        // Card 2: Clear Cache (y: 94..144, h: 50, x: 16..304)
        ui_draw_glass_panel(16, 94, 0.1f, 288, 50, UI_CLR_CYBER_AMBER, false, pulse);
        ui_draw_text(26, 100, 0.44f, UI_CLR_CYBER_AMBER, "[ Clear Cache ]");
        ui_draw_text(26, 120, 0.36f, UI_CLR_CYBER_WHITE, "Delete cached badges & responses on SD card");

        // Card 3: Sound Effects Setting (y: 152..202, h: 50, x: 16..304)
        bool sfx_enabled = (cfg != NULL) ? cfg->sound_enabled : true;
        u32 sfx_clr = sfx_enabled ? UI_CLR_CYBER_GREEN : UI_CLR_CYBER_BORDER;
        ui_draw_glass_panel(16, 152, 0.1f, 288, 50, sfx_clr, false, pulse);
        if (sfx_enabled)
        {
            ui_draw_text(26, 158, 0.44f, UI_CLR_CYBER_GREEN, "[ Sound Effects: ON ]");
            ui_draw_text(26, 178, 0.36f, UI_CLR_CYBER_WHITE, "Tap to disable UI sound effects");
        }
        else
        {
            ui_draw_text(26, 158, 0.44f, UI_CLR_CYBER_BORDER, "[ Sound Effects: OFF ]");
            ui_draw_text(26, 178, 0.36f, UI_CLR_CYBER_TEXT_DIM, "Tap to enable UI sound effects");
        }
    }
    else if (state == APP_STATE_ERROR)
    {
        // Interactive touch cards for error recovery
        // Card 1: Retry (y: 40..84)
        ui_draw_glass_panel(16, 40, 0.1f, 288, 44, UI_CLR_CYBER_AMBER, false, pulse);
        ui_draw_text(30, 48, 0.48f, UI_CLR_CYBER_AMBER, "[A] Retry Connection / Sync");
        ui_draw_text(30, 66, 0.36f, UI_CLR_CYBER_TEXT_DIM, "Attempt network operation again");

        // Card 2: Return to Profile (y: 94..138)
        ui_draw_glass_panel(16, 94, 0.1f, 288, 44, UI_CLR_CYBER_CYAN, false, pulse);
        ui_draw_text(30, 102, 0.48f, UI_CLR_CYBER_CYAN, "[B] Return to User Profile");
        ui_draw_text(30, 120, 0.36f, UI_CLR_CYBER_TEXT_DIM, "Dismiss error screen to profile summary");

        // Card 3: Reload Config (y: 148..192)
        ui_draw_glass_panel(16, 148, 0.1f, 288, 44, UI_CLR_CYBER_GREEN, false, pulse);
        ui_draw_text(30, 156, 0.48f, UI_CLR_CYBER_GREEN, "[Y] Reload config.ini");
        ui_draw_text(30, 174, 0.36f, UI_CLR_CYBER_TEXT_DIM, "Reload credentials from SD card");
    }
    else
    {
        // Scroll View Window: y in [28.0f, 214.0f]
        const float view_top = 28.0f;
        const float view_bottom = 214.0f;
        const float view_height = view_bottom - view_top;

        size_t total_items = 0;
        if (state == APP_STATE_GAME_DETAIL)
        {
            if (progress_loaded && progress != NULL && progress->achievements != NULL)
            {
                total_items = progress->achievement_count;
            }
        }
        else if (state == APP_STATE_SEARCH)
        {
            if (search_results != NULL)
            {
                total_items = search_results_count;
            }
        }
        else if (state == APP_STATE_USER_SUMMARY)
        {
            if (summary_loaded && summary != NULL)
            {
                total_items = summary->recent_games_count;
            }
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
                u32 item_accent = is_selected ? UI_CLR_CYBER_CYAN : UI_CLR_CYBER_BORDER;

                if (state == APP_STATE_GAME_DETAIL)
                {
                    if (progress_loaded && progress != NULL && progress->achievements != NULL && i < progress->achievement_count)
                    {
                        const ra_achievement_t* ach = &progress->achievements[i];
                        if (is_selected)
                        {
                            item_accent = ach->is_unlocked ? (ach->is_hardcore ? UI_CLR_CYBER_GREEN : UI_CLR_CYBER_CYAN) : UI_CLR_CYBER_PINK;
                        }
                    }
                }

                ui_draw_glass_panel(6, item_y, 0.1f, 304, UI_ITEM_HEIGHT - 2.0f, item_accent, is_selected, pulse);

                if (state == APP_STATE_GAME_DETAIL)
                {
                    if (progress_loaded && progress != NULL && progress->achievements != NULL && i < progress->achievement_count)
                    {
                        const ra_achievement_t* ach = &progress->achievements[i];
                        C2D_Image badge_img = badge_get_image(ach->badge_name, ach->is_unlocked);

                        // Badge frame
                        C2D_DrawRectSolid(13.0f, item_y + 3.0f, 0.28f, 34.0f, 34.0f,
                                          ach->is_unlocked ? UI_CLR_CYBER_BORDER_HI : UI_CLR_CYBER_BORDER);
                        C2D_DrawImageAt(badge_img, 14.0f, item_y + 4.0f, 0.3f, NULL,
                                        32.0f / (float)BADGE_TEX_SIZE, 32.0f / (float)BADGE_TEX_SIZE);

                        u32 text_clr = ach->is_unlocked ? UI_CLR_CYBER_WHITE : UI_CLR_CYBER_TEXT_DIM;
                        ui_draw_text(52, item_y + 6.0f, 0.44f, text_clr, ach->title);

                        char pts_str[32];
                        snprintf(pts_str, sizeof(pts_str), "%u pts", ach->points);
                        ui_draw_text(52, item_y + 22.0f, 0.38f, UI_CLR_CYBER_AMBER, pts_str);

                        if (ach->is_hardcore)
                        {
                            ui_draw_text(230, item_y + 12.0f, 0.38f, UI_CLR_CYBER_GREEN, "HARDCORE");
                        }
                        else if (ach->is_unlocked)
                        {
                            ui_draw_text(236, item_y + 12.0f, 0.38f, UI_CLR_CYBER_CYAN, "UNLOCKED");
                        }
                        else
                        {
                            ui_draw_text(250, item_y + 12.0f, 0.38f, UI_CLR_CYBER_RED, "LOCKED");
                        }
                    }
                }
                else if (state == APP_STATE_SEARCH && search_results != NULL && i < search_results_count)
                {
                    const search_result_item_t* item = &search_results[i];
                    C2D_Image icon_img = game_get_icon(item->image_icon);

                    C2D_DrawRectSolid(13.0f, item_y + 3.0f, 0.28f, 34.0f, 34.0f, UI_CLR_CYBER_BORDER);
                    C2D_DrawImageAt(icon_img, 14.0f, item_y + 4.0f, 0.3f, NULL,
                                    32.0f / (float)BADGE_TEX_SIZE, 32.0f / (float)BADGE_TEX_SIZE);

                    ui_draw_text(52, item_y + 6.0f, 0.44f, UI_CLR_CYBER_WHITE, item->title);
                    ui_draw_text(52, item_y + 22.0f, 0.38f, UI_CLR_CYBER_LABEL, item->console_name);

                    char id_tag[24];
                    snprintf(id_tag, sizeof(id_tag), "ID: %lu", (unsigned long)item->game_id);
                    ui_draw_text(240, item_y + 12.0f, 0.36f, UI_CLR_CYBER_AMBER, id_tag);
                }
                else if (state == APP_STATE_USER_SUMMARY && summary != NULL && i < summary->recent_games_count)
                {
                    const ra_recent_game_t* g = &summary->recent_games[i];
                    C2D_Image icon_img = game_get_icon(g->image_icon);

                    C2D_DrawRectSolid(13.0f, item_y + 3.0f, 0.28f, 34.0f, 34.0f, UI_CLR_CYBER_BORDER);
                    C2D_DrawImageAt(icon_img, 14.0f, item_y + 4.0f, 0.3f, NULL,
                                    32.0f / (float)BADGE_TEX_SIZE, 32.0f / (float)BADGE_TEX_SIZE);

                    ui_draw_text(52, item_y + 6.0f, 0.44f, UI_CLR_CYBER_WHITE, g->title);
                    ui_draw_text(52, item_y + 22.0f, 0.38f, UI_CLR_CYBER_LABEL, g->console_name);

                    if (g->last_played[0] != '\0')
                    {
                        char date_short[20];
                        snprintf(date_short, sizeof(date_short), "%.10s", g->last_played);
                        ui_draw_text(230, item_y + 12.0f, 0.36f, UI_CLR_CYBER_TEXT_DIM, date_short);
                    }
                }
            }

            // Scrollbar on right edge
            float total_list_height = (float)total_items * UI_ITEM_HEIGHT;
            if (total_list_height > view_height)
            {
                C2D_DrawRectSolid(314, view_top, 0.1f, UI_SCROLLBAR_WIDTH, view_height, UI_CLR_CYBER_PROGRESS);

                float thumb_ratio = view_height / total_list_height;
                float thumb_h = view_height * thumb_ratio;
                if (thumb_h < 12.0f) thumb_h = 12.0f;

                float max_scroll = total_list_height - view_height;
                float thumb_y = view_top + ((scroll_y / max_scroll) * (view_height - thumb_h));

                u32 scroll_clr = ui_color_lerp(UI_CLR_CYBER_AMBER, UI_CLR_CYBER_CYAN, 0.5f * pulse);
                C2D_DrawRectSolid(314, thumb_y, 0.2f, UI_SCROLLBAR_WIDTH, thumb_h, scroll_clr);
            }
        }
        else
        {
            if (is_loading)
            {
                float angle = ((float)(frame_count % 60) / 60.0f) * 6.283185f;
                float cx = 160.0f, cy = 100.0f, r = 12.0f;
                C2D_DrawCircleSolid(cx, cy, 0.4f, 4.0f, UI_CLR_CYBER_BORDER);
                C2D_DrawCircleSolid(cx + cosf(angle) * r, cy + sinf(angle) * r, 0.5f, 5.0f, UI_CLR_CYBER_CYAN);
                ui_draw_text(85, 126, 0.46f, UI_CLR_CYBER_CYAN, "Loading items from API...");
            }
            else if (state == APP_STATE_GAME_DETAIL)
            {
                ui_draw_text(45, 90, 0.46f, UI_CLR_CYBER_TEXT_DIM, "No achievements found for this title.");
                ui_draw_text(30, 115, 0.40f, UI_CLR_CYBER_AMBER, "Press [B] or tap Back to return to previous screen.");
            }
            else if (state == APP_STATE_SEARCH)
            {
                if (search_query && search_query[0])
                {
                    ui_draw_text(45, 90, 0.46f, UI_CLR_CYBER_TEXT_DIM, "No matching games found.");
                    ui_draw_text(30, 115, 0.40f, UI_CLR_CYBER_AMBER, "Tap [Search] or press [Y] to try another query.");
                }
                else
                {
                    ui_draw_text(50, 90, 0.46f, UI_CLR_CYBER_TEXT_DIM, "Enter a Game ID or Title to search.");
                    ui_draw_text(30, 115, 0.40f, UI_CLR_CYBER_AMBER, "Tap [Search] or press [Y] to open keyboard.");
                }
            }
            else
            {
                ui_draw_text(60, 90, 0.46f, UI_CLR_CYBER_TEXT_DIM, "No items available to display.");
                ui_draw_text(40, 115, 0.40f, UI_CLR_CYBER_AMBER, "Press [A] or tap [Sync] to fetch RetroAchievements data.");
            }
        }
    }

    // Bottom Navigation Bar (y: 216..240)
    C2D_DrawRectSolid(0, 216, 0.6f, UI_SCREEN_BOT_W, 24, UI_CLR_CYBER_BANNER);
    C2D_DrawRectSolid(0, 216, 0.62f, UI_SCREEN_BOT_W, 1.5f, UI_CLR_CYBER_BORDER_HI);

    if (state == APP_STATE_ERROR)
    {
        ui_draw_text_depth(8, 222, 0.65f, 0.35f, UI_CLR_CYBER_WHITE, "[A] Retry  [B] Back  [Y] Reload Config  [START] Exit");
    }
    else if (state == APP_STATE_SETTINGS)
    {
        ui_draw_text_depth(8, 222, 0.65f, 0.35f, UI_CLR_CYBER_WHITE, "[Touch] Select Action  [B] Back  [START] Exit");
    }
    else if (state == APP_STATE_GAME_DETAIL)
    {
        ui_draw_text_depth(8, 222, 0.65f, 0.35f, UI_CLR_CYBER_WHITE, "[D-Pad] Scroll  [A] Inspect  [B] Back  [X] Sync  [START] Exit");
    }
    else if (state == APP_STATE_SEARCH)
    {
        ui_draw_text_depth(8, 222, 0.65f, 0.35f, UI_CLR_CYBER_WHITE, "[D-Pad] Scroll  [A] Select Game  [B] Back  [Y] Search  [START] Exit");
    }
    else if (state == APP_STATE_USER_SUMMARY)
    {
        ui_draw_text_depth(8, 222, 0.65f, 0.35f, UI_CLR_CYBER_WHITE, "[D-Pad] Scroll  [A] Select Game  [X] Sync  [Y] Search  [START] Exit");
    }
    else
    {
        ui_draw_text_depth(8, 222, 0.65f, 0.35f, UI_CLR_CYBER_WHITE, "Loading RetroAchievements for Nintendo 3DS...");
    }

    // Confirmation Modal Dialog Overlay
    if (settings_modal != SETTINGS_MODAL_NONE)
    {
        // Dark translucent scrim/backdrop: depth 0.7f
        C2D_DrawRectSolid(0, 0, 0.7f, UI_SCREEN_BOT_W, UI_SCREEN_BOT_H, C2D_Color32(0x04, 0x05, 0x0A, 215));

        // Modal Box: x: 16..304, y: 35..205 (w: 288, h: 170), depth 0.75f
        u32 modal_accent = (settings_modal == SETTINGS_MODAL_RESET_USER_DATA) ? UI_CLR_CYBER_RED : UI_CLR_CYBER_AMBER;
        ui_draw_glass_panel(16, 35, 0.75f, 288, 170, modal_accent, true, pulse);

        if (settings_modal == SETTINGS_MODAL_RESET_USER_DATA)
        {
            ui_draw_text_depth(26, 44, 0.9f, 0.48f, UI_CLR_CYBER_RED, "CONFIRM: RESET USER DATA");
            ui_draw_text_depth(26, 68, 0.9f, 0.38f, UI_CLR_CYBER_WHITE, "Are you sure you want to reset credentials?");
            ui_draw_text_depth(26, 88, 0.9f, 0.36f, UI_CLR_CYBER_TEXT_DIM, "This deletes config.ini from your SD card");
            ui_draw_text_depth(26, 104, 0.9f, 0.36f, UI_CLR_CYBER_TEXT_DIM, "and returns to initial credential setup.");
            ui_draw_text_depth(26, 126, 0.9f, 0.36f, UI_CLR_CYBER_RED, "This action cannot be undone.");
        }
        else if (settings_modal == SETTINGS_MODAL_CLEAR_CACHE)
        {
            ui_draw_text_depth(26, 44, 0.9f, 0.48f, UI_CLR_CYBER_AMBER, "CONFIRM: CLEAR CACHE");
            ui_draw_text_depth(26, 68, 0.9f, 0.38f, UI_CLR_CYBER_WHITE, "Are you sure you want to clear cached files?");
            ui_draw_text_depth(26, 88, 0.9f, 0.36f, UI_CLR_CYBER_TEXT_DIM, "All badge icons and cached JSON files will be");
            ui_draw_text_depth(26, 104, 0.9f, 0.36f, UI_CLR_CYBER_TEXT_DIM, "deleted from sdmc:/3ds/retroachievements/cache/.");
            ui_draw_text_depth(26, 126, 0.9f, 0.36f, UI_CLR_CYBER_AMBER, "Images will re-download when viewed.");
        }

        // Modal Action 1: [ Confirm ] (x: 24..154, y: 154..194, w: 130, h: 40)
        ui_draw_glass_panel(24, 154, 0.8f, 130, 40, modal_accent, true, pulse);
        ui_draw_text_depth(34, 164, 0.9f, 0.44f, modal_accent, "[A] Confirm");

        // Modal Action 2: [ Cancel ] (x: 166..296, y: 154..194, w: 130, h: 40)
        ui_draw_glass_panel(166, 154, 0.8f, 130, 40, UI_CLR_CYBER_CYAN, false, pulse);
        ui_draw_text_depth(176, 164, 0.9f, 0.44f, UI_CLR_CYBER_CYAN, "[B] Cancel");
    }
}
