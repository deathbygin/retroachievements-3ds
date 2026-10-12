/**
 * @file retro_api.c
 * @brief RetroAchievements 3DS - Phase 4: RetroAchievements API Client Core Implementation
 */

#include "retro_api.h"

#include <curl/curl.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_USER_SUMMARY_BYTES  (128 * 1024)   // 128 KiB
#define MAX_GAME_PROGRESS_BYTES (1024 * 1024)  // 1 MiB
#define URL_BUF_SIZE            512

/**
 * @brief Helper to extract an integer from either a JSON integer or string node.
 */
static uint32_t json_get_uint32(const json_t* obj, const char* key, uint32_t default_val)
{
    if (!json_is_object(obj) || key == NULL)
    {
        return default_val;
    }
    json_t* node = json_object_get(obj, key);
    if (json_is_integer(node))
    {
        return (uint32_t)json_integer_value(node);
    }
    if (json_is_string(node))
    {
        return (uint32_t)strtoul(json_string_value(node), NULL, 10);
    }
    return default_val;
}

/**
 * @brief Helper to safely copy a JSON string with bounded null-termination.
 */
static void json_get_string(const json_t* obj, const char* key, char* dest, size_t dest_size, const char* default_val)
{
    if (dest == NULL || dest_size == 0)
    {
        return;
    }
    dest[0] = '\0';

    if (!json_is_object(obj) || key == NULL)
    {
        if (default_val != NULL)
        {
            strncpy(dest, default_val, dest_size - 1);
            dest[dest_size - 1] = '\0';
        }
        return;
    }

    json_t* node = json_object_get(obj, key);
    if (json_is_string(node))
    {
        const char* val = json_string_value(node);
        if (val != NULL)
        {
            strncpy(dest, val, dest_size - 1);
            dest[dest_size - 1] = '\0';
            return;
        }
    }

    if (default_val != NULL)
    {
        strncpy(dest, default_val, dest_size - 1);
        dest[dest_size - 1] = '\0';
    }
}

const char* ra_result_to_string(ra_result_t result)
{
    switch (result)
    {
        case RA_OK:                    return "Success (RA_OK)";
        case RA_ERR_NOT_CONFIGURED:    return "Credentials not configured";
        case RA_ERR_INVALID_PARAM:     return "Invalid query parameter";
        case RA_ERR_NETWORK:           return "Network connection failed";
        case RA_ERR_HTTP:              return "HTTP transport error";
        case RA_ERR_RATE_LIMITED:      return "API rate limited (HTTP 429)";
        case RA_ERR_SERVER_ERROR:      return "Server error (HTTP 5xx)";
        case RA_ERR_AUTH_FAILED:       return "Authentication failed / Invalid API key";
        case RA_ERR_MALFORMED_JSON:    return "Malformed JSON response";
        case RA_ERR_API_ERROR:         return "RetroAchievements API reported error";
        case RA_ERR_OUT_OF_MEMORY:     return "Out of heap memory";
        default:                       return "Unknown error";
    }
}

void ra_game_progress_free(ra_game_progress_t* progress)
{
    if (progress == NULL)
    {
        return;
    }

    if (progress->achievements != NULL)
    {
        free(progress->achievements);
        progress->achievements = NULL;
    }
    progress->achievement_count = 0;
    progress->achievement_capacity = 0;
}

