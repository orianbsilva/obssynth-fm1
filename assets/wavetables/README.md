# Wavetables

The ObsSynth project's own banks (from the synth repository's `pcm/`, see its `SAMPLES.md`): single cycles of 1024
samples, float32 mono. `tools/gen_wavetables.py` band-limits each to 10 octave levels and writes them as 16-bit
tables (`build/gen/synth_wavetables.h`) for the WT oscillator (`firmware/src/voice.c`).

| File | Waves | TABLE |
|---|---|---|
| `bank_basic_8waves_1024samples_32float.wav` | 8 | BASIC |
| `bank_sine_to_saw_16waves_1024samples_32float.wav` | 16 | S>SAW |
| `bank_harmonic_16waves_1024samples_32float.wav` | 16 | HARM |
| `bank_morph_32waves_1024samples_32float.wav` | 32 | MORPH |
