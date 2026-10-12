/**
 * @file audio.c
 * @brief RetroAchievements 3DS - UI Sound Effects & Audio Subsystem Implementation
 */

#include "audio.h"

#include <3ds.h>
#include <3ds/ndsp/ndsp.h>
#include <3ds/ndsp/channel.h>
#include <3ds/services/dsp.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define AUDIO_SAMPLE_RATE    22050.0f
#define AUDIO_SAMPLE_RATE_I  22050
#define AUDIO_CHANNEL        0

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    s16*        samples;
    u32         nsamples;
    ndspWaveBuf wave_buf;
} sound_effect_t;

static bool           s_ndsp_initialized = false;
static bool           s_sound_enabled = true;
static sound_effect_t s_sfx[SFX_MAX];

static void synthesize_nav(sound_effect_t* sfx)
{
    // Crisp, subtle navigation click (~28ms)
    u32 nsamples = (u32)(0.028f * AUDIO_SAMPLE_RATE_I);
    s16* buf = (s16*)linearAlloc(nsamples * sizeof(s16));
    if (buf == NULL) return;

    float freq = 1100.0f;
    for (u32 i = 0; i < nsamples; i++)
    {
        float t = (float)i / AUDIO_SAMPLE_RATE;
        float env = expf(-t / 0.007f);
        if (i < 30) env *= ((float)i / 30.0f); // 1.3ms attack
        float val = sinf(2.0f * (float)M_PI * freq * t) * 14000.0f * env;
        buf[i] = (s16)val;
    }

    DSP_FlushDataCache(buf, nsamples * sizeof(s16));
    sfx->samples = buf;
    sfx->nsamples = nsamples;
    memset(&sfx->wave_buf, 0, sizeof(ndspWaveBuf));
    sfx->wave_buf.data_pcm16 = buf;
    sfx->wave_buf.nsamples = nsamples;
    sfx->wave_buf.status = NDSP_WBUF_FREE;
    sfx->wave_buf.looping = false;
}

static void synthesize_select(sound_effect_t* sfx)
{
    // Bright responsive button tap (~65ms at 880Hz)
    u32 nsamples = (u32)(0.065f * AUDIO_SAMPLE_RATE_I);
    s16* buf = (s16*)linearAlloc(nsamples * sizeof(s16));
    if (buf == NULL) return;

    float freq = 880.0f;
    for (u32 i = 0; i < nsamples; i++)
    {
        float t = (float)i / AUDIO_SAMPLE_RATE;
        float progress = (float)i / (float)nsamples;
        float env = 1.0f - progress;
        if (i < 40) env *= ((float)i / 40.0f); // 1.8ms attack
        float val = sinf(2.0f * (float)M_PI * freq * t) * 16000.0f * env;
        buf[i] = (s16)val;
    }

    DSP_FlushDataCache(buf, nsamples * sizeof(s16));
    sfx->samples = buf;
    sfx->nsamples = nsamples;
    memset(&sfx->wave_buf, 0, sizeof(ndspWaveBuf));
    sfx->wave_buf.data_pcm16 = buf;
    sfx->wave_buf.nsamples = nsamples;
    sfx->wave_buf.status = NDSP_WBUF_FREE;
    sfx->wave_buf.looping = false;
}

static void synthesize_back(sound_effect_t* sfx)
{
    // Gentle downward pitch drop (~75ms, 660Hz down to 380Hz)
    u32 nsamples = (u32)(0.075f * AUDIO_SAMPLE_RATE_I);
    s16* buf = (s16*)linearAlloc(nsamples * sizeof(s16));
    if (buf == NULL) return;

    float phase = 0.0f;
    for (u32 i = 0; i < nsamples; i++)
    {
        float progress = (float)i / (float)nsamples;
        float freq = 660.0f - (280.0f * progress);
        phase += 2.0f * (float)M_PI * freq / AUDIO_SAMPLE_RATE;
        float env = 1.0f - progress;
        if (i < 40) env *= ((float)i / 40.0f);
        float val = sinf(phase) * 15000.0f * env;
        buf[i] = (s16)val;
    }

    DSP_FlushDataCache(buf, nsamples * sizeof(s16));
    sfx->samples = buf;
    sfx->nsamples = nsamples;
    memset(&sfx->wave_buf, 0, sizeof(ndspWaveBuf));
    sfx->wave_buf.data_pcm16 = buf;
    sfx->wave_buf.nsamples = nsamples;
    sfx->wave_buf.status = NDSP_WBUF_FREE;
    sfx->wave_buf.looping = false;
}

