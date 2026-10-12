/**
 * @file async_api.c
 * @brief RetroAchievements 3DS - Phase 7: Non-blocking Async API Worker Implementation
 */

#include "async_api.h"
#include "ui.h"

#include <ctype.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define ASYNC_STACK_SIZE (48 * 1024)

typedef struct {
    async_req_type_t   type;
    uint32_t           game_id;
    char               query[64];
    app_config_t       config;
    uint32_t           seq;
} async_task_t;

static Thread              s_thread = NULL;
static volatile bool       s_thread_running = false;
static LightLock           s_lock;

static bool                s_has_task = false;
static async_task_t        s_pending_task;

static uint32_t            s_current_seq = 0;
static async_api_status_t  s_status = ASYNC_STATUS_IDLE;
static async_req_type_t    s_result_type = ASYNC_REQ_NONE;
static ra_user_summary_t   s_result_summary;
static ra_game_progress_t  s_result_progress;
static search_result_item_t s_result_search[MAX_SEARCH_RESULTS];
static size_t              s_result_search_count = 0;
static char                s_search_status_msg[128] = {0};
static char                s_err_msg[128] = {0};

static const struct {
    uint32_t id;
    const char* name;
} s_consoles[] = {
    { 3,  "SNES" },
    { 7,  "NES" },
    { 5,  "Game Boy Advance" },
    { 1,  "Mega Drive" },
    { 4,  "Game Boy" },
    { 6,  "Game Boy Color" },
    { 2,  "Nintendo 64" },
    { 12, "PlayStation" },
    { 18, "Nintendo DS" },
    { 8,  "PC Engine" },
    { 9,  "Master System" },
    { 10, "Game Gear" },
    { 22, "Dreamcast" },
    { 41, "PSP" },
    { 21, "PlayStation 2" },
    { 13, "Atari 2600" },
    { 19, "Sega CD" },
    { 20, "32X" },
    { 23, "Neo Geo Pocket" },
    { 24, "Atari 7800" },
    { 25, "Atari Lynx" },
    { 27, "Arcade" },
    { 28, "Virtual Boy" },
    { 29, "MSX" },
    { 33, "SG-1000" },
    { 38, "Apple II" },
    { 39, "Saturn" },
    { 43, "WonderSwan" },
    { 49, "ColecoVision" },
    { 53, "Intellivision" },
    { 54, "Vectrex" }
};

static bool str_contains_caseless(const char* haystack, const char* needle)
{
    if (haystack == NULL || needle == NULL) return false;
    if (*needle == '\0') return true;

    size_t needle_len = strlen(needle);
    size_t hay_len = strlen(haystack);
    if (needle_len > hay_len) return false;

    for (size_t i = 0; i <= hay_len - needle_len; i++)
    {
        if (strncasecmp(&haystack[i], needle, needle_len) == 0)
        {
            return true;
        }
    }
    return false;
}

static bool is_numeric_str(const char* s)
{
    if (s == NULL || *s == '\0') return false;
    while (*s)
    {
        if (!isdigit((unsigned char)*s)) return false;
        s++;
    }
    return true;
}