ra_result_t ra_api_get_user_summary(
    const app_config_t* cfg,
    const char* target_username,
    ra_user_summary_t* out_summary,
    char* out_err_msg,
    size_t err_msg_len
)
{
    if (out_summary == NULL)
    {
        return RA_ERR_INVALID_PARAM;
    }
    memset(out_summary, 0, sizeof(ra_user_summary_t));

    if (out_err_msg != NULL && err_msg_len > 0)
    {
        out_err_msg[0] = '\0';
    }

    if (cfg == NULL || !cfg->is_configured)
    {
        if (out_err_msg != NULL && err_msg_len > 0)
        {
            snprintf(out_err_msg, err_msg_len, "Credentials unconfigured in config.ini");
        }
        return RA_ERR_NOT_CONFIGURED;
    }

    const char* user_to_query = (target_username != NULL && target_username[0] != '\0')
        ? target_username : cfg->username;

    // URL-encode query parameters
    char* enc_auth_user = curl_easy_escape(NULL, cfg->username, 0);
    char* enc_api_key   = curl_easy_escape(NULL, cfg->web_api_key, 0);
    char* enc_query_u   = curl_easy_escape(NULL, user_to_query, 0);

    if (!enc_auth_user || !enc_api_key || !enc_query_u)
    {
        if (enc_auth_user) curl_free(enc_auth_user);
        if (enc_api_key)   curl_free(enc_api_key);
        if (enc_query_u)   curl_free(enc_query_u);
        return RA_ERR_OUT_OF_MEMORY;
    }

    char url[URL_BUF_SIZE];
    snprintf(url, sizeof(url),
             "%sAPI_GetUserSummary.php?z=%s&y=%s&u=%s&g=%d&a=5",
             RA_API_BASE_URL, enc_auth_user, enc_api_key, enc_query_u, RA_MAX_RECENT_GAMES);

    curl_free(enc_auth_user);
    curl_free(enc_api_key);
    curl_free(enc_query_u);

    // Perform bounded HTTPS GET request
    const char* ca_path = cfg->ssl_verify_peer ? "romfs:/certs/cacert.pem" : NULL;
    http_response_t resp;
    net_result_t net_rc = http_get(
        url,
        ca_path,
        MAX_USER_SUMMARY_BYTES,
        10L,
        25L,
        &resp
    );

    // Fallback: If TLS verification failed due to clock skew or missing intermediate cert, retry unverified
    if (net_rc == NET_ERR_TLS_FAILED && ca_path != NULL)
    {
        http_response_free(&resp);
        net_rc = http_get(
            url,
            NULL,
            MAX_USER_SUMMARY_BYTES,
            10L,
            25L,
            &resp
        );
    }

    if (net_rc != NET_OK)
    {
        if (out_err_msg != NULL && err_msg_len > 0)
        {
            snprintf(out_err_msg, err_msg_len, "Network error: %s", resp.error_desc);
        }
        http_response_free(&resp);
        return (net_rc == NET_ERR_NO_WIFI) ? RA_ERR_NETWORK : RA_ERR_HTTP;
    }

    if (resp.http_status == 401 || resp.http_status == 403)
    {
        if (out_err_msg != NULL && err_msg_len > 0)
        {
            snprintf(out_err_msg, err_msg_len, "Invalid Username or API Key (HTTP %ld)", resp.http_status);
        }
        http_response_free(&resp);
        return RA_ERR_AUTH_FAILED;
    }
    if (resp.http_status == 429)
    {
        if (out_err_msg != NULL && err_msg_len > 0)
        {
            snprintf(out_err_msg, err_msg_len, "Rate limit exceeded (HTTP 429)");
        }
        http_response_free(&resp);
        return RA_ERR_RATE_LIMITED;
    }
    if (resp.http_status >= 500)
    {
        if (out_err_msg != NULL && err_msg_len > 0)
        {
            snprintf(out_err_msg, err_msg_len, "RetroAchievements server error (HTTP %ld)", resp.http_status);
        }
        http_response_free(&resp);
        return RA_ERR_SERVER_ERROR;
    }

    // Ingest JSON payload
    json_error_t jerr;
    json_t* root = json_loads(resp.body, 0, &jerr);
    if (!root)
    {
        if (out_err_msg != NULL && err_msg_len > 0)
        {
            snprintf(out_err_msg, err_msg_len, "JSON parse error: %s (line %d)", jerr.text, jerr.line);
        }
        http_response_free(&resp);
        return RA_ERR_MALFORMED_JSON;
    }

    // Check for API-side error response
    json_t* err_node = json_object_get(root, "Error");
    if (json_is_string(err_node))
    {
        const char* err_str = json_string_value(err_node);
        if (out_err_msg != NULL && err_msg_len > 0)
        {
            snprintf(out_err_msg, err_msg_len, "API Error: %s", err_str);
        }
        ra_result_t final_err = (strstr(err_str, "credential") || strstr(err_str, "Key") || strstr(err_str, "key"))
            ? RA_ERR_AUTH_FAILED : RA_ERR_API_ERROR;
        json_decref(root);
        http_response_free(&resp);
        return final_err;
    }

    // Extract user summary attributes
    json_get_string(root, "User", out_summary->username, sizeof(out_summary->username), user_to_query);
    out_summary->total_points          = json_get_uint32(root, "TotalPoints", 0);
    out_summary->total_softcore_points = json_get_uint32(root, "TotalSoftcorePoints", 0);
    out_summary->total_true_points     = json_get_uint32(root, "TotalTruePoints", 0);
    out_summary->rank                  = json_get_uint32(root, "Rank", 0);
    out_summary->total_ranked          = json_get_uint32(root, "TotalRanked", 0);
    json_get_string(root, "UserPic", out_summary->user_pic, sizeof(out_summary->user_pic), "");
    json_get_string(root, "RichPresenceMsg", out_summary->rich_presence, sizeof(out_summary->rich_presence), "");
    json_get_string(root, "Motto", out_summary->motto, sizeof(out_summary->motto), "");
    if (out_summary->motto[0] == '\0')
    {
        json_get_string(root, "motto", out_summary->motto, sizeof(out_summary->motto), "");
    }
    json_get_string(root, "MemberSince", out_summary->member_since, sizeof(out_summary->member_since), "");
    json_get_string(root, "Status", out_summary->status_msg, sizeof(out_summary->status_msg), "Success");

    // Extract recently played games
    json_t* recent_arr = json_object_get(root, "RecentlyPlayed");
    if (json_is_array(recent_arr))
    {
        size_t count = json_array_size(recent_arr);
        if (count > RA_MAX_RECENT_GAMES)
        {
            count = RA_MAX_RECENT_GAMES;
        }
        out_summary->recent_games_count = count;

        for (size_t i = 0; i < count; i++)
        {
            json_t* game_obj = json_array_get(recent_arr, i);
            if (!json_is_object(game_obj))
            {
                continue;
            }
            ra_recent_game_t* g = &out_summary->recent_games[i];
            g->game_id    = json_get_uint32(game_obj, "GameID", 0);
            g->console_id = json_get_uint32(game_obj, "ConsoleID", 0);
            json_get_string(game_obj, "Title", g->title, sizeof(g->title), "Untitled Game");
            json_get_string(game_obj, "ConsoleName", g->console_name, sizeof(g->console_name), "Unknown");
            json_get_string(game_obj, "ImageIcon", g->image_icon, sizeof(g->image_icon), "");
            json_get_string(game_obj, "LastPlayed", g->last_played, sizeof(g->last_played), "");
        }
    }

    // Immediately free both the JSON AST and raw HTTP payload
    json_decref(root);
    http_response_free(&resp);

    return RA_OK;
}