static void synthesize_toggle(sound_effect_t* sfx)
{
    // Two-tone rising chirp (~90ms, 523Hz C5 -> 784Hz G5)
    u32 nsamples = (u32)(0.090f * AUDIO_SAMPLE_RATE_I);
    s16* buf = (s16*)linearAlloc(nsamples * sizeof(s16));
    if (buf == NULL) return;

    u32 half = nsamples / 2;
    for (u32 i = 0; i < nsamples; i++)
    {
        float t = (float)i / AUDIO_SAMPLE_RATE;
        float freq = (i < half) ? 523.25f : 783.99f;
        u32 note_i = (i < half) ? i : (i - half);
        u32 note_len = (i < half) ? half : (nsamples - half);
        float progress = (float)note_i / (float)note_len;
        float env = 1.0f - (progress * 0.7f);
        if (note_i < 30) env *= ((float)note_i / 30.0f);
        float val = sinf(2.0f * (float)M_PI * freq * t) * 15000.0f * env;
        buf[i] = (s16)val;
    }

    DSP_FlushDataCache(buf, nsamples * sizeof(s16));
    sfx->samples = buf;
    sfx->nsamples = nsamples;
    memset(&sfx->wave_buf, 0, sizeof(ndspWaveBuf));
    sfx->wave_buf.data_pcm16 = buf;
    sfx->wave_buf.nsamples = nsamples;
    sfx->wave_buf.status = NDSP_WBUF_FREE;
    sfx->wave_buf.looping = false;
}

static void synthesize_success(sound_effect_t* sfx)
{
    // Rewarding major triad arpeggio (~180ms: D5 -> F#5 -> A5)
    u32 nsamples = (u32)(0.180f * AUDIO_SAMPLE_RATE_I);
    s16* buf = (s16*)linearAlloc(nsamples * sizeof(s16));
    if (buf == NULL) return;

    u32 p1 = (u32)(0.050f * AUDIO_SAMPLE_RATE_I);
    u32 p2 = (u32)(0.100f * AUDIO_SAMPLE_RATE_I);

    for (u32 i = 0; i < nsamples; i++)
    {
        float t = (float)i / AUDIO_SAMPLE_RATE;
        float freq;
        u32 note_i;
        u32 note_len;

        if (i < p1)
        {
            freq = 587.33f; // D5
            note_i = i;
            note_len = p1;
        }
        else if (i < p2)
        {
            freq = 739.99f; // F#5
            note_i = i - p1;
            note_len = p2 - p1;
        }
        else
        {
            freq = 880.00f; // A5
            note_i = i - p2;
            note_len = nsamples - p2;
        }

        float progress = (float)note_i / (float)note_len;
        float env = 1.0f - (progress * 0.85f);
        if (note_i < 30) env *= ((float)note_i / 30.0f);
        float val = sinf(2.0f * (float)M_PI * freq * t) * 16000.0f * env;
        buf[i] = (s16)val;
    }

    DSP_FlushDataCache(buf, nsamples * sizeof(s16));
    sfx->samples = buf;
    sfx->nsamples = nsamples;
    memset(&sfx->wave_buf, 0, sizeof(ndspWaveBuf));
    sfx->wave_buf.data_pcm16 = buf;
    sfx->wave_buf.nsamples = nsamples;
    sfx->wave_buf.status = NDSP_WBUF_FREE;
    sfx->wave_buf.looping = false;
}

