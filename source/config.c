/**
 * @file config.c
 * @brief RetroAchievements 3DS - Phase 3: Secure Credential Storage & Config Implementation
 */

#include "config.h"

#include <3ds.h>
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define LINE_BUF_SIZE 256

/**
 * @brief Volatile memory wiper to prevent compiler dead-code elimination.
 */
static void secure_memzero(void* ptr, size_t len)
{
    if (ptr == NULL || len == 0)
    {
        return;
    }
    volatile unsigned char* p = (volatile unsigned char*)ptr;
    while (len--)
    {
        *p++ = 0;
    }
}

/**
 * @brief In-place whitespace trimmer (removes leading and trailing whitespace).
 */
static char* trim_whitespace(char* str)
{
    if (str == NULL)
    {
        return NULL;
    }

    // Trim leading
    while (isspace((unsigned char)*str))
    {
        str++;
    }

    if (*str == '\0')
    {
        return str;
    }

    // Trim trailing
    char* end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end))
    {
        end--;
    }
    *(end + 1) = '\0';

    return str;
}

/**
 * @brief Recursively ensure directory path exists.
 */
static bool ensure_directory(const char* dir_path)
{
    if (access(dir_path, F_OK) == 0)
    {
        return true;
    }

    // Attempt direct creation
    if (mkdir(dir_path, 0777) == 0)
    {
        return true;
    }

    // If parent doesn't exist, create incrementally
    char temp[128];
    snprintf(temp, sizeof(temp), "%s", dir_path);

    for (char* p = temp + 1; *p; p++)
    {
        if (*p == '/')
        {
            *p = '\0';
            if (strlen(temp) > 0 && access(temp, F_OK) != 0)
            {
                mkdir(temp, 0777);
            }
            *p = '/';
        }
    }

    return (mkdir(dir_path, 0777) == 0 || access(dir_path, F_OK) == 0);
}

config_status_t config_init(void)
{
    // Verify SDMC filesystem is accessible
    if (access("sdmc:/", F_OK) != 0)
    {
        return CONFIG_ERR_SDMC_NOT_READY;
    }

    // Ensure base directories exist
    if (!ensure_directory("sdmc:/3ds") ||
        !ensure_directory(CONFIG_DIR_PATH) ||
        !ensure_directory(CONFIG_CACHE_DIR_PATH))
    {
        return CONFIG_ERR_DIR_CREATE_FAILED;
    }

    return CONFIG_OK;
}

bool config_validate_username(const char* username)
{
    if (username == NULL)
    {
        return false;
    }

    size_t len = strlen(username);
    if (len == 0 || len >= CONFIG_USERNAME_MAX_LEN)
    {
        return false;
    }

    if (strcasecmp(username, "your_username_here") == 0 ||
        strcasecmp(username, "YOUR_USERNAME") == 0 ||
        strcasecmp(username, "YOUR_USERNAME_HERE") == 0 ||
        strcasecmp(username, "username") == 0)
    {
        return false;
    }

    for (size_t i = 0; i < len; i++)
    {
        char c = username[i];
        if (!isalnum((unsigned char)c) && c != '_' && c != '-' && c != '.')
        {
            return false;
        }
    }

    return true;
}

bool config_validate_api_key(const char* api_key)
{
    if (api_key == NULL)
    {
        return false;
    }

    size_t len = strlen(api_key);
    if (len == 0 || len >= CONFIG_API_KEY_MAX_LEN)
    {
        return false;
    }

    if (strcasecmp(api_key, "your_web_api_key_here") == 0 ||
        strcasecmp(api_key, "YOUR_WEB_API_KEY") == 0 ||
        strcasecmp(api_key, "YOUR_WEB_API_KEY_HERE") == 0 ||
        strcasecmp(api_key, "YOUR_API_KEY") == 0 ||
        strcasecmp(api_key, "web_api_key") == 0)
    {
        return false;
    }

    for (size_t i = 0; i < len; i++)
    {
        char c = api_key[i];
        if (isspace((unsigned char)c) || !isprint((unsigned char)c))
        {
            return false;
        }
    }

    return true;
}

bool config_needs_setup(const app_config_t* cfg)
{
    if (cfg == NULL)
    {
        return true;
    }

    // 1. config.ini does not exist on SD card
    if (access(CONFIG_FILE_PATH, F_OK) != 0)
    {
        return true;
    }

    // 2. username is empty or matches placeholder text or invalid characters
    if (!config_validate_username(cfg->username))
    {
        return true;
    }

    // 3. web_api_key is empty or invalid in format/length (not 32 hex chars, or placeholder)
    if (!config_validate_api_key(cfg->web_api_key))
    {
        return true;
    }

    return false;
}

