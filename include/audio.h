/**
 * @file audio.h
 * @brief RetroAchievements 3DS - UI Sound Effects & Audio Subsystem
 */

#pragma once

#include <3ds/types.h>
#include <stdbool.h>

/**
 * @brief UI Sound effect types
 */
typedef enum {
    SFX_NAV = 0,     // D-Pad navigation, list cursor movement, tab tap
    SFX_SELECT,      // Button tap, item selection, action confirm
    SFX_BACK,        // Back navigation, cancel, modal dismiss
    SFX_TOGGLE,      // Setting switch / toggle
    SFX_SUCCESS,     // Connect success, sync complete, cache cleared
    SFX_ERROR,       // Input error, auth failure, action disabled
    SFX_MAX
} sfx_type_t;

/**
 * @brief Initialize the NDSP audio engine and synthesize UI sound effects into linear memory.
 * @return True on success, false if DSP/NDSP unavailable.
 */
bool audio_init(void);

/**
 * @brief Teardown NDSP audio engine and free sound buffers.
 */
void audio_cleanup(void);

/**
 * @brief Play a UI sound effect if audio is initialized and enabled.
 * @param sfx The sound effect to play.
 */
void audio_play_sfx(sfx_type_t sfx);

/**
 * @brief Enable or disable UI sound effects.
 * @param enabled True to enable, false to mute.
 */
void audio_set_enabled(bool enabled);

/**
 * @brief Check if UI sound effects are currently enabled.
 * @return True if sound is enabled.
 */
bool audio_is_enabled(void);

