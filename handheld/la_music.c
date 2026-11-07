/* Custom music and sound effects for Link's Awakening DX on Linux handhelds
 * (MiaUI, ...).
 *
 * With MUSIC set to CUSTOM (options screen), a track or a sound effect the
 * game plays is replaced by an Ogg Vorbis file of your own, when there is one:
 *
 *   <pak>/music/<track>.ogg     <track> = the game's track number, two hex
 *                               digits (05.ogg = MUSIC_OVERWORLD, see the
 *                               MUSIC_* list in the zladx disassembly)
 *   <pak>/music/sfx/j_<id>.ogg  square-wave jingle  (JINGLE_*,    channel 1)
 *   <pak>/music/sfx/w_<id>.ogg  wave sound effect   (WAVE_SFX_*,  channel 3)
 *   <pak>/music/sfx/n_<id>.ogg  noise sound effect  (NOISE_SFX_*, channel 4)
 *
 * Music loop points come from the file's Vorbis comments, in samples:
 * LOOPSTART and LOOPLENGTH (or LOOPEND), as the usual game-music tools write
 * them; without them the whole file loops. Sound effects play once. Any
 * sample rate (resampled to the APU's 44.1 kHz), mono or stereo (more
 * channels are folded into two). Whatever has no file keeps the Game Boy
 * sound.
 *
 * How (RAM from the zladx disassembly, WRAM bank 1):
 *   $D369 wActiveMusicIndex   the track playing (copied from wMusicTrackToPlay)
 *   $D360 / $D370 / $D378     jingle / wave / noise effect *requested*: set
 *                             from hJingle etc. and cleared by PlaySfx in the
 *                             same driver run, so never seen between frames
 *   $D361 / $D371 / $D379     the jingle / wave / noise effect *playing*,
 *                             0 when its channel is free again
 * New effects are caught in PlaySfx (bank $1F, hooked through the trampoline
 * dispatcher, see la_hook_dispatch), where the requests are still set; if
 * that hook never runs, a change of the "playing" bytes starts them instead.
 *
 * While a replacement track plays, the music's channels are silenced in the
 * APU mix (gb_audio_mute_mask): all four, except those a Game Boy sound
 * effect is using right now (channel 1 for a jingle, 2 and 3 for the
 * treasure jingle, 3 for a wave effect, 4 for a noise effect). A replaced
 * effect silences its channel for as long as the Game Boy one would have
 * used it. Files are decoded with stb_vorbis: music in small blocks as the
 * APU asks for samples (gb_audio_mix_hook), effects whole, once, and kept in
 * memory. Both are scaled by the game's master volume (NR50), so the game's
 * fades apply to them too.
 *
 * Debug: every track change goes to the log ("[MUSIC] game track 05 ..."),
 * and so does every sound effect start, with its file name when replaced.
 */
#include "la_music.h"
#include "la_settings.h"
#include "audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#define STB_VORBIS_NO_PUSHDATA_API
#include "third_party/stb_vorbis.c"
#pragma GCC diagnostic pop

#define OUT_RATE  44100
#define BLOCK     2048             /* frames decoded at a time */
#define GAIN      0.70f            /* replacement level against the APU's */
#define SFX_GAIN  0.80f
#define FADE_IN   441              /* 10 ms, no click at the start */

#define W1(a) (ctx->wram[0x1000 + ((a) - 0xD000)])
#define R_TRACK  0xD369
#define R_JINGLE_REQ 0xD360
#define R_JINGLE     0xD361
#define R_WSFX_REQ   0xD370
#define R_WSFX       0xD371
#define R_NSFX_REQ   0xD378
#define R_NSFX       0xD379
#define JINGLE_TREASURE_FOUND 0x01 /* plays on channels 2 and 3, not 1 */

extern uint8_t la_hook_map[];      /* la_extra_buttons.c */

static char s_dir[512];

static void music_dir(void) {
    if (s_dir[0]) return;
    const char* d = getenv("LA_MUSIC_DIR");
    snprintf(s_dir, sizeof(s_dir), "%s", d && *d ? d : "music");
}

