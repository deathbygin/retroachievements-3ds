/**
 * @file config.h
 * @brief RetroAchievements 3DS - Phase 3: Secure Credential Storage & Config
 */

#pragma once

#include <3ds/types.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CONFIG_DIR_PATH         "sdmc:/3ds/retroachievements"
#define CONFIG_FILE_PATH        "sdmc:/3ds/retroachievements/config.ini"
#define CONFIG_CACHE_DIR_PATH   "sdmc:/3ds/retroachievements/cache"

#define CONFIG_USERNAME_MAX_LEN 64
#define CONFIG_API_KEY_MAX_LEN  64

/**
 * @brief Configuration operation status codes.
 */
typedef enum {
    CONFIG_OK = 0,
    CONFIG_CREATED_TEMPLATE = 1,
    CONFIG_ERR_SDMC_NOT_READY = -1,
    CONFIG_ERR_DIR_CREATE_FAILED = -2,
    CONFIG_ERR_FILE_OPEN_FAILED = -3,
    CONFIG_ERR_FILE_WRITE_FAILED = -4,
    CONFIG_ERR_INVALID_FORMAT = -5,
    CONFIG_ERR_CREDENTIALS_INVALID = -6
} config_status_t;

/**
 * @brief Application configuration container.
 */
typedef struct {
    char     username[CONFIG_USERNAME_MAX_LEN];
    char     web_api_key[CONFIG_API_KEY_MAX_LEN];
    bool     sound_enabled;
    uint32_t cache_limit_mb;
    bool     ssl_verify_peer;
    bool     is_configured;     // True if valid non-empty credentials are set
} app_config_t;

typedef app_config_t Config;

/**
 * @brief Check if configuration is missing, blank, or contains default placeholder credentials.
 * @param cfg Configuration struct to validate.
 * @return True if configuration requires first-run or interactive setup, false if valid.
 */
bool config_needs_setup(const app_config_t* cfg);

/**
 * @brief Initialize configuration subsystem and ensure SD directories exist.
 * @return CONFIG_OK or error code.
 */
config_status_t config_init(void);

/**
 * @brief Load configuration from SD card. If missing, initializes default unconfigured settings.
 * @param[out] config Destination configuration struct.
 * @return CONFIG_OK, CONFIG_CREATED_TEMPLATE, or error code.
 */
config_status_t config_load(app_config_t* config);

/**
 * @brief Save current configuration to SD card atomically.
 * @param config Source configuration struct.
 * @return CONFIG_OK or error code.
 */
config_status_t config_save(const app_config_t* config);

/**
 * @brief Validate username format and character set.
 * @param username String to validate.
 * @return True if valid, false otherwise.
 */
bool config_validate_username(const char* username);

/**
 * @brief Validate RetroAchievements Web API key format.
 * @param api_key String to validate.
 * @return True if valid, false otherwise.
 */
bool config_validate_api_key(const char* api_key);

/**
 * @brief Securely wipe sensitive credentials from memory.
 * @param config Configuration struct to zero.
 */
void config_secure_wipe(app_config_t* config);

/**
 * @brief Get total storage space used by the cache directory in megabytes.
 * @return Total megabytes used on SD card, or negative value if calculating / not ready.
 */
float config_get_cache_size_mb(void);

/**
 * @brief Kick off an asynchronous background calculation of the cache storage usage.
 */
void config_refresh_cache_size_async(void);

/**
 * @brief Set the cached storage size value directly (e.g. 0.0f after cache clear).
 */
void config_set_cache_size_mb(float mb);

/**
 * @brief Recursively wipe all cached assets and recreate empty cache directories.
 * @return True on success, false on failure.
 */
bool config_clear_cache(void);

