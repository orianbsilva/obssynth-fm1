/* SPDX-License-Identifier: GPL-3.0-only
 * ObsSynth FM-1: 2026 Orian Beltrame */
/* The synth: NVOICE voices of voice.c sharing one set of parameters, two oscillators each, and two LFOs shared by
 * every voice, each with one destination (PITCH CUT RESO AMP MIX), run once per block. MODE: POLY (VOICES of them),
 * MONO (one voice; a held note under the newest comes back when it is let go, its envelopes restarted on each new
 * key), LEGATO (as MONO, the envelopes restarted only from silence) or UNI (every voice on the newest key, each
 * detuned by DETUN and placed apart in the stereo field, from random phases). The main loop (ui.c) sends notes through a
 * single-producer / single-consumer ring the audio ISR drains at each block, and writes parameters as plain 16-bit
 * stores the ISR reads once per block (a word store is atomic: no lock). Voice stealing: a free voice, else the
 * oldest released one, else the oldest.
 * Parameters 0 .. P_NPRESET - 1 are the sound (a preset keeps them); from P_NPRESET on the device's settings (the GLO
 * page; presets.c keeps them apart). New sound parameters go before P_NPRESET, after the last one: a user preset keeps
 * them by index (an older one loads the newer ones' defaults). */
#define NVOICE 4u
#define VOICE_GAIN 16384                                 /* per voice at velocity 127 (Q15: 0.5); the sum is soft
                                                          * clipped (fx.c) */
#define NHELD 8u                                         /* MONO / LEGATO: the keys held, newest last */
static volatile int32_t live_cut = 64 << 8;              /* the newest gated voice's cutoff (the FILTER page) */

enum {                           /* parameters (the pages: ui.c) */
    P_WAVE, P_TUNE, P_GLIDE, P_VOL,
    P_WAVE2, P_SEMI2, P_FINE2, P_MIX,
    P_AATK, P_ADEC, P_ASUS, P_AREL,
    P_CUT, P_RESO, P_FENV, P_FKEY,
    P_FATK, P_FDEC, P_FSUS, P_FREL,
    P_L1WAVE, P_L1RATE, P_L1DEST, P_L1DEPTH,
    P_L2WAVE, P_L2RATE, P_L2DEST, P_L2DEPTH,
    P_DTIME, P_DFDBK, P_DCOLR, P_DMIX,                   /* the effects (fx.c) */
    P_RSIZE, P_RDAMP, P_RDLY, P_RMIX,
    P_PW1, P_SUB, P_NOISE, P_AVEL,                       /* the + pages (HOME held) */
    P_PW2, P_SYNC, P_KEY2, P_RING,
    P_MODE, P_VOICES, P_AKEYT, P_ALOOP,
    P_FTYPE, P_DRIVE, P_FVEL, P_SLOPE,
    P_FEVEL, P_FKEYT, P_FLOOP,
    P_L1TRIG, P_L1FADE, P_L1PHASE,
    P_L2TRIG, P_L2FADE, P_L2PHASE,
    P_DHP,
    P_RPRE, P_RTONE,
    P_CRATE, P_CDEPTH, P_CWIDTH, P_CMIX,                 /* the chorus (fx.c), the FX button's third page */
    P_UDET,                                              /* MODE UNI: the stacked voices' spread (AMP ENV +) */
    P_ENG1, P_SDET1, P_SSIDE1, P_WPOS1, P_WTAB1,         /* OSC 1's engine (ALGORITHM) and its knobs */
    P_ENG2, P_SDET2, P_SSIDE2, P_WPOS2, P_WTAB2,         /* OSC 2's */
    P_GSMP1, P_GSIZE1, P_GDENS1, P_GSPRAY1,              /* OSC 1's GRAIN (its POS: WT's) */
    P_GSMP2, P_GSIZE2, P_GDENS2, P_GSPRAY2,              /* OSC 2's */
    P_NCOL,                                              /* NOISE's colour (OSC 1 +) */
    P_NPRESET,                   /* -- the sound above, the device's settings below (GLO) -- */
    P_MIDICH = P_NPRESET, P_GTUNE, P_VELC, P_BLIGHT,
    P_COLOR,                                             /* the screen's palette (gfx.c THEMES 6 ..) */
    P_COUNT
};
#define NLFO 2u
#define LFO_P(l, k) (P_L1WAVE + (l) * 4u + (k))       /* LFO l's knob k: WAVE RATE DEST DEPTH */
#define LFO_X(l, k) (P_L1TRIG + (l) * 3u + (k))        /* LFO l's + knob k: TRIG FADE PHASE */
#define OSC_P(n, k) (P_ENG1 + (n) * 5u + (k))          /* OSC n's engine knob k: ENG DETUN SIDE POS TABLE */
#define GR_P(n, k) (P_GSMP1 + (n) * 4u + (k))          /* OSC n's GRAIN knob k: SMPL SIZE DENS SPRAY */
enum { LW_SIN, LW_TRI, LW_SAW, LW_SQR, LW_SH, LW_N };
enum {                           /* the LFOs' destinations (both oscillators, then each one's) */
    LD_OFF, LD_PITCH, LD_CUT, LD_RESO, LD_AMP, LD_MIX, LD_POS,
    LD_PITCH1, LD_PITCH2, LD_SHAPE1, LD_SHAPE2, LD_POS1, LD_POS2, LD_DET1, LD_DET2, LD_NOISE, LD_N
};
enum { VM_POLY, VM_MONO, VM_LEGATO, VM_UNI, VM_N };     /* UNI: every voice on the newest key, spread */
#define MONO_LIKE(m) ((m) != VM_POLY)
enum { VC_LIN, VC_SOFT, VC_HARD, VC_FIX, VC_N };         /* MIDI's velocity curve (midi.c) */
enum { BL_DIM, BL_INV, BL_ON, BL_OFF, BL_N };            /* the panel's backlight (ui.c ui_leds) */

