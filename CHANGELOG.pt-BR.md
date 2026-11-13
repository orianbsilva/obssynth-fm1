# Changelog

[English](CHANGELOG.md)

## 0.1.0 - 2026-11-12

- Osciladores: dois por voz, cada um com seu motor (ALGORITHM): STD (SAW SQR TRI SIN RAMP FOLD FM OCT, moldadas pelo PW), SUPER (sete serras desafinadas), WT (quatro bancos de wavetable, sem aliasing) ou GRAIN (granular sobre três amostras).
- Voz: OSC 2 com SEMI, FINE e MIX; SUB e NOISE em seis cores; SYNC e RING; filtro de 12 ou 24 dB (LP BP HP NOTCH) com DRIVE; ADSRs de amp e de filtro com velocity, key tracking e LOOP; GLIDE.
- Modos: POLY (4 vozes), MONO, LEGATO e UNI (as quatro vozes numa nota, desafinadas e espalhadas no estéreo).
- Modulação: dois LFOs (SIN TRI SAW SQR S&H, 0,05 a 40 Hz) com TRIG, FADE e PHASE, cada um com um destino, também por oscilador.
- Efeitos: delay, reverb de sala (os do Hortator) e chorus estéreo; saída estéreo com saturação suave.
- Presets: 14 sons de fábrica e 16 seus na flash (registros A/B: um corte de energia mantém o banco anterior).
- Tela: cada página desenha o que edita (as curvas dos envelopes, a onda do LFO, a resposta do filtro, a wavetable, os grãos ao vivo), nas paletas do ObsSynth.
- GLOBAL: canal MIDI, afinação geral, curva de velocity, luz dos botões e a paleta da tela, guardados fora dos presets.
- USB: entrada USB-MIDI class compliant, a entrada de atualização do M-UPGRADE e o loader de atualização do Felucca: o FM-1 sempre pode sair deste firmware.
- Flash: os dois chips de flash do FM-1 (Puya P25Q80 e o UC25HQ80 das unidades vendidas desde o outono de 2026).
- Pacote: identidade FM-1_917; gerado e assinado pelo GitHub a partir do código da tag (fm1-build).