static void async_worker_func(void* arg)
{
    (void)arg;

    while (s_thread_running)
    {
        async_task_t task;
        bool has_work = false;

        LightLock_Lock(&s_lock);
        if (s_has_task)
        {
            task = s_pending_task;
            s_has_task = false;
            s_status = ASYNC_STATUS_BUSY;
            has_work = true;
        }
        LightLock_Unlock(&s_lock);

        if (!has_work)
        {
            svcSleepThread(20 * 1000 * 1000LL); // 20 ms yield
            continue;
        }

        // Execute API call outside of lock so UI render loop runs smoothly at 60 FPS
        char err_temp[128] = {0};
        ra_user_summary_t temp_summary;
        ra_game_progress_t temp_progress;
        search_result_item_t temp_search[MAX_SEARCH_RESULTS];
        size_t temp_search_count = 0;

        memset(&temp_summary, 0, sizeof(temp_summary));
        memset(&temp_progress, 0, sizeof(temp_progress));
        memset(temp_search, 0, sizeof(temp_search));

        ra_result_t res = RA_ERR_NETWORK;

        if (task.type == ASYNC_REQ_USER_SUMMARY)
        {
            res = ra_api_get_user_summary(&task.config, NULL, &temp_summary, err_temp, sizeof(err_temp));
        }
        else if (task.type == ASYNC_REQ_GAME_PROGRESS)
        {
            res = ra_api_get_game_progress(&task.config, task.game_id, &temp_progress, err_temp, sizeof(err_temp));
        }
        else if (task.type == ASYNC_REQ_SEARCH)
        {
            mkdir("sdmc:/3ds", 0777);
            mkdir("sdmc:/3ds/retroachievements", 0777);
            mkdir("sdmc:/3ds/retroachievements/cache", 0777);
            mkdir("sdmc:/3ds/retroachievements/cache/gamelists", 0777);

            if (is_numeric_str(task.query))
            {
                uint32_t gid = (uint32_t)strtoul(task.query, NULL, 10);
                temp_search[0].game_id = gid;
                snprintf(temp_search[0].title, sizeof(temp_search[0].title), "Game ID %lu (Direct Lookup)", (unsigned long)gid);
                strncpy(temp_search[0].console_name, "RetroAchievements", sizeof(temp_search[0].console_name) - 1);
                temp_search[0].image_icon[0] = '\0';
                temp_search_count = 1;
                res = RA_OK;
            }
            else
            {
                const size_t num_consoles = sizeof(s_consoles) / sizeof(s_consoles[0]);

                for (size_t c = 0; c < num_consoles; c++)
                {
                    if (task.seq != s_current_seq || !s_thread_running)
                    {
                        break;
                    }

                    LightLock_Lock(&s_lock);
                    snprintf(s_search_status_msg, sizeof(s_search_status_msg), "Scanning %s (%zu found)...",
                             s_consoles[c].name, temp_search_count);
                    LightLock_Unlock(&s_lock);

                    char cache_path[128];
                    snprintf(cache_path, sizeof(cache_path),
                             "sdmc:/3ds/retroachievements/cache/gamelists/c%lu.json",
                             (unsigned long)s_consoles[c].id);

                    char* json_text = NULL;
                    size_t json_size = 0;

                    // 1. Try reading cached console games from SD card
                    FILE* fp = fopen(cache_path, "rb");
                    if (fp != NULL)
                    {
                        fseek(fp, 0, SEEK_END);
                        long fsz = ftell(fp);
                        fseek(fp, 0, SEEK_SET);
                        if (fsz > 0 && fsz < (2 * 1024 * 1024))
                        {
                            json_text = (char*)malloc(fsz + 1);
                            if (json_text != NULL)
                            {
                                size_t nread = fread(json_text, 1, fsz, fp);
                                json_text[nread] = '\0';
                                json_size = nread;
                            }
                        }
                        fclose(fp);
                    }

                    // 2. If not cached, fetch from RetroAchievements API
                    if (json_text == NULL)
                    {
                        ra_result_t dl_rc = ra_api_get_console_games_json(
                            &task.config,
                            s_consoles[c].id,
                            &json_text,
                            &json_size,
                            err_temp,
                            sizeof(err_temp)
                        );

                        if (dl_rc == RA_OK && json_text != NULL && json_size > 0)
                        {
                            char tmp_path[140];
                            snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", cache_path);
                            FILE* wfp = fopen(tmp_path, "wb");
                            if (wfp != NULL)
                            {
                                fwrite(json_text, 1, json_size, wfp);
                                fflush(wfp);
                                fclose(wfp);
                                remove(cache_path);
                                rename(tmp_path, cache_path);
                            }
                        }
                    }

                    // 3. Parse and search games in this console
                    if (json_text != NULL)
                    {
                        json_error_t jerr;
                        json_t* root = json_loads(json_text, 0, &jerr);
                        free(json_text);
                        json_text = NULL;

                        if (root != NULL && json_is_array(root))
                        {
                            size_t arr_len = json_array_size(root);
                            for (size_t g = 0; g < arr_len; g++)
                            {
                                if (task.seq != s_current_seq || !s_thread_running)
                                {
                                    break;
                                }

                                json_t* g_obj = json_array_get(root, g);
                                if (!json_is_object(g_obj)) continue;

                                json_t* title_node = json_object_get(g_obj, "Title");
                                if (!json_is_string(title_node)) continue;
                                const char* title_str = json_string_value(title_node);

                                if (str_contains_caseless(title_str, task.query))
                                {
                                    if (temp_search_count < MAX_SEARCH_RESULTS)
                                    {
                                        search_result_item_t* item = &temp_search[temp_search_count];
                                        json_t* id_node = json_object_get(g_obj, "ID");
                                        item->game_id = (id_node && json_is_integer(id_node)) ? (uint32_t)json_integer_value(id_node) : 0;
                                        strncpy(item->title, title_str, sizeof(item->title) - 1);
                                        item->title[sizeof(item->title) - 1] = '\0';
                                        strncpy(item->console_name, s_consoles[c].name, sizeof(item->console_name) - 1);
                                        item->console_name[sizeof(item->console_name) - 1] = '\0';

                                        json_t* icon_node = json_object_get(g_obj, "ImageIcon");
                                        if (icon_node && json_is_string(icon_node))
                                        {
                                            strncpy(item->image_icon, json_string_value(icon_node), sizeof(item->image_icon) - 1);
                                            item->image_icon[sizeof(item->image_icon) - 1] = '\0';
                                        }
                                        else
                                        {
                                            item->image_icon[0] = '\0';
                                        }

                                        temp_search_count++;
                                        if (temp_search_count >= MAX_SEARCH_RESULTS)
                                        {
                                            break;
                                        }
                                    }
                                }
                            }
                            json_decref(root);
                        }
                    }

                    if (temp_search_count >= MAX_SEARCH_RESULTS)
                    {
                        break;
                    }

                    svcSleepThread(5 * 1000 * 1000LL); // 5 ms yield
                }

                res = RA_OK;
            }
        }

        LightLock_Lock(&s_lock);

        // Verify sequence generation: discard result if request was cancelled or superseded
        if (task.seq != s_current_seq)
        {
            if (task.type == ASYNC_REQ_GAME_PROGRESS && res == RA_OK)
            {
                ra_game_progress_free(&temp_progress);
            }
            // Sequence mismatch: do not update status or overwrite state
        }
        else
        {
            s_result_type = task.type;

            if (res == RA_OK)
            {
                if (task.type == ASYNC_REQ_USER_SUMMARY)
                {
                    s_result_summary = temp_summary;
                }
                else if (task.type == ASYNC_REQ_GAME_PROGRESS)
                {
                    // Clean up any previously stored unused progress
                    ra_game_progress_free(&s_result_progress);
                    s_result_progress = temp_progress;
                }
                else if (task.type == ASYNC_REQ_SEARCH)
                {
                    memcpy(s_result_search, temp_search, sizeof(temp_search));
                    s_result_search_count = temp_search_count;
                    snprintf(s_search_status_msg, sizeof(s_search_status_msg),
                             "Search \"%s\": %zu match(es)", task.query, temp_search_count);
                }
                s_status = ASYNC_STATUS_SUCCESS;
                s_err_msg[0] = '\0';
            }
            else
            {
                s_status = ASYNC_STATUS_ERROR;
                strncpy(s_err_msg, err_temp, sizeof(s_err_msg) - 1);
                s_err_msg[sizeof(s_err_msg) - 1] = '\0';
            }
        }

        LightLock_Unlock(&s_lock);
    }
}

