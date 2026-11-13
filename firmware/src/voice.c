/* SPDX-License-Identifier: GPL-3.0-only
 * ObsSynth FM-1: 2026 Orian Beltrame
 * The trapezoidal SVF's fixed-point form after Hortator's filter.c (2026 DEADACTIVE). */
/* One synth voice: OSC 1 (PW on SQR), OSC 2 (hard SYNC to OSC 1, RING with it) mixed, a SUB square an octave under
 * OSC 1 and NOISE added, DRIVE (a tanh), then the filter: Simper's trapezoidal SVF, LP BP HP or NOTCH, 12 dB or two
 * stages for 24, its band state saturated (a tanh, 4 x a full oscillator of headroom: resonance swells and squashes
 * as it is driven, as ObsSynth4D's SvfFilter); then the amp. Amp and filter each have an ADSR that can LOOP while the key is held. What changes per
 * CTL block (pitches, mix, levels, cutoff, gain: knobs, envelopes, LFOs) comes in a vblock_t (engine.c); the
 * envelopes and the oscillators run per sample.
 * Envelope laws (the tables of tools/gen_tables.py): attack a curve, rising toward 1.3 and stopped at 1 (ATK_K,
 * 1 ms .. ~4 s: an analog envelope's rounded rise, not a line), decay toward SUS and
 * release exponential (DECAY_K: 5 ms .. 4 s to -60 dB). A note retriggered while it sounds (a legato steal) resumes
 * its attack from where the level is: no click. LOOP: the decay reaching SUS starts the attack again. */
#include "synth_wavetables.h"                           /* tools/gen_wavetables.py: the WT banks, band-limited */
#include "synth_samples.h"                              /* tools/gen_samples.py: GRAIN's samples (22.05 kHz) */
enum { W_SAW, W_SQR, W_TRI, W_SIN, W_RAMP, W_FOLD, W_FM, W_OCT, W_N };   /* STD's waves (PW: their SHAPE) */
enum { NC_WHITE, NC_PINK, NC_BROWN, NC_VIOL, NC_DIGI, NC_CRACK, NC_N };   /* NOISE's colours */
enum { OE_STD, OE_SUPER, OE_WT, OE_GR, OE_N };           /* an oscillator's engine (ALGORITHM) */
enum { ES_IDLE, ES_ATK, ES_DEC, ES_REL };
enum { FT_LP, FT_BP, FT_HP, FT_NOTCH, FT_N };

typedef struct {
    int32_t lvl, sus;            /* the level and SUS (Q24) */
    uint32_t atk, dk, rk;        /* attack coefficient per sample (Q24); decay, release factors per sample (Q16) */
    uint8_t st;                  /* ES_ */
    uint8_t loop;                /* LOOP: decay to SUS -> attack again, while the gate is open */
} env_t;

typedef struct { int32_t a1, a2, a3; } svf_t;     /* Q24 */

#define NGR 6u                                           /* GRAIN: the grains an oscillator plays at once, a voice */
typedef struct {                 /* GRAIN: an oscillator's grains in a voice */
    uint32_t p[NGR];             /* each one's read position in the sample, Q12 */
    uint32_t w[NGR];             /* .. its window's phase (0: a free grain) */
    uint32_t winc[NGR];          /* .. its window's step a sample (its SIZE when it started) */
    uint32_t cnt, rng;           /* samples to the next grain; SPRAY's random */
    uint32_t ph;                 /* the root's phase at the note's pitch, Q12 samples (0 .. gper): grains start in
                                  * step with it, so they add up, not cancel */
} gr_t;

typedef struct {
    uint8_t note, gate, active;  /* MIDI note; the key held; sounding */
    uint8_t vel;                 /* 1..127 */
    uint32_t age;                /* note-on order (voice stealing) */
    uint32_t ph, ph2, phs;       /* the oscillators' phases: OSC 1, OSC 2, the SUB */
    uint32_t sph[2][6];          /* SUPER: each oscillator's six side saws */
    gr_t gr[2];                  /* GRAIN: each oscillator's grains */
    uint32_t rng;                /* NOISE */
    env_t amp, flt;
    int32_t s0, s1, s2, s3;      /* SVF states (x 8, FLT_IN): the stage, the 24 dB stage */
    int32_t nz[3], nprev;        /* NOISE: its colour's filter states, the last white sample (VIOL) or held (DIGI) */
    uint32_t ncnt;               /* .. DIGI: samples left to hold */
} voice_t;

