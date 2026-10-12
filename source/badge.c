/**
 * @file badge.c
 * @brief RetroAchievements 3DS - Phase 6: Image & Badge Handling Implementation
 */

#include "badge.h"
#include "network.h"

#include <3ds/synchronization.h>
#include <3ds/thread.h>
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define WORKER_STACK_SIZE   (32 * 1024)
#define QUEUE_MAX           64

typedef enum {
    ASSET_BADGE = 0,
    ASSET_GAME,
    ASSET_USER
} asset_type_t;

typedef struct {
    char key[64];
    bool is_valid;
    uint32_t last_used_tick;
    C3D_Tex tex;
    Tex3DS_SubTexture subtex;
    C2D_Image image;
} badge_cache_entry_t;

typedef struct {
    asset_type_t type;
    char id[64];
    bool is_unlocked;
} download_task_t;

static badge_cache_entry_t s_cache[BADGE_CACHE_CAPACITY];
static uint32_t            s_access_counter = 0;

static C3D_Tex             s_placeholder_tex;
static Tex3DS_SubTexture   s_placeholder_subtex;
static C2D_Image           s_placeholder_image;
static bool                s_placeholder_valid = false;

static download_task_t     s_queue[QUEUE_MAX];
static int                 s_queue_head = 0;
static int                 s_queue_tail = 0;
static int                 s_queue_count = 0;

static LightLock           s_queue_lock;
static Thread              s_worker_thread = NULL;
static volatile bool       s_worker_running = false;

static inline u32 pica_morton_interleave(u32 x, u32 y)
{
    return (x & 1) |
           ((y & 1) << 1) |
           ((x & 2) << 1) |
           ((y & 2) << 2) |
           ((x & 4) << 2) |
           ((y & 4) << 3);
}

/**
 * @brief Convert linear RGBA8 buffer to PICA200 8x8 tiled texture layout,
 * with proportional resampling to fill dst_w x dst_h upright.
 */
static void swizzle_texture_rgba8(const u32* src, u32 src_w, u32 src_h, u32* dst, u32 dst_w, u32 dst_h)
{
    memset(dst, 0, dst_w * dst_h * sizeof(u32));
    const u32 tiles_row = dst_w / 8;

    for (u32 y = 0; y < dst_h; y++)
    {
        u32 src_y = (y * src_h) / dst_h;
        if (src_y >= src_h) src_y = src_h - 1;

        u32 tile_y = y / 8;
        u32 sub_y  = y % 8;

        for (u32 x = 0; x < dst_w; x++)
        {
            u32 src_x = (x * src_w) / dst_w;
            if (src_x >= src_w) src_x = src_w - 1;

            u32 tile_x = x / 8;
            u32 sub_x  = x % 8;

            u32 tile_idx = (tile_y * tiles_row) + tile_x;
            u32 morton   = pica_morton_interleave(sub_x, sub_y);
            u32 dst_idx  = (tile_idx * 64) + morton;

            u32 px = src[src_y * src_w + src_x];
            u32 r  = px & 0xFF;
            u32 g  = (px >> 8) & 0xFF;
            u32 b  = (px >> 16) & 0xFF;
            u32 a  = (px >> 24) & 0xFF;

            // PICA200 GPU_RGBA8 hardware memory layout: Byte 0=A, Byte 1=B, Byte 2=G, Byte 3=R
            dst[dst_idx] = a | (b << 8) | (g << 16) | (r << 24);
        }
    }
}

static bool decode_png_file(const char* filepath, u32** out_pixels, u32* out_w, u32* out_h)
{
    png_image image;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;

    if (!png_image_begin_read_from_file(&image, filepath))
    {
        return false;
    }

    image.format = PNG_FORMAT_RGBA;
    size_t size = PNG_IMAGE_SIZE(image);
    u32* buffer = (u32*)malloc(size);
    if (buffer == NULL)
    {
        png_image_free(&image);
        return false;
    }

    if (!png_image_finish_read(&image, NULL, buffer, 0, NULL))
    {
        free(buffer);
        png_image_free(&image);
        return false;
    }

    *out_w = image.width;
    *out_h = image.height;
    *out_pixels = buffer;
    png_image_free(&image);
    return true;
}

