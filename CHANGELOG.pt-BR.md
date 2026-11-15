# Changelog

[English](CHANGELOG.md)

## 0.2.0 - 2026-11-15

- SUPER: um formato para as suas sete vozes, o WAVE do oscilador na página + (WAVE SIDE, depois NOISE NCOL ou SYNC RING): SAW como antes; SQR, o DETUN abrindo também a largura das quadradas laterais (50 +- 30 %), oco como um PWM; TRI, espalhamento mais estreito e um fold na soma que cresce com o DETUN; SIN, sete senos desafinados, o batimento lento de um órgão ou de um pad. O STD não muda. Presets novos F15 SQUARES (SUPER SQR) e F16 GLASS (SUPER TRI).

## 0.1.1 - 2026-11-15

- Pacote: identidade nova, FM-1_627010, só do ObsSynth (a FM-1_917 caía na faixa FM-1_91x do Felucca). Nada mais muda; a partir da 0.1.0, instale como sempre.

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