typedef struct {                 /* an oscillator's block (engine.c osc_setup) */
    int32_t eng;                 /* OE_ */
    uint32_t inc;                /* its increment (SUPER: the centre saw's) */
    int32_t wave;                /* STD: W_ */
    uint32_t pw;                 /* STD SQR's falling edge (phase), 0x80000000 = 50 % */
    uint32_t sinc[6];            /* SUPER: the side saws' increments */
    int32_t center, side;        /* .. the centre's level, each side's, Q15 */
    const int16_t *wa, *wb;      /* WT: the two waves POS lies between, at the level for the pitch */
    uint32_t wshift, wmask;      /* .. phase -> index, the level's length - 1 */
    int32_t wfrac;               /* .. POS between them, Q15 */
    const int16_t *gs;           /* GRAIN: the sample */
    uint32_t glen;               /* .. its length (samples) */
    uint32_t ginc;               /* .. a grain's read step a sample, Q12 (the note's pitch, the sample at C4) */
    uint32_t gwinc;              /* .. a grain's window step a sample (SIZE) */
    uint32_t gpos, gspray;       /* .. where grains start (POS), Q12, and how far either side at random (SPRAY) */
    uint32_t gevery;             /* .. samples between grains (DENS) */
    uint32_t gper;               /* .. the root's period in the sample, Q12 */
    int32_t ggain;               /* .. the sum's gain, Q15 (the grains overlapping: as loud as one) */
} oscb_t;

typedef struct {                 /* a voice's block (engine.c synth_block) */
    oscb_t o[2];                 /* OSC 1, OSC 2 */
    int32_t m1, m2;              /* OSC 1's and OSC 2's levels, Q15: MIX's equal-power law (engine.c) */
    int32_t sub, noise, ring;    /* levels, Q15 (ring: OSC 2 -> OSC 1 x OSC 2) */
    int32_t ncol;                /* NOISE's colour: NC_ */
    int32_t sync;                /* OSC 2 restarts with OSC 1's cycle */
    int32_t drive;               /* the gain into the tanh, Q12; 0 none */
    int32_t ftype, slope24;      /* FT_; the second stage */
    int32_t k, k1;               /* the SVF's damping, Q12 (HP, NOTCH); 24 dB: k1 the first stage's */
    svf_t c, c1;                 /* the filter (24 dB: its second stage; c1 the first) */
    int32_t gain;                /* Q15, <= 32767 */
    int32_t gl, gr;              /* left / right, Q15: the centre both 32767 (UNI spreads them) */
} vblock_t;

#define FLT_IN 3                                         /* the input x 8 inside the filter (precision) */
#define FLT_SMAX (1 << 28)                               /* state clamp: the 64-bit products never overflow */
#define ENV_LOOP_NEAR (1 << 18)                          /* LOOP: SUS reached within ~1.6 % */

#define ENV_ATK_TGT (ENV1 + ENV1 / 10 * 3)               /* the attack's aim: 1.3, stopped at 1 */
static uint32_t env_atk_step(int32_t p)                  /* ATK 0..127 -> the coefficient per sample: 1 ms .. ~4 s */
{
    return ATK_K[(uint32_t)clamp(p, 0, 127) * 115u / 127u];
}

/* the knobs A D S R (0..127; D and R may be shifted by key tracking) into e (at each note-on; a running envelope
 * keeps its level) */
static void env_set(env_t *e, int32_t a, int32_t d, int32_t s, int32_t r, int32_t loop)
{
    e->atk = env_atk_step(a);
    e->dk = DECAY_K[clamp(d, 0, 127)];
    e->sus = (int32_t)(((int64_t)clamp(s, 0, 127) << 24) / 127);
    e->rk = DECAY_K[clamp(r, 0, 127)];
    e->loop = (uint8_t)(loop && e->sus < ENV1 - ENV_LOOP_NEAR);   /* (SUS at the top: nothing to loop) */
}