static void create_placeholder_texture(void)
{
    if (s_placeholder_valid) return;

    if (!C3D_TexInit(&s_placeholder_tex, BADGE_TEX_SIZE, BADGE_TEX_SIZE, GPU_RGBA8))
    {
        return;
    }

    u32 temp_pixels[BADGE_TEX_SIZE * BADGE_TEX_SIZE];
    for (int y = 0; y < BADGE_TEX_SIZE; y++)
    {
        for (int x = 0; x < BADGE_TEX_SIZE; x++)
        {
            bool is_border = (x <= 2 || x >= BADGE_TEX_SIZE - 3 || y <= 2 || y >= BADGE_TEX_SIZE - 3);
            int dx = abs(x - 32);
            int dy = abs(y - 32);
            bool is_center = (dx + dy <= 14);

            if (is_border || is_center)
            {
                temp_pixels[y * BADGE_TEX_SIZE + x] = C2D_Color32(0xD4, 0xAF, 0x37, 0xFF); // Gold
            }
            else
            {
                temp_pixels[y * BADGE_TEX_SIZE + x] = C2D_Color32(0x28, 0x2A, 0x36, 0xFF); // Card bg
            }
        }
    }

    swizzle_texture_rgba8(temp_pixels, BADGE_TEX_SIZE, BADGE_TEX_SIZE, (u32*)s_placeholder_tex.data, BADGE_TEX_SIZE, BADGE_TEX_SIZE);
    C3D_TexFlush(&s_placeholder_tex);

    // top=1.0f > bottom=0.0f ensures Tex3DS_SubTextureRotated returns FALSE (unrotated)
    s_placeholder_subtex.width  = BADGE_TEX_SIZE;
    s_placeholder_subtex.height = BADGE_TEX_SIZE;
    s_placeholder_subtex.left   = 0.0f;
    s_placeholder_subtex.top    = 1.0f;
    s_placeholder_subtex.right  = 1.0f;
    s_placeholder_subtex.bottom = 0.0f;

    s_placeholder_image.tex    = &s_placeholder_tex;
    s_placeholder_image.subtex = &s_placeholder_subtex;
    s_placeholder_valid = true;
}

static void queue_download(asset_type_t type, const char* id, bool is_unlocked)
{
    if (id == NULL || id[0] == '\0')
    {
        return;
    }

    LightLock_Lock(&s_queue_lock);

    // Prevent duplicate entries in queue
    for (int i = 0; i < s_queue_count; i++)
    {
        int idx = (s_queue_head + i) % QUEUE_MAX;
        if (s_queue[idx].type == type && strcmp(s_queue[idx].id, id) == 0 && s_queue[idx].is_unlocked == is_unlocked)
        {
            LightLock_Unlock(&s_queue_lock);
            return;
        }
    }

    if (s_queue_count < QUEUE_MAX)
    {
        s_queue[s_queue_tail].type = type;
        strncpy(s_queue[s_queue_tail].id, id, sizeof(s_queue[s_queue_tail].id) - 1);
        s_queue[s_queue_tail].id[sizeof(s_queue[s_queue_tail].id) - 1] = '\0';
        s_queue[s_queue_tail].is_unlocked = is_unlocked;

        s_queue_tail = (s_queue_tail + 1) % QUEUE_MAX;
        s_queue_count++;
    }

    LightLock_Unlock(&s_queue_lock);
}