/* ---- music ---------------------------------------------------------------- */
static stb_vorbis* s_v;
static int      s_channels, s_rate;
static unsigned s_loop_start, s_loop_end;   /* frames; end 0 = the file's end */
static unsigned s_pos;                      /* frames decoded so far (file position) */
static short    s_buf[BLOCK * 2];
static int      s_buf_n, s_buf_i;           /* frames in the buffer, next one */
static unsigned s_phase;                    /* 16.16 position between s_prev and s_cur */
static unsigned s_step;                     /* 16.16 file frames per output frame */
static int      s_prev[2], s_cur[2];
static int      s_fade;
static int      s_track = -1;               /* the game's track we follow */
static bool     s_playing;

static unsigned comment_value(const stb_vorbis_comment* c, const char* key) {
    size_t n = strlen(key);
    for (int i = 0; i < c->comment_list_length; i++) {
        const char* e = c->comment_list[i];
        if (!strncasecmp(e, key, n) && e[n] == '=') return (unsigned)strtoul(e + n + 1, NULL, 10);
    }
    return 0;
}

static void stop(void) {
    if (s_v) stb_vorbis_close(s_v);
    s_v = NULL;
    s_playing = false;
}

/* Next frame of the file (looping), as 16-bit stereo. */
static bool next_frame(int out[2]) {
    if (s_buf_i >= s_buf_n) {
        int want = BLOCK;
        if (s_loop_end && s_pos + want > s_loop_end) want = (int)(s_loop_end - s_pos);
        int got = want > 0 ? stb_vorbis_get_samples_short_interleaved(s_v, 2, s_buf, want * 2) : 0;
        if (got <= 0) {                       /* the loop end or the file's end: back to the loop start */
            if (!stb_vorbis_seek(s_v, s_loop_start)) return false;
            s_pos = s_loop_start;
            got = stb_vorbis_get_samples_short_interleaved(s_v, 2, s_buf, BLOCK * 2);
            if (got <= 0) return false;
        }
        s_pos += (unsigned)got;
        s_buf_n = got;
        s_buf_i = 0;
    }
    out[0] = s_buf[s_buf_i * 2];
    out[1] = s_buf[s_buf_i * 2 + 1];
    s_buf_i++;
    return true;
}

static void music_mix(int* l, int* r, int vol_l, int vol_r) {
    if (!s_playing) return;
    s_phase += s_step;
    while (s_phase >= 0x10000) {
        s_phase -= 0x10000;
        s_prev[0] = s_cur[0]; s_prev[1] = s_cur[1];
        if (!next_frame(s_cur)) { fprintf(stderr, "[MUSIC] decode error: stopped\n"); stop(); return; }
    }
    int f = (int)(s_phase >> 4);              /* 0..4095 */
    int a = s_prev[0] + (((s_cur[0] - s_prev[0]) * f) >> 12);
    int b = s_prev[1] + (((s_cur[1] - s_prev[1]) * f) >> 12);
    float g = GAIN;
    if (s_fade < FADE_IN) g *= (float)s_fade++ / FADE_IN;
    *l += (int)(a * g * (vol_l + 1) / 8);
    *r += (int)(b * g * (vol_r + 1) / 8);
}

static bool start(int track) {
    music_dir();
    char path[600];
    snprintf(path, sizeof(path), "%s/%02X.ogg", s_dir, track);
    int err = 0;
    stb_vorbis* v = stb_vorbis_open_filename(path, &err, NULL);
    if (!v) {
        snprintf(path, sizeof(path), "%s/%02x.ogg", s_dir, track);
        v = stb_vorbis_open_filename(path, &err, NULL);
    }
    if (!v) return false;
    stop();
    s_v = v;
    stb_vorbis_info info = stb_vorbis_get_info(v);
    stb_vorbis_comment c = stb_vorbis_get_comment(v);
    s_channels = info.channels;
    s_rate = (int)info.sample_rate;
    s_loop_start = comment_value(&c, "LOOPSTART");
    unsigned len = comment_value(&c, "LOOPLENGTH"), end = comment_value(&c, "LOOPEND");
    s_loop_end = len ? s_loop_start + len : end;
    s_pos = 0;
    s_buf_n = s_buf_i = 0;
    s_step = (unsigned)(((uint64_t)s_rate << 16) / OUT_RATE);
    s_phase = 0;
    s_prev[0] = s_prev[1] = s_cur[0] = s_cur[1] = 0;
    s_fade = 0;
    s_playing = true;
    fprintf(stderr, "[MUSIC] track %02X: %s (%d Hz, %d ch, loop %u-%u)\n", track, path, s_rate, s_channels,
            s_loop_start, s_loop_end);
    return true;
}

