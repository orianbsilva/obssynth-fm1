/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE; ObsSynth FM-1: 2026 Orian Beltrame */
/* The simulator's core (docs/SIMULATOR.md): the firmware (tests/sim_host.h) behind the small API the page drives
 * (web/sim/engine.js). One instance = one power-on. sim_render is the clock: it runs the audio ISR once per half
 * buffer and the main loop's frame every 15 ms of audio, as the device does. */
#include "sim_host.h"

#define SIM_MAX_FRAMES 1024u
#define SIM_FRAME_SAMPLES (FS * 15u / 1000u)             /* main.c: ~60 frames a second at most */

static float sim_out[2u * SIM_MAX_FRAMES];
static uint32_t sim_pos = HALF_FRAMES;                   /* frames of the current half already handed out */
static uint64_t sim_next_frame;
static int32_t sim_knob = 800 * 16;                      /* main.c's MASTER filter state */
static uint32_t sim_master_raw = 800;

void sim_frame(void)                                     /* main.c's loop body, minus the hardware */
{
    uint32_t k10;
    sim_knob += ((int32_t)sim_master_raw * 16 - sim_knob) / 8;
    k10 = (uint32_t)(sim_knob / 16);
    master_q12 = (k10 * k10) >> 8;
    ui_input();
    ui_leds();
    ui_draw();
}

void sim_flash_reset(void);

void sim_init(int fresh)                                 /* main.c's boot order (fresh: the flash erased; else as the
                                                          * page loaded it: no storage reads it yet) */
{
    if (fresh)
        sim_flash_reset();
    theme_set(THEME_TAG | THEME_OBS);
    lcd_fill(0, 0, 240, 240, C_BG);
    synth_init();
    presets_boot();                                      /* the user presets in the flash the page loaded */
    settings_boot();                                     /* .. and GLO's settings */
    ui_init();
    audio_init();
    sim_knob = (int32_t)sim_master_raw * 16;
    sim_frame();
    sim_next_frame = sim_samples + SIM_FRAME_SAMPLES;
}

uint32_t sim_render(uint32_t frames)                     /* interleaved float stereo into sim_audio(); frames run */
{
    uint32_t i, ui_frames = 0;
    if (frames > SIM_MAX_FRAMES)
        frames = SIM_MAX_FRAMES;
    for (i = 0; i < frames; i++) {
        const int32_t *h;
        if (sim_pos == HALF_FRAMES) {
            if (sim_samples >= sim_next_frame) {         /* between halves, as the main loop runs between ISRs */
                sim_frame();
                ui_frames++;
                sim_next_frame += SIM_FRAME_SAMPLES;
            }
            sim_half ^= 1u;
            fm1_alnk0_irq();
            sim_samples += HALF_FRAMES;
            sim_ms_update();
            sim_pos = 0;
        }
        h = &abuf[sim_half * HALF_WORDS];
        sim_out[2u * i] = (float)h[2u * sim_pos] * (1.0f / 8388608.0f);   /* 24-bit */
        sim_out[2u * i + 1u] = (float)h[2u * sim_pos + 1u] * (1.0f / 8388608.0f);
        sim_pos++;
    }
    return ui_frames;
}

void sim_btn(uint32_t label, int down)                   /* label = B_FX .. B_OCTUP (panel.c) */
{
    uint32_t bit;
    if (label >= NB)
        return;
    bit = panel_btn_bit(label);
    if (down) {
        fm1_in.pressed |= bit & ~fm1_in.buttons;
        fm1_in.buttons |= bit;
    } else
        fm1_in.buttons &= ~bit;
}

void sim_key(uint32_t n, int down)                       /* note key 0 (F3) .. 26 (G5) */
{
    uint32_t bit;
    if (n >= NKEYS)
        return;
    bit = 1u << n;
    if (down) {
        fm1_in.notes_pressed |= bit & ~fm1_in.notes;
        fm1_in.notes |= bit;
    } else
        fm1_in.notes &= ~bit;
}

void sim_enc(uint32_t role, int32_t steps)               /* role = EN_SELECT .. EN_K4, + = clockwise */
{
    if (role < NE)
        sim_enc_acc[panel.enc[role]] += steps * panel.dir[role];
}

void sim_master(uint32_t v) { sim_master_raw = v > 1023u ? 1023u : v; }

/* the page's clips (web/sim/clips.js): a preset by its list entry, as the PRESETS knob loads one (ui.c), the name
 * shown; returns the entry now (an out-of-range idx loads nothing) */
uint32_t sim_preset(uint32_t idx)
{
    char b[16];
    if (idx < preset_count()) {
        ui.pre = idx;
        ui.pre_ed = 0;
        preset_load(idx);
        preset_name(b, idx);
        ui_say(b, 0);
    }
    return ui.pre;
}

int sim_midi(uint32_t status, uint32_t d1, uint32_t d2)  /* a MIDI message in, as usb.c queues it (1: queued) */
{
    uint32_t cin = (status >> 4) & 15u;
    if (cin < 8u || mi_w - mi_r >= MQ)
        return 0;
    midi_in_q[mi_w % MQ] = cin | (status & 0xFFu) << 8 | (d1 & 0x7Fu) << 16 | (d2 & 0x7Fu) << 24;
    RING_PUBLISH();
    mi_w++;
    return 1;
}

static int sim_led_lit(uint32_t id)
{
    uint32_t p, r;
    for (p = 0; p < FM1_NCOL; p++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][p] == (int8_t)id)
                return (fm1_led[p] >> r) & 1u;
    return 0;
}

uint32_t sim_leds(void)                                  /* bit b = button label b */
{
    uint32_t b, m = 0;
    for (b = 0; b < NB; b++)
        m |= (uint32_t)sim_led_lit(panel.btn[b]) << b;
    return m;
}

uint32_t sim_key_leds(void)                              /* bit n = note key n */
{
    uint32_t n, m = 0;
    for (n = 0; n < NKEYS; n++)
        m |= (uint32_t)sim_led_lit(14u + n) << n;
    return m;
}

uint32_t sim_glow(void) { return ui_glow_btns(); }       /* bit b = button label b glows dimly while unlit (ui.c) */
uint32_t sim_key_glow(void) { return ui_glow_keys(); }   /* bit n = note key n */
uint32_t sim_playing(void) { return synth_voices() != 0u; }
uint16_t *sim_fb(void) { return fb; }
float *sim_audio(void) { return sim_out; }

/* ---- the flash, as the page keeps it (web/sim/store.js: IndexedDB, the sectors written; sim_host.h) */
void sim_flash_reset(void)
{
    memset(hflash, 0xFF, sizeof hflash);
    memset(sim_dirty, 0, sizeof sim_dirty);
}
uint8_t *sim_flash(void) { return hflash; }
uint32_t *sim_flash_dirty(void) { return sim_dirty; }
void sim_flash_clean(void) { memset(sim_dirty, 0, sizeof sim_dirty); }
