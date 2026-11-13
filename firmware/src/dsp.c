/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * ObsSynth FM-1: 2026 Orian Beltrame */
/* Fixed-point building blocks (the FM-1 has no FPU: no float reaches the firmware). Formats: audio Q15 (+-32767 full
 * scale), envelopes Q24 (ENV1 = 1.0), per-sample factors Q16, phases 2^32 per cycle. Tables: felucca_tables.h
 * (tools/gen_tables.py). */
#define ENV1 (1 << 24)
#define ENV_END (1 << 10)                                /* -84 dB: a released voice ends */

static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int32_t mulq15(int32_t a, int32_t b) { return (a * b) >> 15; }
/* a * k / 65536 without 64-bit: a up to 2^31, k Q16 */
static inline int32_t mulq16(int32_t a, uint32_t k)
{
    return (int32_t)(((a >> 16) * (int32_t)k) + (int32_t)(((uint32_t)(a & 0xFFFF) * k) >> 16));
}
static inline uint32_t qmin_u(uint32_t a, uint32_t b) { return a < b ? a : b; }
static inline int32_t mulq24(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 24); }

/* sine, linearly interpolated between the 1024 table points (Q15) */
static inline int32_t sine_i(uint32_t ph)
{
    uint32_t i = ph >> 22;
    int32_t a = SINE[i], b = SINE[(i + 1u) & 1023u];
    return a + (((b - a) * (int32_t)((ph >> 7) & 0x7FFFu)) >> 15);
}

/* polyBLEP residual (Q15) of a -2 step at the wrap of a phase accumulator (ph, inc: 2^32 per cycle) */
static inline int32_t blep(uint32_t ph, uint32_t inc)
{
    uint32_t d = inc >> 15;
    int32_t x;
    if (!d)
        return 0;
    if (ph < inc) {
        x = (int32_t)(ph / d);                           /* 0..32767 */
        return x + x - ((x * x) >> 15) - 32768;
    }
    if (ph > 0xFFFFFFFFu - inc) {
        x = -(int32_t)((0xFFFFFFFFu - ph) / d);          /* -32767..0 */
        return ((x * x) >> 15) + x + x + 32768;
    }
    return 0;
}

/* tanh(x) for x Q15 (+-2.0 and beyond: +-1), Q15: TANH_Q15 holds 0 .. 2.0 in 256 steps, interpolated */
static inline int32_t soft_clip(int32_t x)
{
    uint32_t a = (uint32_t)(x < 0 ? -x : x), i = a >> 8;
    int32_t y;
    if (i >= 256u)
        y = TANH_Q15[256];
    else
        y = TANH_Q15[i] + (((TANH_Q15[i + 1u] - TANH_Q15[i]) * (int32_t)(a & 255u)) >> 8);
    return x < 0 ? -y : y;
}

/* the phase increment of pitch p in 1/16 semitone (0 = MIDI 0), MIDI 0..127 */
static inline uint32_t pitch_inc(int32_t p) { return PITCH_INC[clamp(p, 0, 127 * 16)]; }