static void badge_worker_func(void* arg)
{
    while (s_worker_running)
    {
        download_task_t task;
        bool has_task = false;

        LightLock_Lock(&s_queue_lock);
        if (s_queue_count > 0)
        {
            task = s_queue[s_queue_head];
            s_queue_head = (s_queue_head + 1) % QUEUE_MAX;
            s_queue_count--;
            has_task = true;
        }
        LightLock_Unlock(&s_queue_lock);

        if (!has_task)
        {
            svcSleepThread(50 * 1000 * 1000LL); // 50 ms idle sleep
            continue;
        }

        char local_path[128];
        char url[180];

        if (task.type == ASSET_BADGE)
        {
            snprintf(local_path, sizeof(local_path), "%s/%s%s.png",
                     BADGE_CACHE_DIR, task.id, task.is_unlocked ? "" : "_lock");
            snprintf(url, sizeof(url), "https://media.retroachievements.org/Badge/%s%s.png",
                     task.id, task.is_unlocked ? "" : "_lock");
        }
        else if (task.type == ASSET_GAME)
        {
            snprintf(local_path, sizeof(local_path), "%s/%s.png", GAME_CACHE_DIR, task.id);
            snprintf(url, sizeof(url), "https://media.retroachievements.org/Images/%s.png", task.id);
        }
        else // ASSET_USER
        {
            snprintf(local_path, sizeof(local_path), "%s/%s.png", USER_CACHE_DIR, task.id);
            snprintf(url, sizeof(url), "https://media.retroachievements.org/UserPic/%s.png", task.id);
        }

        if (access(local_path, F_OK) == 0)
        {
            continue; // Already cached on disk
        }

        http_response_t resp;
        net_result_t rc = http_get(url, NULL, 64 * 1024, 8L, 15L, &resp);

        if (rc == NET_OK && resp.body != NULL && resp.body_size > 0)
        {
            char tmp_path[140];
            snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", local_path);

            FILE* fp = fopen(tmp_path, "wb");
            if (fp != NULL)
            {
                fwrite(resp.body, 1, resp.body_size, fp);
                fflush(fp);
                fclose(fp);
                remove(local_path);
                rename(tmp_path, local_path);
            }
        }
        http_response_free(&resp);

        svcSleepThread(20 * 1000 * 1000LL); // 20 ms yield
    }
}

bool badge_subsystem_init(void)
{
    mkdir("sdmc:/3ds/retroachievements/cache", 0777);
    mkdir(BADGE_CACHE_DIR, 0777);
    mkdir(GAME_CACHE_DIR, 0777);
    mkdir(USER_CACHE_DIR, 0777);

    memset(s_cache, 0, sizeof(s_cache));
    s_access_counter = 0;

    create_placeholder_texture();

    LightLock_Init(&s_queue_lock);
    s_queue_head = 0;
    s_queue_tail = 0;
    s_queue_count = 0;

    s_worker_running = true;
    s_worker_thread = threadCreate(
        badge_worker_func,
        NULL,
        WORKER_STACK_SIZE,
        0x31,
        -2,
        false
    );

    return (s_worker_thread != NULL);
}

void badge_subsystem_cleanup(void)
{
    if (s_worker_running)
    {
        s_worker_running = false;
        if (s_worker_thread != NULL)
        {
            threadJoin(s_worker_thread, U64_MAX);
            threadFree(s_worker_thread);
            s_worker_thread = NULL;
        }
    }

    for (int i = 0; i < BADGE_CACHE_CAPACITY; i++)
    {
        if (s_cache[i].is_valid)
        {
            C3D_TexDelete(&s_cache[i].tex);
            s_cache[i].is_valid = false;
        }
    }

    if (s_placeholder_valid)
    {
        C3D_TexDelete(&s_placeholder_tex);
        s_placeholder_valid = false;
    }
}

C2D_Image badge_get_placeholder(void)
{
    return s_placeholder_image;
}

/**
 * @brief Generic asset retrieval from LRU texture pool or SD card cache.
 */
