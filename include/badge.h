/**
 * @file badge.h
 * @brief RetroAchievements 3DS - Phase 6: Image & Badge Handling
 */

#pragma once

#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>
#include <stddef.h>

#define BADGE_CACHE_DIR      "sdmc:/3ds/retroachievements/cache/badges"
#define GAME_CACHE_DIR       "sdmc:/3ds/retroachievements/cache/games"
#define USER_CACHE_DIR       "sdmc:/3ds/retroachievements/cache/users"
#define BADGE_TEX_SIZE       64
#define BADGE_CACHE_CAPACITY 32

/**
 * @brief Initialize image asset pipeline, directories, LRU texture pool, and downloader thread.
 * @return True on success, false on error.
 */
bool badge_subsystem_init(void);

/**
 * @brief Terminate worker thread and deallocate all linear memory textures.
 */
void badge_subsystem_cleanup(void);

/**
 * @brief Retrieve a GPU-ready C2D_Image for an achievement badge.
 * If cached in linear RAM, returns it immediately.
 * If found on SD card, decodes to linear RAM and returns it.
 * If missing, queues for background download and returns the procedural placeholder.
 *
 * @param badge_name Numeric badge identifier string (e.g. "00123").
 * @param is_unlocked False requests "{badge_name}_lock.png".
 * @return C2D_Image texture object ready for C2D_DrawImageAt.
 */
C2D_Image badge_get_image(const char* badge_name, bool is_unlocked);

/**
 * @brief Retrieve a GPU-ready C2D_Image for a game icon/cover.
 *
 * @param image_icon Game icon identifier or path from API (e.g. "/Images/012345.png" or "012345").
 * @return C2D_Image texture object ready for C2D_DrawImageAt.
 */
C2D_Image game_get_icon(const char* image_icon);

/**
 * @brief Retrieve a GPU-ready C2D_Image for a user avatar.
 *
 * @param user_pic Avatar path from API (e.g. "/UserPic/Username.png").
 * @param username Account username fallback.
 * @return C2D_Image texture object ready for C2D_DrawImageAt.
 */
C2D_Image user_get_avatar(const char* user_pic, const char* username);

/**
 * @brief Get fallback placeholder texture.
 */
C2D_Image badge_get_placeholder(void);

/**
 * @brief Evict all textures from linear RAM and reset the download queue.
 */
void badge_cache_clear_memory(void);
