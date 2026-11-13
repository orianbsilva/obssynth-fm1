# Changelog

[Português](CHANGELOG.pt-BR.md)

## 0.1.0 - 2026-11-12

- Oscillators: two per voice, each with its own engine (ALGORITHM): STD (SAW SQR TRI SIN RAMP FOLD FM OCT, shaped by PW), SUPER (seven detuned saws), WT (four wavetable banks, alias-free) or GRAIN (granular over three samples).
- Voice: OSC 2 with SEMI, FINE and MIX; SUB and NOISE in six colours; SYNC and RING; a 12 or 24 dB filter (LP BP HP NOTCH) with DRIVE; amp and filter ADSRs with velocity, key tracking and LOOP; GLIDE.
- Modes: POLY (4 voices), MONO, LEGATO and UNI (the four voices on one note, detuned and spread in stereo).
- Modulation: two LFOs (SIN TRI SAW SQR S&H, 0.05 to 40 Hz) with TRIG, FADE and PHASE, each to one destination, per oscillator too.
- Effects: delay, room reverb (Hortator's) and a stereo chorus; stereo out with a soft clip.
- Presets: 14 factory sounds and 16 of your own on the flash (A/B records: a power cut keeps the previous bank).
- Screen: each page draws what it edits (the envelope curves, the LFO wave, the filter response, the wavetable, the live grains), in the ObsSynth palettes.
- GLOBAL: MIDI channel, master tuning, velocity curve, button lights and the screen palette, kept apart from the presets.
- USB: class-compliant USB-MIDI in, the M-UPGRADE update entry and Felucca's update loader, so the FM-1 can always leave this firmware again.
- Flash: both FM-1 flash chips (Puya P25Q80 and the UC25HQ80 of units sold since autumn 2026).
- Package identity FM-1_917; the release package is built and signed by GitHub from the tagged source (fm1-build).
