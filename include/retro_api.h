/**
 * @file retro_api.h
 * @brief RetroAchievements 3DS - Phase 4: RetroAchievements API Client Core
 */

#pragma once

#include "config.h"
#include "network.h"

#include <3ds/types.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RA_API_BASE_URL         "https://retroachievements.org/API/"
#define RA_MAX_RECENT_GAMES     10
#define RA_TITLE_MAX_LEN        96
#define RA_DESC_MAX_LEN         160
#define RA_CONSOLE_MAX_LEN      48
#define RA_BADGE_MAX_LEN        16
#define RA_DATE_MAX_LEN         24
#define RA_URL_MAX_LEN          128

/**
 * @brief RetroAchievements API result codes.
 */
typedef enum {
    RA_OK = 0,
    RA_ERR_NOT_CONFIGURED = -1,
    RA_ERR_INVALID_PARAM = -2,
    RA_ERR_NETWORK = -3,
    RA_ERR_HTTP = -4,
    RA_ERR_RATE_LIMITED = -5,       // HTTP 429
    RA_ERR_SERVER_ERROR = -6,       // HTTP 5xx
    RA_ERR_AUTH_FAILED = -7,        // Invalid username or Web API key
    RA_ERR_MALFORMED_JSON = -8,     // JSON parse error
    RA_ERR_API_ERROR = -9,          // API returned error description
    RA_ERR_OUT_OF_MEMORY = -10
} ra_result_t;

/**
 * @brief Summarized recently played game.
 */
typedef struct {
    uint32_t game_id;
    char     title[RA_TITLE_MAX_LEN];
    uint32_t console_id;
    char     console_name[RA_CONSOLE_MAX_LEN];
    char     image_icon[RA_URL_MAX_LEN];
    char     last_played[RA_DATE_MAX_LEN];
} ra_recent_game_t;

/**
 * @brief User Summary Data Model (API_GetUserSummary.php).
 */
typedef struct {
    char             username[CONFIG_USERNAME_MAX_LEN];
    uint32_t         total_points;
    uint32_t         total_softcore_points;
    uint32_t         total_true_points;
    uint32_t         rank;
    uint32_t         total_ranked;
    char             user_pic[RA_URL_MAX_LEN];
    char             rich_presence[RA_DESC_MAX_LEN];
    char             motto[RA_DESC_MAX_LEN];
    char             member_since[RA_DATE_MAX_LEN];
    size_t           recent_games_count;
    ra_recent_game_t recent_games[RA_MAX_RECENT_GAMES];
    char             status_msg[64];
} ra_user_summary_t;

/**
 * @brief Individual Achievement Data Model.
 */
typedef struct {
    uint32_t id;
    char     title[RA_TITLE_MAX_LEN];
    char     description[RA_DESC_MAX_LEN];
    uint16_t points;
    uint16_t true_ratio;
    char     badge_name[RA_BADGE_MAX_LEN];
    char     date_earned[RA_DATE_MAX_LEN];
    char     date_earned_hardcore[RA_DATE_MAX_LEN];
    bool     is_unlocked;
    bool     is_hardcore;
} ra_achievement_t;

/**
 * @brief Game Progress & Achievements Data Model (API_GetGameInfoAndUserProgress.php).
 */
typedef struct {
    uint32_t          game_id;
    char              title[RA_TITLE_MAX_LEN];
    uint32_t          console_id;
    char              console_name[RA_CONSOLE_MAX_LEN];
    char              image_icon[RA_URL_MAX_LEN];
    uint32_t          num_achievements;
    uint32_t          num_awarded;
    uint32_t          num_awarded_hardcore;
    char              user_completion[16];
    char              user_completion_hardcore[16];
    ra_achievement_t* achievements;        // Heap-allocated contiguous array
    size_t            achievement_count;   // Total achievements in array
    size_t            achievement_capacity;
} ra_game_progress_t;

/**
 * @brief Free heap memory allocated for game achievements.
 */
void ra_game_progress_free(ra_game_progress_t* progress);

/**
 * @brief Fetch user summary including score, rank, and recent games.
 *
 * @param cfg Validated application configuration containing credentials.
 * @param target_username Optional target user (NULL to query own profile).
 * @param[out] out_summary Destination user summary struct.
 * @param[out] out_err_msg Optional buffer for detailed error diagnostics (or NULL).
 * @param err_msg_len Size of out_err_msg buffer.
 * @return RA_OK on success, or structured error code.
 */
ra_result_t ra_api_get_user_summary(
    const app_config_t* cfg,
    const char* target_username,
    ra_user_summary_t* out_summary,
    char* out_err_msg,
    size_t err_msg_len
);

/**
 * @brief Fetch game metadata, unlock progress, and full achievement list.
 *
 * @param cfg Validated application configuration containing credentials.
 * @param game_id Numerical RetroAchievements Game ID.
 * @param[out] out_progress Destination game progress struct.
 * @param[out] out_err_msg Optional buffer for detailed error diagnostics (or NULL).
 * @param err_msg_len Size of out_err_msg buffer.
 * @return RA_OK on success, or structured error code.
 */
ra_result_t ra_api_get_game_progress(
    const app_config_t* cfg,
    uint32_t game_id,
    ra_game_progress_t* out_progress,
    char* out_err_msg,
    size_t err_msg_len
);

/**
 * @brief Fetch raw JSON list of games for a specific console from RetroAchievements.
 *
 * @param cfg Validated application configuration containing credentials.
 * @param console_id Target console ID (e.g. 3 for SNES, 7 for NES).
 * @param[out] out_json Allocated buffer containing JSON response (caller must free()).
 * @param[out] out_size Size of downloaded buffer.
 * @param[out] out_err_msg Optional buffer for detailed error diagnostics (or NULL).
 * @param err_msg_len Size of out_err_msg buffer.
 * @return RA_OK on success, or structured error code.
 */
ra_result_t ra_api_get_console_games_json(
    const app_config_t* cfg,
    uint32_t console_id,
    char** out_json,
    size_t* out_size,
    char* out_err_msg,
    size_t err_msg_len
);

/**
 * @brief Convert ra_result_t to a human-readable diagnostic string.
 */
const char* ra_result_to_string(ra_result_t result);

