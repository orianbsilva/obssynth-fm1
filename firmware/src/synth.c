/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * ObsSynth FM-1: 2026 Orian Beltrame */
/* ObsSynth for the M-VAVE FM-1: one compilation unit (the HAL is header-only). Order matters. The simulator
 * (tests/sim_host.h) includes the same files from dsp.c on, with the HAL replaced. */
#include <stdint.h>
#include "fm1_time.h"
#include "fm1_sys.h"
#include "fm1_irq.h"
#include "fm1_guard.h"
#include "fm1_input.h"
#include "fm1_timer.h"
#include "fm1_audio.h"
#include "fm1_adc.h"
#include "fm1_lcd_hw.h"
#include "felucca_tables.h"
static volatile uint32_t fm1_ms;     /* milliseconds since boot (main.c: TIMER5; the simulator: its sample clock) */

#include "libc.c"
#include "lcd.c"
#include "gfx.c"
#include "enc.c"             /* the knobs' decoder (upstream Felucca 1.1.5, #126) on the scan's frames */
/* USB: the update loader's own driver (usb.c, unchanged: the pinned loader is built from it), a class-compliant
 * USB-MIDI device with the M-UPGRADE update entry (ota.c, below) and the UBOOT soft key; no serial console */
#ifndef FELUCCA_ID
#define FELUCCA_ID "FM-1_917"    /* the package identity the update handshake answers (tools/build.py PRODUCT); ObsSynth's own */
#endif
#define FELUCCA_OTA 1
#define FELUCCA_CDC 0
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")   /* a ring slot stored before its index */
/* boot-loop guard (main.c): two boots in a row that die in the first 30 s -> UBOOT; an update's reset is not one */
#define BOOTGUARD_MAGIC 0x42475244u
struct { uint32_t magic, failed, pending; } bootguard __attribute__((section(".noinit")));
#include "usb.c"
#include "dsp.c"
#include "voice.c"           /* oscillator -> filter -> amp, two ADSRs */
#include "engine.c"          /* the voices, the parameters, the note ring */
#include "midi.c"            /* USB-MIDI in -> the voices */
#include "fx.c"              /* delay, reverb, the soft clip */
#include "audio.c"
/* the flash (hal/fm1_flash.h) for storage.c, as Hortator's felucca.c: the part's id checked at boot (main.c); reads in
 * 256-byte IRQ-off windows (the audio keeps up); an erase silences the audio buffer first (IRQs off: the DMA would
 * loop stale audio, a buzz); writes only inside the HAL's store window (FL_STORE_OK) */
#include "fm1_flash.h"
#define FL_JEDEC_FM1 0x856014u                           /* the FM-1's flash id: Puya P25Q80 */
#define FL_JEDEC_FM1_NEW 0xB36014u                       /* UC25HQ80, in units sold since autumn 2026 */
static uint8_t flash_ok;
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    uint8_t *d = dst;
    if (!flash_ok)
        return -9;
    while (n) {
        uint32_t k = n > 256u ? 256u : n, f = irq_save();
        int rc = FL_FAR(fl_read_ram)(off, d, k);
        irq_restore(f);
        if (rc)
            return rc;
        off += k;
        d += k;
        n -= k;
    }
    return 0;
}
static int st_erase(uint32_t off)
{
    uint32_t took, f, i;
    int rc;
    if (!flash_ok || !FL_STORE_OK(off, 0x1000u))
        return -8;
    f = irq_save();
    for (i = 0; i < sizeof abuf / sizeof abuf[0]; i++)
        abuf[i] = 0;
    rc = FL_FAR(fl_erase4k_ram)(off, &took);
    irq_restore(f);
    return rc;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    if (!flash_ok || !FL_STORE_OK(off, n))
        return -8;
    return fl_write(off, src, n);
}
static void flash_boot(void)                             /* main.c, before the presets */
{
    uint32_t f = irq_save();
    uint32_t id = FL_FAR(fl_jedec_ram)();
    flash_ok = id == FL_JEDEC_FM1 || id == FL_JEDEC_FM1_NEW;
    irq_restore(f);
    if (flash_ok)
        fl_plain_window_init();
}
#include "storage.c"         /* A/B records on the flash (Felucca's) */
#include "presets.c"         /* the factory presets, the user bank */
/* the M-UPGRADE update entry (Felucca's ota.c) and its hooks, as Hortator's felucca.c: the package's loader staged at
 * 0xE0000 (outside the store), committed only on the host's "success" */
