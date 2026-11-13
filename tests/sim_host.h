/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE; ObsSynth FM-1: 2026 Orian Beltrame */
/* The simulator's host (docs/SIMULATOR.md): the firmware's sources in firmware/src/synth.c's order, with the HAL
 * replaced by state the simulator drives: the audio sample count as the clock, input levels and latched edges, the
 * LCD as a framebuffer, the audio ISR run on demand. Left out: lcd.c (the framebuffer), main.c (sim_core.c runs its
 * boot and its loop's body). Built with cc (tests/sim_test.c) and as wasm (tools/build_sim.sh). */
#include <stdint.h>
#include <stddef.h>
#define __attribute__(x)
#define memset felucca_memset
#define memcpy felucca_memcpy
#define memcmp felucca_memcmp
#include "felucca_tables.h"
#include "../firmware/src/libc.c"
#undef memset
#undef memcpy
#undef memcmp
#include <string.h>

/* ---- clock: TIMER4 (24 MHz) from the audio sample count; time stops when the audio does */
#define FM1_TICKS_PER_US 24u
static uint64_t sim_samples;
static volatile uint32_t fm1_ms;
static uint32_t fm1_ticks(void) { return (uint32_t)(sim_samples * 24000000u / 44100u); }
static void sim_ms_update(void) { fm1_ms = (uint32_t)(sim_samples * 1000u / 44100u); }

/* ---- input (hal/fm1_input.h's API): levels set by the page, press edges latched until the UI takes them */
#define FM1_NCOL 11u
#include "sim_keymap.h"                                  /* FM1_KEYMAP, from hal/fm1_input.h (tools/build_sim.sh) */
static struct { volatile uint32_t notes, buttons, pressed, notes_pressed; } fm1_in;
static int32_t sim_enc_acc[7];
static uint8_t fm1_led[FM1_NCOL];
static int32_t enc_take(uint32_t e) { int32_t s = sim_enc_acc[e % 7u]; sim_enc_acc[e % 7u] = 0; return s; }
static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t p = fm1_in.pressed;
    if (released)
        *released = 0;
    fm1_in.pressed = 0;
    return p;
}
static uint32_t fm1_input_note_edges(void) { uint32_t p = fm1_in.notes_pressed; fm1_in.notes_pressed = 0; return p; }
static void fm1_led_key(uint32_t id, int on)             /* as hal/fm1_input.h */
{
    uint32_t p, r;
    for (p = 0; p < FM1_NCOL; p++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][p] == (int8_t)id) {
                if (on)
                    fm1_led[p] |= (uint8_t)(1u << r);
                else
                    fm1_led[p] &= (uint8_t)~(1u << r);
            }
}

/* ---- LCD: the 240 x 240 RGB565 framebuffer */
static uint16_t fb[240 * 240];
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            fb[j * 240u + i] = c;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u)
                fb[(y + j) * 240u + x + i] = (uint16_t)((px[j * w + i] >> 8) | (px[j * w + i] << 8));   /* the canvas
                                                                                          * is byte-swapped */
}

/* ---- audio (hal/fm1_audio.h's API for audio.c): sim_core.c runs the ISR once per half, the halves alternate */
#define FM1_AUDIO_HALF 0x80u
static uint32_t sim_half;
static uint8_t fm1_audio_pending(void) { return FM1_AUDIO_HALF; }
static void fm1_audio_ack_aux(uint8_t p) { (void)p; }
static uint32_t fm1_audio_free_half(void) { return sim_half; }
static void fm1_audio_ack_half(void) {}
static void fm1_audio_init(int32_t *buf, uint32_t half_words, void (*isr)(void), uint32_t prio)
{
    (void)buf, (void)half_words, (void)isr, (void)prio;
}
void isr_alnk0(void) {}

/* ---- flash: 1 MiB in RAM (erased = 0xFF); erase / program mark their 4 KB sectors dirty for the page (store.js
 * keeps them in IndexedDB); storage.c's three hooks on it, as the device's (synth.c) */
#define SIM_FLASH_SIZE 0x100000u
static uint8_t hflash[SIM_FLASH_SIZE];
static uint32_t sim_dirty[SIM_FLASH_SIZE / 4096u / 32u];
static void sim_mark(uint32_t off, uint32_t n)
{
    uint32_t s;
    for (s = off / 4096u; s <= (off + n - 1u) / 4096u && s < SIM_FLASH_SIZE / 4096u; s++)
        sim_dirty[s / 32u] |= 1u << (s % 32u);
}
static uint8_t flash_ok = 1;                             /* (the device's: its part's id at boot; ui.c's INFO) */
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    if (off > SIM_FLASH_SIZE || n > SIM_FLASH_SIZE - off)
        return -1;
    memcpy(dst, hflash + off, n);
    return 0;
}
static int st_erase(uint32_t off)
{
    if (off > SIM_FLASH_SIZE - 4096u)
        return -8;
    memset(hflash + off, 0xFF, 4096);
    sim_mark(off, 4096);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    if (off > SIM_FLASH_SIZE || n > SIM_FLASH_SIZE - off)
        return -8;
    for (i = 0; i < n; i++)
        hflash[off + i] &= s[i];                         /* NOR: programming only clears bits */
    if (n)
        sim_mark(off, n);
    return 0;
}

/* ---- MIDI in: usb.c's ring (the device's), filled here by sim_midi (sim_core.c) */
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define MQ 64u
static uint32_t midi_in_q[MQ];
static volatile uint32_t mi_w, mi_r;

/* the LED layers (ui.c leds_commit): the full layer lit; the glow layer is the page's (sim_glow), the device dims it
 * with led_dim.c */
static void leds_commit(const uint32_t *lay)
{
    uint32_t c;
    for (c = 0; c < FM1_NCOL; c++)
        fm1_led[c] = (uint8_t)(lay[c] & 0xFFu);
}

#include "../firmware/src/gfx.c"
#include "../firmware/src/dsp.c"
#include "../firmware/src/voice.c"
#include "../firmware/src/engine.c"
#include "../firmware/src/midi.c"
#include "../firmware/src/fx.c"
#include "../firmware/src/storage.c"
#include "../firmware/src/presets.c"
#include "../firmware/src/audio.c"
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