ra_result_t ra_api_get_game_progress(
    const app_config_t* cfg,
    uint32_t game_id,
    ra_game_progress_t* out_progress,
    char* out_err_msg,
    size_t err_msg_len
)
{
    if (out_progress == NULL)
    {
        return RA_ERR_INVALID_PARAM;
    }
    memset(out_progress, 0, sizeof(ra_game_progress_t));

    if (out_err_msg != NULL && err_msg_len > 0)
    {
        out_err_msg[0] = '\0';
    }

    if (cfg == NULL || !cfg->is_configured)
    {
        if (out_err_msg != NULL && err_msg_len > 0)
        {
            snprintf(out_err_msg, err_msg_len, "Credentials unconfigured in config.ini");
        }
        return RA_ERR_NOT_CONFIGURED;
    }

    if (game_id == 0)
    {
        if (out_err_msg != NULL && err_msg_len > 0)
        {
            snprintf(out_err_msg, err_msg_len, "Invalid GameID: 0");
        }
        return RA_ERR_INVALID_PARAM;
    }

    char* enc_auth_user = curl_easy_escape(NULL, cfg->username, 0);
    char* enc_api_key   = curl_easy_escape(NULL, cfg->web_api_key, 0);

    if (!enc_auth_user || !enc_api_key)
    {
        if (enc_auth_user) curl_free(enc_auth_user);
        if (enc_api_key)   curl_free(enc_api_key);
        return RA_ERR_OUT_OF_MEMORY;
    }

    char url[URL_BUF_SIZE];
    snprintf(url, sizeof(url),
             "%sAPI_GetGameInfoAndUserProgress.php?z=%s&y=%s&u=%s&g=%lu",
             RA_API_BASE_URL, enc_auth_user, enc_api_key, enc_auth_user, (unsigned long)game_id);

    curl_free(enc_auth_user);
    curl_free(enc_api_key);

    const char* ca_path = cfg->ssl_verify_peer ? "romfs:/certs/cacert.pem" : NULL;
    http_response_t resp;
    net_result_t net_rc = http_get(
        url,
        ca_path,
        MAX_GAME_PROGRESS_BYTES,
        10L,
        30L,
        &resp
    );

    // Fallback: If TLS verification failed due to clock skew or missing intermediate cert, retry unverified
    if (net_rc == NET_ERR_TLS_FAILED && ca_path != NULL)
    {
        http_response_free(&resp);
        net_rc = http_get(
            url,
            NULL,
            MAX_GAME_PROGRESS_BYTES,
            10L,
            30L,
            &resp
        );
    }

    if (net_rc != NET_OK)
    {
        if (out_err_msg != NULL && err_msg_len > 0)
        {
            snprintf(out_err_msg, err_msg_len, "Network error: %s", resp.error_desc);
        }
        http_response_free(&resp);
        return (net_rc == NET_ERR_NO_WIFI) ? RA_ERR_NETWORK : RA_ERR_HTTP;
    }

    if (resp.http_status == 429)
    {
        if (out_err_msg != NULL && err_msg_len > 0)
        {
            snprintf(out_err_msg, err_msg_len, "Rate limit exceeded (HTTP 429)");
        }
        http_response_free(&resp);
        return RA_ERR_RATE_LIMITED;
    }
    if (resp.http_status >= 500)
    {
        if (out_err_msg != NULL && err_msg_len > 0)
        {
            snprintf(out_err_msg, err_msg_len, "RetroAchievements server error (HTTP %ld)", resp.http_status);
        }
        http_response_free(&resp);
        return RA_ERR_SERVER_ERROR;
    }

    json_error_t jerr;
    json_t* root = json_loads(resp.body, 0, &jerr);
    if (!root)
    {
        if (out_err_msg != NULL && err_msg_len > 0)
        {
            snprintf(out_err_msg, err_msg_len, "JSON parse error: %s", jerr.text);
        }
        http_response_free(&resp);
        return RA_ERR_MALFORMED_JSON;
    }

    json_t* err_node = json_object_get(root, "Error");
    if (json_is_string(err_node))
    {
        const char* err_str = json_string_value(err_node);
        if (out_err_msg != NULL && err_msg_len > 0)
        {
            snprintf(out_err_msg, err_msg_len, "API Error: %s", err_str);
        }
        json_decref(root);
        http_response_free(&resp);
        return RA_ERR_API_ERROR;
    }

    out_progress->game_id    = json_get_uint32(root, "ID", game_id);
    out_progress->console_id = json_get_uint32(root, "ConsoleID", 0);
    json_get_string(root, "Title", out_progress->title, sizeof(out_progress->title), "Unknown");
    json_get_string(root, "ConsoleName", out_progress->console_name, sizeof(out_progress->console_name), "Unknown");
    json_get_string(root, "ImageIcon", out_progress->image_icon, sizeof(out_progress->image_icon), "");

    out_progress->num_achievements       = json_get_uint32(root, "NumAchievements", 0);
    out_progress->num_awarded            = json_get_uint32(root, "NumAwardedToUser", 0);
    out_progress->num_awarded_hardcore   = json_get_uint32(root, "NumAwardedToUserHardcore", 0);
    json_get_string(root, "UserCompletion", out_progress->user_completion, sizeof(out_progress->user_completion), "0%");
    json_get_string(root, "UserCompletionHardcore", out_progress->user_completion_hardcore, sizeof(out_progress->user_completion_hardcore), "0%");

    // Extract achievements list
    json_t* ach_node = json_object_get(root, "Achievements");
    if (json_is_object(ach_node))
    {
        size_t total_keys = json_object_size(ach_node);
        if (total_keys > 0)
        {
            out_progress->achievements = (ra_achievement_t*)calloc(total_keys, sizeof(ra_achievement_t));
            if (out_progress->achievements == NULL)
            {
                json_decref(root);
                http_response_free(&resp);
                return RA_ERR_OUT_OF_MEMORY;
            }
            out_progress->achievement_capacity = total_keys;
            out_progress->achievement_count = 0;

            const char* key;
            json_t* val;
            json_object_foreach(ach_node, key, val)
            {
                if (!json_is_object(val) || out_progress->achievement_count >= total_keys)
                {
                    continue;
                }
                ra_achievement_t* a = &out_progress->achievements[out_progress->achievement_count++];
                a->id         = json_get_uint32(val, "ID", (uint32_t)strtoul(key, NULL, 10));
                a->points     = (uint16_t)json_get_uint32(val, "Points", 0);
                a->true_ratio = (uint16_t)json_get_uint32(val, "TrueRatio", 0);

                json_get_string(val, "Title", a->title, sizeof(a->title), "Achievement");
                json_get_string(val, "Description", a->description, sizeof(a->description), "");
                json_get_string(val, "BadgeName", a->badge_name, sizeof(a->badge_name), "");
                json_get_string(val, "DateEarned", a->date_earned, sizeof(a->date_earned), "");
                json_get_string(val, "DateEarnedHardcore", a->date_earned_hardcore, sizeof(a->date_earned_hardcore), "");

                a->is_hardcore = (a->date_earned_hardcore[0] != '\0');
                a->is_unlocked = a->is_hardcore || (a->date_earned[0] != '\0');
            }
        }
    }

    // Immediately free both the JSON AST and raw HTTP payload
    json_decref(root);
    http_response_free(&resp);

    return RA_OK;
}