/* ---- sound effects -------------------------------------------------------- */
enum { K_JINGLE, K_WAVE, K_NOISE, K_COUNT };
static const char  k_prefix[K_COUNT] = { 'j', 'w', 'n' };
static const char* k_name[K_COUNT] = { "jingle", "wave", "noise" };

typedef struct {
    short*   pcm;                  /* interleaved stereo */
    unsigned frames, rate;
    int8_t   state;                /* 0 = not looked for, 1 = loaded, -1 = no file */
} Sfx;
static Sfx s_sfx[K_COUNT][256];

typedef struct {
    const Sfx* s;                  /* NULL = idle */
    unsigned   pos, phase, step;   /* pos: frame index; phase/step 16.16 */
} Voice;
static Voice s_voice[K_COUNT];

static int  s_scan_k, s_scan_id = 1;  /* preload position */
static bool s_hook_seen;              /* PlaySfx hook works: no polling */
static bool s_skip_body;              /* $4006 handled; its $401E is the same run */
static uint8_t s_last_playing[K_COUNT];

static const Sfx* sfx_get(int k, int id) {
    Sfx* s = &s_sfx[k][id & 0xFF];
    if (s->state) return s->state > 0 ? s : NULL;
    s->state = -1;
    music_dir();
    char path[600];
    snprintf(path, sizeof(path), "%s/sfx/%c_%02X.ogg", s_dir, k_prefix[k], id);
    if (access(path, R_OK) != 0) {
        snprintf(path, sizeof(path), "%s/sfx/%c_%02x.ogg", s_dir, k_prefix[k], id);
        if (access(path, R_OK) != 0) return NULL;
    }
    int err = 0;
    stb_vorbis* v = stb_vorbis_open_filename(path, &err, NULL);
    if (!v) { fprintf(stderr, "[MUSIC] %s: can't read (error %d)\n", path, err); return NULL; }
    stb_vorbis_info info = stb_vorbis_get_info(v);
    unsigned total = stb_vorbis_stream_length_in_samples(v);
    short* pcm = (short*)malloc(((size_t)total + 1) * 2 * sizeof(short));
    if (!pcm) { stb_vorbis_close(v); return NULL; }
    unsigned n = 0;
    while (n < total) {
        int got = stb_vorbis_get_samples_short_interleaved(v, 2, pcm + n * 2, (int)(total - n) * 2);
        if (got <= 0) break;
        n += (unsigned)got;
    }
    stb_vorbis_close(v);
    if (!n) { free(pcm); return NULL; }
    s->pcm = pcm;
    s->frames = n;
    s->rate = info.sample_rate;
    s->state = 1;
    fprintf(stderr, "[MUSIC] loaded %s (%u frames, %u Hz)\n", path, n, s->rate);
    return s;
}

/* A few files per frame, so the first use doesn't stall the game. */
static void sfx_preload(void) {
    for (int n = 0; n < 12 && s_scan_k < K_COUNT; n++) {
        sfx_get(s_scan_k, s_scan_id);
        if (++s_scan_id > 0x7F) { s_scan_id = 1; s_scan_k++; }
    }
}

static uint8_t sfx_channels(int k, int id) {
    if (k == K_JINGLE) return id == JINGLE_TREASURE_FOUND ? 0x6 : 0x1;
    return k == K_WAVE ? 0x4 : 0x8;
}

static void sfx_start(int k, int id) {
    const Sfx* s = la_settings_music() ? sfx_get(k, id) : NULL;
    fprintf(stderr, "[MUSIC] sfx %s %02X%s\n", k_name[k], id, s ? " (replaced)" : "");
    if (!s) return;
    Voice* v = &s_voice[k];
    v->s = s;
    v->pos = 0;
    v->phase = 0;
    v->step = (unsigned)(((uint64_t)s->rate << 16) / OUT_RATE);
    gb_audio_mute_mask |= sfx_channels(k, id);   /* right away, not from the next frame */
}

static void sfx_mix(int* l, int* r, int vol_l, int vol_r) {
    for (int k = 0; k < K_COUNT; k++) {
        Voice* v = &s_voice[k];
        if (!v->s) continue;
        if (v->pos + 1 >= v->s->frames) { v->s = NULL; continue; }
        const short* p = v->s->pcm + v->pos * 2;
        int f = (int)(v->phase >> 4);
        int a = p[0] + (((p[2] - p[0]) * f) >> 12);
        int b = p[1] + (((p[3] - p[1]) * f) >> 12);
        *l += (int)(a * SFX_GAIN * (vol_l + 1) / 8);
        *r += (int)(b * SFX_GAIN * (vol_r + 1) / 8);
        v->phase += v->step;
        v->pos += v->phase >> 16;
        v->phase &= 0xFFFF;
    }
}

