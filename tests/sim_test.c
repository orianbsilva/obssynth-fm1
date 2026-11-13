/* SPDX-License-Identifier: GPL-3.0-only
 * ObsSynth FM-1: 2026 Orian Beltrame */
/* The simulator's core on the host (tools/build_sim.sh, tests/run_tests.sh): the firmware played as the page plays
 * it, through sim_core.c's API only. Checks: the screen draws, keys sound and end, the pitch, the envelopes, the
 * filter, the voice cap, the knobs and pages, every parameter at its ends stays bounded. */
#include <math.h>
#include <stdio.h>
#include "sim_core.c"

static int fails;
static void check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    fails += !ok;
}

static float buf[2 * 44100 * 4];
static uint32_t render(float *dst, uint32_t frames)      /* frames of the left channel into dst (if any) */
{
    uint32_t done = 0;
    while (done < frames) {
        uint32_t n = frames - done > 512u ? 512u : frames - done, i;
        sim_render(n);
        for (i = 0; dst && i < n; i++)
            dst[done + i] = sim_out[2u * i];
        done += n;
    }
    return frames;
}
static float peak(const float *b, uint32_t n)
{
    float p = 0.0f;
    uint32_t i;
    for (i = 0; i < n; i++)
        p = b[i] > p ? b[i] : -b[i] > p ? -b[i] : p;
    return p;
}
static float energy(const float *b, uint32_t n)
{
    float e = 0.0f;
    uint32_t i;
    for (i = 0; i < n; i++)
        e += b[i] * b[i];
    return e / (float)n;
}
static float freq(const float *b, uint32_t n)            /* rising zero crossings over n samples */
{
    uint32_t i, c = 0, first = 0, last = 0;
    for (i = 1; i < n; i++)
        if (b[i - 1] < 0.0f && b[i] >= 0.0f) {
            if (!c)
                first = i;
            last = i;
            c++;
        }
    return c > 1u ? (float)(c - 1u) * 44100.0f / (float)(last - first) : 0.0f;
}
static void knob(uint32_t k, int32_t steps)              /* a knob turned, one detent at a time */
{
    int32_t s = steps < 0 ? -1 : 1;
    while (steps) {
        sim_enc(EN_K1 + k, s);
        render(0, SIM_FRAME_SAMPLES * 2u);               /* (a frame runs between halves: one window may miss it) */
        steps -= s;
    }
}
static void set(uint32_t id, int32_t v)                  /* a parameter straight (the page does it with knobs) */
{
    synth_param(id, v);
}
static void reset(void)
{
    memset(&fm1_in, 0, sizeof fm1_in);
    sim_init(1);
    render(0, 4410);
}
static uint32_t screen_hash(void)
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < 240u * 240u; i++)
        h = (h ^ fb[i]) * 16777619u;
    return h;
}

