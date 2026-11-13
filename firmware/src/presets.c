/* SPDX-License-Identifier: GPL-3.0-only
 * ObsSynth FM-1: 2026 Orian Beltrame */
/* Presets: NFACT factory sounds (in the firmware: what each changes from INIT, the parameters' defaults) and a bank of
 * NUSER user presets in flash (storage.c, one A/B object: a torn write leaves the previous bank). The list PRESETS
 * turns through: the factory ones, then the user slots that hold one. A user preset keeps its name and every
 * sound parameter (int8: every range fits; P_NPRESET of them), PRE_NPAR of room: a parameter added later loads its
 * default from an older one. An older bank (USB1: 48 of room, 0.1.0; USB2: 80) is read byte by byte as its layout
 * and kept in the new one at the next save. Loading writes the parameters as the knobs do (engine.c synth_param): voices
 * sounding change with them; the device's settings (GLO: P_NPRESET on) are never a preset's: settings_* below. */
#define NFACT 14u
#define NUSER 16u
#define PRE_NAME 8u                                      /* a name's characters, 0-padded (not terminated at 8) */
#define PRE_NPAR 112u
#define UB_MAGIC 0x33425355u                             /* "USB3": the user bank's layout (PRE_NPAR 112) */
#define UB1_MAGIC 0x31425355u                            /* "USB1": 0.1.0's (PRE_NPAR 48) */
#define UB1_NPAR 48u
#define UB2_MAGIC 0x32425355u                            /* "USB2": the + pages' (PRE_NPAR 80) */
#define UB2_NPAR 80u
_Static_assert(P_NPRESET <= PRE_NPAR, "a user preset keeps PRE_NPAR parameters");

typedef struct { uint8_t id; int8_t v; } pkv_t;
typedef struct { const char *name; uint8_t n; pkv_t kv[28]; } fpreset_t;

