/* SPDX-License-Identifier: GPL-3.0-only
 * ObsSynth FM-1: 2026 Orian Beltrame */
/* MIDI in: USB-MIDI event packets (usb.c's ring midi_in_q, filled by usb_poll; the simulator's own, sim_host.h) to
 * the voices, drained at each block in the audio ISR (audio.c), before the voices render: note on / off on any
 * channel, or GLO's CH only (a note on at velocity 0 is a note off), its velocity through GLO's VELC (LIN, SOFT: louder
 * low, HARD: quieter low, FIX: 100), CC 120 / 123 (all sound / notes off) release every voice.
 * A packet: byte 0 cable << 4 | CIN, then the MIDI message's 3 bytes. */
static uint32_t vel_curve(uint32_t v)                   /* 1..127 -> 1..127 */
{
    switch (par[P_VELC]) {
    case VC_SOFT:
        return 127u - (127u - v) * (127u - v) / 127u;
    case VC_HARD:
        return v * v / 127u ? v * v / 127u : 1u;
    case VC_FIX:
        return 100u;
    default:
        return v;
    }
}

static volatile uint32_t midi_seen, midi_drop, midi_last;   /* MIDI in: messages, those not GLO's CH, the last
                                                             * (the GLOBAL page's monitor) */

static void midi_block(void)
{
    while (mi_r != mi_w) {
        uint32_t p, cin, st, d1, d2;
        RING_PUBLISH();                                  /* the slot after the index */
        p = midi_in_q[mi_r % MQ];
        mi_r++;
        cin = p & 15u;
        st = (p >> 8) & 0xF0u;
        d1 = (p >> 16) & 0x7Fu;
        d2 = (p >> 24) & 0x7Fu;
        midi_last = p;
        midi_seen++;
        if (par[P_MIDICH] && ((p >> 8) & 15u) + 1u != (uint32_t)par[P_MIDICH]) {
            midi_drop++;
            continue;                                    /* not GLO's channel */
        }
        if (cin == 9u && st == 0x90u && d2)
            note_on(d1, vel_curve(d2));
        else if ((cin == 8u && st == 0x80u) || (cin == 9u && st == 0x90u))
            note_off(d1);
        else if (cin == 0xBu && st == 0xB0u && (d1 == 120u || d1 == 123u))
            notes_all_off();
    }
}
