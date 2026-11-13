/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE; ObsSynth FM-1: 2026 Orian Beltrame */
/* The effects after the voices (Hortator's fx.c, its delay and its ROOM reverb, mono): the dry sum feeds a delay
 * (a low-pass in its feedback: COLR) and a reverb (4 damped combs + 2 allpasses, Freeverb-like), which can also take
 * the delay's echoes (REVERB's DLY); out = dry + the echoes x DELAY's MIX + the reverb x REVERB's MIX, soft clipped.
 * Stereo: the voices come left / right (UNI spreads them); the CHORUS reads a short line of their mono sum at two
 * points a slow LFO swings, left and right WIDTH apart (0: the same point, mono .. 127: half a cycle), and adds
 * them at MIX; the delay and the reverb take the mono sum and return to both sides.
 * DELAY +: HP cuts the echoes' lows (in the feedback too). REVERB +: PRE, a pre-delay (0 .. 100 ms); TONE, a
 * low-pass on its output. The lines are 16-bit, in the pool (no .bss). With both MIXes at 0 the lines still run (their tails ready when a
 * MIX comes up). */
#define DLY_LEN 65536u                                   /* 1.49 s */
static int16_t dly_buf[DLY_LEN] __attribute__((section(".pool")));
static const uint16_t REV_COMB[4] = {1116, 1188, 1277, 1356};
static const uint16_t REV_AP[2] = {556, 441};
static int16_t rev_comb[1116 + 1188 + 1277 + 1356] __attribute__((section(".pool")));
static int16_t rev_ap[556 + 441] __attribute__((section(".pool")));
#define PRE_LEN 4410u                                    /* the reverb's pre-delay: 100 ms */
#define CHO_LEN 2048u                                    /* the chorus's line: 46 ms */
#define CHO_BASE (320u << 8)                             /* its centre, Q8 samples: 7.3 ms */
static int16_t cho_buf[CHO_LEN] __attribute__((section(".pool")));
static int16_t rev_pre[PRE_LEN] __attribute__((section(".pool")));
static struct {
    uint32_t dly_w, pre_w, cho_w, cho_ph;
    int32_t dly_lp, dly_hlp, rev_lp;
    uint16_t comb_i[4], ap_i[2];
    int32_t comb_lp[4];
} fx;

static inline int32_t sh_tz(int32_t m, uint32_t sh) { return (m + ((m >> 31) & ((1 << sh) - 1))) >> sh; }   /* m >>
                                                          * sh rounded towards zero: tails end in exact silence */

static void fx_init(void)
{
    uint32_t i;
    for (i = 0; i < DLY_LEN; i++)
        dly_buf[i] = 0;
    for (i = 0; i < sizeof rev_comb / sizeof rev_comb[0]; i++)
        rev_comb[i] = 0;
    for (i = 0; i < sizeof rev_ap / sizeof rev_ap[0]; i++)
        rev_ap[i] = 0;
    for (i = 0; i < PRE_LEN; i++)
        rev_pre[i] = 0;
    for (i = 0; i < CHO_LEN; i++)
        cho_buf[i] = 0;
    memset(&fx, 0, sizeof fx);
}

/* TIME 0..127 -> the delay in samples: 10 ms .. 1.47 s, square law (fine at the short end) */
static uint32_t dly_samples(int32_t t)
{
    uint32_t k = (uint32_t)clamp(t, 0, 127);
    return 441u + k * k * 4u;
}

/* ROOM's comb k (its line at c, REV_COMB[k] long): a sample in, its output out */
static inline int32_t room_comb(int16_t *c, uint32_t k, int32_t in, int32_t size, int32_t damp)
{
    uint32_t j = fx.comb_i[k];
    int32_t o = c[j], l = o + sh_tz((fx.comb_lp[k] - o) * (32767 - damp), 15);
    fx.comb_lp[k] = l;
    c[j] = (int16_t)clamp(in + sh_tz(l * size, 15), -32768, 32767);
    fx.comb_i[k] = (uint16_t)(j + 1u >= REV_COMB[k] ? 0u : j + 1u);
    return o;
}

/* the chorus's line read d (Q8 samples) behind the write point, interpolated; x 2 (it keeps half) */
static inline int32_t cho_read(uint32_t d)
{
    uint32_t pos = (fx.cho_w << 8) - d, i0 = pos >> 8, fr = pos & 255u;
    int32_t s0 = cho_buf[i0 & (CHO_LEN - 1u)], s1 = cho_buf[(i0 + 1u) & (CHO_LEN - 1u)];
    return (s0 + (((s1 - s0) * (int32_t)fr) >> 8)) * 2;
}

