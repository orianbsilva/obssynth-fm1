/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * ObsSynth FM-1: 2026 Orian Beltrame */
/* I2S output (ALNK0 -> the codec, hal/fm1_audio.h) and the audio ISR: each half buffer is rendered in blocks of CTL
 * samples (engine.c synth_block, then fx.c fx_block: left and right, Q15), written as 24-bit stereo. The load of the last halves is kept for the
 * screen. */
#define HALF_FRAMES 256u                                 /* I2S half buffer: 5.8 ms at 44.1 kHz */
#define HALF_WORDS (HALF_FRAMES * 2u)
#define OUT_SHIFT 8                                      /* Q15 -> 24-bit (the sum is soft clipped: engine.c) */

static int32_t abuf[2u * HALF_WORDS] __attribute__((aligned(4)));
static volatile uint32_t in_audio;                       /* inside the ISR: TIMER5 nests (main.c) */
static volatile uint32_t t5_nested_ticks;                /* TIMER4 ticks TIMER5 spent nested in this ISR */
static volatile uint32_t cpu_q8;                         /* the render's share of a half, 1/256, smoothed */

void fm1_alnk0_irq(void)                                 /* via isr_alnk0 (hal/fm1_isr.S) */
{
    uint8_t p;
    uint32_t t0;
    in_audio = 1;
    p = fm1_audio_pending();
    t0 = fm1_ticks();
    t5_nested_ticks = 0;
    fm1_audio_ack_aux(p);
    if (p & FM1_AUDIO_HALF) {
        int32_t *o = &abuf[fm1_audio_free_half() * HALF_WORDS], m[CTL], mr[CTL];
        uint32_t b, i, us;
        for (b = 0; b < HALF_FRAMES; b += CTL) {
            midi_block();                                /* midi.c: MIDI in, before the voices */
            synth_block(m, mr, CTL);
            fx_block(m, mr, CTL);
            for (i = 0; i < CTL; i++) {
                o[2u * (b + i)] = m[i] * (1 << OUT_SHIFT);     /* left */
                o[2u * (b + i) + 1u] = mr[i] * (1 << OUT_SHIFT);   /* right */
            }
        }
        fm1_audio_ack_half();
        us = (fm1_ticks() - t0 - t5_nested_ticks) / FM1_TICKS_PER_US;
        cpu_q8 = (cpu_q8 * 15u + us * 256u / (HALF_FRAMES * 1000000u / FS)) / 16u;
    }
    in_audio = 0;
}
extern void isr_alnk0(void);

static void audio_init(void)
{
    uint32_t i;
    for (i = 0; i < 2u * HALF_WORDS; i++)
        abuf[i] = 0;
    fm1_audio_init(abuf, HALF_WORDS, isr_alnk0, 3);
}