void config_secure_wipe(app_config_t* config)
{
    if (config == NULL)
    {
        return;
    }

    secure_memzero(config->username, sizeof(config->username));
    secure_memzero(config->web_api_key, sizeof(config->web_api_key));
    config->is_configured = false;
}

config_status_t config_load(app_config_t* config)
{
    if (config == NULL)
    {
        return CONFIG_ERR_INVALID_FORMAT;
    }

    // Set safe default state
    memset(config, 0, sizeof(app_config_t));
    config->sound_enabled = true;
    config->cache_limit_mb = 100;
    config->ssl_verify_peer = true;
    config->is_configured = false;

    // Ensure directory infrastructure is initialized
    config_status_t init_rc = config_init();
    if (init_rc != CONFIG_OK)
    {
        return init_rc;
    }

    // Clean up any stale temporary config files from interrupted writes
    remove("sdmc:/3ds/retroachievements/config.ini.tmp");

    // Check if configuration file exists on SD card
    if (access(CONFIG_FILE_PATH, F_OK) != 0)
    {
        return CONFIG_CREATED_TEMPLATE;
    }

    FILE* fp = fopen(CONFIG_FILE_PATH, "r");
    if (fp == NULL)
    {
        return CONFIG_ERR_FILE_OPEN_FAILED;
    }

    char line[LINE_BUF_SIZE];
    while (fgets(line, sizeof(line), fp) != NULL)
    {
        char* trimmed = trim_whitespace(line);

        // Skip blank lines and comments
        if (trimmed[0] == '\0' || trimmed[0] == '#' || trimmed[0] == ';')
        {
            continue;
        }

        // Section header
        if (trimmed[0] == '[' && trimmed[strlen(trimmed) - 1] == ']')
        {
            continue;
        }

        // Key = Value parsing
        char* equals = strchr(trimmed, '=');
        if (equals == NULL)
        {
            continue;
        }

        *equals = '\0';
        char* key = trim_whitespace(trimmed);
        char* val = trim_whitespace(equals + 1);

        if (strcasecmp(key, "username") == 0)
        {
            strncpy(config->username, val, CONFIG_USERNAME_MAX_LEN - 1);
            config->username[CONFIG_USERNAME_MAX_LEN - 1] = '\0';
        }
        else if (strcasecmp(key, "web_api_key") == 0)
        {
            strncpy(config->web_api_key, val, CONFIG_API_KEY_MAX_LEN - 1);
            config->web_api_key[CONFIG_API_KEY_MAX_LEN - 1] = '\0';
        }
        else if (strcasecmp(key, "sound_enabled") == 0)
        {
            config->sound_enabled = (strcmp(val, "1") == 0 || strcasecmp(val, "true") == 0);
        }
        else if (strcasecmp(key, "cache_limit_mb") == 0)
        {
            config->cache_limit_mb = (uint32_t)strtoul(val, NULL, 10);
        }
        else if (strcasecmp(key, "ssl_verify_peer") == 0)
        {
            config->ssl_verify_peer = (strcmp(val, "1") == 0 || strcasecmp(val, "true") == 0);
        }
    }

    // Always close file handle
    fclose(fp);

    // Validate credentials
    config->is_configured = config_validate_username(config->username) &&
                            config_validate_api_key(config->web_api_key);

    return CONFIG_OK;
}