typedef struct {
    const char *label;           /* <= 5 chars */
    int16_t min, max, def;
    const char *const *names;    /* an enum's value names, else 0 */
} param_t;

static const char *const N_WAVE[W_N] = {"SAW", "SQR", "TRI", "SIN", "RAMP", "FOLD", "FM", "OCT"};
static const char *const N_NCOL[NC_N] = {"WHITE", "PINK", "BROWN", "VIOL", "DIGI", "CRACK"};
static const char *const N_LWAVE[LW_N] = {"SIN", "TRI", "SAW", "SQR", "S&H"};
static const char *const N_LDEST[LD_N] = {"OFF", "PITCH", "CUT", "RESO", "AMP", "MIX", "POS", "PIT1", "PIT2",
                                          "SHP1", "SHP2", "POS1", "POS2", "DET1", "DET2", "NOISE"};
static const char *const N_LDEST_LONG[LD_N] = {   /* (the LFO page's: what it moves, in full) */
    "OFF", "PITCH", "CUTOFF", "RESONANCE", "AMP", "OSC MIX", "WT/GRAIN POS", "OSC 1 PITCH", "OSC 2 PITCH",
    "OSC 1 SHAPE", "OSC 2 SHAPE", "OSC 1 POS", "OSC 2 POS", "OSC 1 DETUNE", "OSC 2 DETUNE", "NOISE"};
static const char *const N_ENG[OE_N] = {"STD", "SUPER", "WT", "GRAIN"};
static const char *const N_OFFON[2] = {"OFF", "ON"};
static const char *const N_KEY2[2] = {"FIX", "KEY"};
static const char *const N_MODE[VM_N] = {"POLY", "MONO", "LEGAT", "UNI"};
static const char *const N_COLOR[9] = {"OBS", "AMBER", "FOREST", "GREY", "LILAC", "OCEAN", "OLIVA", "SOLAR", "WINE"};
#define COLOR_THEME0 6u                                  /* P_COLOR 0 = gfx.c THEMES[6] (OBS), then 7 .. 14 */
static const char *const N_FTYPE[FT_N] = {"LP", "BP", "HP", "NOTCH"};
static const char *const N_SLOPE[2] = {"12DB", "24DB"};
static const char *const N_TRIG[2] = {"FREE", "NOTE"};
static const char *const N_MIDICH[17] = {"OMNI", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13",
                                         "14", "15", "16"};
