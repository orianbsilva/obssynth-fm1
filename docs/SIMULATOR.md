# O simulador

O firmware do FM-1 roda no browser: o mesmo C de `firmware/src/`, compilado para WebAssembly, com o HAL trocado
por estado que a página controla.

## Como funciona

| Peça | Onde | O quê |
| --- | --- | --- |
| Host | `tests/sim_host.h` | inclui os arquivos do firmware na ordem de `synth.c` (menos `lcd.c` e `main.c`). O relógio vem da contagem de amostras; teclas, botões e knobs são níveis e bordas setados pela página; o LCD é um framebuffer 240×240 RGB565; o ISR de áudio roda sob demanda |
| Núcleo | `tests/sim_core.c` | a API exportada (`sim_init`, `sim_render`, `sim_key`, `sim_btn`, `sim_enc`, `sim_master`, `sim_fb`, `sim_leds`, `sim_key_leds`, `sim_audio`, `sim_playing`). `sim_render` é o relógio: roda o ISR a cada meio buffer (256 amostras) e o frame do loop principal a cada 15 ms de áudio, como no aparelho |
| Motor JS | `web/sim/engine.js` | carrega o `.wasm` e embrulha a API |
| Worklet | `web/sim/worklet.js` | roda o firmware na thread de áudio (128 frames por quantum), recebe os controles como mensagens e devolve a tela e os LEDs a cada frame de UI |
| Página | `web/sim/` (`index.html` em inglês, `pt.html` em português, `app.js`, `layout.js`, `clips.js`, `page.css`, `device.css`, …) | a página do ObsSynth FM-1, na linguagem dos painéis do ObsSynth (as paletas e as fontes de `src/app/common/panel_kit.hpp` do repositório synth): a barra de cima com o display do preset e o LIGAR, os clips ao lado do FM-1, os sons (uma demo por motor), o manual e a instalação. O desenho do painel do FM-1 e a cola do simulador vêm do Hortator; o resto da página é próprio. Os textos dos scripts escolhem a língua pelo `lang` da página (`i18n.js`) |
| Controles | `web/sim/controls.js` | ids dos botões e knobs na ordem dos enums de `panel.c` (`tests/sim_glue.mjs` confere), mapa do teclado do computador |

## Build

`tools/build_sim.sh` gera os headers (`tools/gen_tables.py`, `tools/gen_font.py`, o keymap de
`firmware/hal/fm1_input.h`), compila e roda `tests/sim_test.c`, compila `tests/sim_core.c` para
`wasm32-wasi` (reactor) com `zig cc` e copia a página para `build/sim/`. `--host-only` para no teste de host.

`node tests/sim_glue.mjs` roda o `.wasm` pronto pelo mesmo `engine.js` que a página usa.

## Controles

Os da página: teclas e botões tocam enquanto pressionados; knobs com arraste, roda ou ↑ / ↓; teclado do computador
`Z S X D C V G B H N J M , L . ; / '` de F3, `Q W 3 E 4 R T 6 Y` até G5; ← / → OCT− / OCT+, Esc HOME.

## Limites

- A CPU na tela marca 0 % no simulador: o relógio ali é o das amostras, não o do processador.
- Sem armazenamento: o núcleo exporta a flash (`sim_flash*`) que a página guarda, mas o firmware ainda não escreve nela.
- As imagens dos sons (`web/sim/img/sound-*.png`) são telas do próprio simulador, geradas com o preset carregado e uma nota segurada.
- As fontes da página (Inter e JetBrains Mono, as dos painéis do ObsSynth) vêm do Google Fonts; sem rede, o navegador
  usa as alternativas (DejaVu, Segoe UI, monospace).
