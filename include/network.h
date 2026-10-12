/**
 * @file network.h
 * @brief RetroAchievements 3DS - Phase 2: Networking & HTTPS Subsystem
 */

#pragma once

#include <3ds/types.h>
#include <curl/curl.h>
#include <stdbool.h>
#include <stddef.h>

#define NET_SOC_ALIGN       0x1000
#define NET_SOC_BUFFERSIZE  0x100000 // 1 MiB page-aligned buffer

/**
 * @brief Wi-Fi status enumeration.
 */
typedef enum {
    NET_STATUS_DISABLED = 0,     // Wireless switch off or hardware disabled
    NET_STATUS_DISCONNECTED = 1, // Wi-Fi turned on, but not connected to an AP
    NET_STATUS_CONNECTING = 2,   // Handshaking / obtaining DHCP lease
    NET_STATUS_CONNECTED = 3     // Connected to AP and internet ready
} net_wifi_status_t;

/**
 * @brief High-level structured network result codes.
 */
typedef enum {
    NET_OK = 0,
    NET_ERR_NOT_INITIALIZED = -1,
    NET_ERR_NO_WIFI = -2,
    NET_ERR_INVALID_PARAM = -3,
    NET_ERR_OUT_OF_MEMORY = -4,
    NET_ERR_CURL_INIT = -5,
    NET_ERR_TIMEOUT = -6,
    NET_ERR_DNS_FAILED = -7,
    NET_ERR_TLS_FAILED = -8,
    NET_ERR_HTTP_ERROR = -9,
    NET_ERR_BUFFER_OVERFLOW = -10,
    NET_ERR_GENERIC = -11
} net_result_t;

/**
 * @brief Structured HTTP response container.
 */
typedef struct {
    net_result_t result;
    long http_status;           // HTTP Status Code (e.g., 200, 404, 500)
    CURLcode curl_code;         // Low-level libcurl result code
    char* body;                 // Dynamic null-terminated buffer for payload
    size_t body_size;           // Length in bytes of payload (excluding null terminator)
    char error_desc[128];       // Human-readable error diagnostic
} http_response_t;

/**
 * @brief Initialize AC service, allocate 4KB-aligned SOC buffer, init SOC, and curl global state.
 * @return NET_OK on success, or structured error code.
 */
net_result_t network_init(void);

/**
 * @brief Teardown curl global state, close SOC service, free aligned buffer, and close AC service.
 */
void network_cleanup(void);

/**
 * @brief Query current Wi-Fi status and signal level.
 * @param[out] out_strength Signal strength 0 (none/disconnected) to 3 (strong). Can be NULL.
 * @return Current connection state enum.
 */
net_wifi_status_t network_get_status(u8* out_strength);

/**
 * @brief Check if network is initialized and SOC is ready.
 */
bool network_is_ready(void);

/**
 * @brief Release memory allocated within an http_response_t structure.
 * @param resp Pointer to response structure to clear.
 */
void http_response_free(http_response_t* resp);

/**
 * @brief Perform a synchronous HTTPS GET request with strict timeouts and bounded memory allocation.
 *
 * @param url Full target URL.
 * @param ca_bundle_path Optional path to CA bundle (e.g. "romfs:/certs/cacert.pem"), or NULL for auto/skip.
 * @param max_body_bytes Upper bound on response payload to prevent heap exhaustion (e.g. 512KB).
 * @param connect_timeout_sec Timeout in seconds for TCP connection & TLS handshake.
 * @param total_timeout_sec Total overall transfer timeout in seconds.
 * @param[out] out_response Pointer to destination response structure.
 * @return NET_OK on success, or structured error code.
 */
net_result_t http_get(
    const char* url,
    const char* ca_bundle_path,
    size_t max_body_bytes,
    long connect_timeout_sec,
    long total_timeout_sec,
    http_response_t* out_response
);