static const char *const N_VELC[VC_N] = {"LIN", "SOFT", "HARD", "FIX"};
static const char *const N_BLIGHT[BL_N] = {"DIM", "INV", "ON", "OFF"};
static const param_t PARAMS[P_COUNT] = {
    [P_WAVE] = {"WAVE", 0, W_N - 1, W_SAW, N_WAVE},
    [P_TUNE] = {"TUNE", -24, 24, 0, 0},                  /* semitones */
    [P_GLIDE] = {"GLIDE", 0, 127, 0, 0},
    [P_VOL] = {"VOL", 0, 127, 100, 0},
    [P_WAVE2] = {"WAVE", 0, W_N - 1, W_SAW, N_WAVE},
    [P_SEMI2] = {"SEMI", -24, 24, 0, 0},                 /* OSC 2 re OSC 1, semitones */
    [P_FINE2] = {"FINE", -64, 63, 8, 0},                 /* +-64: about +-32 cents */
    [P_MIX] = {"MIX", 0, 127, 0, 0},                     /* OSC 2's share: 0 OSC 1 only (as before), 127 OSC 2 only */
    [P_AATK] = {"ATK", 0, 127, 0, 0},
    [P_ADEC] = {"DEC", 0, 127, 70, 0},
    [P_ASUS] = {"SUS", 0, 127, 100, 0},
    [P_AREL] = {"REL", 0, 127, 50, 0},
    [P_CUT] = {"CUT", 0, 127, 64, 0},
    [P_RESO] = {"RESO", 0, 127, 30, 0},
    [P_FENV] = {"ENV", -64, 63, 32, 0},                  /* the filter envelope's reach: +-64 cutoff units at full */
    [P_FKEY] = {"KEY", 0, 127, 64, 0},                   /* key tracking 0 .. 100 % (from C4) */
    [P_FATK] = {"ATK", 0, 127, 0, 0},
    [P_FDEC] = {"DEC", 0, 127, 60, 0},
    [P_FSUS] = {"SUS", 0, 127, 20, 0},
    [P_FREL] = {"REL", 0, 127, 50, 0},
    [P_L1WAVE] = {"WAVE", 0, LW_N - 1, LW_SIN, N_LWAVE},
    [P_L1RATE] = {"RATE", 0, 127, 70, 0},                /* 0.05 .. 40 Hz (LFO_INC) */
    [P_L1DEST] = {"DEST", 0, LD_N - 1, LD_OFF, N_LDEST},
    [P_L1DEPTH] = {"DEPTH", -64, 63, 0, 0},
    [P_L2WAVE] = {"WAVE", 0, LW_N - 1, LW_TRI, N_LWAVE},
    [P_L2RATE] = {"RATE", 0, 127, 40, 0},
    [P_L2DEST] = {"DEST", 0, LD_N - 1, LD_OFF, N_LDEST},
    [P_L2DEPTH] = {"DEPTH", -64, 63, 0, 0},
    [P_DTIME] = {"TIME", 0, 127, 70, 0},                 /* 10 ms .. 1.47 s (fx.c dly_samples): 70 ~ 0.45 s */
    [P_DFDBK] = {"FDBK", 0, 120, 60, 0},                 /* the repeats: 120 ~ 0.84 */
    [P_DCOLR] = {"COLR", 0, 127, 70, 0},                 /* the repeats' low-pass: 0 dark .. 127 open */
    [P_DMIX] = {"MIX", 0, 127, 0, 0},                    /* the echoes' level: 0 off (as before) */
    [P_RSIZE] = {"SIZE", 0, 127, 90, 0},
    [P_RDAMP] = {"DAMP", 0, 127, 60, 0},
    [P_RDLY] = {"DLY", 0, 127, 0, 0},                    /* the echoes into the reverb */
    [P_RMIX] = {"MIX", 0, 127, 0, 0},                    /* the reverb's level: 0 off (as before) */
    [P_PW1] = {"PW", 0, 127, 64, 0},                     /* SQR's width: 5 .. 95 %, 64 = 50 % */
    [P_SUB] = {"SUB", 0, 127, 0, 0},                     /* a square an octave under OSC 1 */
    [P_NOISE] = {"NOISE", 0, 127, 0, 0},
    [P_AVEL] = {"VEL", 0, 127, 127, 0},                  /* velocity -> level: 127 full (as before), 0 none */
    [P_PW2] = {"PW", 0, 127, 64, 0},
    [P_SYNC] = {"SYNC", 0, 1, 0, N_OFFON},               /* OSC 2 restarts with OSC 1's cycle */
    [P_KEY2] = {"KEY", 0, 1, 1, N_KEY2},                 /* FIX: OSC 2 at C4 + SEMI, whatever key */
    [P_RING] = {"RING", 0, 127, 0, 0},                   /* OSC 2 -> OSC 1 x OSC 2 */
    [P_MODE] = {"MODE", 0, VM_N - 1, VM_POLY, N_MODE},
    [P_VOICES] = {"VOICE", 1, NVOICE, NVOICE, 0},
    [P_AKEYT] = {"KEYT", 0, 127, 0, 0},                  /* higher notes, shorter DEC / REL: 127 halves them an octave */
    [P_ALOOP] = {"LOOP", 0, 1, 0, N_OFFON},
    [P_FTYPE] = {"TYPE", 0, FT_N - 1, FT_LP, N_FTYPE},
    [P_DRIVE] = {"DRIVE", 0, 127, 0, 0},
    [P_FVEL] = {"VEL", 0, 127, 0, 0},                    /* velocity -> cutoff: +-32 units at full */
    [P_SLOPE] = {"SLOPE", 0, 1, 0, N_SLOPE},
    [P_FEVEL] = {"VEL", 0, 127, 0, 0},                   /* velocity -> the filter envelope's reach */
    [P_FKEYT] = {"KEYT", 0, 127, 0, 0},
    [P_FLOOP] = {"LOOP", 0, 1, 0, N_OFFON},
    [P_L1TRIG] = {"TRIG", 0, 1, 0, N_TRIG},              /* NOTE: a note restarts it at PHASE */
    [P_L1FADE] = {"FADE", 0, 127, 0, 0},                 /* its depth fades in after a note from silence */
    [P_L1PHASE] = {"PHASE", 0, 127, 0, 0},
    [P_L2TRIG] = {"TRIG", 0, 1, 0, N_TRIG},
    [P_L2FADE] = {"FADE", 0, 127, 0, 0},
    [P_L2PHASE] = {"PHASE", 0, 127, 0, 0},
    [P_DHP] = {"HP", 0, 127, 0, 0},                      /* the echoes' low cut: 0 none */
    [P_RPRE] = {"PRE", 0, 127, 0, 0},                    /* the reverb's pre-delay: 0 .. 100 ms */
    [P_RTONE] = {"TONE", 0, 127, 127, 0},                /* the reverb's low-pass: 127 open (as before) */
    [P_CRATE] = {"RATE", 0, 127, 40, 0},                 /* 0.05 .. ~5 Hz */
    [P_CDEPTH] = {"DEPTH", 0, 127, 60, 0},               /* the delay's swing: 0 .. ~5.8 ms */
    [P_CWIDTH] = {"WIDTH", 0, 127, 127, 0},              /* left / right taps apart: 0 the same (mono) .. 180 deg */
    [P_CMIX] = {"MIX", 0, 127, 0, 0},                    /* 0 off (as before) */
    [P_UDET] = {"DETUN", 0, 127, 40, 0},                 /* MODE UNI: the outer voices +-25 cents at 127 */
    [P_ENG1] = {"ENG", 0, OE_N - 1, OE_STD, N_ENG},
    [P_SDET1] = {"DETUN", 0, 127, 50, 0},                /* SUPER: the outer saws +-60 cents at 127 */
    [P_SSIDE1] = {"SIDE", 0, 127, 80, 0},                /* SUPER: the side saws' level against the centre */
    [P_WPOS1] = {"POS", 0, 127, 0, 0},                   /* WT: the position in the table */
    [P_WTAB1] = {"TABLE", 0, WT_NBANK - 1, 0, WT_NAMES},   /* (the names: tools/gen_wavetables.py's BANKS) */
    [P_ENG2] = {"ENG", 0, OE_N - 1, OE_STD, N_ENG},
    [P_SDET2] = {"DETUN", 0, 127, 50, 0},
    [P_SSIDE2] = {"SIDE", 0, 127, 80, 0},
    [P_WPOS2] = {"POS", 0, 127, 0, 0},
    [P_WTAB2] = {"TABLE", 0, WT_NBANK - 1, 0, WT_NAMES},
    [P_GSMP1] = {"SMPL", 0, GR_NSMP - 1, 0, GR_NAMES},   /* GRAIN: the sample */
    [P_GSIZE1] = {"SIZE", 0, 127, 80, 0},                /* .. a grain's length: 10 .. 500 ms */
    [P_GDENS1] = {"DENS", 0, 127, 90, 0},                /* .. grains a second: 2 .. 85 */
    [P_GSPRAY1] = {"SPRAY", 0, 127, 20, 0},              /* .. where each starts: POS +- up to half the sample */
    [P_GSMP2] = {"SMPL", 0, GR_NSMP - 1, 0, GR_NAMES},
    [P_GSIZE2] = {"SIZE", 0, 127, 80, 0},
    [P_GDENS2] = {"DENS", 0, 127, 90, 0},
    [P_GSPRAY2] = {"SPRAY", 0, 127, 20, 0},
    [P_NCOL] = {"NCOL", 0, NC_N - 1, NC_WHITE, N_NCOL},
    [P_MIDICH] = {"CH", 0, 16, 0, N_MIDICH},             /* MIDI in: OMNI, or one channel */
    [P_GTUNE] = {"TUNE", -50, 50, 0, 0},                 /* the whole instrument, cents */
    [P_VELC] = {"VELC", 0, VC_N - 1, VC_LIN, N_VELC},
    [P_BLIGHT] = {"LIGHT", 0, BL_N - 1, BL_DIM, N_BLIGHT},
    [P_COLOR] = {"COLOR", 0, 8, 0, N_COLOR},
};