static void mix(int* l, int* r, int vol_l, int vol_r) {
    music_mix(l, r, vol_l, vol_r);
    sfx_mix(l, r, vol_l, vol_r);
}

/* PlaySfx: the requests of this driver run are still in $D360/$D370/$D378. */
int la_music_sfx_hook(GBContext* ctx, uint16_t addr) {
    if (!ctx || !ctx->wram) return 0;
    if (addr == LA_MUSIC_HOOK_PLAY_SFX2 && s_skip_body) { s_skip_body = false; return 0; }
    s_skip_body = addr == LA_MUSIC_HOOK_PLAY_SFX;
    s_hook_seen = true;
    uint8_t req[K_COUNT] = { W1(R_JINGLE_REQ), W1(R_WSFX_REQ), W1(R_NSFX_REQ) };
    for (int k = 0; k < K_COUNT; k++) if (req[k]) sfx_start(k, req[k]);
    return 0;
}

static void hooks_install(GBContext* ctx) {
    static bool done;
    if (done || !ctx->rom) return;
    done = true;
    const uint8_t* r = ctx->rom + LA_MUSIC_HOOK_BANK * 0x4000u - 0x4000u;
    /* PlaySfx: jp _PlaySfx / _PlaySfx: call PlayActiveJingle */
    bool ok = ctx->rom_size > LA_MUSIC_HOOK_BANK * 0x4000u + 0x40 &&
              r[0x4006] == 0xC3 && r[0x4007] == 0x1E && r[0x4008] == 0x40 && r[0x401E] == 0xCD;
    if (ok) {
        la_hook_map[LA_MUSIC_HOOK_PLAY_SFX >> 3]  |= (uint8_t)(1u << (LA_MUSIC_HOOK_PLAY_SFX & 7));
        la_hook_map[LA_MUSIC_HOOK_PLAY_SFX2 >> 3] |= (uint8_t)(1u << (LA_MUSIC_HOOK_PLAY_SFX2 & 7));
    }
    fprintf(stderr, "[MUSIC] sound effects: %s\n", ok ? "PlaySfx hooked" : "unknown ROM, following the driver state");
}

/* ---- per frame ------------------------------------------------------------ */
void la_music_frame(GBContext* ctx) {
    if (!ctx || !ctx->wram) return;
    hooks_install(ctx);
    gb_audio_mix_hook = mix;
    int track = W1(R_TRACK);
    uint8_t playing[K_COUNT] = { W1(R_JINGLE), W1(R_WSFX), W1(R_NSFX) };
    bool want = la_settings_music();

    /* Without the hook: start an effect when its channel's effect changes. */
    if (!s_hook_seen)
        for (int k = 0; k < K_COUNT; k++)
            if (playing[k] && playing[k] != s_last_playing[k]) sfx_start(k, playing[k]);
    for (int k = 0; k < K_COUNT; k++) s_last_playing[k] = playing[k];

    static bool s_tried;                              /* the current track's file was looked for */
    if (track != s_track) {
        fprintf(stderr, "[MUSIC] game track %02X -> %02X\n", s_track & 0xFF, track);
        s_track = track;
        s_tried = false;
        stop();
    }
    if (!want) {
        if (s_playing) stop();
        s_tried = false;
        for (int k = 0; k < K_COUNT; k++) s_voice[k].s = NULL;
    } else {
        if (!s_playing && !s_tried) {
            s_tried = true;
            if (track != 0x00 && track != 0xFF) start(track);
        }
        sfx_preload();
    }

    /* Channels the Game Boy effects use now, and those of them we replace. */
    uint8_t used = 0, replaced = 0;
    for (int k = 0; k < K_COUNT; k++) {
        if (!playing[k]) continue;
        uint8_t ch = sfx_channels(k, playing[k]);
        used |= ch;
        if (want && s_sfx[k][playing[k]].state > 0) replaced |= ch;
    }
    gb_audio_mute_mask = (uint8_t)((s_playing ? (0xF & ~used) : 0) | replaced);
}