static inline int32_t env_tick(env_t *e)
{
    switch (e->st) {
    case ES_ATK:
        e->lvl += (int32_t)(((int64_t)(ENV_ATK_TGT - e->lvl) * e->atk) >> 24) + 1;
        if (e->lvl >= ENV1) {
            e->lvl = ENV1;
            e->st = ES_DEC;
        }
        break;
    case ES_DEC:
        e->lvl = e->sus + mulq16(e->lvl - e->sus, e->dk);
        if (e->loop && e->lvl - e->sus < ENV_LOOP_NEAR)
            e->st = ES_ATK;                              /* LOOP: up again from here */
        else if (e->sus < ENV_END && e->lvl < ENV_END) { /* decayed to a SUS of 0: over */
            e->lvl = 0;
            e->st = ES_IDLE;
        }
        break;
    case ES_REL:
        e->lvl = mulq16(e->lvl, e->rk);
        if (e->lvl < ENV_END) {
            e->lvl = 0;
            e->st = ES_IDLE;
        }
        break;
    default:
        break;
    }
    return e->lvl;
}

/* the SVF's coefficients from a cutoff 0 .. 127 << 8 (50 Hz .. 18 kHz, SVF_G24) and a damping k (Q12) */
static void svf_coef(svf_t *c, int32_t cut, int32_t k)
{
    int32_t i;
    int64_t g;
    uint32_t den;
    cut = clamp(cut, 0, 127 << 8);
    i = cut >> 8;
    g = SVF_G24[i];
    if (i < 127)
        g += (((int64_t)SVF_G24[i + 1] - g) * (cut & 255)) >> 8;
    den = (uint32_t)((1 << 24) + ((g * (g + ((int64_t)k << 12))) >> 24));
    c->a1 = (int32_t)(((uint64_t)1 << 48) / den);
    c->a2 = (int32_t)(((int64_t)c->a1 * g) >> 24);
    c->a3 = (int32_t)(((int64_t)c->a2 * g) >> 24);
}

/* the band state's saturation: H tanh(x / H), H = 2 ^ 21 (4 x a full oscillator mix, the states being x 8), by
 * TANH_Q15 (tanh over 0 .. 2: past it, held) */
static inline int32_t svf_sat(int32_t x)
{
    uint32_t a = (uint32_t)(x < 0 ? -x : x) >> 6, j = a >> 8;
    int32_t y;
    if (j >= 256u)
        y = TANH_Q15[256] << 6;
    else
        y = (TANH_Q15[j] + (((TANH_Q15[j + 1u] - TANH_Q15[j]) * (int32_t)(a & 255u)) >> 8)) << 6;
    return x < 0 ? -y : y;
}

/* one SVF sample (x and the states x 8): the TYPE's output; *s0 / *s1 its states */
static inline int32_t svf_tick(const svf_t *c, int32_t k, int32_t type, int32_t x, int32_t *s0, int32_t *s1)
{
    int32_t v3 = x - *s1, v1, v2;
    v1 = (int32_t)(((int64_t)c->a1 * *s0 + (int64_t)c->a2 * v3 + (1 << 23)) >> 24);
    v2 = *s1 + (int32_t)(((int64_t)c->a2 * *s0 + (int64_t)c->a3 * v3 + (1 << 23)) >> 24);
    *s0 = svf_sat(clamp(2 * v1 - *s0, -FLT_SMAX, FLT_SMAX));
    *s1 = clamp(2 * v2 - *s1, -FLT_SMAX, FLT_SMAX);
    switch (type) {
    case FT_BP:
        return v1;
    case FT_HP:
        return x - (int32_t)(((int64_t)k * v1) >> 12) - v2;
    case FT_NOTCH:
        return x - (int32_t)(((int64_t)k * v1) >> 12);
    default:
        return v2;
    }
}

