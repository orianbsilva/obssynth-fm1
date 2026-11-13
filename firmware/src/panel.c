/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * ObsSynth FM-1: 2026 Orian Beltrame */
/* The physical panel: which matrix button / encoder carries which printed label (the FM-1's measured table; the
 * calibration screen of the original firmware is not here yet). */
enum { B_FX, B_SCL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC, B_OCTDN, B_OCTUP, NB };
enum { EN_SELECT, EN_ALGO, EN_PRESET, EN_K1, EN_K2, EN_K3, EN_K4, NE };

typedef struct {
    uint8_t btn[NB];             /* matrix button id (0..13) per label */
    uint8_t enc[NE];             /* matrix encoder (0..6) per role */
    int8_t dir[NE];              /* +1 / -1 so that clockwise is + */
} panel_t;

static const panel_t panel = {
    {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0, 1},     /* PLAY = 12, REC = 13, OCT- = 0, OCT+ = 1 */
    {0, 1, 6, 2, 3, 4, 5},                               /* SELECT = enc 0, ALGORITHM = enc 1, PRESETS = enc 6 */
    {1, 1, 1, 1, 1, 1, 1},
};

static uint32_t panel_btn_bit(uint32_t label) { return 1u << panel.btn[label]; }   /* in fm1_in.buttons / edges */
static void panel_led(uint32_t label, int on) { fm1_led_key(panel.btn[label], on); }
static int32_t panel_enc(uint32_t role) { return enc_take(panel.enc[role]) * panel.dir[role]; }   /* + = clockwise */