config_status_t config_save(const app_config_t* config)
{
    if (config == NULL)
    {
        return CONFIG_ERR_INVALID_FORMAT;
    }

    config_status_t init_rc = config_init();
    if (init_rc != CONFIG_OK)
    {
        return init_rc;
    }

    // Atomic write to temporary file first
    const char* tmp_path = "sdmc:/3ds/retroachievements/config.ini.tmp";
    FILE* fp = fopen(tmp_path, "w");
    if (fp == NULL)
    {
        return CONFIG_ERR_FILE_WRITE_FAILED;
    }

    fputs("# =========================================================\n", fp);
    fputs("# RetroAchievements 3DS Configuration\n", fp);
    fputs("# =========================================================\n\n", fp);
    fputs("[Credentials]\n", fp);
    fprintf(fp, "username = %s\n", config->username);
    fprintf(fp, "web_api_key = %s\n\n", config->web_api_key);
    fputs("[Settings]\n", fp);
    fprintf(fp, "sound_enabled = %d\n", config->sound_enabled ? 1 : 0);
    fprintf(fp, "cache_limit_mb = %lu\n", (unsigned long)config->cache_limit_mb);
    fprintf(fp, "ssl_verify_peer = %d\n", config->ssl_verify_peer ? 1 : 0);

    fflush(fp);
    fclose(fp);

    // Replace canonical config with temporary file
    remove(CONFIG_FILE_PATH);
    if (rename(tmp_path, CONFIG_FILE_PATH) != 0)
    {
        return CONFIG_ERR_FILE_WRITE_FAILED;
    }

    return CONFIG_OK;
}

static uint64_t calculate_dir_bytes_recursive(const char* dir_path, int depth)
{
    if (dir_path == NULL || depth > 5)
    {
        return 0; // Prevent runaway recursion
    }

    DIR* d = opendir(dir_path);
    if (d == NULL)
    {
        return 0;
    }

    uint64_t total = 0;
    struct dirent* ent;
    while ((ent = readdir(d)) != NULL)
    {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
        {
            continue;
        }

        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir_path, ent->d_name);

        struct stat st;
        if (stat(path, &st) == 0)
        {
            if (S_ISDIR(st.st_mode))
            {
                total += calculate_dir_bytes_recursive(path, depth + 1);
            }
            else if (S_ISREG(st.st_mode))
            {
                total += (uint64_t)st.st_size;
            }
        }
    }
    closedir(d);
    return total;
}

static Thread s_calc_thread = NULL;
static volatile bool s_calc_running = false;
static float s_cached_mb = -1.0f;

static void cache_calc_worker(void* arg)
{
    (void)arg;
    uint64_t bytes = calculate_dir_bytes_recursive(CONFIG_CACHE_DIR_PATH, 0);
    s_cached_mb = (float)bytes / (1024.0f * 1024.0f);
    s_calc_running = false;
}

void config_refresh_cache_size_async(void)
{
    if (s_calc_running)
    {
        return;
    }
    s_calc_running = true;
    s_calc_thread = threadCreate(cache_calc_worker, NULL, 16 * 1024, 0x32, -2, true);
    if (s_calc_thread == NULL)
    {
        s_calc_running = false;
    }
}

float config_get_cache_size_mb(void)
{
    return s_cached_mb;
}

void config_set_cache_size_mb(float mb)
{
    s_cached_mb = mb;
}

static void wipe_dir_contents_recursive(const char* dir_path, bool delete_self, int depth)
{
    if (dir_path == NULL || depth > 5)
    {
        return;
    }

    // Strict sandboxing check: path MUST be within CONFIG_CACHE_DIR_PATH
    const char* prefix = CONFIG_CACHE_DIR_PATH;
    if (strncmp(dir_path, prefix, strlen(prefix)) != 0)
    {
        return; // Prevent directory traversal escaping the cache sandbox
    }

    DIR* d = opendir(dir_path);
    if (d == NULL)
    {
        return;
    }

    struct dirent* ent;
    while ((ent = readdir(d)) != NULL)
    {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
        {
            continue;
        }

        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir_path, ent->d_name);

        struct stat st;
        if (stat(path, &st) == 0)
        {
            if (S_ISDIR(st.st_mode))
            {
                wipe_dir_contents_recursive(path, true, depth + 1);
            }
            else
            {
                remove(path);
            }
        }
    }
    closedir(d);

    if (delete_self)
    {
        rmdir(dir_path);
    }
}

bool config_clear_cache(void)
{
    // Wipe all files and subdirectories inside cache
    wipe_dir_contents_recursive(CONFIG_CACHE_DIR_PATH, false, 0);

    // Recreate standard empty subdirectories so subsequent downloads succeed
    mkdir(CONFIG_CACHE_DIR_PATH, 0777);
    mkdir("sdmc:/3ds/retroachievements/cache/badges", 0777);
    mkdir("sdmc:/3ds/retroachievements/cache/games", 0777);
    mkdir("sdmc:/3ds/retroachievements/cache/users", 0777);
    mkdir("sdmc:/3ds/retroachievements/cache/gamelists", 0777);

    // Immediately reflect 0 MB usage
    s_cached_mb = 0.0f;

    return true;
}

