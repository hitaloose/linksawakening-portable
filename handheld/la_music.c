/* Custom music for Link's Awakening DX on Linux handhelds (MiaUI, ...).
 *
 * With MUSIC set to CUSTOM (options screen), a track the game plays is
 * replaced by an Ogg Vorbis file of your own, when there is one:
 *
 *   <pak>/music/<track>.ogg     <track> = the game's track number, two hex
 *                               digits (05.ogg = MUSIC_OVERWORLD, see the
 *                               MUSIC_* list in the zladx disassembly)
 *
 * Loop points come from the file's Vorbis comments, in samples:
 * LOOPSTART and LOOPLENGTH (or LOOPEND), as the usual game-music tools write
 * them; without them the whole file loops. Any sample rate (resampled to
 * the APU's 44.1 kHz), mono or stereo. Tracks without a file keep the
 * original Game Boy music.
 *
 * How (RAM from the zladx disassembly, WRAM bank 1):
 *   $D369 wActiveMusicIndex   the track playing (copied from wMusicTrackToPlay)
 *   $D360 wActiveJingle       a square-wave sound effect is playing (channel 1)
 *   $D370 wActiveWaveSfx      a wave sound effect is playing (channel 3)
 *   $D378 wActiveNoiseSfx     a noise sound effect is playing (channel 4)
 * While a replacement plays, the music's channels are silenced in the APU
 * mix (gb_audio_mute_mask): channel 2 always, channels 1, 3 and 4 only while
 * no sound effect is using them. The file is decoded with stb_vorbis in
 * small blocks as the APU asks for samples (gb_audio_mix_hook), and scaled
 * by the game's master volume (NR50), so the game's fades apply to it too.
 *
 * Debug: every track change goes to the log ("[MUSIC] track 05 ..."), and
 * so does every sound effect start, with the channel it took.
 */
#include "la_music.h"
#include "la_settings.h"
#include "audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#define FADE_IN   441              /* 10 ms, no click at the start */

#define W1(a) (ctx->wram[0x1000 + ((a) - 0xD000)])
#define R_TRACK  0xD369
#define R_JINGLE 0xD360
#define R_WSFX   0xD370
#define R_NSFX   0xD378

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
static char     s_dir[512];

static void music_dir(void) {
    if (s_dir[0]) return;
    const char* d = getenv("LA_MUSIC_DIR");
    snprintf(s_dir, sizeof(s_dir), "%s", d && *d ? d : "music");
}

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
    gb_audio_mute_mask = 0;
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

static void mix(int* l, int* r, int vol_l, int vol_r) {
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
    gb_audio_mix_hook = mix;
    fprintf(stderr, "[MUSIC] track %02X: %s (%d Hz, %d ch, loop %u-%u)\n", track, path, s_rate, s_channels,
            s_loop_start, s_loop_end);
    return true;
}

void la_music_frame(GBContext* ctx) {
    if (!ctx || !ctx->wram) return;
    int track = W1(R_TRACK);
    uint8_t jingle = W1(R_JINGLE), wsfx = W1(R_WSFX), nsfx = W1(R_NSFX);
    static uint8_t last_j, last_w, last_n;
    if (jingle && jingle != last_j) fprintf(stderr, "[MUSIC] sfx jingle %02X (ch1)\n", jingle);
    if (wsfx && wsfx != last_w) fprintf(stderr, "[MUSIC] sfx wave %02X (ch3)\n", wsfx);
    if (nsfx && nsfx != last_n) fprintf(stderr, "[MUSIC] sfx noise %02X (ch4)\n", nsfx);
    last_j = jingle; last_w = wsfx; last_n = nsfx;

    static bool s_tried;                              /* the current track's file was looked for */
    bool want = la_settings_music();
    if (track != s_track) {
        fprintf(stderr, "[MUSIC] game track %02X -> %02X\n", s_track & 0xFF, track);
        s_track = track;
        s_tried = false;
        stop();
    }
    if (!want) { if (s_playing) stop(); s_tried = false; }
    else if (!s_playing && !s_tried) {
        s_tried = true;
        if (track != 0x00 && track != 0xFF) start(track);
    }
    gb_audio_mute_mask = s_playing ? (uint8_t)(0x2 | (jingle ? 0 : 0x1) | (wsfx ? 0 : 0x4) | (nsfx ? 0 : 0x8)) : 0;
}