static int16_t par[P_COUNT];
static voice_t vc[NVOICE];
static uint32_t vc_age;
static volatile uint32_t master_q12 = 2048;             /* MASTER (main.c: the pot), 0 .. ~4096 */
static int32_t glide_p;                                  /* GLIDE: the pitch playing (1/16 semitone), mono glide */
static struct {
    uint32_t ph[NLFO], rng;                              /* phases, S&H's noise */
    int32_t out[NLFO];                                   /* outputs (Q15) */
    int32_t fade[NLFO];                                  /* FADE: the depth's share, Q24 */
} lfo;
static struct { uint8_t note[NHELD], vel[NHELD], n; } held;   /* MONO / LEGATO: the keys down, newest last */

#define EV_N 32u                                         /* note events: main loop -> audio ISR */
typedef struct { uint8_t on, note, vel; } ev_t;
static ev_t ev_ring[EV_N];
static volatile uint32_t ev_w, ev_r;

static void fx_init(void);                               /* fx.c */

static void synth_init(void)
{
    uint32_t i;
    for (i = 0; i < P_COUNT; i++)
        par[i] = PARAMS[i].def;
    for (i = 0; i < NVOICE; i++)
        vc[i].active = 0;
    ev_w = ev_r = 0;
    glide_p = 0;
    held.n = 0;
    for (i = 0; i < NLFO; i++) {
        lfo.ph[i] = 0;
        lfo.out[i] = 0;
        lfo.fade[i] = ENV1;
    }
    lfo.rng = 0x1234567u;
    fx_init();
}

static void synth_param(uint32_t id, int32_t v)          /* the main loop: a knob */
{
    if (id < P_COUNT)
        par[id] = (int16_t)clamp(v, PARAMS[id].min, PARAMS[id].max);
}

static void synth_note(uint32_t note, uint32_t vel, int on)   /* the main loop: a key (vel 0: off) */
{
    uint32_t w = ev_w;
    if (w - ev_r >= EV_N)
        return;                                          /* full: dropped (the ISR drains it every ~0.7 ms) */
    ev_ring[w % EV_N].on = (uint8_t)(on == 2 ? 2 : on != 0);   /* (2: every voice off, synth_all_off) */
    ev_ring[w % EV_N].note = (uint8_t)clamp((int32_t)note, 0, 127);
    ev_ring[w % EV_N].vel = (uint8_t)clamp((int32_t)vel, 1, 127);
    __asm__ volatile("" ::: "memory");                   /* the slot before the index */
    ev_w = w + 1u;
}

/* KEYT: a DEC / REL knob shifted for the note: 127 halves the time an octave up (DECAY_K: ~13 a doubling) */
static int32_t keyt(int32_t knob, uint32_t note, int32_t kt)
{
    return knob - ((int32_t)note - 60) * kt * 11 / (127 * 10);
}

