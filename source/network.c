/**
 * @file network.c
 * @brief RetroAchievements 3DS - Phase 2: Networking & HTTPS Subsystem Implementation
 */

#include "network.h"

#include <3ds.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool  s_ac_initialized   = false;
static bool  s_soc_initialized  = false;
static bool  s_curl_initialized = false;
static void* s_soc_buffer       = NULL;

typedef struct {
    char*  data;
    size_t size;
    size_t capacity;
    size_t max_limit;
    bool   overflow;
} write_buffer_t;

static size_t write_memory_callback(void* contents, size_t size, size_t nmemb, void* userp)
{
    const size_t realsize = size * nmemb;
    write_buffer_t* mem = (write_buffer_t*)userp;

    if (mem->size + realsize + 1 > mem->max_limit)
    {
        mem->overflow = true;
        return 0; // Triggers CURLE_WRITE_ERROR to abort download without heap exhaustion
    }

    if (mem->size + realsize + 1 > mem->capacity)
    {
        size_t new_cap = (mem->capacity == 0) ? 4096 : (mem->capacity * 2);
        while (new_cap < mem->size + realsize + 1)
        {
            new_cap *= 2;
        }
        if (new_cap > mem->max_limit + 1)
        {
            new_cap = mem->max_limit + 1;
        }

        char* new_ptr = (char*)realloc(mem->data, new_cap);
        if (new_ptr == NULL)
        {
            return 0; // Out of memory abort
        }
        mem->data = new_ptr;
        mem->capacity = new_cap;
    }

    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = '\0';

    return realsize;
}

net_result_t network_init(void)
{
    Result rc;

    // 1. Initialize AC (Access Point Connection) service for status monitoring
    rc = acInit();
    if (R_SUCCEEDED(rc))
    {
        s_ac_initialized = true;
    }

    // 2. Allocate 4KB (0x1000) page-aligned buffer for 3DS SOC kernel service
    if (s_soc_buffer == NULL)
    {
        s_soc_buffer = memalign(NET_SOC_ALIGN, NET_SOC_BUFFERSIZE);
        if (s_soc_buffer == NULL)
        {
            return NET_ERR_OUT_OF_MEMORY;
        }
    }

    // 3. Register SOC service with kernel
    rc = socInit((u32*)s_soc_buffer, NET_SOC_BUFFERSIZE);
    if (R_FAILED(rc))
    {
        free(s_soc_buffer);
        s_soc_buffer = NULL;
        return NET_ERR_NOT_INITIALIZED;
    }
    s_soc_initialized = true;

    // 4. Initialize libcurl global state
    CURLcode curl_rc = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (curl_rc != CURLE_OK)
    {
        socExit();
        s_soc_initialized = false;
        free(s_soc_buffer);
        s_soc_buffer = NULL;
        return NET_ERR_CURL_INIT;
    }
    s_curl_initialized = true;

    return NET_OK;
}

void network_cleanup(void)
{
    // Exact reverse lifecycle ordering:
    // 1. Global curl cleanup
    if (s_curl_initialized)
    {
        curl_global_cleanup();
        s_curl_initialized = false;
    }

    // 2. SOC kernel service exit (MUST precede freeing the buffer)
    if (s_soc_initialized)
    {
        socExit();
        s_soc_initialized = false;
    }

    // 3. Free page-aligned SOC buffer once memory permissions revert
    if (s_soc_buffer != NULL)
    {
        free(s_soc_buffer);
        s_soc_buffer = NULL;
    }

    // 4. AC service shutdown
    if (s_ac_initialized)
    {
        acExit();
        s_ac_initialized = false;
    }
}

net_wifi_status_t network_get_status(u8* out_strength)
{
    u8 strength = osGetWifiStrength();
    if (out_strength != NULL)
    {
        *out_strength = strength;
    }

    if (strength == 0)
    {
        return NET_STATUS_DISABLED;
    }

    if (s_ac_initialized)
    {
        u32 ac_status = 1;
        Result rc = ACU_GetStatus(&ac_status);
        if (R_SUCCEEDED(rc))
        {
            if (ac_status == 3)
            {
                return NET_STATUS_CONNECTED;
            }
            else if (ac_status == 2)
            {
                return NET_STATUS_CONNECTING;
            }
            else
            {
                return NET_STATUS_DISCONNECTED;
            }
        }
    }

    // Fallback based directly on hardware signal strength
    return (strength > 0) ? NET_STATUS_CONNECTED : NET_STATUS_DISABLED;
}

bool network_is_ready(void)
{
    return s_soc_initialized && s_curl_initialized;
}

void http_response_free(http_response_t* resp)
{
    if (resp == NULL)
    {
        return;
    }
    if (resp->body != NULL)
    {
        free(resp->body);
        resp->body = NULL;
    }
    resp->body_size = 0;
    resp->http_status = 0;
    resp->curl_code = CURLE_OK;
    resp->result = NET_OK;
    resp->error_desc[0] = '\0';
}