static void synthesize_error(sound_effect_t* sfx)
{
    // Double buzz warning tone (~160ms)
    u32 nsamples = (u32)(0.160f * AUDIO_SAMPLE_RATE_I);
    s16* buf = (s16*)linearAlloc(nsamples * sizeof(s16));
    if (buf == NULL) return;

    u32 b1_end = (u32)(0.060f * AUDIO_SAMPLE_RATE_I);
    u32 b2_start = (u32)(0.085f * AUDIO_SAMPLE_RATE_I);

    for (u32 i = 0; i < nsamples; i++)
    {
        if (i >= b1_end && i < b2_start)
        {
            buf[i] = 0; // Brief silence between buzzes
            continue;
        }

        float t = (float)i / AUDIO_SAMPLE_RATE;
        float freq = (i < b1_end) ? 180.0f : 150.0f;
        float wave = sinf(2.0f * (float)M_PI * freq * t) + 0.4f * sinf(2.0f * (float)M_PI * freq * 2.0f * t);
        float env = (i < b1_end) ? (1.0f - ((float)i / (float)b1_end) * 0.4f)
                                 : (1.0f - ((float)(i - b2_start) / (float)(nsamples - b2_start)) * 0.5f);
        float val = wave * 11000.0f * env;
        buf[i] = (s16)val;
    }

    DSP_FlushDataCache(buf, nsamples * sizeof(s16));
    sfx->samples = buf;
    sfx->nsamples = nsamples;
    memset(&sfx->wave_buf, 0, sizeof(ndspWaveBuf));
    sfx->wave_buf.data_pcm16 = buf;
    sfx->wave_buf.nsamples = nsamples;
    sfx->wave_buf.status = NDSP_WBUF_FREE;
    sfx->wave_buf.looping = false;
}

bool audio_init(void)
{
    Result rc = ndspInit();
    if (R_FAILED(rc))
    {
        s_ndsp_initialized = false;
        return false;
    }
    s_ndsp_initialized = true;

    // Configure dedicated UI sound channel
    ndspChnReset(AUDIO_CHANNEL);
    ndspChnSetFormat(AUDIO_CHANNEL, NDSP_FORMAT_MONO_PCM16);
    ndspChnSetRate(AUDIO_CHANNEL, AUDIO_SAMPLE_RATE);
    ndspChnSetInterp(AUDIO_CHANNEL, NDSP_INTERP_LINEAR);

    // Set stereo balance (front-left and front-right volume)
    float mix[12] = {0};
    mix[0] = 0.65f;
    mix[1] = 0.65f;
    ndspChnSetMix(AUDIO_CHANNEL, mix);

    // Initialize all SFX containers
    memset(s_sfx, 0, sizeof(s_sfx));

    synthesize_nav(&s_sfx[SFX_NAV]);
    synthesize_select(&s_sfx[SFX_SELECT]);
    synthesize_back(&s_sfx[SFX_BACK]);
    synthesize_toggle(&s_sfx[SFX_TOGGLE]);
    synthesize_success(&s_sfx[SFX_SUCCESS]);
    synthesize_error(&s_sfx[SFX_ERROR]);

    return true;
}

void audio_cleanup(void)
{
    if (s_ndsp_initialized)
    {
        ndspChnWaveBufClear(AUDIO_CHANNEL);
        ndspExit();
        s_ndsp_initialized = false;
    }

    for (int i = 0; i < SFX_MAX; i++)
    {
        if (s_sfx[i].samples != NULL)
        {
            linearFree(s_sfx[i].samples);
            s_sfx[i].samples = NULL;
            s_sfx[i].nsamples = 0;
        }
    }
}

void audio_play_sfx(sfx_type_t id)
{
    if (!s_ndsp_initialized || !s_sound_enabled)
    {
        return;
    }

    if (id < 0 || id >= SFX_MAX)
    {
        return;
    }

    sound_effect_t* sfx = &s_sfx[id];
    if (sfx->samples == NULL || sfx->nsamples == 0)
    {
        return;
    }

    // Stop currently active playback and re-queue cleanly
    ndspChnWaveBufClear(AUDIO_CHANNEL);
    sfx->wave_buf.status = NDSP_WBUF_FREE;
    sfx->wave_buf.looping = false;
    ndspChnWaveBufAdd(AUDIO_CHANNEL, &sfx->wave_buf);
}

void audio_set_enabled(bool enabled)
{
    s_sound_enabled = enabled;
    if (!enabled && s_ndsp_initialized)
    {
        ndspChnWaveBufClear(AUDIO_CHANNEL);
    }
}

bool audio_is_enabled(void)
{
    return s_sound_enabled;
}