static void voice_start(voice_t *v, uint32_t note, uint32_t vel, int retrig)
{
    uint32_t l, any = 0, i;
    for (i = 0; i < NVOICE; i++)
        any |= vc[i].active && vc[i].gate;
    if (!v->active) {                                    /* from silence: the envelopes and the filter from 0 */
        v->amp.lvl = v->flt.lvl = 0;
        v->s0 = v->s1 = v->s2 = v->s3 = 0;
        v->nz[0] = v->nz[1] = v->nz[2] = v->nprev = 0;
        v->ncnt = 0;
        v->ph = v->ph2 = v->phs = 0;
        v->rng = 0x9E3779B9u ^ (note << 8);
        if (par[P_MODE] == VM_UNI) {                     /* UNI: the stacked voices from random phases (in step,
                                                          * they would start as one, louder, and drift apart) */
            lfo.rng = lfo.rng * 1664525u + 1013904223u;
            v->ph = lfo.rng;
            v->ph2 = lfo.rng ^ 0x5BD1E995u;
        }
        memset(v->gr, 0, sizeof v->gr);                  /* GRAIN: no grain yet, the first at once */
        v->gr[0].rng = v->rng ^ 0x2545F491u;
        v->gr[1].rng = v->rng * 2654435761u;
        for (i = 0; i < 6u; i++) {                       /* SUPER's side saws from random phases (as above) */
            lfo.rng = lfo.rng * 1664525u + 1013904223u;
            v->sph[0][i] = lfo.rng;
            v->sph[1][i] = lfo.rng * 2654435761u;
        }
    }
    v->note = (uint8_t)note;
    v->vel = (uint8_t)vel;
    v->gate = 1;
    v->active = 1;
    v->age = ++vc_age;
    if (retrig || v->amp.st == ES_IDLE || v->amp.st == ES_REL) {
        env_set(&v->amp, par[P_AATK], keyt(par[P_ADEC], note, par[P_AKEYT]), par[P_ASUS],
                keyt(par[P_AREL], note, par[P_AKEYT]), par[P_ALOOP]);
        env_set(&v->flt, par[P_FATK], keyt(par[P_FDEC], note, par[P_FKEYT]), par[P_FSUS],
                keyt(par[P_FREL], note, par[P_FKEYT]), par[P_FLOOP]);
        v->amp.st = v->flt.st = ES_ATK;                  /* (a stolen voice: from where its level is) */
    }
    for (l = 0; l < NLFO; l++) {
        if (par[LFO_X(l, 0)])                            /* TRIG NOTE: the LFO from PHASE */
            lfo.ph[l] = (uint32_t)clamp(par[LFO_X(l, 2)], 0, 127) << 25;
        if (!any)                                        /* FADE: from a note out of silence */
            lfo.fade[l] = par[LFO_X(l, 1)] ? 0 : ENV1;
    }
}

static void held_drop(uint32_t note)                     /* MONO / LEGATO: the key off the stack */
{
    uint32_t i, k = 0;
    for (i = 0; i < held.n; i++)
        if (held.note[i] != note) {
            held.note[k] = held.note[i];
            held.vel[k++] = held.vel[i];
        }
    held.n = (uint8_t)k;
}

static void note_on(uint32_t note, uint32_t vel)
{
    uint32_t i, b = NVOICE, nv = (uint32_t)clamp(par[P_VOICES], 1, NVOICE);
    voice_t *v;
    if (par[P_MODE] != VM_POLY) {                        /* one voice: the newest key */
        held_drop(note);
        if (held.n == NHELD)
            held_drop(held.note[0]);
        held.note[held.n] = (uint8_t)note;
        held.vel[held.n++] = (uint8_t)vel;
        if (par[P_MODE] == VM_UNI) {                     /* UNI: every voice on it */
            for (i = 0; i < NVOICE; i++)
                voice_start(&vc[i], note, vel, 1);
            return;
        }
        v = &vc[0];
        for (i = 1; i < NVOICE; i++)                     /* (from POLY: the others let go) */
            if (vc[i].active && vc[i].gate) {
                vc[i].gate = 0;
                vc[i].amp.st = vc[i].flt.st = ES_REL;
            }
        voice_start(v, note, vel, par[P_MODE] == VM_MONO || !(v->active && v->gate));
        return;
    }
    for (i = 0; i < nv && b == NVOICE; i++)              /* the same note again: its voice */
        if (vc[i].active && vc[i].note == note)
            b = i;
    for (i = 0; i < nv && b == NVOICE; i++)
        if (!vc[i].active)
            b = i;
    if (b == NVOICE) {                                   /* steal: the oldest released, else the oldest */
        uint32_t rel = NVOICE, old = 0;
        for (i = 0; i < nv; i++) {
            if (!vc[i].gate && (rel == NVOICE || vc[i].age < vc[rel].age))
                rel = i;
            if (vc[i].age < vc[old].age)
                old = i;
        }
        b = rel < NVOICE ? rel : old;
    }
    voice_start(&vc[b], note, vel, 1);
}

static void note_off(uint32_t note)
{
    uint32_t i, nm = 0;
    if (MONO_LIKE(par[P_MODE])) {
        held_drop(note);
        nm = par[P_MODE] == VM_UNI ? NVOICE : 1u;        /* the voices the key drives: UNI all, MONO / LEGATO one */
        for (i = 0; i < nm; i++)
            if (vc[i].active && vc[i].gate && vc[i].note == note) {
                if (held.n) {                            /* the key under it comes back, no restart */
                    vc[i].note = held.note[held.n - 1u];
                    vc[i].vel = held.vel[held.n - 1u];
                } else {
                    vc[i].gate = 0;
                    vc[i].amp.st = vc[i].flt.st = ES_REL;
                }
            }
    }
    for (i = nm; i < NVOICE; i++)                        /* (MONO: voice 0 above; the others, a POLY leftover) */
        if (vc[i].active && vc[i].gate && vc[i].note == note) {
            vc[i].gate = 0;
            vc[i].amp.st = ES_REL;
            vc[i].flt.st = ES_REL;
        }
}

static void notes_all_off(void)                         /* every voice released (the ISR) */
{
    uint32_t i;
    held.n = 0;
    for (i = 0; i < NVOICE; i++)
        if (vc[i].active && vc[i].gate) {
            vc[i].gate = 0;
            vc[i].amp.st = ES_REL;
            vc[i].flt.st = ES_REL;
        }
}

static void synth_all_off(void) { synth_note(0u, 1u, 2); }   /* the main loop: every voice released (an update) */