int main(void)
{
    uint32_t i, ok, colors, h0, h1;
    float p, f;

    reset();
    for (i = 1, colors = 1; i < 240u * 240u; i++)
        colors += fb[i] != fb[0] && (i < 2 || fb[i] != fb[i - 1]);
    check("boot: the screen draws (title, page, values)", colors > 50u);
    render(buf, 4410);
    check("silence with no key held", peak(buf, 4410) == 0.0f);

    reset();
    set(P_WAVE, W_SIN);
    set(P_CUT, 127);
    set(P_FENV, 0);
    sim_key(16, 1);                                      /* key 16 = A4 (F3 + 16) */
    render(0, 4410);
    render(buf, 44100);
    p = peak(buf, 44100);
    f = freq(buf, 44100);
    check("a key sounds: A4 at 440 Hz (+-0.5 %)", f > 437.8f && f < 442.2f);
    printf("      A4: %.2f Hz, peak %.3f\n", (double)f, (double)p);
    check("its level: audible, below full scale", p > 0.05f && p < 0.5f);
    sim_key(16, 0);
    render(buf, 44100);
    check("released: it ends (REL 50) and the voice frees", peak(buf + 22050, 22050) == 0.0f && synth_voices() == 0u);

    reset();
    set(P_WAVE, W_SIN);
    set(P_CUT, 127);
    sim_btn(B_OCTUP, 1);
    render(0, SIM_FRAME_SAMPLES * 2u);
    sim_btn(B_OCTUP, 0);
    sim_key(16, 1);
    render(0, 4410);
    render(buf, 44100);
    f = freq(buf, 44100);
    check("OCT+: an octave up (880 Hz)", f > 875.6f && f < 884.4f);
    sim_key(16, 0);

    reset();                                             /* amp envelope: ATK slow -> the start is quiet */
    set(P_WAVE, W_SAW);
    set(P_AATK, 100);
    sim_key(7, 1);
    render(buf, 44100);
    check("ATK: a slow attack rises (first 10 ms quieter than 0.9-1 s)",
          peak(buf, 441) * 4.0f < peak(buf + 39690, 4410));
    sim_key(7, 0);
    set(P_AATK, 0);
    set(P_ASUS, 0);
    set(P_ADEC, 40);
    render(0, 44100);
    sim_key(7, 1);
    render(buf, 44100 * 2);
    check("SUS 0: the note decays to silence while held, its voice frees", peak(buf + 44100, 44100) == 0.0f &&
          synth_voices() == 0u);
    sim_key(7, 0);

    reset();                                             /* filter: CUT 0 darker than CUT 127; ENV opens it */
    set(P_WAVE, W_SAW);
    set(P_FENV, 0);
    set(P_CUT, 127);
    sim_key(7, 1);
    render(0, 4410);
    render(buf, 22050);
    {
        float open = energy(buf, 22050), shut, env;
        uint32_t k;
        float hi_open = 0.0f, hi_shut = 0.0f;
        for (k = 1; k < 22050u; k++)                     /* the first difference: the highs */
            hi_open += (buf[k] - buf[k - 1]) * (buf[k] - buf[k - 1]);
        set(P_CUT, 10);
        render(0, 4410);
        render(buf, 22050);
        shut = energy(buf, 22050);
        for (k = 1; k < 22050u; k++)
            hi_shut += (buf[k] - buf[k - 1]) * (buf[k] - buf[k - 1]);
        check("CUT: closed, the highs drop by > 20 dB", hi_shut * 100.0f < hi_open && shut < open);
        sim_key(7, 0);
        render(0, 44100);
        set(P_FENV, 63);
        set(P_FSUS, 0);
        set(P_FDEC, 80);
        sim_key(7, 1);
        render(buf, 4410);
        env = energy(buf, 4410);
        check("ENV: the filter envelope opens a closed filter at the note's start", env > shut * 2.0f);
        sim_key(7, 0);
    }

    reset();                                             /* 6 keys: 4 voices, bounded */
    for (i = 0; i < 6u; i++)
        sim_key(i * 3u, 1);
    render(0, SIM_FRAME_SAMPLES * 2u);
    render(buf, 22050);
    check("six keys: four voices at most, the sum bounded", synth_voices() == NVOICE && peak(buf, 22050) < 1.0f);
    for (i = 0; i < 6u; i++)
        sim_key(i * 3u, 0);
    render(0, 44100 * 2);
    check("all released: every voice ends", synth_voices() == 0u);

    reset();                                             /* MIDI in (usb.c's ring): note on / off, any channel */
    set(P_WAVE, W_SIN);
    set(P_CUT, 127);
    set(P_FENV, 0);
    sim_midi(0x93, 69, 100);                             /* A4 on channel 4 */
    render(0, 4410);
    render(buf, 44100);
    f = freq(buf, 44100);
    check("MIDI note on (channel 4, A4): 440 Hz", f > 437.8f && f < 442.2f && synth_voices() == 1u);
    sim_midi(0x83, 69, 0);
    render(0, 44100);
    check("MIDI note off: released, the voice ends", synth_voices() == 0u);
    sim_midi(0x90, 60, 100);
    sim_midi(0x90, 64, 100);
    render(0, 4410);
    sim_midi(0x90, 60, 0);                               /* note on at velocity 0 = off */
    render(0, 44100);
    ok = synth_voices() == 1u;
    sim_midi(0xB0, 123, 0);                              /* all notes off */
    render(0, 44100);
    check("MIDI: velocity 0 is a note off; CC 123 releases every voice", ok && synth_voices() == 0u);

    reset();                                             /* OSC 2: an octave up, alone */
    set(P_WAVE2, W_SIN);
    set(P_SEMI2, 12);
    set(P_FINE2, 0);
    set(P_MIX, 127);
    set(P_CUT, 127);
    set(P_FENV, 0);
    sim_key(16, 1);
    render(0, 4410);
    render(buf, 44100);
    f = freq(buf, 44100);
    check("OSC 2: SEMI +12, MIX 127: A5 (880 Hz)", f > 875.6f && f < 884.4f);
    set(P_MIX, 64);
    render(0, 4410);
    render(buf, 44100);
    p = peak(buf, 44100);
    check("MIX 64: both oscillators, bounded", p > 0.05f && p < 1.0f);
    sim_key(16, 0);

    reset();                                             /* LFO 1 on PITCH: the pitch moves */
    set(P_WAVE, W_SIN);
    set(P_CUT, 127);
    set(P_FENV, 0);
    set(P_L1DEST, LD_PITCH);
    set(P_L1DEPTH, 16);                                  /* +-3 semitones */
    set(P_L1RATE, 40);                                   /* ~0.4 Hz */
    sim_key(16, 1);
    render(0, 4410);
    {
        float lo = 1e9f, hi = 0.0f;
        uint32_t w;
        for (w = 0; w < 12u; w++) {                      /* 12 windows of 0.25 s over 3 s */
            render(buf, 11025);
            f = freq(buf, 11025);
            lo = f < lo ? f : lo;
            hi = f > hi ? f : hi;
        }
        printf("      LFO PITCH: %.1f .. %.1f Hz\n", (double)lo, (double)hi);
        check("LFO 1 on PITCH (+-3 st): the pitch swings around A4", lo < 420.0f && hi > 460.0f && lo > 350.0f &&
              hi < 540.0f);
    }
    sim_key(16, 0);

    reset();                                             /* LFO 2 on AMP: a tremolo */
    set(P_WAVE, W_SIN);
    set(P_CUT, 127);
    set(P_L2DEST, LD_AMP);
    set(P_L2DEPTH, 63);
    set(P_L2WAVE, LW_SQR);
    set(P_L2RATE, 30);                                   /* ~0.25 Hz: 2 s high, 2 s low */
    sim_key(16, 1);
    {
        float a[8];
        uint32_t w;
        for (w = 0; w < 8u; w++) {
            render(buf, 22050);
            a[w] = peak(buf, 22050);
        }
        float mx = 0.0f, mn = 1.0f;
        for (w = 1; w < 8u; w++) {
            mx = a[w] > mx ? a[w] : mx;
            mn = a[w] < mn ? a[w] : mn;
        }
        check("LFO 2 on AMP (SQR, depth 63): loud and silent halves", mx > 0.05f && mn < mx * 0.05f);
    }
    sim_key(16, 0);

    reset();                                             /* LFO 1 on CUT: the brightness moves */
    set(P_WAVE, W_SAW);
    set(P_CUT, 50);
    set(P_FENV, 0);
    set(P_L1DEST, LD_CUT);
    set(P_L1DEPTH, 63);
    set(P_L1WAVE, LW_SQR);
    set(P_L1RATE, 30);
    sim_key(7, 1);
    {
        float e[8], mx = 0.0f, mn = 1e9f;
        uint32_t w;
        for (w = 0; w < 8u; w++) {
            render(buf, 22050);
            e[w] = energy(buf, 22050);
        }
        for (w = 1; w < 8u; w++) {
            mx = e[w] > mx ? e[w] : mx;
            mn = e[w] < mn ? e[w] : mn;
        }
        check("LFO 1 on CUT (SQR): bright and dark halves (> 6 dB apart)", mn * 4.0f < mx);
    }
    sim_key(7, 0);

    reset();                                             /* the LFO button: LFO 1, LFO 2, LFO 1; SELECT leaves */
    sim_enc(EN_SELECT, 3);                               /* (OSC 1, OSC 2, AMP, AMP ENV) */
    render(0, SIM_FRAME_SAMPLES * 2u);
    sim_btn(B_LFO, 1);
    render(0, SIM_FRAME_SAMPLES * 2u);
    sim_btn(B_LFO, 0);
    render(0, SIM_FRAME_SAMPLES * 2u);
    ok = ui.page == PG_LFO1 && ((sim_leds() >> B_LFO) & 1u);
    sim_btn(B_LFO, 1);
    render(0, SIM_FRAME_SAMPLES * 2u);
    sim_btn(B_LFO, 0);
    render(0, SIM_FRAME_SAMPLES * 2u);
    ok = ok && ui.page == PG_LFO1 + 1u;
    sim_btn(B_LFO, 1);
    render(0, SIM_FRAME_SAMPLES * 2u);
    sim_btn(B_LFO, 0);
    render(0, SIM_FRAME_SAMPLES * 2u);
    check("LFO: LFO 1, LFO 2, LFO 1 again (round, no going back), its LED lit", ok && ui.page == PG_LFO1 &&
          ((sim_leds() >> B_LFO) & 1u));
    knob(2, 2);
    check("KNOB 3 on LFO 1: DEST OFF -> PITCH -> CUT", par[P_L1DEST] == LD_CUT);
    sim_enc(EN_SELECT, 1);
    render(0, SIM_FRAME_SAMPLES * 2u);
    check("SELECT leaves the LFOs: back on the page it was (AMP ENV), the LED off",
          ui.page == PG_AENV && !((sim_leds() >> B_LFO) & 1u));

    reset();                                             /* ENV: AMP ENV, FILTER ENV, back (from OSC 2) */
    sim_enc(EN_SELECT, 1);
    render(0, SIM_FRAME_SAMPLES * 2u);
    {
        uint32_t seen[3], w;
        for (w = 0; w < 3u; w++) {
            sim_btn(B_ENV, 1);
            render(0, SIM_FRAME_SAMPLES * 2u);
            sim_btn(B_ENV, 0);
            render(0, SIM_FRAME_SAMPLES * 2u);
            seen[w] = ui.page | ((sim_leds() >> B_ENV) & 1u) << 8;
        }
        check("ENV: AMP ENV, FILTER ENV, AMP ENV again (round), its LED lit",
              seen[0] == (PG_AENV | 256u) && seen[1] == (PG_FENV | 256u) && seen[2] == (PG_AENV | 256u));
    }
    sim_btn(B_LFO, 1);
    render(0, SIM_FRAME_SAMPLES * 2u);
    sim_btn(B_LFO, 0);
    render(0, SIM_FRAME_SAMPLES * 2u);
    sim_btn(B_EDIT, 1);
    render(0, SIM_FRAME_SAMPLES * 2u);
    sim_btn(B_EDIT, 0);
    render(0, SIM_FRAME_SAMPLES * 2u);
    check("EDIT from LFO 1: AMP (EDIT lit, LFO not)", ui.page == PG_AMP &&
          ((sim_leds() >> B_EDIT) & 1u) && !((sim_leds() >> B_LFO) & 1u));

    reset();                                             /* DELAY: a short note, then its echo TIME later */
    set(P_WAVE, W_SAW);
    set(P_AATK, 0);
    set(P_ASUS, 0);
    set(P_ADEC, 20);                                     /* a ~25 ms blip */
    set(P_DTIME, 60);                                    /* 441 + 3600 x 4: 14841 samples, 0.34 s */
    set(P_DFDBK, 0);
    sim_key(16, 1);                                      /* (SUS 0: the blip ends on its own) */
    render(buf, 44100);
    sim_key(16, 0);
    {
        uint32_t t0 = 0, w;
        float dry_late, echo;
        for (w = 0; w < 44100u && buf[w] == 0.0f; w++)
            ;
        t0 = w;                                          /* the blip's start in buf */
        dry_late = peak(buf + t0 + 4410, 8000);          /* MIX 0: nothing after the blip */
        check("DELAY MIX 0: no echo", dry_late == 0.0f);
        reset();
        set(P_WAVE, W_SAW);
        set(P_ASUS, 0);
        set(P_ADEC, 20);
        set(P_DTIME, 60);
        set(P_DFDBK, 0);
        set(P_DMIX, 127);
        sim_key(16, 1);                                      /* (SUS 0: the blip ends on its own) */
        render(buf, 44100);
        sim_key(16, 0);
        for (w = 0; w < 44100u && buf[w] == 0.0f; w++)
            ;
        t0 = w;
        echo = peak(buf + t0 + 14841 - 200, 2000);       /* the echo around 0.34 s later */
        printf("      DELAY: blip %.3f, echo %.3f, between %.4f\n", (double)peak(buf + t0, 2000), (double)echo,
               (double)peak(buf + t0 + 4410, 8000));
        check("DELAY MIX 127: the echo TIME later, silence between", echo > 0.02f && peak(buf + t0 + 4410, 8000) == 0.0f);
    }

    reset();                                             /* REVERB: a tail after the note */
    set(P_WAVE, W_SAW);
    set(P_ASUS, 0);
    set(P_ADEC, 20);
    set(P_RMIX, 127);
    set(P_RSIZE, 110);
    sim_key(16, 1);
    render(0, SIM_FRAME_SAMPLES * 2u);
    sim_key(16, 0);
    render(buf, 44100 * 2);
    check("REVERB MIX 127: a tail half a second on, gone after the voice ended", peak(buf + 22050, 4410) > 0.001f &&
          synth_voices() == 0u);
    {
        uint32_t w;
        float worst = 0.0f;
        reset();                                         /* the effects at their ends, six keys: bounded */
        set(P_DFDBK, 120);
        set(P_DMIX, 127);
        set(P_DTIME, 5);
        set(P_RSIZE, 127);
        set(P_RDAMP, 0);
        set(P_RDLY, 127);
        set(P_RMIX, 127);
        for (w = 0; w < 6u; w++)
            sim_key(w * 3u, 1);
        render(buf, 44100 * 3);
        worst = peak(buf, 44100 * 3);
        for (w = 0; w < 6u; w++)
            sim_key(w * 3u, 0);
        check("FX at their ends, six keys: bounded below full scale", worst < 1.0f);
    }

    reset();                                             /* FX: DELAY, REVERB, CHORUS, back */
    {
        uint32_t seen[4], w;
        for (w = 0; w < 4u; w++) {
            sim_btn(B_FX, 1);
            render(0, SIM_FRAME_SAMPLES * 2u);
            sim_btn(B_FX, 0);
            render(0, SIM_FRAME_SAMPLES * 2u);
            seen[w] = ui.page | ((sim_leds() >> B_FX) & 1u) << 8;
        }
        check("FX: DELAY, REVERB, CHORUS, DELAY again (round), its LED lit",
              seen[0] == (PG_DLY | 256u) && seen[1] == (PG_REV | 256u) && seen[2] == (PG_CHO | 256u) &&
              seen[3] == (PG_DLY | 256u));
    }

    reset();                                             /* PRESETS: the next one loaded (F2 BASS) */
    sim_enc(EN_PRESET, 1);
    render(0, SIM_FRAME_SAMPLES * 2u);
    check("PRESETS: F2 BASS loaded (TUNE -12, OSC 2 SQR an octave down)", ui.pre == 1u && par[P_TUNE] == -12 &&
          par[P_WAVE2] == W_SQR && par[P_SEMI2] == -12 && par[P_MIX] == 50);
    sim_enc(EN_PRESET, -2);
    render(0, SIM_FRAME_SAMPLES * 2u);
    check("PRESETS back past F1: wraps to the last one (F14 CHIMES: OSC 1 and 2 GRAIN)", ui.pre == NFACT - 1u &&
          par[P_ENG1] == OE_GR && par[P_ENG2] == OE_GR);
    for (i = 0; i < NFACT; i++) {                        /* every factory preset sounds, bounded, and ends */
        uint32_t w;
        float pk;
        sim_enc(EN_PRESET, 1);
        render(0, SIM_FRAME_SAMPLES * 2u);
        for (w = 0; w < 3u; w++)
            sim_key(w * 4u + 7u, 1);
        render(buf, 44100);
        pk = peak(buf, 44100);
        for (w = 0; w < 3u; w++)
            sim_key(w * 4u + 7u, 0);
        render(0, 44100 * 5);
        if (!(pk > 0.02f && pk < 1.0f) || synth_voices()) {
            printf("      %s: peak %.3f, voices %u\n", FACTORY[ui.pre].name, (double)pk, synth_voices());
            break;
        }
    }
    check("every factory preset: a three-note chord sounds, bounded, its voices end", i == NFACT);

    reset();                                             /* SAVE: a user slot, kept over a power cycle */
    sim_enc(EN_PRESET, 3);                               /* F4 PAD */
    render(0, SIM_FRAME_SAMPLES * 2u);
    knob(1, 5);                                          /* OSC 1: TUNE +5, edited */
    ok = ui.pre == 3u && ui.pre_ed && par[P_TUNE] == 5;
    sim_btn(B_SAVE, 1);
    render(0, SIM_FRAME_SAMPLES * 2u);
    sim_btn(B_SAVE, 0);
    render(0, SIM_FRAME_SAMPLES * 2u);
    ok = ok && ui.page == PG_SAVE && ui.save_slot == 0u && ((sim_leds() >> B_SAVE) & 1u);
    knob(0, 2);                                          /* slot U03 */
    sim_flash_clean();
    sim_btn(B_SAVE, 1);
    render(0, SIM_FRAME_SAMPLES * 2u);
    sim_btn(B_SAVE, 0);
    render(0, SIM_FRAME_SAMPLES * 2u);
    {
        uint32_t dirty = 0, w;
        for (w = 0; w < sizeof sim_dirty / sizeof sim_dirty[0]; w++)
            dirty |= sim_dirty[w];
        check("SAVE, KNOB 1, SAVE: written to U03 (flash sectors dirty for the page), back on OSC 1, its name kept",
              ok && ui.save_slot == 2u && ((ubank.used >> 2) & 1u) && dirty && ui.page == 0u && !ui.pre_ed &&
              ubank.p[2].name[0] == 'P' && ubank.p[2].name[2] == 'D');
    }
    memset(&fm1_in, 0, sizeof fm1_in);                   /* power off and on, the flash kept (the page's IndexedDB) */
    sim_init(0);
    render(0, 4410);
    ok = preset_count() == NFACT + 1u && par[P_TUNE] == 0;
    sim_enc(EN_PRESET, -1);                              /* from INIT back: the last one, U03 */
    render(0, SIM_FRAME_SAMPLES * 2u);
    check("power cycle: U03 still there; PRESETS loads it (PAD with TUNE +5)",
          ok && preset_slot(ui.pre) == 2 && par[P_TUNE] == 5 && par[P_AATK] == 95 && par[P_RMIX] == 70);
    for (i = 0; i < 2u; i++) {                           /* SAVE, SAVE on U03 PAD: written over, same name */
        sim_btn(B_SAVE, 1);
        render(0, SIM_FRAME_SAMPLES * 2u);
        sim_btn(B_SAVE, 0);
        render(0, SIM_FRAME_SAMPLES * 2u);
        if (!i)
            ok = ui.save_slot == 2u && ui.save_name[0] == 'P' && ui.save_name[2] == 'D' && ui.save_name[3] == ' ';
    }
    check("SAVE, SAVE on a user preset: the same slot, the same name, no new slot",
          ok && preset_count() == NFACT + 1u && ubank.p[2].name[0] == 'P' && ubank.p[2].name[3] == 0);
    sim_btn(B_SAVE, 1);                                  /* SAVE AS: U06, "POD" */
    render(0, SIM_FRAME_SAMPLES * 2u);
    sim_btn(B_SAVE, 0);
    render(0, SIM_FRAME_SAMPLES * 2u);
    knob(0, 3);                                          /* slot U03 -> U06 */
    knob(1, 1);                                          /* the cursor on the second character */
    knob(2, 14);                                         /* A (1) -> O (15) */
    ok = ui.save_slot == 5u && ui.save_cur == 1u && ui.save_name[1] == 'O';
    sim_btn(B_SAVE, 1);
    render(0, SIM_FRAME_SAMPLES * 2u);
    sim_btn(B_SAVE, 0);
    render(0, SIM_FRAME_SAMPLES * 2u);
    check("SAVE AS: KNOB 1 U06, KNOB 2 / 3 the name POD; written, U03 PAD kept, POD the loaded one",
          ok && ubank.p[5].name[0] == 'P' && ubank.p[5].name[1] == 'O' && ubank.p[5].name[2] == 'D' &&
          ubank.p[5].name[3] == 0 && ubank.p[2].name[1] == 'A' && preset_count() == NFACT + 2u &&
          preset_slot(ui.pre) == 5);
    knob(2, 0);
    sim_btn(B_SAVE, 1);                                  /* KNOB 3 wraps: past # back to the space */
    render(0, SIM_FRAME_SAMPLES * 2u);
    sim_btn(B_SAVE, 0);
    render(0, SIM_FRAME_SAMPLES * 2u);
    ui.save_cur = 3u;
    knob(2, -1);
    check("the name's characters wrap (space, back: #)", ui.save_name[3] == '#');
    sim_enc(EN_SELECT, 1);
    render(0, SIM_FRAME_SAMPLES * 2u);
    sim_btn(B_SAVE, 1);                                  /* SAVE then SELECT: nothing written */
    render(0, SIM_FRAME_SAMPLES * 2u);
    sim_btn(B_SAVE, 0);
    render(0, SIM_FRAME_SAMPLES * 2u);
    sim_flash_clean();
    sim_enc(EN_SELECT, 1);
    render(0, SIM_FRAME_SAMPLES * 2u);
    {
        uint32_t dirty = 0, w;
        for (w = 0; w < sizeof sim_dirty / sizeof sim_dirty[0]; w++)
            dirty |= sim_dirty[w];
        check("SAVE, then SELECT: out, nothing written", ui.page != PG_SAVE && !dirty);
    }
    sim_flash_reset();                                   /* a torn bank (the header's CRC wrong): an empty bank, no crash */
    hflash[0x97000u] = 0x46;
    presets_boot();
    check("a bank that does not check out: empty, the factory presets still there", preset_count() == NFACT);

    reset();                                             /* GLO: the INFO page, GLO again: back */
    sim_enc(EN_SELECT, 4);                               /* (FILTER) */
    render(0, SIM_FRAME_SAMPLES * 2u);
    h0 = screen_hash();
    sim_btn(B_GLO, 1);
    render(0, SIM_FRAME_SAMPLES * 2u);
    sim_btn(B_GLO, 0);
    render(0, SIM_FRAME_SAMPLES * 2u);
    ok = ui.page == PG_GLO && ((sim_leds() >> B_GLO) & 1u) && screen_hash() != h0;
    sim_btn(B_GLO, 1);
    render(0, SIM_FRAME_SAMPLES * 2u);
    sim_btn(B_GLO, 0);
    render(0, SIM_FRAME_SAMPLES * 2u);
    ok = ok && ui.page == PG_GLO2;
    sim_btn(B_GLO, 1);
    render(0, SIM_FRAME_SAMPLES * 2u);
    sim_btn(B_GLO, 0);
    render(0, SIM_FRAME_SAMPLES * 2u);
    check("GLO: GLOBAL (its LED lit), GLOBAL 2, GLOBAL again (round)", ok && ui.page == PG_GLO && !ui.shift &&
          ((sim_leds() >> B_GLO) & 1u));

    reset();                                             /* SHIFT: HOME on FILTER shows FILTER +, and it stays */
    sim_enc(EN_SELECT, 4);
    render(0, SIM_FRAME_SAMPLES * 2u);
    sim_btn(B_HOME, 1);
    render(0, SIM_FRAME_SAMPLES * 2u);
    sim_btn(B_HOME, 0);
    render(0, SIM_FRAME_SAMPLES * 2u);
    ok = ui_view() == PG_PLUS0 + 3u && ((sim_leds() >> B_HOME) & 1u);
    knob(0, 2);                                          /* TYPE LP -> BP -> HP, HOME let go */
    ok = ok && par[P_FTYPE] == FT_HP && ui_view() == PG_PLUS0 + 3u;
    sim_btn(B_HOME, 1);
    render(0, SIM_FRAME_SAMPLES * 2u);
    sim_btn(B_HOME, 0);
    render(0, SIM_FRAME_SAMPLES * 2u);
    ok = ok && ui.page == PG_FILT && ui_view() == PG_FILT && !((sim_leds() >> B_HOME) & 1u);
    sim_btn(B_HOME, 1);                                  /* on again, then another page: off */
    render(0, SIM_FRAME_SAMPLES * 2u);
    sim_btn(B_HOME, 0);
    render(0, SIM_FRAME_SAMPLES * 2u);
    sim_btn(B_SCL, 1);
    render(0, SIM_FRAME_SAMPLES * 2u);
    sim_btn(B_SCL, 0);
    render(0, SIM_FRAME_SAMPLES * 2u);
    check("SHIFT: HOME on FILTER shows FILTER + (HOME lit) and it stays, KNOB 1 its TYPE; HOME again: FILTER; "
          "another page: SHIFT off", ok && ui.page == 0u && ui_view() == 0u && !ui.shift);
    sim_btn(B_SEQ, 1);                                   /* (back on FILTER, as the GLO part below starts from) */
    render(0, SIM_FRAME_SAMPLES * 2u);
    sim_btn(B_SEQ, 0);
    render(0, SIM_FRAME_SAMPLES * 2u);
    sim_btn(B_GLO, 1);
    render(0, SIM_FRAME_SAMPLES * 2u);
    sim_btn(B_GLO, 0);
    render(0, SIM_FRAME_SAMPLES * 2u);
    sim_btn(B_HOME, 1);
    render(0, SIM_FRAME_SAMPLES * 2u);
    ok = ui.page == PG_GLO && ui_view() == PG_INFO;
    sim_btn(B_HOME, 0);
    render(0, SIM_FRAME_SAMPLES * 2u);
    check("GLO's SHIFT page: INFO", ok);

    reset();                                             /* FILTER + TYPE HP: a 440 Hz sine under a 2.5 kHz cut */
    set(P_WAVE, W_SIN);
    set(P_FENV, 0);
    set(P_FKEY, 0);
    set(P_RESO, 0);
    set(P_CUT, 127);
    sim_key(16, 1);
    render(0, 4410);
    render(buf, 22050);
    p = peak(buf, 22050);
    set(P_FTYPE, FT_HP);
    set(P_CUT, 90);
    render(0, 4410);
    render(buf, 22050);
    check("FILTER + TYPE HP: a sine well under the cut drops > 20 dB", peak(buf, 22050) < p * 0.1f);
    sim_key(16, 0);

    reset();                                             /* SLOPE 24 dB: a sine an octave over the cut, 12 vs 24 */
    {
        float p12, p24, p0;
        set(P_WAVE, W_SIN);
        set(P_FENV, 0);
        set(P_FKEY, 0);
        set(P_RESO, 0);
        set(P_CUT, 127);
        sim_key(16, 1);                                  /* (440 Hz) */
        render(0, 4410);
        render(buf, 22050);
        p0 = peak(buf, 22050);
        set(P_CUT, 30);                                  /* (~200 Hz: 440, an octave over it) */
        render(0, 4410);
        render(buf, 22050);
        p12 = peak(buf, 22050);
        set(P_SLOPE, 1);
        render(0, 4410);
        render(buf, 22050);
        p24 = peak(buf, 22050);
        printf("      440 Hz over a ~200 Hz cut: 12 dB %.1f dB, 24 dB %.1f dB\n", (double)(20.0f * log10f(p12 / p0)),
               (double)(20.0f * log10f(p24 / p0)));
        check("FILTER + SLOPE 24 dB: twice the drop of 12 dB over the cut", p12 < p0 * 0.3f && p24 < p12 * 0.3f);
        sim_key(16, 0);
    }

    reset();                                             /* RESO: exponential, the knob's first half already heard */
    {
        float r[3];
        int32_t rs[3] = {0, 64, 127};
        uint32_t k;
        set(P_FENV, 0);
        set(P_FKEY, 0);
        set(P_CUT, 48);
        sim_key(0, 1);
        for (k = 0; k < 3u; k++) {
            set(P_RESO, rs[k]);
            render(0, 4410);
            render(buf, 22050);
            r[k] = peak(buf, 22050);
        }
        printf("      RESO 0 / 64 / 127: peak %.3f %.3f %.3f\n", (double)r[0], (double)r[1], (double)r[2]);
        check("RESO: half the knob gives more than half the swell; the top bounded (the band saturates)",
              r[1] - r[0] > 0.5f * (r[2] - r[0]) && r[2] > r[0] * 1.5f && r[2] < 1.0f);
        sim_key(0, 0);
    }

    reset();                                             /* OSC 1 + PW: SQR's duty cycle */
    {
        uint32_t k, pos;
        float d50, d90;
        set(P_WAVE, W_SQR);
        set(P_FENV, 0);
        set(P_CUT, 127);
        set(P_RESO, 0);
        sim_key(7, 1);
        render(0, 4410);
        render(buf, 22050);
        for (k = 0, pos = 0; k < 22050u; k++)
            pos += buf[k] > 0.0f;
        d50 = (float)pos / 22050.0f;
        set(P_PW1, 120);
        render(0, 4410);
        render(buf, 22050);
        for (k = 0, pos = 0; k < 22050u; k++)
            pos += buf[k] > 0.0f;
        d90 = (float)pos / 22050.0f;
        printf("      PW 64: %.2f high, PW 120: %.2f high\n", (double)d50, (double)d90);
        check("OSC 1 + PW: SQR high half the cycle at 64, ~90 % at 120", d50 > 0.45f && d50 < 0.55f && d90 > 0.8f);
        sim_key(7, 0);
    }

    reset();                                             /* OSC 1 + SUB: the pitch an octave down */
    set(P_WAVE, W_SIN);
    set(P_SUB, 127);
    set(P_FENV, 0);
    set(P_CUT, 30);                                      /* (the sub's square smoothed: one crossing a cycle) */
    set(P_RESO, 0);
    sim_key(16, 1);
    render(0, 4410);
    render(buf, 44100);
    f = freq(buf, 44100);
    printf("      SUB: %.1f Hz\n", (double)f);
    check("OSC 1 + SUB 127 under a sine A4: the lowest component, 220 Hz, leads (crossings under 300 Hz)",
          f > 200.0f && f < 300.0f);
    sim_key(16, 0);

    reset();                                             /* AMP ENV + MODE MONO: the key under comes back */
    set(P_WAVE, W_SIN);
    set(P_CUT, 127);
    set(P_FENV, 0);
    set(P_MODE, VM_MONO);
    sim_key(7, 1);                                       /* C4 */
    render(0, SIM_FRAME_SAMPLES * 2u);
    sim_key(16, 1);                                      /* A4 over it */
    render(0, 4410);
    render(buf, 22050);
    f = freq(buf, 22050);
    ok = synth_voices() == 1u && f > 437.8f && f < 442.2f;
    sim_key(16, 0);
    render(0, 4410);
    render(buf, 22050);
    f = freq(buf, 22050);
    check("MONO: one voice, the newest key (A4); let go: C4 again, still one voice",
          ok && synth_voices() == 1u && f > 260.3f && f < 262.9f);
    sim_key(7, 0);
    render(0, 44100);
    {                                                    /* MONO restarts the envelopes on a new key, LEGATO not */
        int32_t before, after_mono, before2, after_leg;
        uint32_t m;
        set(P_AATK, 0);
        set(P_ADEC, 100);
        set(P_ASUS, 30);
        for (m = 0; m < 2u; m++) {
            set(P_MODE, m ? VM_LEGATO : VM_MONO);
            sim_key(7, 1);
            render(0, 22050);                            /* decayed part of the way toward SUS */
            before = vc[0].amp.lvl;
            sim_key(16, 1);
            render(0, SIM_FRAME_SAMPLES * 2u);
            if (!m) {
                before2 = before;
                after_mono = vc[0].amp.lvl;
            } else {
                after_leg = vc[0].amp.lvl;
            }
            sim_key(16, 0);
            sim_key(7, 0);
            render(0, 44100 * 3);
        }
        printf("      MONO: %.2f -> %.2f, LEGATO: %.2f -> %.2f\n", (double)before2 / ENV1, (double)after_mono / ENV1,
               (double)before / ENV1, (double)after_leg / ENV1);
        check("MONO restarts the envelope on a new key (it rises); LEGATO keeps it (no rise)",
              after_mono > before2 + ENV1 / 20 && after_leg <= before);
    }

    reset();                                             /* AMP ENV + VOICES 2 */
    set(P_VOICES, 2);
    for (i = 0; i < 4u; i++)
        sim_key(i * 4u, 1);
    render(0, 4410);
    check("AMP ENV + VOICES 2: four keys, two voices", synth_voices() == 2u);
    for (i = 0; i < 4u; i++)
        sim_key(i * 4u, 0);

    reset();                                             /* AMP ENV + LOOP: SUS 0 keeps cycling while held */
    set(P_ASUS, 0);
    set(P_ADEC, 30);
    set(P_ALOOP, 1);
    sim_key(7, 1);
    render(buf, 44100 * 2);
    ok = synth_voices() == 1u && peak(buf + 66150, 22050) > 0.01f;
    sim_key(7, 0);
    render(0, 44100);
    check("AMP ENV + LOOP: a SUS-0 note keeps sounding while held, ends when let go", ok && synth_voices() == 0u);

    reset();                                             /* GLOBAL TUNE +50 cents */
    set(P_WAVE, W_SIN);
    set(P_CUT, 127);
    set(P_FENV, 0);
    set(P_GTUNE, 50);
    sim_key(16, 1);
    render(0, 4410);
    render(buf, 44100);
    f = freq(buf, 44100);
    check("GLOBAL TUNE +50: A4 at 452.9 Hz (+-0.5 %)", f > 450.6f && f < 455.2f);
    sim_key(16, 0);

    reset();                                             /* GLOBAL CH 2: channel 1 ignored, 2 plays */
    set(P_MIDICH, 2);
    sim_midi(0x90, 60, 100);
    render(0, 4410);
    ok = synth_voices() == 0u;
    sim_midi(0x91, 60, 100);
    render(0, 4410);
    check("GLOBAL CH 2: a note on channel 1 ignored, on channel 2 played", ok && synth_voices() == 1u);
    sim_midi(0x91, 60, 0);
    render(0, 44100);

    reset();                                             /* GLOBAL VELC HARD: a soft note quieter than LIN's */
    {
        float lin, hard;
        set(P_WAVE, W_SIN);
        sim_midi(0x90, 69, 50);
        render(0, 4410);
        render(buf, 22050);
        lin = peak(buf, 22050);
        sim_midi(0x90, 69, 0);
        render(0, 44100);
        set(P_VELC, VC_HARD);
        sim_midi(0x90, 69, 50);
        render(0, 4410);
        render(buf, 22050);
        hard = peak(buf, 22050);
        sim_midi(0x90, 69, 0);
        render(0, 44100);
        check("GLOBAL VELC HARD: velocity 50 quieter than with LIN", hard < lin * 0.6f && hard > 0.0f);
    }

    reset();                                             /* LFO + TRIG NOTE: a note restarts it at PHASE */
    set(P_L1TRIG, 1);
    set(P_L1PHASE, 64);                                  /* half a cycle */
    set(P_L1RATE, 10);
    render(0, 44100);
    sim_key(7, 1);
    render(0, SIM_FRAME_SAMPLES * 2u);
    check("LFO 1 + TRIG NOTE, PHASE 64: a note restarts it at half a cycle",
          lfo.ph[0] >= 0x80000000u && lfo.ph[0] < 0x81000000u);
    sim_key(7, 0);
    render(0, 44100);
    set(P_L1FADE, 100);                                  /* FADE: the depth from 0 after a note from silence */
    sim_key(7, 1);
    render(0, SIM_FRAME_SAMPLES * 2u);
    ok = lfo.fade[0] < ENV1 / 4;
    render(0, 44100 * 4);
    check("LFO 1 + FADE: the depth starts low after a note from silence, full later", ok && lfo.fade[0] == ENV1);
    sim_key(7, 0);

    reset();                                             /* GLOBAL settings: kept over a power cycle, not by presets */
    sim_btn(B_GLO, 1);
    render(0, SIM_FRAME_SAMPLES * 2u);
    sim_btn(B_GLO, 0);
    render(0, SIM_FRAME_SAMPLES * 2u);
    knob(0, 3);                                          /* CH 3 */
    knob(3, 1);                                          /* LIGHT INV */
    sim_btn(B_GLO, 1);                                   /* GLOBAL 2: KNOB 1 COLOR LILAC */
    render(0, SIM_FRAME_SAMPLES * 2u);
    sim_btn(B_GLO, 0);
    render(0, SIM_FRAME_SAMPLES * 2u);
    knob(0, 4);
    render(0, SIM_FRAME_SAMPLES * 2u);
    ok = C_BG == THEMES[COLOR_THEME0 + 4u].bg && C_INK == THEMES[COLOR_THEME0 + 4u].ink && fb[0] == C_BG;
    sim_btn(B_SCL, 1);                                   /* out (SEL: OSC 1): written */
    render(0, SIM_FRAME_SAMPLES * 2u);
    sim_btn(B_SCL, 0);
    render(0, SIM_FRAME_SAMPLES * 2u);
    ok = ok && par[P_MIDICH] == 3 && par[P_BLIGHT] == BL_INV && ui.msg && ui.msg[0] == 'S' && ui.msg[9] == 'S';
    sim_enc(EN_PRESET, 2);                               /* a preset: the settings stay */
    render(0, SIM_FRAME_SAMPLES * 2u);
    ok = ok && par[P_MIDICH] == 3;
    memset(&fm1_in, 0, sizeof fm1_in);
    sim_init(0);
    render(0, 4410);
    check("GLOBAL: CH 3, LIGHT INV, COLOR LILAC (the screen in its colours) written when GLO is left (SETTINGS "
          "SAVED); a preset keeps them; a power cycle too",
          ok && par[P_MIDICH] == 3 && par[P_BLIGHT] == BL_INV && par[P_COLOR] == 4 && sim_glow() == 0u &&
          C_INK == THEMES[COLOR_THEME0 + 4u].ink);

    reset();                                             /* CHORUS: MIX 0 one sound both sides, MIX up: two */
    {
        uint32_t k;
        float dl = 0.0f, dlr = 0.0f;
        set(P_WAVE, W_SAW);
        sim_key(7, 1);
        render(0, 4410);
        sim_render(512);
        for (k = 0; k < 512u; k++)
            dl += (sim_out[2u * k] - sim_out[2u * k + 1u]) * (sim_out[2u * k] - sim_out[2u * k + 1u]);
        set(P_CMIX, 100);
        render(0, 4410);
        sim_render(512);
        for (k = 0; k < 512u; k++)
            dlr += (sim_out[2u * k] - sim_out[2u * k + 1u]) * (sim_out[2u * k] - sim_out[2u * k + 1u]);
        ok = dl == 0.0f && dlr > 0.01f;
        set(P_CWIDTH, 0);                                /* WIDTH 0: both taps the same: mono again */
        render(0, 4410);
        sim_render(512);
        for (k = 0, dlr = 0.0f; k < 512u; k++)
            dlr += (sim_out[2u * k] - sim_out[2u * k + 1u]) * (sim_out[2u * k] - sim_out[2u * k + 1u]);
        check("CHORUS: MIX 0 the same both sides; MIX 100 left and right apart; WIDTH 0 the same again",
              ok && dlr == 0.0f);
        sim_key(7, 0);
    }

    reset();                                             /* MODE UNI: one key, four voices, spread, detuned */
    {
        uint32_t k;
        float d = 0.0f;
        set(P_MODE, VM_UNI);
        sim_key(7, 1);
        render(0, 4410);
        ok = synth_voices() == 4u;
        sim_render(512);
        for (k = 0; k < 512u; k++)
            d += (sim_out[2u * k] - sim_out[2u * k + 1u]) * (sim_out[2u * k] - sim_out[2u * k + 1u]);
        render(buf, 44100);
        p = peak(buf, 44100);
        sim_key(7, 0);
        render(0, 44100);
        check("MODE UNI: a key, four voices; left and right apart; bounded; all four end when let go",
              ok && d > 0.01f && p > 0.05f && p < 1.0f && synth_voices() == 0u);
        set(P_UDET, 0);                                  /* (AMP's fourth knob is DETUN under UNI) */
        ok = page_param(PG_AMP, 3) == P_UDET;
        set(P_MODE, VM_POLY);
        check("AMP: the fourth knob DETUN under UNI, VOICE otherwise", ok && page_param(PG_AMP, 3) == P_VOICES);
    }

    reset();                                             /* a 0.1.0 bank ("USB1") read and converted */
    {
        static ubank1_t old;
        memset(&old, 0, sizeof old);
        old.magic = UB1_MAGIC;
        old.np = 36;
        old.used = 1u | 1u << 5;
        for (i = 0; i < 36u; i++)
            old.p[0].v[i] = old.p[5].v[i] = (int8_t)PARAMS[i].def;
        old.p[0].v[P_TUNE] = 7;
        old.p[0].name[0] = 'O';
        old.p[0].name[1] = 'L';
        old.p[0].name[2] = 'D';
        ok = st_save(OBJ_UBANK, &old, sizeof old) == 0;
        presets_boot();
        check("a 0.1.0 user bank (USB1): its presets read, their values kept, the new parameters at their defaults",
              ok && ubank.magic == UB_MAGIC && ubank.used == (1u | 1u << 5) && ubank.p[0].v[P_TUNE] == 7 &&
              ubank.p[0].v[P_PW1] == 64 && ubank.p[0].v[P_AVEL] == 127 && ubank.p[0].name[0] == 'O' &&
              preset_count() == NFACT + 2u);
    }

    reset();                                             /* ALGORITHM: the shown oscillator's engine */
    h0 = screen_hash();
    sim_enc(EN_ALGO, 1);
    render(0, SIM_FRAME_SAMPLES * 2u);
    ok = par[P_ENG1] == OE_SUPER && par[P_ENG2] == OE_STD && screen_hash() != h0 && page_param(0, 0) == P_SDET1;
    knob(0, 5);
    ok = ok && par[P_SDET1] == PARAMS[P_SDET1].def + 5 && par[P_WAVE] == W_SAW;
    sim_enc(EN_SELECT, 1);                               /* OSC 2: ALGORITHM is OSC 2's */
    render(0, SIM_FRAME_SAMPLES * 2u);
    sim_enc(EN_ALGO, 2);
    render(0, SIM_FRAME_SAMPLES * 2u);
    check("ALGORITHM: OSC 1 SUPER (KNOB 1 its DETUN); on OSC 2, OSC 2's: WT (KNOB 1 its POS)",
          ok && par[P_ENG2] == OE_WT && par[P_ENG1] == OE_SUPER && page_param(1, 0) == P_WPOS2 &&
          page_param(PG_PLUS0 + 1u, 0) == P_WTAB2);

    reset();                                             /* SUPER: seven saws, bounded, not the plain saw */
    {
        float e_std = 0.0f, e_sup = 0.0f;
        uint32_t k;
        set(P_CUT, 127);
        set(P_FENV, 0);
        sim_key(7, 1);
        render(0, 4410);
        render(buf, 22050);
        for (k = 0; k < 22050u; k++)
            e_std += buf[k] * buf[k];
        memcpy(buf + 22050, buf, 22050 * sizeof buf[0]);
        set(P_ENG1, OE_SUPER);
        set(P_SSIDE1, 127);
        set(P_SDET1, 127);
        render(0, 4410);
        render(buf, 22050);
        for (k = 0; k < 22050u; k++)
            e_sup += buf[k] * buf[k];
        p = peak(buf, 22050);
        printf("      SUPER: energy %.4f (STD saw %.4f), peak %.3f\n", (double)e_sup, (double)e_std, (double)p);
        check("SUPER (DETUN, SIDE 127): sounds, bounded, other than the plain saw",
              p > 0.05f && p < 1.0f && e_sup > e_std * 0.3f && (e_sup > e_std * 1.05f || e_sup < e_std * 0.95f));
        sim_key(7, 0);
    }

    reset();                                             /* WT: S>SAW at POS 0, a sine; POS 127, brighter */
    {
        float h0w = 0.0f, h1w = 0.0f;
        uint32_t k;
        set(P_ENG1, OE_WT);
        set(P_WTAB1, 1);
        set(P_WPOS1, 0);
        set(P_CUT, 127);
        set(P_FENV, 0);
        set(P_RESO, 0);
        sim_key(16, 1);
        render(0, 4410);
        render(buf, 44100);
        f = freq(buf, 44100);
        for (k = 1; k < 44100u; k++)
            h0w += (buf[k] - buf[k - 1]) * (buf[k] - buf[k - 1]);
        set(P_WPOS1, 127);
        render(0, 4410);
        render(buf, 44100);
        for (k = 1; k < 44100u; k++)
            h1w += (buf[k] - buf[k - 1]) * (buf[k] - buf[k - 1]);
        printf("      WT S>SAW: %.2f Hz at POS 0; highs x%.1f at POS 127\n", (double)f, (double)(h1w / h0w));
        check("WT S>SAW: POS 0 a 440 Hz sine; POS 127 brighter (the saw's highs)", f > 437.8f && f < 442.2f &&
              h1w > h0w * 1.5f);
        sim_key(16, 0);
    }
    {                                                    /* WT's level for a pitch: its harmonics under Nyquist */
        uint32_t nt, worst = 0;
        oscb_t o;
        set(P_ENG1, OE_WT);
        for (nt = 24; nt <= 120u; nt += 4) {
            uint32_t inc = pitch_inc((int32_t)nt * 16), lv, off;
            osc_setup(&o, 0, inc, 0, 0, 0);
            off = (uint32_t)(o.wa - WT_BANKS[par[P_WTAB1]].data) % WT_FRAME;
            for (lv = 0; lv < WT_LEVELS && WT_LOFF[lv] != off; lv++)
                ;
            if (lv < WT_LEVELS && (uint64_t)(512u >> lv) * inc > 0x80000000ull && lv < WT_LEVELS - 1u)
                worst++;                                 /* a level with a harmonic past Nyquist */
        }
        check("WT: every note's level keeps its harmonics under Nyquist (no aliasing)", worst == 0u);
    }

    reset();                                             /* LFO 1 on POS: the WT sweeps */
    {
        float e[8], mx = 0.0f, mn = 1e9f;
        uint32_t w, k;
        set(P_ENG1, OE_WT);
        set(P_WTAB1, 1);
        set(P_CUT, 127);
        set(P_FENV, 0);
        set(P_L1DEST, LD_POS);
        set(P_L1DEPTH, 63);
        set(P_L1WAVE, LW_SQR);
        set(P_L1RATE, 30);
        sim_key(7, 1);
        for (w = 0; w < 8u; w++) {
            render(buf, 22050);
            for (k = 1, e[w] = 0.0f; k < 22050u; k++)
                e[w] += (buf[k] - buf[k - 1]) * (buf[k] - buf[k - 1]);
        }
        for (w = 1; w < 8u; w++) {
            mx = e[w] > mx ? e[w] : mx;
            mn = e[w] < mn ? e[w] : mn;
        }
        check("LFO 1 on POS (SQR): the WT dark and bright halves", mn * 1.5f < mx);
        sim_key(7, 0);
    }

    reset();                                             /* the WT view: the wave at POS, live with the LFO */
    {
        uint32_t hs, hw0, hw1, hl0, hl1;
        render(0, SIM_FRAME_SAMPLES * 2u);
        hs = screen_hash();
        sim_enc(EN_ALGO, 2);                             /* OSC 1: WT */
        render(0, SIM_FRAME_SAMPLES * 2u);
        set(P_WTAB1, 1);
        render(0, SIM_FRAME_SAMPLES * 2u);
        hw0 = screen_hash();
        knob(0, 127);                                    /* POS to the end: another wave drawn, the marker moved */
        hw1 = screen_hash();
        ok = par[P_WPOS1] == 127 && hs != hw0 && hw0 != hw1;
        set(P_L1DEST, LD_POS);                           /* the LFO on POS: the view moves by itself */
        set(P_L1DEPTH, 63);
        set(P_L1RATE, 60);
        set(P_WPOS1, 64);
        render(0, SIM_FRAME_SAMPLES * 4u);
        hl0 = screen_hash();
        render(0, 11025);
        hl1 = screen_hash();
        check("WT view: OSC 1 under WT drawn other than STD, POS moves it, the LFO on POS moves it live",
              ok && hl0 != hl1);
    }

    reset();                                             /* the OSC views: STD's PW, SUPER's DETUN drawn */
    {
        uint32_t h0, h1, h2, h3;
        render(0, SIM_FRAME_SAMPLES * 2u);
        set(P_WAVE, W_SQR);
        render(0, SIM_FRAME_SAMPLES * 2u);
        h0 = screen_hash();
        set(P_PW1, 20);
        render(0, SIM_FRAME_SAMPLES * 2u);
        h1 = screen_hash();
        set(P_ENG1, OE_SUPER);
        render(0, SIM_FRAME_SAMPLES * 2u);
        h2 = screen_hash();
        set(P_SDET1, 127);
        render(0, SIM_FRAME_SAMPLES * 2u);
        h3 = screen_hash();
        check("OSC views: STD SQR's PW, SUPER, SUPER's DETUN each change the wave drawn",
              h0 != h1 && h1 != h2 && h2 != h3);
    }

    reset();                                             /* GRAIN: the organ (C4) at A4 is A4; bounded; near a saw */
    {
        float e_std = 0.0f, e_gr = 0.0f, pk;
        uint32_t k, h0, h1;
        set(P_CUT, 127);
        set(P_FENV, 0);
        set(P_RESO, 0);
        sim_key(16, 1);
        render(0, 4410);
        render(buf, 22050);
        for (k = 0; k < 22050u; k++)
            e_std += buf[k] * buf[k];
        set(P_ENG1, OE_GR);
        set(P_GSPRAY1, 0);
        set(P_WPOS1, 60);
        render(0, 8820);
        render(buf, 44100);
        {                                                /* (its octave is strong: the period by correlation) */
            float c0 = 0.0f, ch = 0.0f, cp = 0.0f;
            for (k = 0; k + 100u < 44100u; k++) {
                c0 += buf[k] * buf[k];
                ch += buf[k] * buf[k + 50u];
                cp += buf[k] * buf[k + 100u];
            }
            f = cp / c0;                                 /* A4's period (100.2 samples) against half of it */
            printf("      GRAIN ORGAN at A4: correlation at A4's period %.2f, at half %.2f\n", (double)f,
                   (double)(ch / c0));
            f = f > 0.4f && cp > ch * 1.2f ? 440.0f : 0.0f;
        }
        for (k = 0; k < 22050u; k++)
            e_gr += buf[k] * buf[k];
        pk = peak(buf, 44100);
        printf("      GRAIN ORGAN at A4: %.2f Hz, energy %.4f (STD saw %.4f), peak %.3f\n", (double)f, (double)e_gr,
               (double)e_std, (double)pk);
        check("GRAIN: the organ sample played at A4 is 440 Hz, bounded, within 10 dB of a saw",
              f > 436.0f && f < 444.0f && pk < 1.0f && e_gr > e_std * 0.1f && e_gr < e_std * 10.0f);
        sim_enc(EN_SELECT, 0);
        render(0, SIM_FRAME_SAMPLES * 2u);
        h0 = screen_hash();
        render(0, 4410);
        h1 = screen_hash();
        check("GRAIN view: the grains move on the screen while a note sounds", h0 != h1);
        sim_key(16, 0);
        for (k = 0; k < GR_NSMP; k++) {                  /* every sample, the knobs at their ends: bounded */
            set(P_GSMP1, (int32_t)k);
            set(P_GSIZE1, 127);
            set(P_GDENS1, 127);
            set(P_GSPRAY1, 127);
            set(P_WPOS1, 127);
            sim_key(26, 1);
            render(buf, 22050);
            pk = peak(buf, 22050) > pk ? peak(buf, 22050) : pk;
            sim_key(26, 0);
            render(0, 22050);
        }
        check("GRAIN: every sample, SIZE / DENS / SPRAY / POS at the top, the highest key: bounded", pk < 1.0f);
    }

    reset();                                             /* STD's new waves: each sounds, bounded, its own */
    {
        uint32_t w, k, same = 0;
        float e[W_N], pk = 0.0f;
        set(P_CUT, 127);
        set(P_FENV, 0);
        set(P_RESO, 0);
        set(P_PW1, 100);
        sim_key(16, 1);
        for (w = 0; w < W_N; w++) {
            set(P_WAVE, (int32_t)w);
            render(0, 2205);
            render(buf, 22050);
            for (k = 1, e[w] = 0.0f; k < 22050u; k++)
                e[w] += (buf[k] - buf[k - 1]) * (buf[k] - buf[k - 1]);
            pk = peak(buf, 22050) > pk ? peak(buf, 22050) : pk;
            for (k = 0; k < w; k++)
                same += w != W_RAMP && fabsf(e[w] - e[k]) < e[k] * 0.02f;   /* (RAMP: the saw's mirror) */
        }
        sim_key(16, 0);
        check("STD: RAMP FOLD FM OCT sound, bounded, each its own highs (SHAPE 100)", pk > 0.05f && pk < 1.0f && !same);
    }
    {                                                    /* NOISE's colours: BROWN < PINK < WHITE < VIOL in highs */
        uint32_t c, i, r = 1;
        float hi[NC_N];
        for (c = 0; c < NC_N; c++) {
            voice_t vv;
            int32_t y, prev = 0;
            memset(&vv, 0, sizeof vv);
            for (i = 0, hi[c] = 0.0f; i < 44100u; i++) {
                r = r * 1664525u + 1013904223u;
                y = noise_tick(&vv, (int32_t)(r >> 16) - 32768, (int32_t)c);
                hi[c] += (float)(y - prev) * (float)(y - prev);
                prev = y;
            }
        }
        check("NOISE colours: BROWN darker than PINK, darker than WHITE, darker than VIOL; CRACK sparse",
              hi[NC_BROWN] < hi[NC_PINK] && hi[NC_PINK] < hi[NC_WHITE] && hi[NC_WHITE] < hi[NC_VIOL] &&
              hi[NC_CRACK] < hi[NC_WHITE] * 0.2f);
    }

    reset();                                             /* the section buttons: SEL, EDIT, SEQ, each two pages round */
    {
        static const uint8_t BT[3] = {B_SCL, B_EDIT, B_SEQ};
        static const uint32_t PA[3][2] = {{0u, 1u}, {PG_AMP, PG_AENV}, {PG_FILT, PG_FENV}};
        uint32_t b, w;
        ok = 1;
        for (b = 0; b < 3u; b++)
            for (w = 0; w < 3u; w++) {
                sim_btn(BT[b], 1);
                render(0, SIM_FRAME_SAMPLES * 2u);
                sim_btn(BT[b], 0);
                render(0, SIM_FRAME_SAMPLES * 2u);
                ok = ok && ui.page == PA[b][b == 0u ? (w + 1u) % 2u : w % 2u] && ((sim_leds() >> BT[b]) & 1u);
            }
        sim_enc(EN_SELECT, -1);                          /* SELECT from FILTER: AMP ENV (its order) */
        render(0, SIM_FRAME_SAMPLES * 2u);
        check("SEL: OSC 1 <-> OSC 2; EDIT: AMP <-> AMP ENV; SEQ: FILTER <-> FILTER ENV (each lit); SELECT in order",
              ok && ui.page == PG_AENV);
    }

    reset();                                             /* the pages' own views */
    {
        static const uint8_t BT[] = {B_EDIT, B_EDIT, B_SEQ, B_SEQ, B_LFO, B_FX, B_FX, B_FX, B_GLO, B_GLO};
        uint32_t hs[sizeof BT], k, j, same = 0, h0, h1;
        set(P_L1DEST, LD_CUT);
        set(P_L1DEPTH, 40);
        for (k = 0; k < sizeof BT; k++) {
            sim_btn(BT[k], 1);
            render(0, SIM_FRAME_SAMPLES * 2u);
            sim_btn(BT[k], 0);
            render(0, SIM_FRAME_SAMPLES * 2u);
            hs[k] = screen_hash();
            for (j = 0; j < k; j++)
                same += hs[j] == hs[k];
        }
        ok = !same && view_own(PG_AMP) && view_own(PG_AENV) && view_own(PG_GLO2);
        sim_btn(B_GLO, 1);                               /* GLOBAL: the MIDI monitor shows what comes in */
        render(0, SIM_FRAME_SAMPLES * 2u);
        sim_btn(B_GLO, 0);
        render(0, SIM_FRAME_SAMPLES * 2u);
        h0 = screen_hash();
        sim_midi(0x90, 64, 90);
        render(0, 4410);
        h1 = screen_hash();
        ok = ok && h0 != h1 && midi_seen >= 1u && ((midi_last >> 16) & 0x7Fu) == 64u;
        sim_midi(0x80, 64, 0);
        sim_btn(B_LFO, 1);                               /* LFO 1: it moves by itself */
        render(0, SIM_FRAME_SAMPLES * 2u);
        sim_btn(B_LFO, 0);
        render(0, SIM_FRAME_SAMPLES * 2u);
        h0 = screen_hash();
        render(0, 11025);
        check("the pages' own views: AMP, AMP ENV, FILTER, FILTER ENV, LFO, DELAY, REVERB, CHORUS, GLOBAL, GLOBAL 2 "
              "each its own; GLOBAL's monitor shows a note in; the LFO's moves", ok && screen_hash() != h0);
    }
    {                                                    /* the new LFO destinations: OSC 1's pitch only */
        float f1, f2;
        reset();
        set(P_CUT, 127);
        set(P_FENV, 0);
        set(P_WAVE, W_SIN);
        set(P_MIX, 0);
        set(P_L1DEST, LD_PITCH2);                        /* OSC 2's: OSC 1 (heard, MIX 0) untouched */
        set(P_L1DEPTH, 63);
        set(P_L1WAVE, LW_SQR);
        sim_key(16, 1);
        render(0, 4410);
        render(buf, 22050);
        f1 = freq(buf, 22050);
        set(P_L1DEST, LD_PITCH1);                        /* OSC 1's: it moves */
        render(0, 4410);
        render(buf, 44100);
        f2 = freq(buf, 44100);
        sim_key(16, 0);
        check("LFO DEST PIT2 leaves OSC 1 alone, PIT1 moves it", f1 > 437.0f && f1 < 443.0f &&
              (f2 < 430.0f || f2 > 450.0f));
    }

    reset();                                             /* a USB2 bank (80 a preset) read and converted */
    {
        static uint8_t raw[12u + NUSER * (PRE_NAME + 80u)];
        uint32_t magic = 0x32425355u, used = 1u << 2;
        memset(raw, 0, sizeof raw);
        memcpy(raw, &magic, 4);
        raw[4] = 69;                                     /* np: the USB2 firmware's parameters */
        memcpy(raw + 8, &used, 4);
        for (i = 0; i < 69u; i++)
            raw[12u + 2u * (PRE_NAME + 80u) + PRE_NAME + i] = (uint8_t)(int8_t)PARAMS[i].def;
        raw[12u + 2u * (PRE_NAME + 80u) + PRE_NAME + P_TUNE] = (uint8_t)(int8_t)-3;
        raw[12u + 2u * (PRE_NAME + 80u)] = 'B';
        ok = st_save(OBJ_UBANK, raw, sizeof raw) == 0;
        presets_boot();
        check("a USB2 bank: read, its values kept, the engines at their defaults (STD)",
              ok && ubank.magic == UB_MAGIC && ubank.used == used && ubank.p[2].v[P_TUNE] == -3 &&
              ubank.p[2].v[P_ENG1] == OE_STD && ubank.p[2].v[P_SDET1] == 50 && ubank.p[2].name[0] == 'B');
    }

    reset();                                             /* the panel: KNOB 1 = WAVE, SELECT = the page */
    h0 = screen_hash();
    knob(0, 1);
    check("KNOB 1 on OSC 1: WAVE SAW -> SQR", par[P_WAVE] == W_SQR);
    h1 = screen_hash();
    check("the screen shows it", h1 != h0);
    sim_enc(EN_SELECT, 1);
    render(0, SIM_FRAME_SAMPLES * 2u);
    check("SELECT: the next page (OSC 2)", ui.page == 1u && screen_hash() != h1);
    knob(3, 10);
    check("KNOB 4 on OSC 2: MIX up 10 (one detent a frame: x1)", par[P_MIX] == PARAMS[P_MIX].def + 10);
    sim_enc(EN_SELECT, 10);
    render(0, SIM_FRAME_SAMPLES * 2u);
    check("SELECT stops at its last page (FILTER ENV; the LFOs are the LFO button's)", ui.page == PG_FENV);
    sim_btn(B_HOME, 1);
    render(0, SIM_FRAME_SAMPLES * 2u);
    sim_btn(B_HOME, 0);
    render(0, SIM_FRAME_SAMPLES * 2u);
    check("HOME pressed and let go: FILTER ENV + shown, still (the page FILTER ENV)",
          ui.page == PG_FENV && ui.shift && ui_view() == PG_PLUS0 + 4u);

    ok = 1;                                              /* every parameter at min and max, the others default */
    for (i = 0; i < P_COUNT; i++) {
        int32_t e;
        for (e = 0; e < 2; e++) {
            uint32_t n;
            reset();
            set(i, e ? PARAMS[i].max : PARAMS[i].min);
            for (n = 0; n < 4u; n++)
                sim_key(n * 7u, 1);
            render(buf, 22050);
            for (n = 0; n < 4u; n++)
                sim_key(n * 7u, 0);
            render(0, SIM_FRAME_SAMPLES * 2u);
            render(buf + 22050, 44100 * 5);
            if (peak(buf, 22050 + 44100 * 5) >= 1.0f || (synth_voices() != 0u && i != P_AREL)) {
                printf("      %s at %d: peak %.3f, voices %u\n", PARAMS[i].label, e ? PARAMS[i].max : PARAMS[i].min,
                       (double)peak(buf, 22050 + 44100 * 5), synth_voices());
                ok = 0;
            }
        }
    }
    check("every parameter at its ends: bounded, the notes end after release (REL max aside)", (int)ok);

    reset();
    {
        uint64_t t0 = sim_samples;
        for (i = 0; i < 4u; i++)
            sim_key(i * 5u, 1);
        render(0, 44100);
        check("the clock: sim_render advances the sample count", sim_samples - t0 >= 44100u - HALF_FRAMES);
    }
    printf(fails ? "sim_test: %d FAILED\n" : "sim_test: all ok\n", fails);
    return fails != 0;
}