bool async_api_init(void)
{
    LightLock_Init(&s_lock);
    s_has_task = false;
    s_current_seq = 0;
    s_status = ASYNC_STATUS_IDLE;
    s_result_type = ASYNC_REQ_NONE;
    memset(&s_result_summary, 0, sizeof(s_result_summary));
    memset(&s_result_progress, 0, sizeof(s_result_progress));
    memset(s_result_search, 0, sizeof(s_result_search));
    s_result_search_count = 0;
    s_search_status_msg[0] = '\0';
    s_err_msg[0] = '\0';

    s_thread_running = true;
    s_thread = threadCreate(
        async_worker_func,
        NULL,
        ASYNC_STACK_SIZE,
        0x30,
        -2,
        false
    );

    return (s_thread != NULL);
}

void async_api_cleanup(void)
{
    if (s_thread_running)
    {
        s_thread_running = false;
        if (s_thread != NULL)
        {
            threadJoin(s_thread, U64_MAX);
            threadFree(s_thread);
            s_thread = NULL;
        }
    }

    LightLock_Lock(&s_lock);
    ra_game_progress_free(&s_result_progress);
    s_status = ASYNC_STATUS_IDLE;
    LightLock_Unlock(&s_lock);
}

bool async_api_request_user_summary(const app_config_t* cfg)
{
    if (!s_thread_running || cfg == NULL)
    {
        return false;
    }

    LightLock_Lock(&s_lock);

    s_current_seq++;
    s_pending_task.type = ASYNC_REQ_USER_SUMMARY;
    s_pending_task.game_id = 0;
    s_pending_task.query[0] = '\0';
    s_pending_task.config = *cfg;
    s_pending_task.seq = s_current_seq;

    s_has_task = true;
    s_status = ASYNC_STATUS_BUSY;

    LightLock_Unlock(&s_lock);
    return true;
}