static void synth_events(void)                           /* the ISR: the notes sent since the last block */
{
    while (ev_r != ev_w) {
        const ev_t *e = &ev_ring[ev_r % EV_N];
        if (e->on == 2u)
            notes_all_off();
        else if (e->on)
            note_on(e->note, e->vel);
        else
            note_off(e->note);
        ev_r++;
    }
}

static uint32_t synth_voices(void)                       /* voices sounding (the screen) */
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += vc[i].active;
    return n;
}

/* the LFOs one block (n samples) on: their outputs, Q15 bipolar; FADE's share toward full */
static void lfo_block(uint32_t n)
{
    uint32_t l;
    for (l = 0; l < NLFO; l++) {
        uint32_t ph = lfo.ph[l], inc = (uint32_t)(((uint64_t)LFO_INC[clamp(par[LFO_P(l, 1)], 0, 127)] * n) / CTL);
        int32_t x;
        switch (par[LFO_P(l, 0)]) {
        case LW_TRI:
            x = (int32_t)(ph >> 16) - 32768;
            x = 2 * (x < 0 ? -x : x) - 32767;
            break;
        case LW_SAW:
            x = (int32_t)(ph >> 16) - 32768;
            break;
        case LW_SQR:
            x = ph < 0x80000000u ? 32767 : -32767;
            break;
        case LW_SH:                                      /* a new random level each cycle */
            if (ph + inc < ph || !lfo.out[l]) {
                lfo.rng = lfo.rng * 1664525u + 1013904223u;
                lfo.out[l] = ((int32_t)(lfo.rng >> 16) - 32768) | 1;
            }
            x = lfo.out[l];
            break;
        default:
            x = sine_i(ph);
            break;
        }
        lfo.out[l] = clamp(x, -32767, 32767);
        lfo.ph[l] = ph + inc;
        if (lfo.fade[l] < ENV1)                          /* FADE: 1 ms .. ~4 s to full (ENV_LIN, a CTL step) */
            lfo.fade[l] = (int32_t)qmin_u((uint32_t)lfo.fade[l] +
                                          (uint32_t)(((uint64_t)ENV_LIN[(uint32_t)clamp(par[LFO_X(l, 1)], 0, 127) *
                                                                        115u / 127u] * n) / CTL), ENV1);
    }
}

/* LFO l's depth now: DEPTH (+-64) x FADE's share */
static inline int32_t lfo_depth(uint32_t l)
{
    return (int32_t)(((int64_t)par[LFO_P(l, 3)] * lfo.fade[l]) >> 24);
}

/* the LFOs' sum on destination d, each as its depth (+-64) x its output: Q15 x 64 (+-64 x 32767 at full) */
static int32_t lfo_mod(uint32_t d);
/* OSC n's POS moved by the LFOs (POS and its own), +-64 */
static int32_t lfo_pos(uint32_t n) { return (lfo_mod(LD_POS) + lfo_mod(n ? LD_POS2 : LD_POS1)) >> 15; }
static int32_t lfo_mod(uint32_t d)
{
    uint32_t l;
    int32_t m = 0;
    for (l = 0; l < NLFO; l++)
        if ((uint32_t)par[LFO_P(l, 2)] == d)
            m += lfo_depth(l) * lfo.out[l];
    return m;
}

/* OSC n's block for this voice: its engine, at increment inc (TUNE, SEMI, FINE, glide, LFO, GLOBAL TUNE in it);
 * mpos, mshape, mdet the LFOs' on its POS, SHAPE (PW) and DETUN (+-64 each) */
static const int16_t SOFF[6] = {-1000, -570, -180, 180, 560, 980};      /* SUPER: the sides' spread (/1000) */