/* one sample of a STD wave at phase ph (Q15); pw, PW's phase (5 .. 94 %, 50 % at 64), is each wave's SHAPE: SQR its
 * width; TRI its peak (a saw at either end); FOLD how far a sine folds (sin(a sin), a 0.25 .. 2 turns); FM the index of
 * a sine on its own octave; OCT the saw an octave up's level. SAW, SQR, RAMP, OCT polyBLEP'd; FOLD and FM are not
 * band-limited (their highs alias up the keyboard, as an analog folder's would not) */
static inline int32_t osc_tick(uint32_t ph, uint32_t inc, int32_t wave, uint32_t pw)   /* Q15 */
{
    int32_t x, a = (int32_t)(pw >> 16) - 3277;          /* (SHAPE: 0 .. ~58300) */
    switch (wave) {
    case W_SQR:                                          /* high until pw, then low: an edge at 0 and at pw */
        x = (ph < pw ? 32767 : -32767) + blep(ph, inc) - blep(ph - pw, inc);
        break;
    case W_TRI:
        if (pw == 0x80000000u) {
            x = (int32_t)(ph >> 16) - 32768;             /* saw -1 .. +1, folded */
            x = 2 * (x < 0 ? -x : x) - 32767;
        } else {                                         /* up to the peak at pw, down after */
            uint32_t p = ph >> 16, k = pw >> 16;
            x = p < k ? (int32_t)(p * 65534u / k) - 32767 : 32767 - (int32_t)((p - k) * 65534u / (65536u - k));
        }
        break;
    case W_SIN:
        x = sine_i(ph);
        break;
    case W_RAMP:
        x = 32768 - (int32_t)(ph >> 16) + blep(ph, inc);
        break;
    case W_FOLD:
        x = sine_i((uint32_t)((int64_t)sine_i(ph) * (32768 + a * 4)));
        break;
    case W_FM:
        x = sine_i(ph + (uint32_t)((int64_t)sine_i(ph * 2u) * (a * 3)));
        break;
    case W_OCT: {
        int32_t m = clamp(a * 9 / 16, 0, 32767), s1 = (int32_t)(ph >> 16) - 32768 - blep(ph, inc);
        int32_t s2 = (int32_t)((ph * 2u) >> 16) - 32768 - blep(ph * 2u, inc * 2u);
        x = (s1 * (32767 - m / 3) + s2 * (m * 2 / 3)) >> 15;
        break;
    }
    default:
        x = (int32_t)(ph >> 16) - 32768 - blep(ph, inc);
        break;
    }
    return clamp(x, -32767, 32767);
}

/* NOISE: a white sample (rng's) in its colour, Q15-ish: PINK (Kellet's three poles), BROWN (a leaky sum), VIOL (the
 * white's difference: bright), DIGI (held 11 samples, 16 levels: crunch), CRACK (sparse clicks, each decaying fast) */
static inline int32_t noise_tick(voice_t *v, int32_t w, int32_t col)
{
    int32_t y;
    switch (col) {
    case NC_PINK:
        v->nz[0] = (int32_t)(((int64_t)v->nz[0] * 32690 + (int64_t)w * 3245) >> 15);
        v->nz[1] = (int32_t)(((int64_t)v->nz[1] * 31556 + (int64_t)w * 9716) >> 15);
        v->nz[2] = (int32_t)(((int64_t)v->nz[2] * 18678 + (int64_t)w * 34494) >> 15);
        return clamp((v->nz[0] + v->nz[1] + v->nz[2] + ((w * 6056) >> 15)) * 3 / 10, -65534, 65534);
    case NC_BROWN:
        v->nz[0] += (w >> 4) - (v->nz[0] >> 7);
        return clamp(v->nz[0] * 2, -32767, 32767);
    case NC_VIOL:
        y = (w - v->nprev) * 7 / 10;                     /* (as loud as the white) */
        v->nprev = w;
        return y;
    case NC_DIGI:
        if (!v->ncnt) {
            v->ncnt = 11u;
            v->nprev = (w >> 12) << 12;
        }
        v->ncnt--;
        return v->nprev;
    case NC_CRACK:
        if (((uint32_t)w & 0x3FFu) == 0x155u)            /* (~40 a second, each its own size) */
            v->nz[0] = (w & 0x400) ? 32767 - (w & 0x3800) : (w & 0x3800) - 32767;
        y = v->nz[0];
        v->nz[0] -= v->nz[0] >> 4;
        return y;
    default:
        return w;
    }
}