static C2D_Image get_cached_asset(const char* cache_key, const char* local_path, asset_type_t type, const char* id, bool is_unlocked)
{
    // 1. Search in-memory LRU cache
    for (int i = 0; i < BADGE_CACHE_CAPACITY; i++)
    {
        if (s_cache[i].is_valid && strcmp(s_cache[i].key, cache_key) == 0)
        {
            s_cache[i].last_used_tick = ++s_access_counter;
            return s_cache[i].image;
        }
    }

    // 2. Check SD card cache
    if (access(local_path, F_OK) != 0)
    {
        queue_download(type, id, is_unlocked);
        return badge_get_placeholder();
    }

    // 3. Decode PNG from SD
    u32* raw_pixels = NULL;
    u32 img_w = 0, img_h = 0;
    if (!decode_png_file(local_path, &raw_pixels, &img_w, &img_h))
    {
        return badge_get_placeholder();
    }

    // 4. Find LRU slot for eviction
    int target_slot = -1;
    uint32_t oldest_tick = UINT32_MAX;

    for (int i = 0; i < BADGE_CACHE_CAPACITY; i++)
    {
        if (!s_cache[i].is_valid)
        {
            target_slot = i;
            break;
        }
        if (s_cache[i].last_used_tick < oldest_tick)
        {
            oldest_tick = s_cache[i].last_used_tick;
            target_slot = i;
        }
    }

    if (target_slot < 0) target_slot = 0;

    badge_cache_entry_t* entry = &s_cache[target_slot];

    if (entry->is_valid)
    {
        C3D_TexDelete(&entry->tex);
        entry->is_valid = false;
    }

    if (!C3D_TexInit(&entry->tex, BADGE_TEX_SIZE, BADGE_TEX_SIZE, GPU_RGBA8))
    {
        free(raw_pixels);
        return badge_get_placeholder();
    }

    swizzle_texture_rgba8(raw_pixels, img_w, img_h, (u32*)entry->tex.data, BADGE_TEX_SIZE, BADGE_TEX_SIZE);
    free(raw_pixels);

    C3D_TexFlush(&entry->tex);

    // Unrotated orientation: top=1.0f > bottom=0.0f
    entry->subtex.width  = BADGE_TEX_SIZE;
    entry->subtex.height = BADGE_TEX_SIZE;
    entry->subtex.left   = 0.0f;
    entry->subtex.top    = 1.0f;
    entry->subtex.right  = 1.0f;
    entry->subtex.bottom = 0.0f;

    entry->image.tex    = &entry->tex;
    entry->image.subtex = &entry->subtex;

    snprintf(entry->key, sizeof(entry->key), "%s", cache_key);
    entry->is_valid = true;
    entry->last_used_tick = ++s_access_counter;

    return entry->image;
}

C2D_Image badge_get_image(const char* badge_name, bool is_unlocked)
{
    if (badge_name == NULL || badge_name[0] == '\0')
    {
        return badge_get_placeholder();
    }

    char key[64];
    snprintf(key, sizeof(key), "badge_%s%s", badge_name, is_unlocked ? "" : "_lock");

    char local_path[128];
    snprintf(local_path, sizeof(local_path), "%s/%s%s.png",
             BADGE_CACHE_DIR, badge_name, is_unlocked ? "" : "_lock");

    return get_cached_asset(key, local_path, ASSET_BADGE, badge_name, is_unlocked);
}

C2D_Image game_get_icon(const char* image_icon)
{
    if (image_icon == NULL || image_icon[0] == '\0')
    {
        return badge_get_placeholder();
    }

    // Extract clean ID from paths like "/Images/012345.png" or "012345"
    char clean_id[48];
    const char* p = strrchr(image_icon, '/');
    const char* start = (p != NULL) ? p + 1 : image_icon;
    strncpy(clean_id, start, sizeof(clean_id) - 1);
    clean_id[sizeof(clean_id) - 1] = '\0';

    char* dot = strrchr(clean_id, '.');
    if (dot != NULL)
    {
        *dot = '\0'; // Strip .png extension
    }

    if (clean_id[0] == '\0')
    {
        return badge_get_placeholder();
    }

    char key[64];
    snprintf(key, sizeof(key), "game_%s", clean_id);

    char local_path[128];
    snprintf(local_path, sizeof(local_path), "%s/%s.png", GAME_CACHE_DIR, clean_id);

    return get_cached_asset(key, local_path, ASSET_GAME, clean_id, true);
}

C2D_Image user_get_avatar(const char* user_pic, const char* username)
{
    const char* target_user = (username != NULL && username[0] != '\0') ? username : "user";

    // Clean avatar identifier
    char clean_id[48];
    if (user_pic != NULL && user_pic[0] != '\0')
    {
        const char* p = strrchr(user_pic, '/');
        const char* start = (p != NULL) ? p + 1 : user_pic;
        strncpy(clean_id, start, sizeof(clean_id) - 1);
        clean_id[sizeof(clean_id) - 1] = '\0';

        char* dot = strrchr(clean_id, '.');
        if (dot != NULL)
        {
            *dot = '\0';
        }
    }
    else
    {
        strncpy(clean_id, target_user, sizeof(clean_id) - 1);
        clean_id[sizeof(clean_id) - 1] = '\0';
    }

    if (clean_id[0] == '\0')
    {
        return badge_get_placeholder();
    }

    char key[64];
    snprintf(key, sizeof(key), "user_%s", clean_id);

    char local_path[128];
    snprintf(local_path, sizeof(local_path), "%s/%s.png", USER_CACHE_DIR, clean_id);

    return get_cached_asset(key, local_path, ASSET_USER, clean_id, true);
}