static void osc_setup(oscb_t *o, uint32_t n, uint32_t inc, int32_t mpos, int32_t mshape, int32_t mdet)
{
    uint32_t k;
    o->eng = par[OSC_P(n, 0)];
    o->inc = inc;
    o->wave = n ? par[P_WAVE2] : par[P_WAVE];
    o->pw = 0x80000000u + (uint32_t)((clamp(par[n ? P_PW2 : P_PW1] + mshape, 0, 127) - 64) * 0x1CCCCCD);   /* 5 .. 94 % */
    if (o->eng == OE_SUPER) {
        int32_t det = clamp(par[OSC_P(n, 1)] + mdet, 0, 127), side = par[OSC_P(n, 2)];
        for (k = 0; k < 6u; k++)                         /* +-60 cents outer at DETUN 127 (1 cent ~ 37.9 / 65536) */
            o->sinc[k] = (uint32_t)(((uint64_t)inc * (uint32_t)(65536 + SOFF[k] * det * 60 * 38 / (1000 * 127))) >> 16);
        o->center = 32767 - side * 103;                  /* equal power: the centre 1 .. 0.6, each side 0 .. 0.33 */
        o->side = side * 84;                             /* (0.6^2 + 6 x 0.33^2 = 1: one saw's level) */
    } else if (o->eng == OE_WT) {
        const wt_bank_t *bk = &WT_BANKS[(uint32_t)clamp(par[OSC_P(n, 4)], 0, WT_NBANK - 1)];
        uint32_t fpos = (uint32_t)clamp(par[OSC_P(n, 3)] + mpos, 0, 127) * (bk->n - 1u) * 256u / 127u;
        uint32_t fa = fpos >> 8, fb = fa + 1u < bk->n ? fa + 1u : fa, lv = 0, len, sh = 32;
        while (lv < WT_LEVELS - 1u && (uint64_t)(512u >> lv) * inc > 0x80000000ull)
            lv++;                                        /* the level whose harmonics all stay under Nyquist */
        len = WT_LLEN[lv];
        while ((1u << (32u - sh)) < len)
            sh--;                                        /* (len = 2 ^ (32 - sh)) */
        o->wa = bk->data + fa * WT_FRAME + WT_LOFF[lv];
        o->wb = bk->data + fb * WT_FRAME + WT_LOFF[lv];
        o->wshift = sh;
        o->wmask = len - 1u;
        o->wfrac = (int32_t)(fpos & 255u) << 7;
    } else if (o->eng == OE_GR) {
        /* the sum of ov grains at random phases: ~sqrt(ov) a grain's level (a Hann window: 0.6 of it, rms) */
        static const uint16_t OVG[NGR + 1u] = {0, 52000, 37000, 30000, 26000, 23300, 21300};
        const gr_smp_t *sm = &GR_SMPS[(uint32_t)clamp(par[GR_P(n, 0)], 0, GR_NSMP - 1)];
        uint32_t size = TIME_MS_X10[32u + (uint32_t)clamp(par[GR_P(n, 1)], 0, 127) * 54u / 127u] * 441u / 100u;
        uint32_t every = TIME_MS_X10[86u - (uint32_t)clamp(par[GR_P(n, 2)], 0, 127) * 52u / 127u] * 441u / 100u;
        uint32_t ov = size / every + 1u;
        o->gs = sm->data;
        o->glen = sm->n;
        o->ginc = (uint32_t)(((uint64_t)inc << 11) / pitch_inc((int32_t)sm->root * 16));   /* (its root at 22.05 kHz:
                                                                                         * half a sample) */
        o->gwinc = 0xFFFFFFFFu / size;
        o->gpos = ((uint32_t)clamp(par[OSC_P(n, 3)] + mpos, 0, 127) * (sm->n - 2u) / 127u) << 12;
        o->gspray = (sm->n * (uint32_t)clamp(par[GR_P(n, 3)], 0, 127) / 254u) << 12;
        o->gevery = every;
        o->gper = (uint32_t)((1ull << 43) / pitch_inc((int32_t)sm->root * 16));   /* (22050 / f x 4096) */
        o->ggain = OVG[ov < NGR ? ov : NGR];
    }
}