#define KV(...) (sizeof((pkv_t[]){__VA_ARGS__}) / sizeof(pkv_t)), {__VA_ARGS__}
static const fpreset_t FACTORY[NFACT] = {
    {"INIT", 0, {{0, 0}}},
    {"BASS", KV({P_TUNE, -12}, {P_WAVE2, W_SQR}, {P_SEMI2, -12}, {P_FINE2, 0}, {P_MIX, 50}, {P_CUT, 30},
                {P_RESO, 50}, {P_FENV, 40}, {P_FKEY, 40}, {P_FDEC, 45}, {P_FSUS, 10}, {P_ADEC, 60}, {P_ASUS, 90},
                {P_AREL, 25})},
    {"LEAD", KV({P_FINE2, 14}, {P_MIX, 64}, {P_GLIDE, 30}, {P_CUT, 70}, {P_RESO, 40}, {P_FENV, 20},
                {P_L1RATE, 86}, {P_L1DEST, LD_PITCH}, {P_L1DEPTH, 1}, {P_DMIX, 45}, {P_DTIME, 75}, {P_DFDBK, 50},
                {P_RMIX, 25})},
    {"PAD", KV({P_SEMI2, 12}, {P_FINE2, 20}, {P_MIX, 45}, {P_AATK, 95}, {P_ADEC, 90}, {P_ASUS, 110},
               {P_AREL, 95}, {P_CUT, 45}, {P_RESO, 20}, {P_FENV, 25}, {P_FATK, 90}, {P_FDEC, 100}, {P_FSUS, 60},
               {P_L1RATE, 30}, {P_L1DEST, LD_CUT}, {P_L1DEPTH, 12}, {P_DMIX, 25}, {P_DTIME, 90}, {P_DFDBK, 55},
               {P_RSIZE, 115}, {P_RDAMP, 70}, {P_RMIX, 70})},
    {"PLUCK", KV({P_WAVE, W_SQR}, {P_WAVE2, W_SQR}, {P_SEMI2, 12}, {P_MIX, 30}, {P_ADEC, 50}, {P_ASUS, 0},
                 {P_AREL, 45}, {P_CUT, 25}, {P_RESO, 55}, {P_FENV, 50}, {P_FDEC, 40}, {P_FSUS, 0}, {P_DMIX, 55},
                 {P_DTIME, 68}, {P_DFDBK, 55}, {P_DCOLR, 50}, {P_RMIX, 30})},
    {"WOBBLE", KV({P_TUNE, -12}, {P_WAVE2, W_SQR}, {P_SEMI2, -12}, {P_MIX, 55}, {P_CUT, 35}, {P_RESO, 75},
                  {P_FENV, 0}, {P_ASUS, 110}, {P_L1RATE, 70}, {P_L1DEST, LD_CUT}, {P_L1DEPTH, 50})},
    {"BELLS", KV({P_WAVE, W_SIN}, {P_WAVE2, W_SIN}, {P_SEMI2, 19}, {P_FINE2, 0}, {P_MIX, 45}, {P_ADEC, 95},
                 {P_ASUS, 0}, {P_AREL, 90}, {P_CUT, 127}, {P_RESO, 0}, {P_FENV, 0}, {P_DMIX, 30}, {P_RSIZE, 110},
                 {P_RMIX, 60})},
    {"BRASS", KV({P_FINE2, 10}, {P_MIX, 50}, {P_AATK, 25}, {P_ASUS, 100}, {P_AREL, 35}, {P_CUT, 35},
                 {P_RESO, 25}, {P_FENV, 45}, {P_FATK, 40}, {P_FDEC, 70}, {P_FSUS, 40}, {P_RMIX, 30})},
    {"SUPER", KV({P_MODE, VM_UNI}, {P_UDET, 60}, {P_SEMI2, 12}, {P_FINE2, 6}, {P_MIX, 40}, {P_GLIDE, 10},
                 {P_CUT, 80}, {P_RESO, 15}, {P_FENV, 15}, {P_AREL, 60}, {P_CMIX, 50}, {P_CDEPTH, 60},
                 {P_DMIX, 25}, {P_DTIME, 75}, {P_RMIX, 40}, {P_RSIZE, 110})},
    {"STRINGS", KV({P_WAVE, W_SQR}, {P_PW1, 30}, {P_FINE2, 12}, {P_MIX, 64}, {P_AATK, 85}, {P_ADEC, 90},
                   {P_ASUS, 105}, {P_AREL, 85}, {P_CUT, 60}, {P_RESO, 10}, {P_FENV, 10}, {P_FATK, 70},
                   {P_L1RATE, 80}, {P_L1DEST, LD_PITCH}, {P_L1DEPTH, 1}, {P_L1FADE, 90}, {P_CMIX, 90},
                   {P_CRATE, 30}, {P_CDEPTH, 90}, {P_RMIX, 60}, {P_RSIZE, 115})},
    {"SAWS", KV({P_ENG1, OE_SUPER}, {P_SDET1, 70}, {P_SSIDE1, 90}, {P_ENG2, OE_SUPER}, {P_SEMI2, 12},
                {P_SDET2, 50}, {P_MIX, 35}, {P_CUT, 85}, {P_RESO, 10}, {P_FENV, 10}, {P_AATK, 10}, {P_AREL, 55},
                {P_CMIX, 40}, {P_RMIX, 45}, {P_RSIZE, 110}, {P_DMIX, 20})},
    {"WAVES", KV({P_ENG1, OE_WT}, {P_WTAB1, 3}, {P_WPOS1, 20}, {P_ENG2, OE_WT}, {P_WTAB2, 1}, {P_WPOS2, 60},
                 {P_SEMI2, 7}, {P_MIX, 40}, {P_L1WAVE, LW_TRI}, {P_L1RATE, 35}, {P_L1DEST, LD_POS},
                 {P_L1DEPTH, 40}, {P_CUT, 95}, {P_RESO, 20}, {P_FENV, 15}, {P_AATK, 40}, {P_AREL, 70},
                 {P_CMIX, 50}, {P_RMIX, 50}, {P_RSIZE, 115})},
    {"GRAINS", KV({P_ENG1, OE_GR}, {P_GSMP1, 2}, {P_WPOS1, 10}, {P_GSIZE1, 95}, {P_GDENS1, 100}, {P_GSPRAY1, 25},
                  {P_MIX, 0}, {P_L1WAVE, LW_TRI}, {P_L1RATE, 25}, {P_L1DEST, LD_POS}, {P_L1DEPTH, 60}, {P_CUT, 100},
                  {P_FENV, 0}, {P_AATK, 50}, {P_AREL, 75}, {P_CMIX, 45}, {P_RMIX, 55}, {P_RSIZE, 115})},
    {"CHIMES", KV({P_ENG1, OE_GR}, {P_GSMP1, 1}, {P_WPOS1, 30}, {P_GSIZE1, 70}, {P_GDENS1, 110}, {P_GSPRAY1, 90},
                  {P_ENG2, OE_GR}, {P_GSMP2, 0}, {P_SEMI2, 12}, {P_WPOS2, 70}, {P_GSPRAY2, 60}, {P_MIX, 45},
                  {P_CUT, 110}, {P_FENV, 0}, {P_AATK, 20}, {P_ADEC, 90}, {P_ASUS, 60}, {P_AREL, 85}, {P_DMIX, 35},
                  {P_DFDBK, 60}, {P_RMIX, 50}, {P_RSIZE, 110})},
};

