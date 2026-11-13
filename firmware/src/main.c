/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * ObsSynth FM-1: 2026 Orian Beltrame */
/* Boot and main loop (after Felucca's): WDT first, the boot-loop guard, fatal vectors, guards; then the LCD, the
 * input scan (TIMER5, 10 kHz), the audio (ALNK0), USB (polled from TIMER5 at 2 kHz). The loop reads MASTER, the
 * panel, draws (~60 frames a second at most), answers the M-UPGRADE update entry (ota.c) and runs an update when the
 * host asks; a host's soft key (SysEx F0 22 24 35 7D F7) or OCT- + OCT+ held 5 s enters UBOOT (the update / recovery
 * mode, docs/RECOVERY.md). */
extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
extern uint32_t _pool_start[], _pool_end[], _rt_start[], _rt_end[], _rt_load[];

/* TIMER5 outranks ALNK0: the scan keeps its 100 us pace while a half renders; nested it only scans and counts ms.
 * USB (usb.c usb_poll: all SIE traffic) every 5th tick, 2 kHz, never nested: a poll due during a render waits for
 * the first tick after it (as Felucca's) */
void fm1_timer5_irq(void)
{
    static uint32_t last, acc, sub, owed;
    uint32_t t0 = fm1_ticks();
    fm1_timer5_ack();
    fm1_input_tick();
    led_dim_tick();                                      /* led_dim.c: the dimmed LEDs (the column the next tick lights)
                                                          * and, once a scan frame, the knobs (enc.c) */
    acc += t0 - last;                                    /* milliseconds from the 24 MHz TIMER4 */
    last = t0;
    while (acc >= 1000u * FM1_TICKS_PER_US) {
        acc -= 1000u * FM1_TICKS_PER_US;
        fm1_ms++;
    }
    if (sub++ % 5u == 0u)
        owed = 1;
    if (in_audio) {
        t5_nested_ticks += fm1_ticks() - t0;
        return;
    }
    if (owed) {
        owed = 0;
        usb_poll();
    }
}
extern void isr_timer5(void);

static void hexs(char *b, uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 8u; i++)
        b[i] = "0123456789ABCDEF"[(v >> (28u - 4u * i)) & 15u];
    b[8] = 0;
}

static void fm1_fault(const fm1_crash_t *c)
{
    char b[12];
    uint32_t t0;
    fm1_audio_stop();
    lcd_fill(0, 0, 240, 240, RGB(160, 0, 0));
    draw_text_box(0, 8, 240, &FONT_S, "CRASH", C_WHITE, 1);
    hexs(b, c->vec);
    draw_text_box(10, 40, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->pc);
    draw_text_box(10, 60, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->emu);
    draw_text_box(10, 84, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->rets);
    draw_text_box(10, 102, 220, &FONT_S, b, C_WHITE, 0);
    t0 = fm1_ticks();
    while ((uint32_t)(fm1_ticks() - t0) < 4000u * 1000u * FM1_TICKS_PER_US)
        ;
    fm1_reboot();
}

static void master_read(int32_t *knob)                  /* MASTER pot: a slow IIR, then a square law */
{
    int32_t a = fm1_adc_read(FM1_ADC_MASTER);
    uint32_t k10;
    if (a < 0)
        return;
    *knob += (a * 16 - *knob) / 8;
    k10 = (uint32_t)(*knob / 16);
    master_q12 = (k10 * k10) >> 8;                       /* 0 .. ~4096 */
}

static void uboot_enter(const char *why)                 /* UBOOT, RAM intact: the update / recovery mode */
{
    fm1_audio_stop();
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 110, 240, &FONT_S, why, RGB(80, 120, 255), 1);
    fm1_delay_ms(20);
    usb_detach();
    fm1_delay_ms(30);
    bootguard.pending = 0;                               /* intentional reset: not a failed boot */
    fm1_enter_uboot();
}

