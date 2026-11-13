/* SPDX-License-Identifier: GPL-3.0-only
 * ObsSynth FM-1: 2026 Orian Beltrame */
/* The panel and the screen, one page at a time. The 27 keys play the synth (F3 .. G5, OCT- / OCT+ shift them by
 * octaves, -2 .. +2); KNOB 1..4 edit the page's four parameters. Each page has its button (CYCLES): a press opens its
 * first page, on one of them the next, round, the button lit there: SEL OSC 1 / OSC 2, EDIT AMP / AMP ENV, SEQ FILTER /
 * FILTER ENV, ENV AMP ENV / FILTER ENV, LFO LFO 1 / LFO 2, FX DELAY / REVERB / CHORUS, GLO GLOBAL / GLOBAL 2. SELECT
 * turns the sound's pages in order (SEL_PG: OSC 1 .. FILTER ENV; from another page, back to the last of them). PRESETS
 * turns through the presets (presets.c), each loaded at once. SAVE opens the SAVE page (KNOB 1 the user slot), a second
 * SAVE writes it there and goes back; any other way out leaves nothing written. GLO: the device's settings (MIDI
 * channel, tuning, velocity curve, backlight), written when the page is left.
 * HOME is SHIFT only, a switch: a press shows the page's + page (PLUS_OF: OSC 1 + .. REVERB +, GLO's: INFO), its
 * knobs the + ones, HOME lit, until HOME again or another page.
 * ALGORITHM turns the oscillator's engine (STD SUPER WT GRAIN) of the OSC page shown (OSC 2 or OSC 2 +: OSC 2; else
 * OSC 1); the OSC page's first knob follows it (WAVE, DETUN, POS, POS), and its + page's (PW, SIDE, TABLE; GRAIN: all
 * four, SMPL SIZE DENS SPRAY).
 * The screen (240 x 240) is two strips of the canvas (gfx.c, 240 x 124 at most): the page's title and the state on
 * top, the four parameters below; each is drawn again only when what it shows changed. */
#ifndef SYNTH_VERSION
#define SYNTH_VERSION "DEV"                              /* tools/build.py: VERSION.txt */
#endif
#ifndef SYNTH_BUILD
#define SYNTH_BUILD "LOCAL"                              /* tools/build.py: the commit, * if the tree differs */
#endif
#define NKEYS 27u
#define KEY_NOTE0 53u                                    /* key 0 = F3 */

typedef struct {
    const char *title;
    uint8_t id[4];
} page_t;

static const page_t PAGES[] = {
    {"OSC 1", {P_WAVE, P_TUNE, P_GLIDE, P_SUB}},           /* the sound's pages (SELECT: SEL_PG's order) */
    {"OSC 2", {P_WAVE2, P_SEMI2, P_FINE2, P_MIX}},
    {"AMP ENV", {P_AATK, P_ADEC, P_ASUS, P_AREL}},
    {"FILTER", {P_CUT, P_RESO, P_FENV, P_FKEY}},
    {"FILTER ENV", {P_FATK, P_FDEC, P_FSUS, P_FREL}},
    {"LFO 1", {P_L1WAVE, P_L1RATE, P_L1DEST, P_L1DEPTH}},   /* the LFO button's: PG_LFO1, PG_LFO1 + 1 */
    {"LFO 2", {P_L2WAVE, P_L2RATE, P_L2DEST, P_L2DEPTH}},
    {"DELAY", {P_DTIME, P_DFDBK, P_DCOLR, P_DMIX}},      /* the FX button's: PG_DLY, PG_REV */
    {"REVERB", {P_RSIZE, P_RDAMP, P_RDLY, P_RMIX}},
    {"SAVE", {0xFF, 0xFF, 0xFF, 0xFF}},                  /* the SAVE button's: PG_SAVE (drawn by draw_save) */
    {"GLOBAL", {P_MIDICH, P_GTUNE, P_VELC, P_BLIGHT}},   /* the GLO button's: PG_GLO, the device's settings */
    {"OSC 1 +", {P_PW1, P_NOISE, P_NCOL, 0xFF}},         /* the + pages (HOME held): PG_PLUS0 .. */
    {"OSC 2 +", {P_PW2, P_SYNC, P_KEY2, P_RING}},
    {"AMP ENV +", {P_AKEYT, P_ALOOP, 0xFF, 0xFF}},
    {"FILTER +", {P_FTYPE, P_DRIVE, P_FVEL, P_SLOPE}},
    {"FILT ENV +", {P_FEVEL, P_FKEYT, P_FLOOP, 0xFF}},
    {"LFO 1 +", {P_L1TRIG, P_L1FADE, P_L1PHASE, 0xFF}},
    {"LFO 2 +", {P_L2TRIG, P_L2FADE, P_L2PHASE, 0xFF}},
    {"DELAY +", {P_DHP, 0xFF, 0xFF, 0xFF}},
    {"REVERB +", {P_RPRE, P_RTONE, 0xFF, 0xFF}},
    {"INFO", {0xFF, 0xFF, 0xFF, 0xFF}},                  /* GLO's + page: PG_INFO (drawn by draw_info) */
    {"CHORUS", {P_CRATE, P_CDEPTH, P_CWIDTH, P_CMIX}},   /* the FX button's third: PG_CHO */
    {"GLOBAL 2", {P_COLOR, 0xFF, 0xFF, 0xFF}},           /* the GLO button's second: PG_GLO2 */
    {"AMP", {P_VOL, P_AVEL, P_MODE, P_VOICES}},          /* EDIT's first: PG_AMP (VOICES: DETUN under UNI) */
};
#define NPAGES (sizeof(PAGES) / sizeof(PAGES[0]))
#define PG_LFO1 5u
#define PG_DLY (PG_LFO1 + 2u)
#define PG_REV (PG_LFO1 + 3u)
#define PG_SAVE (PG_LFO1 + 4u)
#define PG_GLO (PG_LFO1 + 5u)
#define PG_PLUS0 (PG_GLO + 1u)
#define PG_INFO (PG_PLUS0 + 9u)
#define PG_NONE 0xFFu
static const uint8_t PLUS_OF[PG_GLO + 1u] = {           /* each page's + page (HOME held), PG_NONE none */
    PG_PLUS0, PG_PLUS0 + 1u, PG_PLUS0 + 2u, PG_PLUS0 + 3u, PG_PLUS0 + 4u,   /* OSC 1 .. FILTER ENV */
    PG_PLUS0 + 5u, PG_PLUS0 + 6u, PG_PLUS0 + 7u, PG_PLUS0 + 8u,             /* LFO 1 LFO 2 DELAY REVERB */
    PG_NONE, PG_INFO,                                                       /* SAVE, GLOBAL */
};
#define PG_CHO (PG_INFO + 1u)
#define PG_GLO2 (PG_INFO + 2u)
#define PG_AMP (PG_INFO + 3u)
_Static_assert(PG_AMP + 1u == sizeof(PAGES) / sizeof(PAGES[0]), "PAGES: .. INFO, CHORUS, GLOBAL 2, AMP, last");
#define PG_AENV 2u
#define PG_FILT 3u
#define PG_FENV 4u
static const uint8_t SEL_PG[] = {0u, 1u, PG_AMP, PG_AENV, PG_FILT, PG_FENV};   /* SELECT's pages, in order */
#define NSEL (sizeof SEL_PG)

static int32_t sel_pos(uint32_t pg)                      /* pg's place among SELECT's pages, -1: not one */
{
    uint32_t i;
    for (i = 0; i < NSEL; i++)
        if (SEL_PG[i] == pg)
            return (int32_t)i;
    return -1;
}
#define OFF_SEL(pg) (sel_pos(pg) < 0)                    /* a page only a button opens (LFO, FX, SAVE, GLO) */

/* each button's pages: a press opens its first, or on one of them the next, round (a page in two: ENV's and EDIT's
 * AMP ENV, ENV's and SEQ's FILTER ENV, the button pressed decides) */
static const struct { uint8_t btn, n, pg[3]; } CYCLES[] = {
    {B_ENV, 2, {PG_AENV, PG_FENV, 0}},                   /* ENV: AMP ENV, FILTER ENV */
    {B_LFO, 2, {PG_LFO1, PG_LFO1 + 1u, 0}},              /* LFO: LFO 1, LFO 2 */
    {B_FX, 3, {PG_DLY, PG_REV, PG_CHO}},                 /* FX: DELAY, REVERB, CHORUS */
    {B_GLO, 2, {PG_GLO, PG_GLO2, 0}},                    /* GLO: GLOBAL, GLOBAL 2 */
    {B_SCL, 2, {0u, 1u, 0}},                             /* SEL: OSC 1, OSC 2 */
    {B_EDIT, 2, {PG_AMP, PG_AENV, 0}},                   /* EDIT: AMP, AMP ENV */
    {B_SEQ, 2, {PG_FILT, PG_FENV, 0}},                   /* SEQ: FILTER, FILTER ENV */
};
#define NCYC (sizeof(CYCLES) / sizeof(CYCLES[0]))

static struct {
    uint32_t page;
    uint32_t sel_page;           /* the last of SELECT's pages shown (SELECT on an LFO or FX page returns to it) */
    uint32_t color;              /* the palette shown (P_COLOR), 0xFF none yet */
    int32_t octave;              /* -2 .. +2 */
    uint32_t held;               /* the keys held (bit n), as sent */
    uint8_t note[NKEYS];         /* the note each held key sent (an octave change keeps its note-off right) */
    uint32_t sig_top, sig_bot;   /* what each strip shows (0: draw again) */
    uint32_t wt_live, wt_ms;     /* the WT view: the position drawn (knob + LFO), when (redrawn at most 20 a second
                                  * for the LFO alone) */
    uint32_t cpu_ms;             /* when the CPU figure was taken */
    uint32_t cpu_shown;
    uint32_t pre;                /* the preset loaded last (presets.c's list), PRE_NONE none */
    uint32_t pre_ed;             /* a knob moved since: the sound is not that preset any more */
    uint32_t save_slot;          /* the SAVE page's user slot */
    char save_name[PRE_NAME + 1]; /* .. the name to write, space-padded (SAVE AS: edited) */
    uint32_t save_cur;           /* .. the character KNOB 3 changes (0 .. PRE_NAME - 1) */
    uint32_t save_ret;           /* the page SAVE goes back to */
    uint32_t shift;              /* HOME held: the + pages */
    uint32_t set_dirty;          /* a GLO setting changed: written when the page is left */
    const char *msg;             /* a word for the top strip (SAVED ...), until msg_until */
    char msg_b[16];
    uint32_t msg_until;
} ui;
#define PRE_NONE 0xFFFFu