net_result_t http_get(
    const char* url,
    const char* ca_bundle_path,
    size_t max_body_bytes,
    long connect_timeout_sec,
    long total_timeout_sec,
    http_response_t* out_response
)
{
    if (out_response == NULL)
    {
        return NET_ERR_INVALID_PARAM;
    }
    memset(out_response, 0, sizeof(http_response_t));
    out_response->result = NET_ERR_GENERIC;

    if (!network_is_ready())
    {
        out_response->result = NET_ERR_NOT_INITIALIZED;
        snprintf(out_response->error_desc, sizeof(out_response->error_desc),
                 "Network subsystem is not initialized");
        return NET_ERR_NOT_INITIALIZED;
    }

    if (url == NULL || url[0] == '\0')
    {
        out_response->result = NET_ERR_INVALID_PARAM;
        snprintf(out_response->error_desc, sizeof(out_response->error_desc),
                 "Target URL is null or empty");
        return NET_ERR_INVALID_PARAM;
    }

    // Pre-flight check: Non-blocking test for Wi-Fi availability
    u8 strength = 0;
    net_wifi_status_t wifi_st = network_get_status(&strength);
    if (wifi_st != NET_STATUS_CONNECTED || strength == 0)
    {
        out_response->result = NET_ERR_NO_WIFI;
        snprintf(out_response->error_desc, sizeof(out_response->error_desc),
                 "Wi-Fi disconnected or signal 0 (status: %d)", wifi_st);
        return NET_ERR_NO_WIFI;
    }

    CURL* curl = curl_easy_init();
    if (curl == NULL)
    {
        out_response->result = NET_ERR_CURL_INIT;
        snprintf(out_response->error_desc, sizeof(out_response->error_desc),
                 "Failed to allocate curl easy handle");
        return NET_ERR_CURL_INIT;
    }

    char curl_errbuf[CURL_ERROR_SIZE];
    curl_errbuf[0] = '\0';

    write_buffer_t mem_buf;
    mem_buf.data = (char*)malloc(4096);
    if (mem_buf.data == NULL)
    {
        curl_easy_cleanup(curl);
        out_response->result = NET_ERR_OUT_OF_MEMORY;
        snprintf(out_response->error_desc, sizeof(out_response->error_desc),
                 "Failed initial buffer allocation (4KB)");
        return NET_ERR_OUT_OF_MEMORY;
    }
    mem_buf.data[0] = '\0';
    mem_buf.size = 0;
    mem_buf.capacity = 4096;
    mem_buf.max_limit = (max_body_bytes > 0) ? max_body_bytes : (512 * 1024);
    mem_buf.overflow = false;

    // Basic options
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&mem_buf);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "RetroAchievements-3DS/0.1.0 (Nintendo 3DS; devkitARM)");
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_errbuf);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);

    // Strict timeouts to avoid hanging main thread
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (connect_timeout_sec > 0) ? connect_timeout_sec : 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (total_timeout_sec > 0) ? total_timeout_sec : 20L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 500L); // 500 bytes/sec
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 5L);    // for 5 seconds -> abort

    // SSL / TLS Root Certificate Configuration
    if (ca_bundle_path != NULL && access(ca_bundle_path, R_OK) == 0)
    {
        curl_easy_setopt(curl, CURLOPT_CAINFO, ca_bundle_path);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    }
    else
    {
        // Explicitly unverified when ca_bundle_path is NULL or disabled
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    // Execute transfer
    CURLcode res = curl_easy_perform(curl);
    out_response->curl_code = res;

    // Query HTTP status code
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &out_response->http_status);

    if (res == CURLE_OK)
    {
        out_response->body = mem_buf.data;
        out_response->body_size = mem_buf.size;

        if (out_response->http_status >= 400)
        {
            out_response->result = NET_ERR_HTTP_ERROR;
            snprintf(out_response->error_desc, sizeof(out_response->error_desc),
                     "HTTP Error code %ld", out_response->http_status);
        }
        else
        {
            out_response->result = NET_OK;
            snprintf(out_response->error_desc, sizeof(out_response->error_desc), "Success");
        }
    }
    else
    {
        // Free payload buffer on failure
        free(mem_buf.data);
        out_response->body = NULL;
        out_response->body_size = 0;

        if (mem_buf.overflow)
        {
            out_response->result = NET_ERR_BUFFER_OVERFLOW;
            snprintf(out_response->error_desc, sizeof(out_response->error_desc),
                     "Payload exceeded limit (%zu bytes)", mem_buf.max_limit);
        }
        else if (res == CURLE_OPERATION_TIMEDOUT)
        {
            out_response->result = NET_ERR_TIMEOUT;
            snprintf(out_response->error_desc, sizeof(out_response->error_desc),
                     "Connection or transfer timed out");
        }
        else if (res == CURLE_COULDNT_RESOLVE_HOST)
        {
            out_response->result = NET_ERR_DNS_FAILED;
            snprintf(out_response->error_desc, sizeof(out_response->error_desc),
                     "Could not resolve host / DNS failure");
        }
        else if (res == CURLE_SSL_CONNECT_ERROR || res == CURLE_PEER_FAILED_VERIFICATION)
        {
            out_response->result = NET_ERR_TLS_FAILED;
            snprintf(out_response->error_desc, sizeof(out_response->error_desc),
                     "TLS/SSL handshake or certificate error: %s",
                     curl_errbuf[0] ? curl_errbuf : curl_easy_strerror(res));
        }
        else
        {
            out_response->result = NET_ERR_GENERIC;
            snprintf(out_response->error_desc, sizeof(out_response->error_desc),
                     "cURL error (%d): %s", res,
                     curl_errbuf[0] ? curl_errbuf : curl_easy_strerror(res));
        }
    }

    curl_easy_cleanup(curl);
    return out_response->result;
}