bool async_api_request_game_progress(const app_config_t* cfg, uint32_t game_id)
{
    if (!s_thread_running || cfg == NULL)
    {
        return false;
    }

    LightLock_Lock(&s_lock);

    s_current_seq++;
    s_pending_task.type = ASYNC_REQ_GAME_PROGRESS;
    s_pending_task.game_id = game_id;
    s_pending_task.query[0] = '\0';
    s_pending_task.config = *cfg;
    s_pending_task.seq = s_current_seq;

    s_has_task = true;
    s_status = ASYNC_STATUS_BUSY;

    LightLock_Unlock(&s_lock);
    return true;
}

bool async_api_request_search(const app_config_t* cfg, const char* query)
{
    if (!s_thread_running || cfg == NULL || query == NULL || query[0] == '\0')
    {
        return false;
    }

    LightLock_Lock(&s_lock);

    s_current_seq++;
    s_pending_task.type = ASYNC_REQ_SEARCH;
    s_pending_task.game_id = 0;
    strncpy(s_pending_task.query, query, sizeof(s_pending_task.query) - 1);
    s_pending_task.query[sizeof(s_pending_task.query) - 1] = '\0';
    s_pending_task.config = *cfg;
    s_pending_task.seq = s_current_seq;

    s_has_task = true;
    s_status = ASYNC_STATUS_BUSY;
    snprintf(s_search_status_msg, sizeof(s_search_status_msg), "Searching RetroAchievements for \"%s\"...", query);

    LightLock_Unlock(&s_lock);
    return true;
}

void async_api_cancel(void)
{
    LightLock_Lock(&s_lock);
    s_current_seq++; // Invalidates in-flight or pending sequence
    s_has_task = false;
    ra_game_progress_free(&s_result_progress);
    s_status = ASYNC_STATUS_IDLE;
    LightLock_Unlock(&s_lock);
}

bool async_api_is_busy(void)
{
    LightLock_Lock(&s_lock);
    bool busy = (s_status == ASYNC_STATUS_BUSY);
    LightLock_Unlock(&s_lock);
    return busy;
}

void async_api_get_progress_status(char* out_buf, size_t out_size)
{
    if (out_buf == NULL || out_size == 0) return;
    LightLock_Lock(&s_lock);
    strncpy(out_buf, s_search_status_msg, out_size - 1);
    out_buf[out_size - 1] = '\0';
    LightLock_Unlock(&s_lock);
}

async_api_status_t async_api_poll(
    async_req_type_t* out_type,
    ra_user_summary_t* out_summary,
    ra_game_progress_t* out_progress,
    search_result_item_t* out_search_results,
    size_t* out_search_count,
    char* err_buf,
    size_t err_size
)
{
    LightLock_Lock(&s_lock);

    async_api_status_t cur_status = s_status;

    if (cur_status == ASYNC_STATUS_SUCCESS)
    {
        if (out_type != NULL)
        {
            *out_type = s_result_type;
        }

        if (s_result_type == ASYNC_REQ_USER_SUMMARY && out_summary != NULL)
        {
            *out_summary = s_result_summary;
        }
        else if (s_result_type == ASYNC_REQ_GAME_PROGRESS && out_progress != NULL)
        {
            // Transfer ownership of dynamically allocated achievements to caller
            *out_progress = s_result_progress;
            memset(&s_result_progress, 0, sizeof(s_result_progress));
        }
        else if (s_result_type == ASYNC_REQ_SEARCH)
        {
            if (out_search_results != NULL)
            {
                memcpy(out_search_results, s_result_search, sizeof(s_result_search));
            }
            if (out_search_count != NULL)
            {
                *out_search_count = s_result_search_count;
            }
        }

        s_status = ASYNC_STATUS_IDLE;
        s_result_type = ASYNC_REQ_NONE;
    }
    else if (cur_status == ASYNC_STATUS_ERROR)
    {
        if (err_buf != NULL && err_size > 0)
        {
            strncpy(err_buf, s_err_msg, err_size - 1);
            err_buf[err_size - 1] = '\0';
        }

        s_status = ASYNC_STATUS_IDLE;
        s_result_type = ASYNC_REQ_NONE;
    }

    LightLock_Unlock(&s_lock);
    return cur_status;
}
