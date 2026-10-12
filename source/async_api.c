/**
 * @file async_api.c
 * @brief RetroAchievements 3DS - Phase 7: Non-blocking Async API Worker Implementation
 */

#include "async_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASYNC_STACK_SIZE (32 * 1024)

typedef struct {
    async_req_type_t   type;
    uint32_t           game_id;
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
static char                s_err_msg[128] = {0};

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

        // Execute API call outside of lock so UI render loop runs at 60 FPS
        char err_temp[128] = {0};
        ra_user_summary_t temp_summary;
        ra_game_progress_t temp_progress;
        memset(&temp_summary, 0, sizeof(temp_summary));
        memset(&temp_progress, 0, sizeof(temp_progress));

        ra_result_t res = RA_ERR_NETWORK;

        if (task.type == ASYNC_REQ_USER_SUMMARY)
        {
            res = ra_api_get_user_summary(&task.config, NULL, &temp_summary, err_temp, sizeof(err_temp));
        }
        else if (task.type == ASYNC_REQ_GAME_PROGRESS)
        {
            res = ra_api_get_game_progress(&task.config, task.game_id, &temp_progress, err_temp, sizeof(err_temp));
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
    s_pending_task.config = *cfg;
    s_pending_task.seq = s_current_seq;

    s_has_task = true;
    s_status = ASYNC_STATUS_BUSY;

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

async_api_status_t async_api_poll(
    async_req_type_t* out_type,
    ra_user_summary_t* out_summary,
    ra_game_progress_t* out_progress,
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