#include "ota.c"
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void) { fm1_wdt_feed(); }
static int ota_in_area(uint32_t off, uint32_t n) { return FL_IN(off, n, OTA_AREA, OTA_AREA + OTA_AREA_LEN); }
static int ota_erase(uint32_t off)
{
    uint32_t took, i;
    if (!ota_in_area(off, 0x1000u) || (off & 0xFFFu))
        return -8;
    for (i = 0; i < sizeof abuf / sizeof abuf[0]; i++)
        abuf[i] = 0;                                     /* (the DMA would loop stale audio while IRQs are off) */
    return fl_erase4k(off, &took);
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!ota_in_area(off, n))
        return -8;
    return fl_write(off, p, n);
}
static int ota_fread(uint32_t off, void *p, uint32_t n) { return st_read(off, p, n); }
static void ota_show(uint32_t step, int32_t code)
{
    static const char *const STEP[] = {"", "PACKAGE", "CHECK HEAD", "LOADER", "CONFIRM", "RESTART"};
    char b[24];
    lcd_fill(0, 0, 240, 240, C_BG);
    draw_text_box(0, 92, 240, &FONT_S, "UPDATE", C_INK, 1);
    if (step < 9u) {
        draw_text_box(0, 124, 240, &FONT_S, STEP[step < 6u ? step : 0], C_INK, 1);
        return;
    }
    if (code == 1)
        str_cpy(b, "DRY RUN OK", sizeof b);
    else {
        uint32_t k = 0, v = (uint32_t)-code;
        str_cpy(b, "FAILED  -", sizeof b);
        while (b[k])
            k++;
        if (v >= 10u)
            b[k++] = (char)('0' + v / 10u);
        b[k++] = (char)('0' + v % 10u);
        b[k] = 0;
    }
    draw_text_box(0, 124, 240, &FONT_S, b, C_INK, 1);
    fm1_delay_ms(1500);
}
/* the FM-1's own report (the installer's "Read the FM-1"): SysEx F0 7D 'O' 'B' 'S' 01 F7 (7D: the non-commercial id)
 * answered with F0 7D 'O' 'B' 'S' 02 "v=VERSION;b=BUILD;p=USER PRESETS;f=FLASH OK" F7. It looks at the pending frame
 * before ota.c does and leaves any other one to it (ota.c unchanged: the pinned loader is built from it) */
#ifndef SYNTH_VERSION
#define SYNTH_VERSION "DEV"
#endif
#ifndef SYNTH_BUILD
#define SYNTH_BUILD "LOCAL"
#endif
static void info_service(void)
{
    static const uint8_t Q[5] = {0x7D, 'O', 'B', 'S', 0x01};
    static const char V[] = "v=" SYNTH_VERSION ";b=" SYNTH_BUILD ";p=";
    static uint8_t r[64];
    const uint8_t *p;
    uint32_t n, k, i, used = 0;
    if (!ota_frame_get(&p, &n) || n != sizeof Q || memcmp(p, Q, sizeof Q))
        return;                                          /* none, or not ours: ota_service's */
    ota_frame_done();
    for (i = 0; i < NUSER; i++)
        used += (ubank.used >> i) & 1u;
    r[0] = 0xF0;
    r[1] = 0x7D;
    r[2] = 'O';
    r[3] = 'B';
    r[4] = 'S';
    r[5] = 0x02;
    for (k = 6, i = 0; V[i] && k < sizeof r - 12u; i++)
        r[k++] = (uint8_t)(V[i] & 0x7F);
    if (used >= 10u)
        r[k++] = (uint8_t)('0' + used / 10u);
    r[k++] = (uint8_t)('0' + used % 10u);
    r[k++] = ';';
    r[k++] = 'f';
    r[k++] = '=';
    r[k++] = flash_ok ? '1' : '0';
    r[k++] = 0xF7;
    ota_wire_send(r, k);
}

static void ota_commit(const uint8_t *parm)
{
    bootguard.pending = 0;                               /* an intentional reset */
    usb_detach();
    fm1_delay_ms(30);
    fm1_enter_update(parm);                              /* the record into RAM, a core reset (fm1_sys.h) */
}
#include "panel.c"
#include "led_dim.c"         /* the LEDs' dim level: PWM over the scan (Hortator's, unchanged) */
static void leds_commit(const uint32_t *lay)             /* ui.c: the LED layers, one store per column */
{
    uint32_t c;
    led_glow_w = 0;                                      /* no timed pulse (it busy-waits in the scan's ISR: ~6 % CPU) */
    led_mid_ph = LED_PH_25;                              /* the backlight: the mid layer, frames 0 4 8 12 16 of 20 */
    led_br_on = 0;
    for (c = 0; c < FM1_NCOL; c++)
        led_lay[c] = lay[c];                             /* the scan sees a column whole or not at all */
    led_dim_on = 1;                                      /* (after the layers: the hook composes from them) */
}
#include "ui.c"
#include "main.c"