/* one block (n <= CTL samples) into outl / outr, Q15 (cleared first), x VOL x MASTER, not clipped: fx.c clips */
static void synth_block(int32_t *outl, int32_t *outr, uint32_t n)
{
    uint32_t i, j, gt, uni = par[P_MODE] == VM_UNI;
    int32_t newest = -1, m_pitch, m_cut, m_reso, m_mix, m_pos, amp_q15, m_p1, m_p2, m_pos1, m_pos2;
    vblock_t b;
    for (i = 0; i < n; i++)
        outl[i] = outr[i] = 0;
    synth_events();
    lfo_block(n);
    /* the modulations this block: PITCH +-12 semitones at full (1/16 st), CUT +-64 cutoff units (<< 8), RESO +-64,
     * AMP a tremolo down to silence at full (either sign: its depth), MIX +-64 of 127 */
    m_pitch = (int32_t)(((int64_t)lfo_mod(LD_PITCH) * 12 * 16) >> 21);
    m_cut = lfo_mod(LD_CUT) >> 7;
    m_reso = lfo_mod(LD_RESO) >> 15;
    m_mix = lfo_mod(LD_MIX) >> 15;
    m_pos = lfo_mod(LD_POS) >> 15;
    m_p1 = (int32_t)(((int64_t)lfo_mod(LD_PITCH1) * 12 * 16) >> 21);   /* (each oscillator's own) */
    m_p2 = (int32_t)(((int64_t)lfo_mod(LD_PITCH2) * 12 * 16) >> 21);
    m_pos1 = m_pos + (lfo_mod(LD_POS1) >> 15);
    m_pos2 = m_pos + (lfo_mod(LD_POS2) >> 15);
    {
        uint32_t l;                                      /* AMP: 1 - |depth| / 64 x (1 - lfo) / 2 */
        amp_q15 = 32767;
        for (l = 0; l < NLFO; l++)
            if (par[LFO_P(l, 2)] == LD_AMP) {
                int32_t dpt = lfo_depth(l);
                dpt = dpt < 0 ? -dpt : dpt;
                amp_q15 -= (dpt * ((32767 - lfo.out[l]) >> 1)) >> 6;
            }
        amp_q15 = clamp(amp_q15, 0, 32767);
    }
    {   /* MIX: equal power, cos / sin over a quarter cycle: both at ~71 % in the middle, so two oscillators that are
         * not in step (detuned, another wave) sound as loud as one (a linear cross-fade dipped 3 dB there) */
        uint32_t q = (uint32_t)clamp(par[P_MIX] + m_mix, 0, 127) * (0x40000000u / 127u);
        b.m2 = sine_i(q);
        b.m1 = sine_i(0x40000000u - q);
    }
    b.sub = par[P_SUB] * 258;
    b.noise = clamp(par[P_NOISE] + (lfo_mod(LD_NOISE) >> 15), 0, 127) * 129;   /* (loud: half the scale at full) */
    b.ncol = par[P_NCOL];
    b.ring = par[P_RING] * 258;
    b.sync = par[P_SYNC];
    b.drive = DRIVE_Q12[clamp(par[P_DRIVE], 0, 127)];   /* x1 .. x8 into the tanh, in even dB */
    b.ftype = par[P_FTYPE];
    b.slope24 = par[P_SLOPE];
    gt = (uint32_t)(65536 + par[P_GTUNE] * 38);         /* GLO TUNE: +-50 cents (1 cent ~ 37.9 / 65536) */
    for (i = 0; i < NVOICE; i++)                         /* the newest gated note: GLIDE's target */
        if (vc[i].active && vc[i].gate && (newest < 0 || vc[i].age > vc[(uint32_t)newest].age))
            newest = (int32_t)i;
    if (newest >= 0) {                                   /* GLIDE: the pitch slides toward the newest note */
        int32_t pt = ((int32_t)vc[(uint32_t)newest].note + par[P_TUNE]) * 16, d, s;
        if (!par[P_GLIDE] || !glide_p) {
            glide_p = pt;
        } else {
            d = pt - glide_p;
            s = (int32_t)(((int64_t)d * (int32_t)((uint32_t)ENV1 / (1u + (uint32_t)par[P_GLIDE] * (uint32_t)par[P_GLIDE] / 16u))) >> 24);
            glide_p += s ? s : d > 0 ? 1 : d < 0 ? -1 : 0;
        }
    }
    for (i = 0; i < NVOICE; i++) {
        voice_t *v = &vc[i];
        int32_t p, p2, cut, fe, famt, vel = v->vel;
        uint32_t udt = 65536u;
        if (!v->active)
            continue;
        p = ((int32_t)v->note + par[P_TUNE]) * 16;
        if (par[P_GLIDE] && ((int32_t)i == newest || (uni && v->gate)))
            p = glide_p;                                 /* (UNI: every stacked voice glides as one) */
        b.gl = b.gr = 32767;                             /* the centre: both sides full */
        if (uni) {                                       /* UNI: voice i at -3 -1 +1 +3 thirds of the spread */
            int32_t pos = 2 * (int32_t)i - 3, pq = pos * 7645;   /* (x 0.7 of full a side) */
            udt = (uint32_t)(65536 + pos * par[P_UDET] * 25 * 38 / (3 * 127));   /* +-25 cents outer at 127 */
            b.gl = pq > 0 ? 32767 - pq : 32767;
            b.gr = pq < 0 ? 32767 + pq : 32767;
        }
        for (j = 0, fe = 0; j < n; j++)                  /* the filter envelope at the block's rate */
            fe = env_tick(&v->flt);
        /* cutoff, in octaves: CUT + ENV x the envelope (+-6 octaves at full, 90 units; x velocity: FENV +'s VEL)
         * + KEY x the note from C4 (127 units over 50 Hz .. 18 kHz: 319 / 256 a semitone) + FILTER +'s VEL x
         * velocity (+-32 units) + the LFOs */
        famt = par[P_FENV] * (127 - par[P_FEVEL] + par[P_FEVEL] * vel / 127) / 127;
        cut = (par[P_CUT] << 8) + (int32_t)(((int64_t)famt * 360 * fe) >> 24) +
              ((int32_t)v->note - 60) * 319 * par[P_FKEY] / 127 + (vel - 64) * par[P_FVEL] * 128 / 127 + m_cut;
        if ((int32_t)i == newest)
            live_cut = cut;                              /* (the FILTER page draws it) */
        b.k = RESO_K[clamp(par[P_RESO] + m_reso, 0, 127)];   /* damping 1.414 .. 0.054 (Q12), exponential */
        if (b.slope24) {                                 /* 24 dB: Q 0.54 then Q 1.31 x the resonance (Butterworth) */
            b.k1 = 7578;
            svf_coef(&b.c1, cut, b.k1);
            b.k = b.k * 2216 >> 12;
        }
        svf_coef(&b.c, cut, b.k);
        p += m_pitch;
        p2 = (par[P_KEY2] ? p : (60 + par[P_TUNE]) * 16 + m_pitch) + par[P_SEMI2] * 16 + m_p2;   /* OSC 2: KEY or C4 */
        p += m_p1;
        {
            uint32_t inc1 = (uint32_t)(((uint64_t)pitch_inc(p) * gt) >> 16),
                     inc2 = (uint32_t)(((uint64_t)pitch_inc(p2) * gt) >> 16);
            if (uni) {
                inc1 = (uint32_t)(((uint64_t)inc1 * udt) >> 16);
                inc2 = (uint32_t)(((uint64_t)inc2 * udt) >> 16);
            }
            inc2 = (uint32_t)(((uint64_t)inc2 * (uint32_t)(65536 + par[P_FINE2] * 19)) >> 16);   /* FINE ~+-32 c */
            osc_setup(&b.o[0], 0, inc1, m_pos1, lfo_mod(LD_SHAPE1) >> 15, lfo_mod(LD_DET1) >> 15);
            osc_setup(&b.o[1], 1, inc2, m_pos2, lfo_mod(LD_SHAPE2) >> 15, lfo_mod(LD_DET2) >> 15);
        }
        /* the level: velocity x VEL (127: all of it, 0: none) x the AMP LFO */
        b.gain = (int32_t)(((int64_t)VOICE_GAIN * (127 - par[P_AVEL] + par[P_AVEL] * vel / 127) / 127 * amp_q15) >> 15);
        if (uni)
            b.gain >>= 1;                                /* (four voices, not in step: about two voices' level) */
        voice_render(v, outl, outr, n, &b);
    }
    {
        int32_t vol = (int32_t)(((uint32_t)par[P_VOL] * master_q12) >> 7);   /* Q12 x VOL / 128 */
        for (i = 0; i < n; i++) {
            outl[i] = (int32_t)(((int64_t)outl[i] * vol) >> 12);
            outr[i] = (int32_t)(((int64_t)outr[i] * vol) >> 12);
        }
    }
}