ra_result_t ra_api_get_console_games_json(
    const app_config_t* cfg,
    uint32_t console_id,
    char** out_json,
    size_t* out_size,
    char* out_err_msg,
    size_t err_msg_len
)
{
    if (out_json == NULL || out_size == NULL)
    {
        return RA_ERR_INVALID_PARAM;
    }
    *out_json = NULL;
    *out_size = 0;

    if (cfg == NULL || !cfg->is_configured)
    {
        if (out_err_msg != NULL && err_msg_len > 0)
        {
            snprintf(out_err_msg, err_msg_len, "Configuration uninitialized or missing credentials");
        }
        return RA_ERR_NOT_CONFIGURED;
    }

    char* enc_auth_user = curl_easy_escape(NULL, cfg->username, 0);
    char* enc_api_key   = curl_easy_escape(NULL, cfg->web_api_key, 0);

    char url[URL_BUF_SIZE];
    snprintf(url, sizeof(url),
             "%sAPI_GetGameList.php?z=%s&y=%s&i=%lu&f=1",
             RA_API_BASE_URL,
             enc_auth_user ? enc_auth_user : cfg->username,
             enc_api_key ? enc_api_key : cfg->web_api_key,
             (unsigned long)console_id);

    if (enc_auth_user) curl_free(enc_auth_user);
    if (enc_api_key) curl_free(enc_api_key);

    const char* ca_path = cfg->ssl_verify_peer ? "romfs:/certs/cacert.pem" : NULL;

    http_response_t resp;
    net_result_t net_rc = http_get(url, ca_path, 2 * 1024 * 1024, 8L, 18L, &resp);

    if (net_rc == NET_ERR_TLS_FAILED && ca_path != NULL)
    {
        http_response_free(&resp);
        net_rc = http_get(url, NULL, 2 * 1024 * 1024, 8L, 18L, &resp);
    }

    if (net_rc != NET_OK)
    {
        if (out_err_msg != NULL && err_msg_len > 0)
        {
            snprintf(out_err_msg, err_msg_len, "Network error: %s", resp.error_desc);
        }
        http_response_free(&resp);
        return (net_rc == NET_ERR_NO_WIFI) ? RA_ERR_NETWORK : RA_ERR_HTTP;
    }

    if (resp.http_status != 200)
    {
        if (out_err_msg != NULL && err_msg_len > 0)
        {
            snprintf(out_err_msg, err_msg_len, "HTTP %ld returned from RetroAchievements", resp.http_status);
        }
        http_response_free(&resp);
        return RA_ERR_HTTP;
    }

    *out_json = resp.body;
    *out_size = resp.body_size;
    resp.body = NULL; // Take ownership
    http_response_free(&resp);

    return RA_OK;
}