static inline int32_t saw_tick(uint32_t ph, uint32_t inc) { return (int32_t)(ph >> 16) - 32768 - blep(ph, inc); }

/* an oscillator's sample at phase ph (Q15): STD's wave; SUPER's centre saw and six side saws (sph, advanced here);
 * WT's two waves read at the phase (linear between the level's points) and between them at POS */
/* GRAIN: a sample of o's grains in g: a grain starts every gevery samples (in a free slot, else over the oldest) at
 * POS, in step with the root's period (a grain starting later starts that much further on in the cycle: the
 * grains overlapping play the same phase; started all at POS they would comb, cancelling at some pitches), +-
 * SPRAY at random; each reads the sample at the note's pitch under a Hann window SIZE long, and ends there (or at the
 * sample's end) */
static int32_t grain_val(const oscb_t *o, gr_t *g)
{
    uint32_t k, last = o->glen - 1u;
    int32_t s = 0;
    g->ph += o->ginc;
    if (g->ph >= o->gper)
        g->ph = o->ginc < o->gper ? g->ph - o->gper : g->ph % o->gper;
    if (!g->cnt) {
        uint32_t f = 0, p = o->gpos + g->ph;
        for (k = 1; k < NGR; k++)                        /* a free slot, else the oldest (the widest window phase) */
            if (g->w[f] && (!g->w[k] || g->w[k] > g->w[f]))
                f = k;
        if (o->gspray) {
            g->rng = g->rng * 1664525u + 1013904223u;
            p += (uint32_t)(((uint64_t)g->rng * (o->gspray * 2u + 1u)) >> 32) - o->gspray;
            if ((int32_t)p < 0)
                p = 0;
        }
        g->p[f] = p < (last << 12) ? p : (last - 1u) << 12;
        g->w[f] = 1;
        g->winc[f] = o->gwinc;
        g->cnt = o->gevery;
    } else {
        g->cnt--;
    }
    for (k = 0; k < NGR; k++) {
        uint32_t w = g->w[k], i = g->p[k] >> 12, wi, nw;
        int32_t a, h;
        if (!w)
            continue;
        if (i >= last) {                                 /* (the sample's end: the grain stops) */
            g->w[k] = 0;
            continue;
        }
        a = o->gs[i] + (((o->gs[i + 1u] - o->gs[i]) * (int32_t)(g->p[k] & 4095u)) >> 12);
        wi = w >> 24;
        h = HANN_Q15[wi] + (((HANN_Q15[wi + 1u] - HANN_Q15[wi]) * (int32_t)((w >> 16) & 255u)) >> 8);
        s += (a * h) >> 15;
        g->p[k] += o->ginc;
        nw = w + g->winc[k];
        g->w[k] = nw < w ? 0u : nw;                      /* (the window's end: the grain is over) */
    }
    return (int32_t)(((int64_t)s * o->ggain) >> 15);
}

static inline int32_t osc_val(const oscb_t *o, uint32_t ph, uint32_t *sph, gr_t *g)
{
    int32_t x;
    uint32_t k;
    if (o->eng == OE_GR) {
        x = grain_val(o, g);
    } else if (o->eng == OE_SUPER) {
        int32_t s = 0;
        for (k = 0; k < 6u; k++) {
            s += saw_tick(sph[k], o->sinc[k]);
            sph[k] += o->sinc[k];
        }
        x = (int32_t)(((int64_t)saw_tick(ph, o->inc) * o->center + (int64_t)s * o->side) >> 15);
        return clamp(x, -65534, 65534);                  /* (seven saws: as loud as one, peaks up to x2) */
    } else if (o->eng == OE_WT) {
        uint32_t i = ph >> o->wshift, j = (i + 1u) & o->wmask, f = (ph >> (o->wshift - 15u)) & 0x7FFFu;
        int32_t a0 = o->wa[i], a1 = o->wa[j], b0 = o->wb[i], b1 = o->wb[j];
        int32_t sa = a0 + (((a1 - a0) * (int32_t)f) >> 15), sb = b0 + (((b1 - b0) * (int32_t)f) >> 15);
        x = sa + (((sb - sa) * o->wfrac) >> 15);
    } else {
        x = osc_tick(ph, o->inc, o->wave, o->pw);
    }
    return clamp(x, -32767, 32767);
}

