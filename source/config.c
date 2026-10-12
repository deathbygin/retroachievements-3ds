/**
 * @file config.c
 * @brief RetroAchievements 3DS - Phase 3: Secure Credential Storage & Config Implementation
 */

#include "config.h"

#include <ctype.h>
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

    if (strcmp(username, "your_username_here") == 0)
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
    // RetroAchievements Web API keys are 32 hex/alphanumeric characters
    if (len < 16 || len >= CONFIG_API_KEY_MAX_LEN)
    {
        return false;
    }

    if (strcmp(api_key, "your_web_api_key_here") == 0)
    {
        return false;
    }

    for (size_t i = 0; i < len; i++)
    {
        char c = api_key[i];
        if (!isalnum((unsigned char)c))
        {
            return false;
        }
    }

    return true;
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

/**
 * @brief Write default template configuration to file with user documentation.
 */
static config_status_t write_template_file(const char* path)
{
    FILE* fp = fopen(path, "w");
    if (fp == NULL)
    {
        return CONFIG_ERR_FILE_WRITE_FAILED;
    }

    fputs("# =========================================================\n", fp);
    fputs("# RetroAchievements 3DS Configuration\n", fp);
    fputs("# =========================================================\n", fp);
    fputs("# To obtain your Web API Key:\n", fp);
    fputs("# 1. Log in to https://retroachievements.org\n", fp);
    fputs("# 2. Navigate to your Settings page\n", fp);
    fputs("# 3. Click 'Keys' on the left navigation\n", fp);
    fputs("# 4. Copy your 'Web API Key' below (NOT your password!)\n\n", fp);
    fputs("[Credentials]\n", fp);
    fputs("username = your_username_here\n", fp);
    fputs("web_api_key = your_web_api_key_here\n\n", fp);
    fputs("[Settings]\n", fp);
    fputs("sound_enabled = 1\n", fp);
    fputs("cache_limit_mb = 100\n", fp);
    fputs("ssl_verify_peer = 1\n", fp);

    fflush(fp);
    fclose(fp);

    return CONFIG_OK;
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

    // Check if configuration file exists
    if (access(CONFIG_FILE_PATH, F_OK) != 0)
    {
        config_status_t tmpl_rc = write_template_file(CONFIG_FILE_PATH);
        if (tmpl_rc != CONFIG_OK)
        {
            return tmpl_rc;
        }
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