static void uboot_poll(void)                             /* OCT- + OCT+ held 5 s; the host's soft key */
{
    static uint32_t t0;
    uint32_t both = panel_btn_bit(B_OCTDN) | panel_btn_bit(B_OCTUP);
    if (usb.uboot_req)
        uboot_enter("UBOOT (USB)");
    if ((fm1_in.buttons & both) != both) {
        t0 = fm1_ms;
        return;
    }
    if (fm1_ms - t0 > 5000u)
        uboot_enter("UBOOT");
}

static void update_poll(void)                            /* M-UPGRADE: the handshake, an update when asked */
{
    info_service();                                      /* synth.c: the installer's "Read the FM-1" first */
    ota_service();                                       /* (ota.c: answers the identity, the version) */
    if (!usb.ota_req)
        return;
    usb.ota_req = 0;
    synth_all_off();                                     /* the voices released: the update runs in silence */
    if (flash_ok)
        ota_session();                                   /* returns only if nothing was committed */
    lcd_fill(0, 0, 240, 240, C_BG);
    ui.sig_top = ui.sig_bot = 0;                         /* the screen drawn again */
}

static void fm1_main(void)
{
    int32_t knob = 512 * 16;
    lcd_init();
    theme_set(THEME_TAG | THEME_OBS);
    lcd_fill(0, 0, 240, 240, C_BG);
    draw_text_box(0, 100, 240, &FONT_L, "OBSSYNTH", C_INK, 1);
    draw_text_box(0, 140, 240, &FONT_S, SYNTH_VERSION " " SYNTH_BUILD, C_INK, 1);   /* (the GLO page: INFO) */
    fm1_input_init();
    fm1_adc_init();
    synth_init();
    flash_boot();                                        /* synth.c: the flash's id */
    if (flash_ok)
        ota_boot_cleanup();                              /* the staging area an update left */
    presets_boot();
    settings_boot();                                     /* GLO's settings (presets.c) */
    ui_init();
    audio_init();
    usb_start();
    fm1_timer5_start(isr_timer5, 4);                     /* above ALNK0 (3): the scan nests into the render */
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    fm1_delay_ms(400);
    for (;;) {
        uint32_t m = fm1_ms;
        fm1_wdt_feed();
        if (fm1_ms > 30000u && bootguard.pending) {      /* a crash or hang in the first 30 s counts */
            bootguard.pending = 0;
            bootguard.failed = 0;
        }
        usb_retry(fm1_ms);                               /* (usb.c: the cable in after boot) */
        master_read(&knob);
        uboot_poll();
        update_poll();
        ui_input();
        ui_leds();
        ui_draw();
        while (fm1_ms - m < 15u) {                       /* ~60 UI frames a second at most */
            ui_input();
            info_service();
            ota_service();                               /* the updater's requests without waiting a frame */
        }
    }
}

void fm1_cstart(void)
{
    uint32_t *s, *d, p3, src, wdt;
    fm1_time_init();
    fm1_reset_reason();
    p3 = fm1_boot.p3_rst;
    src = fm1_boot.rst_src;
    wdt = fm1_boot.wdt_con;
    fm1_wdt_arm(0x0D);
    if (bootguard.magic != BOOTGUARD_MAGIC) {
        bootguard.magic = BOOTGUARD_MAGIC;
        bootguard.failed = 0;
        bootguard.pending = 0;
    }
    if (bootguard.pending)
        bootguard.failed++;
    bootguard.pending = 1;
    if (bootguard.failed >= 2u) {
        bootguard.failed = 0;
        bootguard.pending = 0;
        fm1_enter_uboot();
    }
    fm1_irq_init();
    for (d = _bss_start; d < _bss_end; d++)
        *d = 0;
    for (d = _pool_start; d < _pool_end; d++)
        *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++)
        *d = *s;
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++)
        *d = *s;                                         /* code that must run from RAM */
    fm1_mailbox_clear();
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    fm1_boot.p3_rst = (uint8_t)p3;
    fm1_boot.rst_src = src;
    fm1_boot.wdt_con = (uint8_t)wdt;
    fm1_main();
    for (;;)
        ;
}