/* n samples of v added to outl / outr (Q15), its block b */
static void voice_render(voice_t *v, int32_t *outl, int32_t *outr, uint32_t n, const vblock_t *b)
{
    uint32_t i, ph = v->ph, ph2 = v->ph2, phs = v->phs, rng = v->rng, inc1 = b->o[0].inc, inc2 = b->o[1].inc,
             incs = inc1 >> 1;
    int32_t s0 = v->s0, s1 = v->s1, s2 = v->s2, s3 = v->s3, m1 = b->m1, m2 = b->m2;
    for (i = 0; i < n; i++) {
        int32_t x1, x2, x, y, a;
        x1 = osc_val(&b->o[0], ph, v->sph[0], &v->gr[0]);
        x2 = osc_val(&b->o[1], ph2, v->sph[1], &v->gr[1]);
        if (b->ring)                                     /* RING: OSC 2 toward OSC 1 x OSC 2 */
            x2 += (int32_t)(((((int64_t)x1 * x2) >> 15) - x2) * b->ring >> 15);
        x = (int32_t)(((int64_t)x1 * m1 + (int64_t)x2 * m2) >> 15);   /* (equal power; SUPER up to x2: 64-bit) */
        if (b->sub)                                      /* SUB: a square an octave under OSC 1 */
            x += (osc_tick(phs, incs, W_SQR, 0x80000000u) * b->sub) >> 15;
        if (b->noise) {
            rng = rng * 1664525u + 1013904223u;
            x += (noise_tick(v, (int32_t)(rng >> 16) - 32768, b->ncol) * b->noise) >> 15;
        }
        if (b->drive)                                    /* DRIVE: gain into a tanh */
            x = soft_clip((int32_t)(((int64_t)x * b->drive) >> 12));
        x = clamp(x, -65534, 65534) * (1 << FLT_IN);
        if (b->sync && ph + inc1 < ph)
            ph2 = 0;                                     /* SYNC: OSC 1 wraps, OSC 2 starts again */
        else
            ph2 += inc2;
        ph += inc1;
        phs += incs;
        if (b->slope24) {                                /* 24 dB: a Butterworth pair, the resonance in the second */
            y = svf_tick(&b->c1, b->k1, b->ftype, x, &s0, &s1);
            y = svf_tick(&b->c, b->k, b->ftype, clamp(y, -FLT_SMAX / 2, FLT_SMAX / 2), &s2, &s3);
        } else {
            y = svf_tick(&b->c, b->k, b->ftype, x, &s0, &s1);
        }
        a = env_tick(&v->amp);
        y = clamp(y >> FLT_IN, -98301, 98301);           /* back to Q15 (resonance: up to ~3x) */
        {
            int32_t o = (int32_t)(((int64_t)mulq24(y, a) * b->gain) >> 15);
            outl[i] += (int32_t)(((int64_t)o * b->gl) >> 15);   /* (|o| up to ~98301: 64-bit products) */
            outr[i] += (int32_t)(((int64_t)o * b->gr) >> 15);
        }
    }
    v->ph = ph;
    v->ph2 = ph2;
    v->phs = phs;
    v->rng = rng;
    v->s0 = s0;
    v->s1 = s1;
    v->s2 = s2;
    v->s3 = s3;
    if (v->amp.st == ES_IDLE) {                         /* (the filter envelope may still run: the amp decides) */
        v->active = 0;
        v->s0 = v->s1 = v->s2 = v->s3 = 0;
    }
}
