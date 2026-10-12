/**
 * @file async_api.h
 * @brief RetroAchievements 3DS - Phase 7: Non-blocking Async API Worker
 */

#pragma once

#include "config.h"
#include "retro_api.h"
#include "ui.h"

#include <3ds.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    ASYNC_STATUS_IDLE = 0,
    ASYNC_STATUS_BUSY,
    ASYNC_STATUS_SUCCESS,
    ASYNC_STATUS_ERROR
} async_api_status_t;

typedef enum {
    ASYNC_REQ_NONE = 0,
    ASYNC_REQ_USER_SUMMARY,
    ASYNC_REQ_GAME_PROGRESS,
    ASYNC_REQ_SEARCH
} async_req_type_t;

/**
 * @brief Initialize background worker thread and synchronization primitives.
 * @return True on success, false otherwise.
 */
bool async_api_init(void);

/**
 * @brief Stop worker thread and free all pending task memory.
 */
void async_api_cleanup(void);

/**
 * @brief Request an asynchronous fetch of the user summary.
 * @param cfg Application configuration containing credentials.
 * @return True if enqueued, false if worker is busy or uninitialized.
 */
bool async_api_request_user_summary(const app_config_t* cfg);

/**
 * @brief Request an asynchronous fetch of a game's progress and achievements.
 * @param cfg Application configuration containing credentials.
 * @param game_id Target RetroAchievements game ID.
 * @return True if enqueued, false if worker is busy or uninitialized.
 */
bool async_api_request_game_progress(const app_config_t* cfg, uint32_t game_id);

/**
 * @brief Request an asynchronous global search across all RetroAchievements games.
 * @param cfg Application configuration containing credentials.
 * @param query Search query string (game title keywords or game ID).
 * @return True if enqueued, false if worker is busy or uninitialized.
 */
bool async_api_request_search(const app_config_t* cfg, const char* query);

/**
 * @brief Invalidate and cancel any in-flight background request.
 * Outdated network responses will be safely discarded without modifying state.
 */
void async_api_cancel(void);

/**
 * @brief Check if the background worker is currently executing a network request.
 * @return True if busy, false if idle.
 */
bool async_api_is_busy(void);

/**
 * @brief Get the latest real-time status message from background operations (e.g. search progress).
 */
void async_api_get_progress_status(char* out_buf, size_t out_size);

/**
 * @brief Poll the async worker for completed results.
 * When a result is ready (SUCCESS or ERROR), this consumes the result and resets to IDLE.
 *
 * @param out_type Pointer to receive completed request type (optional).
 * @param out_summary Pointer to receive completed user summary (optional).
 * @param out_progress Pointer to receive completed game progress (transfers ownership).
 * @param out_search_results Pointer to array of search results (optional).
 * @param out_search_count Pointer to receive number of search results (optional).
 * @param err_buf Buffer to receive error message on ASYNC_STATUS_ERROR.
 * @param err_size Size of err_buf in bytes.
 * @return ASYNC_STATUS_BUSY, ASYNC_STATUS_SUCCESS, ASYNC_STATUS_ERROR, or ASYNC_STATUS_IDLE.
 */
async_api_status_t async_api_poll(
    async_req_type_t* out_type,
    ra_user_summary_t* out_summary,
    ra_game_progress_t* out_progress,
    search_result_item_t* out_search_results,
    size_t* out_search_count,
    char* err_buf,
    size_t err_size
);