static void ui_init(void)
{
    ui.page = ui.sel_page = 0;
    ui.octave = 0;
    ui.held = 0;
    ui.sig_top = ui.sig_bot = 0;
    ui.pre = 0;                                          /* power-on: INIT (the parameters' defaults) */
    ui.color = 0xFFu;                                    /* (the palette set at the first frame) */
    ui.pre_ed = 0;
    ui.msg = 0;
}

static void ui_say(const char *a, const char *b)         /* a + b on the top strip for 1.5 s */
{
    uint32_t k = 0;
    while (*a && k < sizeof ui.msg_b - 1u)
        ui.msg_b[k++] = *a++;
    while (b && *b && k < sizeof ui.msg_b - 1u)
        ui.msg_b[k++] = *b++;
    ui.msg_b[k] = 0;
    ui.msg = ui.msg_b;
    ui.msg_until = fm1_ms + 1500u;
}

/* the SAVE page's characters (KNOB 3 turns through them; the font's own: md_font.txt) */
static const char NAME_CH[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-+#";
#define NAME_NCH (sizeof NAME_CH - 1u)

static uint32_t name_ch_idx(char c)                      /* c's place in NAME_CH, 0 (space) if none */
{
    uint32_t i;
    for (i = 0; i < NAME_NCH; i++)
        if (NAME_CH[i] == c)
            return i;
    return 0;
}

/* the SAVE button. Off the SAVE page: open it, the slot the loaded user preset's (SAVE, SAVE: written over it), else
 * the first empty one, else U01; the name the loaded preset's ("F4 PAD" -> "PAD"; INIT or none: "USER"), space-padded
 * to PRE_NAME for editing. On it: the sound written to that slot under that name (trailing spaces dropped; all
 * spaces: "USER"), then back. SAVE AS: change the slot or the name before the second press */
static void save_press(void)
{
    char b[16];
    uint32_t i, k;
    int s;
    if (ui.page != PG_SAVE) {
        s = ui.pre == PRE_NONE ? -1 : preset_slot(ui.pre);
        if (s < 0)
            for (i = 0, s = 0; i < NUSER; i++)
                if (!((ubank.used >> i) & 1u)) {
                    s = (int)i;
                    break;
                }
        ui.save_slot = (uint32_t)s;
        for (k = 0; k < PRE_NAME; k++)
            ui.save_name[k] = ' ';
        ui.save_name[PRE_NAME] = 0;
        k = 0;
        if (ui.pre != PRE_NONE) {                        /* the loaded preset's name: after "F2 " / "U03 " */
            preset_name(b, ui.pre);
            for (i = 0; b[i] && b[i] != ' '; i++)
                ;
            for (; b[i] && b[i + 1u] && k < PRE_NAME; i++)
                ui.save_name[k++] = b[i + 1u];
        }
        if (k == 0u || (k == 4u && ui.save_name[0] == 'I' && ui.save_name[1] == 'N' && ui.save_name[2] == 'I' &&
                        ui.save_name[3] == 'T')) {
            ui.save_name[0] = 'U';
            ui.save_name[1] = 'S';
            ui.save_name[2] = 'E';
            ui.save_name[3] = 'R';
        }
        ui.save_cur = 0;
        ui.save_ret = ui.page;
        ui.page = PG_SAVE;
        return;
    }
    for (k = PRE_NAME; k > 0u && ui.save_name[k - 1u] == ' '; k--)
        ;                                                /* the name without its trailing spaces */
    for (i = 0; i < k; i++)
        b[i] = ui.save_name[i];
    b[k] = 0;
    if (!k) {
        b[0] = 'U'; b[1] = 'S'; b[2] = 'E'; b[3] = 'R'; b[4] = 0;
    }
    {
        char t[4];
        t[0] = 'U';
        t[1] = (char)('0' + (ui.save_slot + 1u) / 10u);
        t[2] = (char)('0' + (ui.save_slot + 1u) % 10u);
        t[3] = 0;
        if (preset_save(ui.save_slot, b) == 0) {
            ui.pre = (uint32_t)preset_index(ui.save_slot);
            ui.pre_ed = 0;
            ui_say("SAVED ", t);
        } else {
            ui_say("SAVE FAILED ", t);
        }
    }
    ui.page = ui.save_ret;
}

/* page v's knob k: its parameter (AMP ENV +'s VOICE is DETUN under MODE UNI; the OSC pages' first knob follows the
 * oscillator's engine: WAVE / DETUN / POS / POS, on its + page PW / SIDE / TABLE; GRAIN's + page is all its own:
 * SMPL SIZE DENS SPRAY), P_COUNT or more: none */
static uint32_t page_param(uint32_t v, uint32_t k)
{
    uint32_t id = PAGES[v].id[k & 3u], n;
    if (id == P_VOICES && par[P_MODE] == VM_UNI)
        return P_UDET;
    if ((v == PG_PLUS0 || v == PG_PLUS0 + 1u) && par[OSC_P(v - PG_PLUS0, 0)] == OE_GR)
        return GR_P(v - PG_PLUS0, k & 3u);
    if ((k & 3u) == 0u && (v <= 1u || v == PG_PLUS0 || v == PG_PLUS0 + 1u)) {
        n = v <= 1u ? v : v - PG_PLUS0;
        if (par[OSC_P(n, 0)] == OE_GR && v <= 1u)
            return OSC_P(n, 3);                          /* POS */
        if (par[OSC_P(n, 0)] == OE_SUPER)
            return v <= 1u ? OSC_P(n, 1) : OSC_P(n, 2);  /* DETUN / SIDE */
        if (par[OSC_P(n, 0)] == OE_WT)
            return v <= 1u ? OSC_P(n, 3) : OSC_P(n, 4);  /* POS / TABLE */
    }
    return id;
}

/* page pg has a + page (HOME): the sound's and the effects', GLO's and GLOBAL 2's INFO */
static int has_plus(uint32_t pg)
{
    return (pg <= PG_GLO && PLUS_OF[pg] != PG_NONE) || pg == PG_GLO2;
}

/* the page shown: its + page while SHIFT is on (HOME), else the page */
static uint32_t ui_view(void)
{
    if (ui.shift && ui.page <= PG_GLO && PLUS_OF[ui.page] != PG_NONE)
        return PLUS_OF[ui.page];
    if (ui.shift && ui.page == PG_GLO2)
        return PG_INFO;                                  /* (GLOBAL 2's + page: INFO too) */
    return ui.page;
}

static void key_down(uint32_t n)
{
    int32_t note = (int32_t)(KEY_NOTE0 + n) + 12 * ui.octave;
    ui.note[n] = (uint8_t)clamp(note, 0, 127);
    ui.held |= 1u << n;
    synth_note(ui.note[n], 100u, 1);
}

static void key_up(uint32_t n)
{
    ui.held &= ~(1u << n);
    synth_note(ui.note[n], 0u, 0);
}

static void ui_input(void)
{
    uint32_t pressed = fm1_input_edges(0), taps = fm1_input_note_edges(), now = fm1_in.notes, n, k, view, was = ui.page;
    uint32_t home = panel_btn_bit(B_HOME);
    int32_t d;
    for (n = 0; n < NKEYS; n++) {
        uint32_t bit = 1u << n;
        if (((taps | now) & bit) && !(ui.held & bit))
            key_down(n);                                 /* (a tap shorter than a frame: on, then off below) */
        if ((ui.held & bit) && !(now & bit))
            key_up(n);
    }
    if ((pressed & panel_btn_bit(B_OCTDN)) && ui.octave > -2)
        ui.octave--;
    if ((pressed & panel_btn_bit(B_OCTUP)) && ui.octave < 2)
        ui.octave++;
    if ((pressed & home) && has_plus(ui.page))           /* HOME: the + page on, or off; it stays (the knobs free) */
        ui.shift = !ui.shift;
    for (k = 0; k < NCYC; k++)                           /* a page button: its first page, or its next, round */
        if (pressed & panel_btn_bit(CYCLES[k].btn)) {
            uint32_t j = CYCLES[k].n;
            while (j-- > 0u && CYCLES[k].pg[j] != ui.page)
                ;
            ui.page = CYCLES[k].pg[j < CYCLES[k].n ? (j + 1u) % CYCLES[k].n : 0u];
            if (!OFF_SEL(ui.page))
                ui.sel_page = ui.page;
        }
    if (pressed & panel_btn_bit(B_SAVE))
        save_press();
    d = panel_enc(EN_PRESET);
    if (d) {                                             /* PRESETS: the next / previous one, loaded */
        uint32_t n = preset_count(), cur = ui.pre == PRE_NONE || ui.pre >= n ? 0u : ui.pre;
        char b[16];
        ui.pre = (uint32_t)((((int32_t)cur + d) % (int32_t)n + (int32_t)n) % (int32_t)n);   /* the detents turned,
                                                                                             * wrapping */
        ui.pre_ed = 0;
        preset_load(ui.pre);
        preset_name(b, ui.pre);
        ui_say(b, 0);
    }
    d = panel_enc(EN_ALGO);
    if (d) {                                             /* ALGORITHM: the shown oscillator's engine */
        uint32_t v = ui_view(), on = v == 1u || v == PG_PLUS0 + 1u;
        int32_t e = (((par[OSC_P(on, 0)] + d) % (int32_t)OE_N) + (int32_t)OE_N) % (int32_t)OE_N;
        synth_param(OSC_P(on, 0), e);
        ui.pre_ed = 1;
        ui_say(on ? "OSC 2 " : "OSC 1 ", N_ENG[e]);
    }
    d = panel_enc(EN_SELECT);
    if (d) {
        if (OFF_SEL(ui.page))                            /* out of the LFOs / FX: back where SELECT was */
            ui.page = ui.sel_page;
        else
            ui.page = SEL_PG[clamp(sel_pos(ui.page) + d, 0, (int32_t)NSEL - 1)];
        ui.sel_page = ui.page;
    }
    if (ui.page != was)
        ui.shift = 0;                                    /* another page: itself, not its + page */
    view = ui_view();
    for (k = 0; k < 4u; k++) {
        uint32_t id = page_param(view, k);
        d = panel_enc(EN_K1 + k);
        if (d && view == PG_SAVE) {                      /* SAVE: KNOB 1 the slot, 2 the cursor, 3 the character */
            if (k == 0u)
                ui.save_slot = (uint32_t)clamp((int32_t)ui.save_slot + d, 0, (int32_t)NUSER - 1);
            else if (k == 1u)
                ui.save_cur = (uint32_t)clamp((int32_t)ui.save_cur + d, 0, (int32_t)PRE_NAME - 1);
            else if (k == 2u) {
                int32_t c = (int32_t)name_ch_idx(ui.save_name[ui.save_cur]) + d;
                c = (c % (int32_t)NAME_NCH + (int32_t)NAME_NCH) % (int32_t)NAME_NCH;   /* wrapping */
                ui.save_name[ui.save_cur] = NAME_CH[c];
            }
        } else if (d && id < P_COUNT) {
            int32_t r = PARAMS[id].max - PARAMS[id].min;
            synth_param(id, par[id] + (r > 16 && (d > 1 || d < -1) ? d * 2 : d));   /* a quick turn: x2 */
            if (id < P_NPRESET)
                ui.pre_ed = 1;                           /* the sound: not the preset any more */
            else
                ui.set_dirty = 1;                        /* a setting: written when GLO is left */
        }
    }
    if ((was == PG_GLO || was == PG_GLO2) && ui.page != PG_GLO && ui.page != PG_GLO2 && ui.set_dirty) {
                                                         /* GLO left: the settings to flash */
        ui.set_dirty = 0;
        ui_say(settings_save() == 0 ? "SETTINGS SAVED" : "SETTINGS FAILED", 0);
    }
}

/* the backlight (as Hortator's BACKLIGHT DIM): what glows dimly while unlit, so the panel reads in the dark. The
 * buttons that do something (an unused one stays dark: the glow says what works) and every key. The simulator shows
 * it (sim_core.c sim_glow); the FM-1 dims it with led_dim.c's mid layer (ui_leds, synth.c leds_commit) */
static uint32_t ui_glow_all_btns(void)                  /* the buttons that do something */
{
    return 1u << B_HOME | 1u << B_ENV | 1u << B_LFO | 1u << B_FX | 1u << B_SAVE | 1u << B_GLO | 1u << B_SCL |
           1u << B_EDIT | 1u << B_SEQ | 1u << B_OCTDN |
           1u << B_OCTUP;
}
static uint32_t ui_glow_btns(void)
{
    return par[P_BLIGHT] == BL_DIM ? ui_glow_all_btns() : 0u;   /* (the simulator draws DIM's glow; INV / ON light
                                                                  * them fully: sim_leds) */
}
static uint32_t ui_glow_keys(void) { return par[P_BLIGHT] == BL_DIM ? (1u << NKEYS) - 1u : 0u; }

/* the LEDs as layers (as Hortator's ui_input.c): per scan column a word, bits 0..7 lit, 8..15 the backlight (led_dim.c's
 * mid layer: lit in 5 of 20 scan frames, ~25 %, no timed pulse: no CPU to speak of), packed row bits; one store per column (leds_commit: led_dim.c on the device, fm1_led in the simulator) so the
 * 10 kHz scan never sees half a picture. An LED's place in the matrix: hal/fm1_input.h's FM1_KEYMAP (ids 0..13 the
 * buttons as panel.c's btn, 14..40 the keys) */
static uint8_t led_pos[41];                              /* (column << 3) | row bit, 0xFF none */
static uint8_t led_pos_ok;

static void led_pos_init(void)
{
    uint32_t id, c, r;
    for (id = 0; id < 41u; id++) {
        led_pos[id] = 0xFF;
        for (c = 0; c < FM1_NCOL; c++)
            for (r = 1; r < 5u; r++)
                if (FM1_KEYMAP[r][c] == (int8_t)id)
                    led_pos[id] = (uint8_t)((c << 3) | r);
    }
    led_pos_ok = 1;
}

/* an LED into the layers, by GLO's LIGHT: DIM lit full, a backlit one dim (the mid layer); INV a backlit one full and
 * dark while lit (the stock firmware's way); ON every backlit one full; OFF only what is lit */
static void led_lay_put(uint32_t *lay, uint32_t id, int lit, int backlit)
{
    uint32_t q = led_pos[id], bit, m = (uint32_t)par[P_BLIGHT];
    if (q == 0xFFu)
        return;
    bit = 1u << (q & 7u);
    if (m == BL_INV && backlit)
        lay[q >> 3] |= lit ? 0u : bit;
    else if (lit || (m == BL_ON && backlit))
        lay[q >> 3] |= bit;
    else if (m == BL_DIM && backlit)
        lay[q >> 3] |= bit << 8;                         /* the mid layer */
}

static void ui_leds(void)
{
    uint32_t lay[FM1_NCOL] = {0}, n, gb = ui_glow_all_btns(), gk = (1u << NKEYS) - 1u, lit[NB] = {0};
    if (!led_pos_ok)
        led_pos_init();
    for (n = 0; n < NKEYS; n++)
        led_lay_put(lay, 14u + n, (int)((ui.held >> n) & 1u), (int)((gk >> n) & 1u));
    lit[B_OCTDN] = ui.octave < 0;
    lit[B_OCTUP] = ui.octave > 0;
    for (n = 0; n < NCYC; n++)                           /* a page button: lit on its own pages */
        lit[CYCLES[n].btn] |= ui.page == CYCLES[n].pg[0] || ui.page == CYCLES[n].pg[1] ||
                              (CYCLES[n].n > 2u && ui.page == CYCLES[n].pg[2]);
    lit[B_SAVE] = ui.page == PG_SAVE;
    lit[B_HOME] = ui.shift;                              /* SHIFT */
    for (n = 0; n < NB; n++)
        led_lay_put(lay, panel.btn[n], (int)lit[n], (int)((gb >> n) & 1u));
    leds_commit(lay);
}

static void num_text(char *b, int32_t v, int sign)         /* -128 .. 999 */
{
    char t[5];
    uint32_t i = 0, u = (uint32_t)(v < 0 ? -v : v);
    if (v < 0)
        *b++ = '-';
    else if (sign && v > 0)
        *b++ = '+';
    do
        t[i++] = (char)('0' + u % 10u);
    while ((u /= 10u) && i < 4u);
    while (i)
        *b++ = t[--i];
    *b = 0;
}

static void param_text(char *b, uint32_t id)
{
    const param_t *p = &PARAMS[id];
    if (p->names) {
        const char *s = p->names[clamp(par[id], p->min, p->max) - p->min];
        while ((*b++ = *s++))
            ;
        return;
    }
    num_text(b, par[id], p->min < 0);
}

static uint32_t hash(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

/* page v's knobs: their names at y, their values under them (the top strip's last lines) */
static void draw_nums(uint32_t v, int32_t y)
{
    char b[16];
    uint32_t k;
    for (k = 0; k < 4u; k++) {
        uint32_t id = page_param(v, k);
        const param_t *p;
        int32_t x0 = (int32_t)k * 60;
        if (id >= P_COUNT)
            continue;
        p = &PARAMS[id];
        {                                                /* PW: SHAPE on the waves other than SQR */
            const char *lb = (id == P_PW1 || id == P_PW2) && par[id == P_PW1 ? P_WAVE : P_WAVE2] != W_SQR ? "SHAPE"
                                                                                                         : p->label;
            md_text(x0 + 30 - md_text_w(&MD_F3, 2, lb) / 2, y, &MD_F3, 2, lb, C_INK);
        }
        param_text(b, id);
        md_text(x0 + 30 - md_text_w(&MD_F3, 2, b) / 2, y + 14, &MD_F3, 2, b, C_INK);
    }
}

static void draw_top(void)
{
    char b[16];
    int32_t x;
    uint32_t v = ui_view();
    cv_begin(240, 120, C_BG);
    if (ui.msg) {                                        /* the header: a word for a while, else the preset */
        md_text(12, 8, &MD_F3, 2, ui.msg, C_INK);
    } else if (ui.pre != PRE_NONE) {
        char pn[16];
        preset_name(pn, ui.pre);
        x = md_text(12, 8, &MD_F3, 2, pn, C_INK);
        if (ui.pre_ed)
            md_text(x + 4, 8, &MD_F3, 2, "*", C_INK);
    }
    {                                       /* the header's right: O(ctave) V(oices) C(PU), each letter apart */
        static const char *const L[3] = {"O", "V", "C"};
        char nb[3][8];
        int32_t k, w = 0;
        num_text(nb[0], ui.octave, 1);
        num_text(nb[1], (int32_t)synth_voices(), 0);
        num_text(nb[2], (int32_t)ui.cpu_shown, 0);
        nb[2][3] = 0;
        for (k = 0; nb[2][k]; k++)
            ;
        nb[2][k] = '%';
        nb[2][k + 1] = 0;
        for (k = 0; k < 3; k++)
            w += md_text_w(&MD_F3, 2, L[k]) + 4 + md_text_w(&MD_F3, 2, nb[k]) + (k ? 12 : 0);
        x = 228 - w;
        for (k = 0; k < 3; k++) {
            x = md_text(x + (k ? 12 : 0), 8, &MD_F3, 2, L[k], C_INK) + 2;
            x = md_text(x, 8, &MD_F3, 2, nb[k], C_INK) - 2;
        }
    }
    cv_rect(12, 22, 216, 1, C_INK);
    x = md_text(12, 32, &MD_F3, 4, PAGES[v].title, C_INK);
    if (v <= 1u && par[OSC_P(v, 0)] != OE_STD)          /* OSC 1 / 2: the engine, unless STD */
        md_text(x + 12, 32, &MD_F3, 4, N_ENG[par[OSC_P(v, 0)]], C_INK);
    if ((v < PG_PLUS0 || v > PG_INFO) && v != PG_SAVE) {   /* the title's right: n / pages (a + page: its "+") */
        uint32_t pos = 0, tot = 1, c, j;                 /* n / its button's pages (a sound page: its section's) */
        for (c = 0; c < NCYC; c++)
            for (j = 0; j < CYCLES[c].n; j++)            /* (the last button with it: SEL EDIT SEQ's) */
                if (CYCLES[c].pg[j] == v) {
                    pos = j;
                    tot = CYCLES[c].n;
                }
        num_text(b, (int32_t)pos + 1, 0);
        b[1] = '/';
        b[2] = (char)('0' + tot);
        b[3] = 0;
        md_text(228 - md_text_w(&MD_F3, 2, b), 42, &MD_F3, 2, b, C_INK);
    }
    draw_nums(v, 74);                                    /* the knobs' numbers (the bottom: the wave, the bars) */
    cv_blit(0, 0);
}

static void draw_save(void)                              /* the SAVE page: the slot, the name, how to write */
{
    char b[16];
    uint32_t sl = ui.save_slot, i;
    cv_begin(240, 120, C_BG);
    b[0] = 'U';
    b[1] = (char)('0' + (sl + 1u) / 10u);
    b[2] = (char)('0' + (sl + 1u) % 10u);
    b[3] = 0;
    md_text(12, 4, &MD_F3, 4, b, C_INK);
    if ((ubank.used >> sl) & 1u) {                       /* what the slot holds: written over */
        md_text(84, 4, &MD_F3, 2, "OVER", C_INK);
        for (i = 0; i < PRE_NAME && ubank.p[sl].name[i]; i++)
            b[i] = ubank.p[sl].name[i];
        b[i] = 0;
        md_text(84, 16, &MD_F3, 2, b, C_INK);
    } else {
        md_text(84, 10, &MD_F3, 2, "EMPTY", C_INK);
    }
    for (i = 0; i < PRE_NAME; i++) {                     /* the name, a cell a character, the cursor's underlined */
        char c[2] = {ui.save_name[i], 0};
        int32_t x = 12 + (int32_t)i * 27;
        md_text(x + 4, 34, &MD_F3, 4, c, C_INK);
        cv_rect(x, 58, 21, i == ui.save_cur ? 3 : 1, C_INK);
    }
    md_text(12, 70, &MD_F3, 2, "K1 SLOT K2 POS K3 CHAR", C_INK);
    md_text(12, 86, &MD_F3, 2, "SAVE AGAIN: WRITE", C_INK);
    md_text(12, 102, &MD_F3, 2, "SELECT, HOME: BACK", C_INK);
    cv_blit(0, 120);
}

static void draw_info(void)                              /* the INFO page: what this FM-1 runs and keeps */
{
    char b[16];
    uint32_t s, n = 0;
    int32_t x;
    cv_begin(240, 120, C_BG);
    x = md_text(12, 4, &MD_F3, 2, "VERSION", C_INK);
    md_text(x + 12, 4, &MD_F3, 2, SYNTH_VERSION, C_INK);
    x = md_text(12, 22, &MD_F3, 2, "BUILD", C_INK);
    md_text(x + 12, 22, &MD_F3, 2, SYNTH_BUILD, C_INK);
    for (s = 0; s < NUSER; s++)
        n += (ubank.used >> s) & 1u;
    x = md_text(12, 40, &MD_F3, 2, "PRESETS", C_INK);
    num_text(b, (int32_t)n, 0);
    x = md_text(x + 12, 40, &MD_F3, 2, b, C_INK);
    md_text(x + 8, 40, &MD_F3, 2, "OF 16 USED", C_INK);
    x = md_text(12, 58, &MD_F3, 2, "FLASH", C_INK);
    md_text(x + 12, 58, &MD_F3, 2, flash_ok ? "OK" : "NOT FOUND", C_INK);
    md_text(12, 86, &MD_F3, 2, "LET GO OF HOME: BACK", C_INK);
    cv_blit(0, 120);
}

/* the oscillator a page shows: OSC 1 / 2 and their + pages, 2: none */
static uint32_t view_osc(uint32_t v)
{
    return v <= 1u ? v : v == PG_PLUS0 || v == PG_PLUS0 + 1u ? v - PG_PLUS0 : 2u;
}

/* OSC n's WT position now, knob + the LFOs' POS: Q8 of the bank's waves (0 .. (waves - 1) << 8) */
static uint32_t wt_live_pos(uint32_t n)
{
    const wt_bank_t *bk = &WT_BANKS[(uint32_t)clamp(par[OSC_P(n, 4)], 0, WT_NBANK - 1)];
    return (uint32_t)clamp(par[OSC_P(n, 3)] + lfo_pos(n), 0, 127) * (bk->n - 1u) * 256u / 127u;
}

#define WV_X 12                                          /* the wave area: x 12 .. 227, under the knobs' numbers */
#define WV_W 216
#define WV_PH 39768216u                                  /* 2 ^ 33 / 216: two cycles across the area */
static int16_t wv_y[WV_W];                               /* a curve to plot: y a pixel */

/* wv_y[0 .. w) from x0: 0 faint (dashes: 2 px on, 2 off; the steep parts dotted), 1 solid, 2 bold (two pixels) */
static void wv_plot(int32_t x0, uint32_t w, uint32_t style)
{
    uint32_t i;
    for (i = 1; i < w; i++) {
        if (style == 0u) {
            int32_t d = wv_y[i] - wv_y[i - 1];
            if (d > 1 || d < -1)
                cv_line_dot(x0 + (int32_t)i - 1, wv_y[i - 1], x0 + (int32_t)i, wv_y[i], C_INK);
            else if (!(i & 3u))
                cv_line(x0 + (int32_t)i - 1, wv_y[i - 1], x0 + (int32_t)i, wv_y[i], C_INK);
            continue;
        }
        cv_line(x0 + (int32_t)i - 1, wv_y[i - 1], x0 + (int32_t)i, wv_y[i], C_INK);
        if (style == 2u)
            cv_line(x0 + (int32_t)i - 1, wv_y[i - 1] + 1, x0 + (int32_t)i, wv_y[i] + 1, C_INK);
    }
}

static int32_t wt_peak = 30000;                          /* the bank drawn's peak: +-amp */

/* bank bk's wave at Q8 position q (the 64-point level, the two waves either side mixed as the oscillator does),
 * w pixels a cycle, into wv_y around y0, +-amp */
static void wt_curve(const wt_bank_t *bk, uint32_t q, uint32_t w, int32_t y0, int32_t amp)
{
    uint32_t fa = q >> 8, fb = fa + 1u < bk->n ? fa + 1u : fa, px;
    int32_t frac = (int32_t)(q & 255u);
    const int16_t *wa = bk->data + fa * WT_FRAME + WT_LOFF[4], *wb = bk->data + fb * WT_FRAME + WT_LOFF[4];
    for (px = 0; px < w; px++) {
        uint32_t ph = px * 64u * 256u / w, i = ph >> 8, j = (i + 1u) & 63u, f = ph & 255u;
        int32_t sa = wa[i] + (((wa[j] - wa[i]) * (int32_t)f) >> 8), sb = wb[i] + (((wb[j] - wb[i]) * (int32_t)f) >> 8);
        wv_y[px] = (int16_t)(y0 - (sa + (((sb - sa) * frac) >> 8)) * amp / wt_peak);
    }
}

/* num_text at the end of b */
static void num_cat(char *b, int32_t v)
{
    while (*b)
        b++;
    num_text(b, v, 0);
}

/* WT, as ObsSynth4D draws it: the bank's waves stacked from the front (bottom left, the first) to the back (top
 * right), faint; the knob's wave solid when the LFO moves it away; the live one (knob + LFO) bold; the bank and
 * "knob > live / waves" over it, the table along the bottom (the knob a tick, the live position a block) */
#define WT_FW 134                                        /* a wave's width */
#define WT_YB 92                                         /* the front wave's centre line */
#define WT_RISE 54                                       /* the back wave's, so many px up */
static void draw_wt(uint32_t n)
{
    const wt_bank_t *bk = &WT_BANKS[(uint32_t)clamp(par[OSC_P(n, 4)], 0, WT_NBANK - 1)];
    uint32_t nw = bk->n, shown = nw < 16u ? nw : 16u, live = wt_live_pos(n), j, e;
    uint32_t knob = (uint32_t)clamp(par[OSC_P(n, 3)], 0, 127) * (nw - 1u) * 256u / 127u;
    uint32_t span = (nw - 1u) << 8;                      /* (positions are Q8 of waves: 0 .. span) */
    wt_peak = 1;
    int32_t x;
    char b[16];
    for (j = 0; j < nw * 64u; j++) {                     /* the bank's peak (at the level drawn): its full height */
        int32_t a = bk->data[(j >> 6) * WT_FRAME + WT_LOFF[4] + (j & 63u)];
        a = a < 0 ? -a : a;
        wt_peak = a > wt_peak ? a : wt_peak;
    }
    for (j = shown; j-- > 0;) {                          /* the stack, faint */
        uint32_t q = (j * (nw - 1u) / (shown - 1u)) << 8;
        wt_curve(bk, q, WT_FW, WT_YB - (int32_t)(q * WT_RISE / span), 16);
        wv_plot(WV_X + (int32_t)(q * (WV_W - WT_FW) / span), WT_FW, 0);
    }
    if (knob >> 5 != live >> 5) {                        /* the knob's own wave */
        wt_curve(bk, knob, WT_FW, WT_YB - (int32_t)(knob * WT_RISE / span), 16);
        wv_plot(WV_X + (int32_t)(knob * (WV_W - WT_FW) / span), WT_FW, 1);
    }
    wt_curve(bk, live, WT_FW, WT_YB - (int32_t)(live * WT_RISE / span), 16);
    wv_plot(WV_X + (int32_t)(live * (WV_W - WT_FW) / span), WT_FW, 2);
    md_text(WV_X, 4, &MD_F3, 2, bk->name, C_INK);
    b[0] = 0;
    if (knob >> 8 != live >> 8) {
        num_cat(b, (int32_t)(knob >> 8) + 1);
        for (e = 0; b[e]; e++)
            ;
        b[e] = '>';
        b[e + 1u] = 0;
    }
    num_cat(b, (int32_t)(live >> 8) + 1);
    for (e = 0; b[e]; e++)
        ;
    b[e] = '/';
    b[e + 1u] = 0;
    num_cat(b, (int32_t)nw);
    md_text(WV_X + WV_W - md_text_w(&MD_F3, 2, b), 4, &MD_F3, 2, b, C_INK);
    cv_hdot(WV_X, WV_X + WV_W - 1, 117, C_INK);         /* the table */
    x = WV_X + (int32_t)(knob * (WV_W - 1) / span);
    cv_rect(x, 113, 1, 7, C_INK);
    x = WV_X + (int32_t)(live * (WV_W - 1) / span);
    cv_rect(x - 2, 115, 5, 5, C_INK);
}

/* STD: two cycles of the wave (SQR at its PW), as the oscillator makes it */
static void draw_std(uint32_t n)
{
    int32_t wave = par[n ? P_WAVE2 : P_WAVE];
    uint32_t pw = 0x80000000u + (uint32_t)((clamp(par[n ? P_PW2 : P_PW1], 0, 127) - 64) * 0x1CCCCCD), px;
    char b[16];
    for (px = 0; px < WV_W; px++)
        wv_y[px] = (int16_t)(64 - osc_tick(px * WV_PH + (wave == W_SAW ? 0x80000000u : 0u), WV_PH, wave, pw) * 44 /
                             32767);                     /* (the saw from its zero) */
    cv_hdot(WV_X, WV_X + WV_W - 1, 64, C_INK);
    wv_plot(WV_X, WV_W, 2);
    param_text(b, n ? P_WAVE2 : P_WAVE);
    md_text(WV_X, 4, &MD_F3, 2, b, C_INK);
}

/* SUPER: the centre saw bold, the six around it faint at their level (SIDE), fanning out from the middle as far as
 * DETUN takes them (twelve times the real spread, to be seen over two cycles) */
static void draw_super(uint32_t n)
{
    int32_t det = par[OSC_P(n, 1)], side = par[OSC_P(n, 2)], ac = 44 * (32767 - side * 103) / 32767;
    int32_t as = 44 * side / 127;                        /* (the sides drawn as loud as the centre at SIDE 127) */
    uint32_t px, k;
    for (k = 0; k < 6u; k++) {
        int64_t d = 65536 + 12 * (SOFF[k] * det * 60 * 38 / (1000 * 127));
        for (px = 0; px < WV_W; px++) {
            int64_t off = ((int64_t)px - (int64_t)(WV_W / 2)) * (int64_t)WV_PH * d >> 16;
            uint32_t ph = (WV_W / 2u) * WV_PH + 0x80000000u + (uint32_t)off;
            wv_y[px] = (int16_t)(64 - saw_tick(ph, WV_PH) * as / 32767);
        }
        wv_plot(WV_X, WV_W, 0);
    }
    for (px = 0; px < WV_W; px++)
        wv_y[px] = (int16_t)(64 - saw_tick(px * WV_PH + 0x80000000u, WV_PH) * ac / 32767);
    wv_plot(WV_X, WV_W, 2);
    md_text(WV_X, 4, &MD_F3, 2, "7 SAWS", C_INK);
}

/* the voice the GRAIN view follows: the newest sounding, -1 none */
static int32_t gr_voice(void)
{
    int32_t b = -1;
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (vc[i].active && (b < 0 || vc[i].age > vc[(uint32_t)b].age))
            b = (int32_t)i;
    return b;
}

/* OSC n's GRAIN position now, knob + the LFOs' POS, in the sample's samples */
static uint32_t gr_live_pos(uint32_t n, uint32_t len)
{
    return (uint32_t)clamp(par[OSC_P(n, 3)] + lfo_pos(n), 0, 127) * (len - 2u) / 127u;
}

/* GRAIN: the sample (its overview: the outline, a dotted fill), and the newest voice's grains live: each one's window
 * as an arc under the sample, over the part it reads, and where it reads now a bar in the sample as tall as the window
 * is open; the sample's name and the grains sounding over it, along the bottom POS (a block) and SPRAY's reach */
#define GR_YW 48                                         /* the sample's centre line */
#define GR_YA 106                                        /* the grains' arcs: their base */
static void draw_gr(uint32_t n)
{
    const gr_smp_t *sm = &GR_SMPS[(uint32_t)clamp(par[GR_P(n, 0)], 0, GR_NSMP - 1)];
    uint32_t len = sm->n, pos = gr_live_pos(n, len), spray = len * (uint32_t)clamp(par[GR_P(n, 3)], 0, 127) / 254u;
    int32_t vi = gr_voice(), x, cnt = 0, x0, x1;
    char b[16];
    for (x = 0; x < WV_W; x++) {                         /* the sample */
        int32_t ya = GR_YW - sm->ov[1][x] * 26 / 127, yb = GR_YW - sm->ov[0][x] * 26 / 127;
        cv_pset(WV_X + x, ya, C_INK);
        cv_pset(WV_X + x, yb, C_INK);
        if (!(x & 1))
            cv_vdot(WV_X + x, ya + 2, yb - 1, C_INK);
    }
    if (vi >= 0) {                                       /* the grains */
        const voice_t *v = &vc[(uint32_t)vi];
        const gr_t *g = &v->gr[n];
        int32_t note = (int32_t)v->note + par[P_TUNE] + (n ? par[P_SEMI2] : 0);
        uint32_t ginc = (uint32_t)(((uint64_t)pitch_inc(note * 16) << 11) / pitch_inc((int32_t)sm->root * 16)), k;
        for (k = 0; k < NGR; k++) {
            uint32_t w = g->w[k], p = g->p[k] >> 12, span, st, px;
            int32_t h, py = 0;
            if (!w || !g->winc[k])
                continue;
            cnt++;
            span = (uint32_t)(((uint64_t)(0xFFFFFFFFu / g->winc[k]) * ginc) >> 12);
            st = (uint32_t)(((uint64_t)span * (w >> 16)) >> 16);
            st = st < p ? p - st : 0u;
            x0 = WV_X + (int32_t)(st * (WV_W - 1u) / len);
            x1 = x0 + (int32_t)(span * (WV_W - 1u) / len);
            x1 = x1 < x0 + 4 ? x0 + 4 : x1;
            for (x = x0; x <= x1 && x < WV_X + WV_W; x++) {   /* its window, as an arc (to the sample's end) */
                int32_t y = GR_YA - HANN_Q15[(uint32_t)(x - x0) * 256u / (uint32_t)(x1 - x0)] * 22 / 32767;
                if (x > x0)
                    cv_line(x - 1, py, x, y, C_INK);
                py = y;
            }
            h = HANN_Q15[w >> 24] * 26 / 32767;          /* where it reads now, as open as its window */
            px = (uint32_t)WV_X + p * (WV_W - 1u) / len;
            cv_rect((int32_t)px - 1, GR_YW - 27, 4, 55, C_BG);   /* (clear of the sample's dots) */
            cv_rect((int32_t)px, GR_YW - h, 2, 2 * h + 1, C_INK);
        }
    }
    cv_hdot(WV_X, WV_X + WV_W - 1, 117, C_INK);         /* POS and SPRAY's reach */
    x0 = WV_X + (int32_t)((pos > spray ? pos - spray : 0u) * (WV_W - 1u) / len);
    x1 = WV_X + (int32_t)((pos + spray < len ? pos + spray : len - 1u) * (WV_W - 1u) / len);
    cv_rect(x0, 113, x1 - x0 + 1, 1, C_INK);
    cv_rect(x0, 113, 1, 4, C_INK);
    cv_rect(x1, 113, 1, 4, C_INK);
    x = WV_X + (int32_t)(pos * (WV_W - 1u) / len);
    cv_rect(x - 2, 115, 5, 5, C_INK);
    md_text(WV_X, 4, &MD_F3, 2, sm->name, C_INK);
    if (cnt) {
        num_text(b, cnt, 0);
        x = md_text_w(&MD_F3, 2, "GRAINS");
        x0 = md_text(WV_X + WV_W - x - 6 - md_text_w(&MD_F3, 2, b), 4, &MD_F3, 2, b, C_INK);
        md_text(x0 + 4, 4, &MD_F3, 2, cnt > 1 ? "GRAINS" : "GRAIN", C_INK);
    }
}

/* an oscillator page (n its oscillator): the knobs' numbers, the wave under them */
static void draw_osc(uint32_t v, uint32_t n)
{
    (void)v;
    cv_begin(240, 120, C_BG);
    if (par[OSC_P(n, 0)] == OE_GR)
        draw_gr(n);
    else if (par[OSC_P(n, 0)] == OE_WT)
        draw_wt(n);
    else if (par[OSC_P(n, 0)] == OE_SUPER)
        draw_super(n);
    else
        draw_std(n);
    cv_blit(0, 120);
}

/* ---- each page's own view, the bottom strip (the knobs' numbers are on top): the envelopes' curves, the LFOs'
 * waves, the filter's response, the effects as pictures of what they do, AMP's voices, GLOBAL's velocity curve and
 * MIDI monitor, GLOBAL 2's palettes. Each draws into the canvas cv_begin'd by draw_bottom; view_live (ui_draw) says
 * when one moves by itself (redrawn at most 20 times a second) */

static const char *const NOTE_NM[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static void note_text(char *b, uint32_t note)            /* "C4", "F#2" */
{
    const char *s = NOTE_NM[note % 12u];
    while (*s)
        *b++ = *s++;
    num_text(b, (int32_t)(note / 12u) - 1, 0);
}

/* the block marking a live point: a gap around it, so it reads on any line */
static void live_dot(int32_t x, int32_t y)
{
    cv_rect(x - 4, y - 4, 9, 9, C_BG);
    cv_rect(x - 3, y - 3, 7, 7, C_INK);
}

static void text_right(int32_t y, const char *s) { md_text(WV_X + WV_W - md_text_w(&MD_F3, 2, s), y, &MD_F3, 2, s, C_INK); }

/* AMP ENV (f 0) or FILTER ENV (f 1): the ADSR as it sounds (the attack's rounded rise, the decay and the release
 * falling exponentially), the segments as wide as their knobs (their times go in even steps), SUS a dotted level;
 * LOOP said; the newest voice's envelope a point running along the curve */
#define EV_YB 104                                        /* the curve's floor; its top EV_YB - EV_H */
#define EV_H 88
static void draw_env(uint32_t f)
{
    int32_t a = par[f ? P_FATK : P_AATK], d = par[f ? P_FDEC : P_ADEC], s = par[f ? P_FSUS : P_ASUS];
    int32_t r = par[f ? P_FREL : P_AREL], sq = s * 32767 / 127, x, lvl;
    int32_t xa = WV_X + 4 + a * 48 / 127, xd = xa + 4 + d * 48 / 127, xs = xd + 40, xr = xs + 4 + r * 48 / 127;
    int32_t vi = gr_voice();
    for (x = WV_X; x <= xr; x++) {                       /* the curve, a column at a time */
        if (x < xa)
            lvl = ENV_ATK_SHAPE[(x - WV_X) * 64 / (xa - WV_X)];
        else if (x < xd)
            lvl = sq + (((32767 - sq) * ENV_DEC_SHAPE[(x - xa) * 64 / (xd - xa)]) >> 15);
        else if (x < xs)
            lvl = sq;
        else
            lvl = (sq * ENV_DEC_SHAPE[clamp((x - xs) * 64 / (xr - xs), 0, 64)]) >> 15;
        wv_y[x - WV_X] = (int16_t)(EV_YB - lvl * EV_H / 32767);
    }
    cv_hdot(WV_X, WV_X + WV_W - 1, EV_YB, C_INK);
    cv_hdot(xd, xs, EV_YB - sq * EV_H / 32767, C_INK);
    cv_vdot(xa, EV_YB - EV_H, EV_YB, C_INK);
    cv_vdot(xd, EV_YB - EV_H, EV_YB, C_INK);
    cv_vdot(xs, EV_YB - EV_H, EV_YB, C_INK);
    wv_plot(WV_X, (uint32_t)(xr - WV_X + 1), 2);
    md_text((WV_X + xa) / 2 - 3, 108, &MD_F3, 2, "A", C_INK);
    md_text((xa + xd) / 2 - 3, 108, &MD_F3, 2, "D", C_INK);
    md_text((xd + xs) / 2 - 3, 108, &MD_F3, 2, "S", C_INK);
    md_text((xs + xr) / 2 - 3, 108, &MD_F3, 2, "R", C_INK);
    if (par[f ? P_FLOOP : P_ALOOP])
        text_right(4, "LOOP");
    if (vi >= 0) {                                       /* the newest voice: where its envelope is */
        const env_t *e = f ? &vc[(uint32_t)vi].flt : &vc[(uint32_t)vi].amp;
        int32_t y = EV_YB - (e->lvl >> 9) * EV_H / 32767, x0, x1, best = 0, bd = 1 << 30;
        if (e->st == ES_ATK) {
            x0 = WV_X;
            x1 = xa;
        } else if (e->st == ES_DEC && (e->lvl >> 9) - sq > 400) {
            x0 = xa;
            x1 = xd;
        } else if (e->st == ES_DEC) {
            x0 = x1 = (xd + xs) / 2;                     /* (at SUS: held) */
        } else {
            x0 = xs;
            x1 = xr;
        }
        for (x = x0; x <= x1 && x <= xr; x++) {
            int32_t dd = wv_y[x - WV_X] - y;
            dd = dd < 0 ? -dd : dd;
            if (dd < bd) {
                bd = dd;
                best = x;
            }
        }
        if (e->st != ES_IDLE)
            live_dot(best, y);
    }
}

/* LFO l: its wave over two cycles, as tall as DEPTH (upside down below 0), what it moves written over it, its rate
 * in Hz and TRIG under it; where it is now a point on the first cycle */
static int32_t lfo_shape(int32_t w, uint32_t ph)         /* LFO wave w at phase ph, Q15 (S&H: a fixed pattern) */
{
    int32_t x;
    switch (w) {
    case LW_TRI:
        x = (int32_t)(ph >> 16) - 32768;
        return 2 * (x < 0 ? -x : x) - 32767;
    case LW_SAW:
        return (int32_t)(ph >> 16) - 32768;
    case LW_SQR:
        return ph < 0x80000000u ? 32767 : -32767;
    case LW_SH: {
        static const int16_t SH[8] = {12000, -26000, 30000, -4000, 20000, -30000, 2000, -16000};
        return SH[ph >> 29];
    }
    default:
        return sine_i(ph);
    }
}

static void draw_lfo(uint32_t l)
{
    int32_t w = par[LFO_P(l, 0)], dep = par[LFO_P(l, 3)], amp = (dep < 0 ? -dep : dep) * 36 / 64, sg = dep < 0 ? -1 : 1;
    const char *dn = N_LDEST_LONG[(uint32_t)clamp(par[LFO_P(l, 2)], 0, LD_N - 1)];
    uint32_t px, hz = LFO_HZ_X100[(uint32_t)clamp(par[LFO_P(l, 1)], 0, 127)];
    char b[16];
    int32_t x;
    md_text(120 - md_text_w(&MD_F3, 2, dn) / 2, 4, &MD_F3, 2, dn, C_INK);
    for (px = 0; px < WV_W; px++)
        wv_y[px] = (int16_t)(62 - sg * lfo_shape(w, px * WV_PH) * amp / 32767);
    cv_hdot(WV_X, WV_X + WV_W - 1, 62, C_INK);
    wv_plot(WV_X, WV_W, 2);
    md_text(WV_X, 108, &MD_F3, 2, par[LFO_X(l, 0)] ? "NOTE" : "FREE", C_INK);
    num_text(b, (int32_t)(hz / 100u), 0);            /* the rate: "1.25 HZ" */
    for (x = 0; b[x]; x++)
        ;
    b[x++] = '.';
    b[x++] = (char)('0' + hz / 10u % 10u);
    b[x++] = (char)('0' + hz % 10u);
    b[x++] = ' ';
    b[x++] = 'H';
    b[x++] = 'Z';
    b[x] = 0;
    text_right(108, b);
    if (amp) {                                           /* where it is: its phase, its output now (FADE in it) */
        int32_t ld = lfo_depth(l);
        x = WV_X + (int32_t)((lfo.ph[l] >> 16) * 107u / 65535u);
        live_dot(x, 62 - lfo.out[l] * (ld < 0 ? -ld : ld) * 36 / 64 / 32767 * sg);
    }
}

/* FILTER: its response, gain against frequency over the cutoff's range (50 Hz .. 18 kHz left to right, +18 .. -48
 * dB): TYPE, SLOPE, RESO as the filter computes them; the cutoff a dotted line, 0 dB a dotted level; the newest
 * voice's (ENV, KEY, LFO in it) bold over the knob's while a note sounds */
static int32_t fr_h2(int32_t d, int32_t k, int32_t type)  /* |H|^2 (Q16) d units from the cut, damping k (Q12) */
{
    int64_t r2 = FR_R2[clamp(d, -50, 50) + 50], a = 65536 - r2, k16 = (int64_t)k << 4, den, num;
    den = a * a + (((k16 * k16) >> 16) * r2);
    if (den < 1)
        den = 1;
    switch (type) {
    case FT_HP:
        num = r2 * r2;
        break;
    case FT_BP:
        num = r2 << 16;
        break;
    case FT_NOTCH:
        num = a * a;
        break;
    default:
        num = (int64_t)1 << 32;
        break;
    }
    {                                                    /* num / den, Q14 (room for the resonant peaks), unsigned
                                                          * 64 / 32 (the firmware links no signed 64-bit divide) */
        uint64_t un = (uint64_t)num << 14, ud = (uint64_t)den, q;
        while (ud >> 32) {
            ud >>= 1;
            un >>= 1;
        }
        q = un / (uint32_t)ud;
        return q < 1u ? 1 : q > 0x7FFFFFFFu ? 0x7FFFFFFF : (int32_t)q;
    }
}

static int32_t fr_db10(int32_t d, int32_t k, int32_t type, int32_t slope24)   /* the gain in dB x 10 */
{
    int64_t h = fr_h2(d, k, type);
    int32_t msb = 0, lg, out;
    if (slope24)                                         /* (the 24 dB pair: Q 0.54, then Q 1.31 x the resonance) */
        h = (fr_h2(d, 7578, type) * (int64_t)fr_h2(d, k * 2216 >> 12, type)) >> 14;
    if (h < 1)
        h = 1;
    while ((h >> msb) > 1)
        msb++;
    lg = (msb - 14) * 256 + LOG2_FRAC[(uint32_t)((h << 6 >> msb) & 63)];   /* log2(|H|^2), 1/256 */
    out = lg * 30103 / 256000;                           /* (10 log10 2 = 3.0103: dB x 10; 32-bit) */
    if ((type == FT_LP && d > 50) || (type == FT_HP && d < -50))   /* (past the table: the slope goes on) */
        out -= (d > 0 ? d - 50 : -50 - d) * 120 * (slope24 ? 2 : 1) / 15;
    else if (type == FT_BP && (d > 50 || d < -50))
        out -= (d > 0 ? d - 50 : -50 - d) * 60 * (slope24 ? 2 : 1) / 15;
    return out;
}

static void fr_curve(int32_t cut, int32_t k)             /* the response at cut (units) into wv_y */
{
    uint32_t px;
    for (px = 0; px < WV_W; px++) {
        int32_t u = (int32_t)(px * 127u / (WV_W - 1u)), db = fr_db10(u - cut, k, par[P_FTYPE], par[P_SLOPE]);
        wv_y[px] = (int16_t)clamp(20 + (180 - db) * 84 / 660, 16, 106);
    }
}

static void draw_filter(void)
{
    static const char *const FT[FT_N] = {"LP", "BP", "HP", "NOTCH"};
    int32_t k = RESO_K[clamp(par[P_RESO], 0, 127)], live = gr_voice() >= 0, lc = clamp(live_cut >> 8, 0, 127);
    char b[16];
    int32_t x;
    cv_hdot(WV_X, WV_X + WV_W - 1, 20 + 180 * 84 / 660, C_INK);   /* 0 dB */
    cv_vdot(WV_X + par[P_CUT] * (WV_W - 1) / 127, 16, 106, C_INK);
    fr_curve(par[P_CUT], k);
    wv_plot(WV_X, WV_W, live ? 0 : 2);
    if (live) {
        fr_curve(lc, k);
        wv_plot(WV_X, WV_W, 2);
    }
    x = md_text(WV_X, 4, &MD_F3, 2, FT[(uint32_t)clamp(par[P_FTYPE], 0, FT_N - 1)], C_INK);
    md_text(x + 6, 4, &MD_F3, 2, par[P_SLOPE] ? "24" : "12", C_INK);
    {                                                    /* the cutoff in Hz ("850 HZ", "4.2 KHZ") */
        uint32_t hz = CUTOFF_HZ[live ? lc : par[P_CUT]];
        num_text(b, (int32_t)(hz < 1000u ? hz : hz / 1000u), 0);
        for (x = 0; b[x]; x++)
            ;
        if (hz >= 1000u) {
            b[x++] = '.';
            b[x++] = (char)('0' + hz / 100u % 10u);
        }
        b[x++] = ' ';
        if (hz >= 1000u)
            b[x++] = 'K';
        b[x++] = 'H';
        b[x++] = 'Z';
        b[x] = 0;
        text_right(4, b);
    }
}

/* DELAY: the note, then its echoes along the time (0 .. 1.5 s), each a ring as big as it is loud (MIX, then x
 * FEEDBACK each time), TIME their spacing; COLR (darker echoes) fills them less */
static void draw_delay(void)
{
    uint32_t dl = dly_samples(par[P_DTIME]), k;
    int32_t lvl = par[P_DMIX] * 258, fb = par[P_DFDBK] * 230, x, rad;
    char b[16];
    cv_hdot(WV_X, WV_X + WV_W - 1, 70, C_INK);
    cv_rect(WV_X + 2 - 8, 62, 17, 17, C_INK);            /* the note */
    for (k = 1; k < 40u && lvl > 600; k++) {
        x = WV_X + 2 + (int32_t)((uint64_t)dl * k * (WV_W - 8) / 66150u);
        if (x > WV_X + WV_W - 2)
            break;
        rad = 2 + lvl * 16 / 32767;
        cv_ring(x, 70, rad, C_INK);
        if (rad > 3 && par[P_DCOLR] < 64)                /* (bright: a dot in each) */
            cv_rect(x - 1, 69, 3, 3, C_INK);
        lvl = (lvl * fb) >> 15;
    }
    num_text(b, (int32_t)(dl * 10u / 441u), 0);
    for (x = 0; b[x]; x++)
        ;
    b[x++] = ' ';
    b[x++] = 'M';
    b[x++] = 'S';
    b[x] = 0;
    text_right(4, b);
    md_text(WV_X, 108, &MD_F3, 2, "0", C_INK);
    text_right(108, "1.5 S");
}

/* REVERB: the room (frames going back, as deep as SIZE; the far ones dotted as DAMP takes the highs) and its tail:
 * the sound dying away over 0 .. 5 s (its RT60 from SIZE), after PRE's gap, as tall as MIX */
static void draw_reverb(void)
{
    int32_t size = 25000 + par[P_RSIZE] * 50, nf = 3 + par[P_RSIZE] / 22, f, x;
    uint32_t t60 = 207u * 32768u / (uint32_t)(32768 - size), pre = (uint32_t)par[P_RPRE] * 100u / 127u, rng = 0x1234567u;   /* ms */
    int32_t h = 20 + par[P_RMIX] * 60 / 127;
    char b[16];
    for (f = 0; f < nf; f++) {                           /* the room: frames toward its back */
        int32_t w = 76 - f * 64 / nf, hh = 76 - f * 64 / nf;
        if (f > 1 && f >= nf - par[P_RDAMP] / 32) {     /* (the far ones dotted: DAMP) */
            cv_hdot(46 - w / 2, 46 + w / 2, 60 - hh / 2, C_INK);
            cv_hdot(46 - w / 2, 46 + w / 2, 60 + hh / 2, C_INK);
            cv_vdot(46 - w / 2, 60 - hh / 2, 60 + hh / 2, C_INK);
            cv_vdot(46 + w / 2, 60 - hh / 2, 60 + hh / 2, C_INK);
        } else {
            cv_frame(46 - w / 2, 60 - hh / 2, w, hh, C_INK);
        }
    }
    cv_line(8, 22, 46 - 6, 60 - 6, C_INK);               /* (the corners' lines, the perspective) */
    cv_line(84, 22, 46 + 6, 60 - 6, C_INK);
    cv_line(8, 98, 46 - 6, 60 + 6, C_INK);
    cv_line(84, 98, 46 + 6, 60 + 6, C_INK);
    cv_hdot(96, 227, 100, C_INK);
    for (x = 96; x < 228; x += 2) {                      /* the tail: 0 .. 5 s across 132 px */
        uint32_t t = (uint32_t)(x - 96) * 5000u / 132u, i;
        int32_t e;
        if (t < pre)
            continue;
        i = (t - pre) * 64u / t60;
        e = i > 64u ? 0 : (int32_t)(64u - i) * 512;      /* (in dB: falling straight to -60 at its RT60) */
        rng = rng * 1664525u + 1013904223u;
        e = e * (int32_t)(16384u + (rng >> 18)) >> 15;   /* (a rough edge: the echoes' grain) */
        if (e > 300)
            cv_rect(x, 100 - e * h / 32767, 1, e * h / 32767, C_INK);
    }
    num_text(b, (int32_t)(t60 / 1000u), 0);
    for (x = 0; b[x]; x++)
        ;
    b[x++] = '.';
    b[x++] = (char)('0' + t60 / 100u % 10u);
    b[x++] = ' ';
    b[x++] = 'S';
    b[x] = 0;
    text_right(4, b);
    md_text(96, 108, &MD_F3, 2, "0", C_INK);
    text_right(108, "5 S");
}

/* CHORUS: the two delayed copies' swing, left and right, as two ribbons swaying across the strip (RATE their speed,
 * DEPTH how far, WIDTH how far apart in phase), moving */
static void draw_chorus(void)
{
    uint32_t hz = LFO_HZ_X100[(uint32_t)clamp(par[P_CRATE], 0, 127) * 90u / 127u], px, side;
    uint32_t woff = (uint32_t)clamp(par[P_CWIDTH], 0, 127) * (0x80000000u / 127u), t = fm1_ms * (4295u * hz / 100u);
    int32_t dep = 6 + par[P_CDEPTH] * 22 / 127;
    for (side = 0; side < 2u; side++) {
        for (px = 0; px < WV_W; px++) {
            uint32_t ph = t + (side ? woff : 0u) + px * (0x40000000u / 108u);   /* (half a cycle a 54 px) */
            wv_y[px] = (int16_t)((side ? 80 : 40) + sine_i(ph) * dep / 32767);
        }
        wv_plot(WV_X, WV_W, par[P_CMIX] ? 2 : 1);       /* (MIX 0: thin) */
        md_text(2, side ? 77 : 37, &MD_F3, 1, side ? "R" : "L", C_INK);
    }
}

/* AMP: MODE in big letters, the four voices (each its note and its level now), VOL as a bar */
static void draw_amp(void)
{
    static const char *const MD[] = {"POLY", "MONO", "LEGATO", "UNI"};
    uint32_t i;
    char b[8];
    md_text(WV_X, 6, &MD_F3, 4, MD[(uint32_t)clamp(par[P_MODE], 0, 3)], C_INK);
    for (i = 0; i < NVOICE; i++) {
        int32_t x = WV_X + (int32_t)i * 48, lv;
        cv_frame(x, 40, 42, 62, C_INK);
        if (!vc[i].active)
            continue;
        note_text(b, vc[i].note);
        md_text(x + 21 - md_text_w(&MD_F3, 2, b) / 2, 46, &MD_F3, 2, b, C_INK);
        lv = (vc[i].amp.lvl >> 9) * 40 / 32767;
        cv_rect(x + 4, 98 - lv, 34, lv, C_INK);
    }
    cv_frame(208, 40, 20, 62, C_INK);                    /* VOL */
    cv_rect(210, 100 - par[P_VOL] * 58 / 127, 16, par[P_VOL] * 58 / 127, C_INK);
    md_text(WV_X, 108, &MD_F3, 2, "VOICES", C_INK);
    text_right(108, "VOL");
}

/* GLOBAL: the velocity curve (VELC: in, left to right; out, up) and a MIDI monitor: what came in last, how many,
 * how many GLO's CH let pass */
static void draw_global(void)
{
    uint32_t v, p = midi_last, st = (p >> 8) & 0xF0u;
    char b[24];
    int32_t x;
    cv_frame(WV_X, 14, 96, 96, C_INK);
    for (v = 1; v < 128u; v++)
        wv_y[v - 1u] = (int16_t)(108 - (int32_t)vel_curve(v) * 92 / 127);
    for (v = 1; v < 127u; v++)
        cv_line(WV_X + 2 + (int32_t)((v - 1u) * 92u / 126u), wv_y[v - 1u], WV_X + 2 + (int32_t)(v * 92u / 126u),
                wv_y[v], C_INK);
    md_text(WV_X, 2, &MD_F3, 1, "VELOCITY", C_INK);
    md_text(120, 4, &MD_F3, 2, "MIDI IN", C_INK);
    num_text(b, (int32_t)(midi_seen % 1000u), 0);
    for (x = 0; b[x]; x++)
        ;
    b[x++] = ' ';
    b[x++] = 'M';
    b[x++] = 'S';
    b[x++] = 'G';
    b[x] = 0;
    md_text(120, 22, &MD_F3, 2, b, C_INK);
    if (midi_seen) {
        const char *ty = st == 0x90u ? ((p >> 24) & 0x7Fu ? "ON" : "OFF") : st == 0x80u ? "OFF" : st == 0xB0u ? "CC"
                       : st == 0xE0u ? "BEND" : "MSG";
        x = md_text(120, 44, &MD_F3, 2, ty, C_INK);
        if (st == 0x90u || st == 0x80u) {
            note_text(b, (p >> 16) & 0x7Fu);
            md_text(x + 8, 44, &MD_F3, 2, b, C_INK);
        } else {
            num_text(b, (int32_t)((p >> 16) & 0x7Fu), 0);
            md_text(x + 8, 44, &MD_F3, 2, b, C_INK);
        }
        x = md_text(120, 62, &MD_F3, 2, "CH", C_INK);
        num_text(b, (int32_t)((p >> 8) & 15u) + 1, 0);
        md_text(x + 6, 62, &MD_F3, 2, b, C_INK);
        if (midi_drop) {
            num_text(b, (int32_t)(midi_drop % 1000u), 0);
            x = md_text(120, 84, &MD_F3, 2, b, C_INK);
            md_text(x + 6, 84, &MD_F3, 2, "NOT CH", C_INK);
        }
    } else {
        md_text(120, 44, &MD_F3, 2, "NOTHING", C_INK);
        md_text(120, 62, &MD_F3, 2, "YET", C_INK);
    }
}

/* GLOBAL 2: the palettes by name, the one on in reverse */
static void draw_palettes(void)
{
    uint32_t i;
    for (i = 0; i < 9u; i++) {
        int32_t x = WV_X + (int32_t)(i % 3u) * 74, y = 10 + (int32_t)(i / 3u) * 34, on = (uint32_t)par[P_COLOR] == i;
        if (on)
            cv_rect(x - 4, y - 6, 70, 22, C_INK);
        md_text(x, y, &MD_F3, 2, N_COLOR[i], on ? C_BG : C_INK);
    }
}

/* the pages with a view of their own (the others: their four values as bars) */
static int view_own(uint32_t v)
{
    return v == PG_AENV || v == PG_FENV || v == PG_LFO1 || v == PG_LFO1 + 1u || v == PG_FILT || v == PG_DLY ||
           v == PG_REV || v == PG_CHO || v == PG_AMP || v == PG_GLO || v == PG_GLO2;
}

static void draw_own(uint32_t v)
{
    cv_begin(240, 120, C_BG);
    if (v == PG_AENV || v == PG_FENV)
        draw_env(v == PG_FENV);
    else if (v == PG_LFO1 || v == PG_LFO1 + 1u)
        draw_lfo(v - PG_LFO1);
    else if (v == PG_FILT)
        draw_filter();
    else if (v == PG_DLY)
        draw_delay();
    else if (v == PG_REV)
        draw_reverb();
    else if (v == PG_CHO)
        draw_chorus();
    else if (v == PG_AMP)
        draw_amp();
    else if (v == PG_GLO)
        draw_global();
    else
        draw_palettes();
    cv_blit(0, 120);
}

/* what makes page v's view move by itself (0: nothing; else a value that changes when it should be drawn again) */
static uint32_t view_live(uint32_t v)
{
    uint32_t n = view_osc(v), tick = (fm1_ms / 50u) * 2u + 2u;
    if (n < 2u && par[OSC_P(n, 0)] == OE_WT)
        return (wt_live_pos(n) >> 5) + 1u;
    if (n < 2u && par[OSC_P(n, 0)] == OE_GR)
        return gr_voice() >= 0 ? tick : gr_live_pos(n, 4096u) * 2u + 1u;
    if (v == PG_AENV || v == PG_FENV || v == PG_FILT || v == PG_AMP)
        return gr_voice() >= 0 ? tick : 1u;
    if ((v == PG_LFO1 || v == PG_LFO1 + 1u) && par[LFO_P(v - PG_LFO1, 3)])
        return tick;
    if (v == PG_CHO)
        return tick;
    if (v == PG_GLO)
        return midi_seen * 2u + 1u;
    return 0u;
}

static void draw_bottom(void)
{
    uint32_t k, v = ui_view();
    if (v == PG_INFO) {
        draw_info();
        return;
    }
    if (v == PG_SAVE) {
        draw_save();
        return;
    }
    if (view_osc(v) < 2u) {
        draw_osc(v, view_osc(v));                        /* OSC 1 / 2 (and +): the numbers, the wave */
        return;
    }
    if (view_own(v)) {
        draw_own(v);                                     /* a page with a view of its own */
        return;
    }
    cv_begin(240, 120, C_BG);
    for (k = 0; k < 4u; k++) {
        uint32_t id = page_param(v, k);
        const param_t *p;
        int32_t x0 = (int32_t)k * 60, h = 100, f;
        if (id >= P_COUNT)
            continue;                                    /* (an empty column; its number is in the top strip) */
        p = &PARAMS[id];
        cv_frame(x0 + 20, 10, 20, h, C_INK);              /* the value as a bar */
        f = (par[id] - p->min) * (h - 4) / (p->max - p->min);
        cv_rect(x0 + 22, 10 + h - 2 - f, 16, f, C_INK);
    }
    cv_blit(0, 120);
}

static void ui_draw(void)
{
    uint32_t k, top, bot;
    if (fm1_ms - ui.cpu_ms >= 1000u) {
        ui.cpu_ms = fm1_ms;
        ui.cpu_shown = cpu_q8 * 100u / 256u;
    }
    if (ui.msg && (int32_t)(fm1_ms - ui.msg_until) >= 0)
        ui.msg = 0;
    if (ui.color != (uint32_t)par[P_COLOR]) {            /* GLOBAL 2's COLOR: the palette, the screen drawn again */
        ui.color = (uint32_t)par[P_COLOR];
        theme_set(THEME_TAG | (COLOR_THEME0 + ui.color));
        ui.sig_top = ui.sig_bot = 0;
    }
    top = hash(hash(hash(hash(2166136261u, ui_view()), (uint32_t)(ui.octave + 8)), synth_voices()), ui.cpu_shown);
    top = hash(hash(hash(top, ui.pre), ui.pre_ed), ui.msg ? (uint32_t)(uintptr_t)ui.msg ^ ui.msg_until : 0u);
    top = hash(hash(top, (uint32_t)par[P_ENG1]), (uint32_t)par[P_ENG2]);   /* (the title shows the engine) */
    top = hash(hash(top, (uint32_t)par[P_WAVE]), (uint32_t)par[P_WAVE2]);   /* (PW's label: SHAPE or PW) */
    for (k = 0; k < 4u; k++)                             /* (and the knobs' numbers) */
        if (page_param(ui_view(), k) < P_COUNT)
            top = hash(hash(top, page_param(ui_view(), k)), (uint16_t)par[page_param(ui_view(), k)]);
    top |= 1u;
    bot = hash(2166136261u, ui_view());
    if (ui_view() == PG_INFO) {
        bot = hash(bot, ubank.used);
    } else if (ui_view() == PG_SAVE) {
        bot = hash(hash(hash(bot, ui.save_slot), ubank.used), ui.save_cur);
        for (k = 0; k < PRE_NAME; k++)
            bot = hash(bot, (uint8_t)ui.save_name[k]);
    } else {
        for (k = 0; k < 4u; k++)
            if (page_param(ui_view(), k) < P_COUNT)
                bot = hash(hash(bot, page_param(ui_view(), k)), (uint16_t)par[page_param(ui_view(), k)]);
        if (view_osc(ui_view()) < 2u) {                  /* (the wave: all its oscillator's settings) */
            uint32_t n = view_osc(ui_view());
            for (k = 0; k < 5u; k++)
                bot = hash(bot, (uint16_t)par[OSC_P(n, k)]);
            for (k = 0; k < 4u; k++)
                bot = hash(bot, (uint16_t)par[GR_P(n, k)]);
            bot = hash(hash(bot, (uint16_t)par[n ? P_WAVE2 : P_WAVE]), (uint16_t)par[n ? P_PW2 : P_PW1]);
        }
    }
    for (k = 0; k < P_COUNT; k++)                        /* (the views draw from any parameter) */
        bot = hash(bot, (uint16_t)par[k]);
    bot |= 1u;
    if (top != ui.sig_top) {
        ui.sig_top = top;
        draw_top();
    }
    {   /* the WT view follows the LFO's POS: drawn again when the live position moves, 20 times a second at most */
        uint32_t live = view_live(ui_view());            /* (WT's POS, GRAIN's grains, the envelopes, LFOs ...) */
        if (live != ui.wt_live && (bot != ui.sig_bot || fm1_ms - ui.wt_ms >= 50u)) {
            ui.wt_live = live;
            ui.wt_ms = fm1_ms;
            ui.sig_bot = 0;                              /* (drawn below) */
        }
    }
    if (bot != ui.sig_bot) {
        ui.sig_bot = bot;
        draw_bottom();
    }
}