typedef struct { char name[PRE_NAME]; int8_t v[PRE_NPAR]; } upreset_t;
typedef struct {
    uint32_t magic;
    uint16_t np, rsv;            /* the parameters each slot holds (P_COUNT when written) */
    uint32_t used;               /* bit s: slot s holds a preset */
    upreset_t p[NUSER];
} ubank_t;
static ubank_t ubank;
typedef struct { char name[PRE_NAME]; int8_t v[UB1_NPAR]; } upreset1_t;   /* "USB1" (the tests write one) */
typedef struct { uint32_t magic; uint16_t np, rsv; uint32_t used; upreset1_t p[NUSER]; } ubank1_t;
#define UB_HEAD 12u                                      /* magic, np, rsv, used */
static uint8_t ub_in[sizeof(ubank_t)] __attribute__((aligned(4)));   /* (what the flash holds, read once at boot) */

static void presets_boot(void)                           /* the user bank from flash (an older one converted), else
                                                          * an empty one */
{
    static const struct { uint32_t magic, npar; } OLD[2] = {{UB1_MAGIC, UB1_NPAR}, {UB2_MAGIC, UB2_NPAR}};
    int n = st_load(OBJ_UBANK, ub_in, sizeof ub_in);
    uint32_t s, k, o, magic, np, used;
    if (n < (int)UB_HEAD) {
        memset(&ubank, 0, sizeof ubank);
        ubank.magic = UB_MAGIC;
        ubank.np = P_NPRESET;
        return;
    }
    memcpy(&magic, ub_in, 4);
    np = (uint32_t)ub_in[4] | (uint32_t)ub_in[5] << 8;
    memcpy(&used, ub_in + 8, 4);
    if (n == (int)sizeof ubank && magic == UB_MAGIC && np <= PRE_NPAR) {
        memcpy(&ubank, ub_in, sizeof ubank);
        return;
    }
    memset(&ubank, 0, sizeof ubank);
    ubank.magic = UB_MAGIC;
    ubank.np = P_NPRESET;
    for (o = 0; o < 2u; o++)                             /* an older layout: names and values, the newer ones' defaults */
        if (magic == OLD[o].magic && n == (int)(UB_HEAD + NUSER * (PRE_NAME + OLD[o].npar)) && np <= OLD[o].npar) {
            ubank.used = used;
            for (s = 0; s < NUSER; s++) {
                const uint8_t *q = ub_in + UB_HEAD + s * (PRE_NAME + OLD[o].npar);
                for (k = 0; k < PRE_NAME; k++)
                    ubank.p[s].name[k] = (char)q[k];
                for (k = 0; k < P_NPRESET; k++)
                    ubank.p[s].v[k] = (int8_t)(k < np ? (int8_t)q[PRE_NAME + k] : PARAMS[k].def);
            }
        }
}

static uint32_t preset_count(void)                       /* the list: factory, then the used user slots */
{
    uint32_t n = NFACT, s;
    for (s = 0; s < NUSER; s++)
        n += (ubank.used >> s) & 1u;
    return n;
}

static int preset_slot(uint32_t idx)                     /* the user slot of list entry idx, -1 for a factory one */
{
    uint32_t s;
    if (idx < NFACT)
        return -1;
    idx -= NFACT;
    for (s = 0; s < NUSER; s++)
        if (((ubank.used >> s) & 1u) && !idx--)
            return (int)s;
    return -1;
}