/* one block: outl / outr (the voices x VOL x MASTER, Q15, may exceed it) -> + chorus + delay + reverb -> soft clipped */
static void fx_block(int32_t *outl, int32_t *outr, uint32_t n)
{
    uint32_t i, k, dl = dly_samples(par[P_DTIME]);
    int32_t fb = par[P_DFDBK] * 230, col = 2000 + par[P_DCOLR] * 240, dmix = par[P_DMIX] * 258;
    int32_t size = 25000 + par[P_RSIZE] * 50, damp = 32767 - par[P_RDAMP] * 200;
    int32_t d2r = par[P_RDLY] * 258, rmix = par[P_RMIX] * 258;
    int32_t hk = par[P_DHP] * 40, tk = 2000 + par[P_RTONE] * 240;   /* HP: ~0 .. 1 kHz; TONE: dark .. open */
    uint32_t pre = (uint32_t)par[P_RPRE] * (PRE_LEN - 1u) / 127u;
    int32_t cmix = par[P_CMIX] * 258, swing = par[P_CDEPTH] * 2 * 256;   /* the chorus: its level, its swing (Q8) */
    uint32_t cinc = LFO_INC[(uint32_t)clamp(par[P_CRATE], 0, 127) * 90u / 127u] / CTL;   /* 0.05 .. ~5 Hz */
    uint32_t woff = (uint32_t)clamp(par[P_CWIDTH], 0, 127) * (0x80000000u / 127u);       /* right's phase */
    for (i = 0; i < n; i++) {
        int32_t l = clamp(outl[i], -65534, 65534), r = clamp(outr[i], -65534, 65534), dry = (l + r) / 2, x, e, a,
                in;
        int16_t *c = rev_comb;
        /* the delay: its line read TIME back, the feedback low-passed (COLR), written at half (headroom) */
        x = dly_buf[(fx.dly_w - dl) & (DLY_LEN - 1u)];
        if (hk) {                                        /* HP: the echo minus its low-passed self */
            fx.dly_hlp += mulq15(x - fx.dly_hlp, hk);
            x -= fx.dly_hlp;
        }
        fx.dly_lp += mulq15(x - fx.dly_lp, col);
        dly_buf[fx.dly_w & (DLY_LEN - 1u)] = (int16_t)clamp((dry >> 1) + mulq15(fx.dly_lp, fb), -32768, 32767);
        fx.dly_w++;
        e = x << 1;                                      /* the echo, back to full */
        /* the reverb: the dry sum and DLY x the echoes, 1/8 at -4 dB into the combs */
        in = mulq15(clamp(dry + mulq15(e, d2r), -65534, 65534), 2580);
        if (pre) {                                       /* PRE: the input through the pre-delay line */
            rev_pre[fx.pre_w] = (int16_t)clamp(in, -32768, 32767);
            in = rev_pre[(fx.pre_w + PRE_LEN - pre) % PRE_LEN];
            fx.pre_w = fx.pre_w + 1u >= PRE_LEN ? 0u : fx.pre_w + 1u;
        }
        a = room_comb(c, 0, in, size, damp) + room_comb(c + 1116, 1, in, size, damp) +
            room_comb(c + 1116 + 1188, 2, in, size, damp) + room_comb(c + 1116 + 1188 + 1277, 3, in, size, damp);
        c = rev_ap;
        for (k = 0; k < 2u; k++) {
            uint32_t j = fx.ap_i[k];
            int32_t o = c[j];
            c[j] = (int16_t)clamp(a + sh_tz(o, 1), -32768, 32767);
            a = o - a;                                   /* Freeverb: out = buf - in */
            fx.ap_i[k] = (uint16_t)(j + 1u >= REV_AP[k] ? 0u : j + 1u);
            c += REV_AP[k];
        }
        a = clamp(a, -65534, 65534);                     /* (x MIX stays in 32 bits) */
        fx.rev_lp += (int32_t)(((int64_t)(a - fx.rev_lp) * tk) >> 15);   /* TONE (64-bit: the swing is +-2 x 65534) */
        a = fx.rev_lp;
        if (cmix) {                                      /* CHORUS: two swinging taps of the line */
            int32_t wl = cho_read(CHO_BASE + (uint32_t)((sine_i(fx.cho_ph) * swing) >> 15)),
                    wr = cho_read(CHO_BASE + (uint32_t)((sine_i(fx.cho_ph + woff) * swing) >> 15));
            l += mulq15(clamp(wl, -65534, 65534), cmix);
            r += mulq15(clamp(wr, -65534, 65534), cmix);
        }
        cho_buf[fx.cho_w & (CHO_LEN - 1u)] = (int16_t)clamp(dry >> 1, -32768, 32767);   /* (written even at MIX 0:
                                                                                          * ready when it comes up) */
        fx.cho_w++;
        fx.cho_ph += cinc;
        e = mulq15(e, dmix) + mulq15(a, rmix);           /* the delay and the reverb: both sides */
        outl[i] = soft_clip(l + e);
        outr[i] = soft_clip(r + e);
    }
}