static int preset_index(uint32_t slot)                   /* the list entry of user slot slot (used), else -1 */
{
    uint32_t s, i = NFACT;
    for (s = 0; s < NUSER; s++)
        if ((ubank.used >> s) & 1u) {
            if (s == slot)
                return (int)i;
            i++;
        }
    return -1;
}

static void preset_name(char *b, uint32_t idx)           /* "F2 BASS", "U03 MYPAD": 13 characters at most */
{
    int s = preset_slot(idx);
    const char *n;
    uint32_t k = 0, i;
    if (idx < NFACT) {
        b[k++] = 'F';
        if (idx + 1u >= 10u)                             /* F1 .. F9, F10 .. */
            b[k++] = (char)('0' + (idx + 1u) / 10u);
        b[k++] = (char)('0' + (idx + 1u) % 10u);
        n = FACTORY[idx].name;
    } else if (s >= 0) {
        b[k++] = 'U';
        b[k++] = (char)('0' + (s + 1) / 10);
        b[k++] = (char)('0' + (s + 1) % 10);
        n = ubank.p[s].name;
    } else {
        b[0] = 0;
        return;
    }
    b[k++] = ' ';
    for (i = 0; i < PRE_NAME && n[i]; i++)
        b[k++] = n[i];
    b[k] = 0;
}

static void preset_load(uint32_t idx)                    /* list entry idx: every parameter */
{
    uint32_t i;
    int s = preset_slot(idx);
    if (idx < NFACT) {
        for (i = 0; i < P_NPRESET; i++)
            synth_param(i, PARAMS[i].def);
        for (i = 0; i < FACTORY[idx].n; i++)
            synth_param(FACTORY[idx].kv[i].id, FACTORY[idx].kv[i].v);
    } else if (s >= 0) {
        for (i = 0; i < P_NPRESET; i++)
            synth_param(i, i < ubank.np ? ubank.p[s].v[i] : PARAMS[i].def);
    }
}

/* the sound now into user slot slot, named name (8 characters at most); 0 written, else storage.c's error */
static int preset_save(uint32_t slot, const char *name)
{
    uint32_t i;
    upreset_t *u;
    if (slot >= NUSER)
        return -1;
    u = &ubank.p[slot];
    memset(u, 0, sizeof *u);
    for (i = 0; i < PRE_NAME && name[i]; i++)
        u->name[i] = name[i];
    for (i = 0; i < P_NPRESET; i++)
        u->v[i] = (int8_t)par[i];
    if (ubank.np < P_NPRESET) {                          /* an older bank: its slots get the new parameters' defaults */
        uint32_t s, k;
        for (s = 0; s < NUSER; s++)
            if (s != slot)
                for (k = ubank.np; k < P_NPRESET; k++)
                    ubank.p[s].v[k] = (int8_t)PARAMS[k].def;
    }
    ubank.np = P_NPRESET;
    ubank.used |= 1u << slot;
    return st_save(OBJ_UBANK, &ubank, sizeof ubank);
}

/* the device's settings (GLO: P_NPRESET .. P_COUNT - 1) in flash, apart from the presets: read at boot, written when
 * the GLO page is left with something changed (ui.c) */
#define SET_MAGIC 0x31544553u                            /* "SET1" */
#define SET_N 8u                                         /* room */
_Static_assert(P_COUNT - P_NPRESET <= SET_N, "the settings record keeps SET_N");
typedef struct { uint32_t magic; uint8_t n, rsv[3]; int8_t v[SET_N]; } settings_t;

static void settings_boot(void)
{
    settings_t st;
    uint32_t i;
    if (st_load(OBJ_SETTINGS, &st, sizeof st) != (int)sizeof st || st.magic != SET_MAGIC)
        return;                                          /* none yet: the defaults (synth_init) */
    for (i = 0; i < P_COUNT - P_NPRESET && i < st.n; i++)
        synth_param(P_NPRESET + i, st.v[i]);
}

static int settings_save(void)                           /* 0 written */
{
    settings_t st;
    uint32_t i;
    memset(&st, 0, sizeof st);
    st.magic = SET_MAGIC;
    st.n = (uint8_t)(P_COUNT - P_NPRESET);
    for (i = 0; i < st.n; i++)
        st.v[i] = (int8_t)par[P_NPRESET + i];
    return st_save(OBJ_SETTINGS, &st, sizeof st);
}
