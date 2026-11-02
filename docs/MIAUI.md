# Port do Link's Awakening DX para MiaUI (RG35XX)

Plano criado em 2026-11-01. Repositório: este clone (`ports/linksawakening-portable`,
upstream sp00nznet/linksawakening-portable v0.1.0). ROM: `../../Legend of Zelda, The -
Link's Awakening DX (USA, Europe) (Rev 2) (SGB Enhanced) (GB Compatible).gbc`,
SHA-1 `1c091225688d966928cc74336dbef2e07d12a47c` = revisão testada pelo projeto.

## O que já sabemos

| Lado | Fato | Onde |
|---|---|---|
| Jogo | Backend SDL2 pronto (`platform_sdl.cpp`, 1551 linhas): janela + `SDL_Renderer`, áudio `SDL_OpenAudioDevice`, `SDL_GameController` com fallback para joystick cru | `LinksAwakening/gbrecomp/runtime/src/` |
| Jogo | ImGui e multiplayer são opcionais (`LA_HAS_IMGUI`, `LA_HAS_MULTIPLAYER`); existem `menu_gui_stubs.c` e `asset_viewer_stubs.c` | idem |
| Jogo | `gen/rom.c` gerado tem ~109 MB: a compilação é lenta e pesada em RAM | `scripts/build_android.sh` |
| MiaUI | SDL2 + GLES2 via EGL (PVR), janela 640x480 `SDL_WINDOW_OPENGL`, swap interval 1 | `MiaUI-GPU-RG35XX/src/platform/platform.c:270` |
| MiaUI | Botões chegam como joystick (A=0 B=1 X=2 Y=3 L1=4 R1=5 L2=6 R2=7 SELECT=8 START=9 MENU=10 PLUS=11 MINUS=12, d-pad 13-16) e como teclas SDLK estranhas | `MiaUI-GPU-RG35XX/src/platform/platform.h` |
| MiaUI | Toolchain que funciona: Docker `debian:bookworm` + `gcc-arm-linux-gnueabihf` + `libsdl2-dev:armhf` (glibc 2.36 <= 2.37 do device) | `MiaUI-GPU-RG35XX/perf-update/docker/build-all.sh` |
| MiaUI | App = pasta `ROMS/Tools/<Nome>.pak/` com `launch.sh` | `skeleton/ROMS/Tools/Music.pak` |
| MiaUI | Painel 60,46 Hz (o áudio precisa acompanhar, mesmo problema do PS1) | memória miaui-perf |

## Estratégia

Reaproveitar `platform_sdl.cpp` (sem ImGui, sem multiplayer), compilado com o mesmo
toolchain do perf-update e linkado contra a `libSDL2-2.0.so.0` do próprio MiaUI (a ABI do SDL2 é
estável). Só escrevemos um backend próprio (`platform_miaui.c` com blit GLES2, no estilo do
minarch) se o `SDL_Renderer` não funcionar no SDL2 do device.

## Etapas

### 1. ROM para C (Mac, Docker)
- Copiar só a ROM para uma pasta própria (`ports/rom/`) e montar como `/rom:ro` (o script
  pega o primeiro `.gbc`/`.zip` da pasta).
- `docker run --rm -v $PWD:/src -v ../rom:/rom:ro devkitpro/devkitppc bash /src/scripts/recomp.sh`
  (ou rodar o mesmo script num bookworm com cmake/ninja/python3, se a imagem devkitppc não
  tiver build arm64).
- Pronto quando: `LinksAwakening/gen/rom.c` existe.

### 2. Build nativo de referência no Mac (opcional, mas rápido de diagnosticar)
- CMake do runtime com SDL2 do Homebrew, sem ImGui. Confirma que o jogo roda e mostra
  as flags/arquivos que o build precisa antes de partir para o cross.

### 3. `scripts/build_miaui.sh` (cross armhf)
- Imagem `debian:bookworm` + os mesmos pacotes do perf-update (`g++-arm-linux-gnueabihf`,
  `libsdl2-dev:armhf`).
- Compilar `gen/*.c`, `rom_main.c`, runtime (`gbrt.c`, `ppu.c`, `audio.c`, `interpreter.c`,
  `platform_sdl.cpp`, stubs) com `-DGB_HAS_SDL2`, sem `LA_HAS_IMGUI`/`LA_HAS_MULTIPLAYER`,
  `-mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard`.
- `rom.c` em `-O1` (ou `-O2` se a RAM do Docker aguentar; subir a memória do Docker
  Desktop para 8 GB+ se o cc1 for morto).
- Linkar só com `-lSDL2 -lpthread -lm` e checar com `readelf -d` / `objdump -T` que
  nenhum símbolo exige glibc > 2.37.
- Adicionar a linha `miaui` em `scripts/targets.tsv`.
- Pronto quando: `build-miaui/linksawakening.elf` (ARM EABI5 hard-float) existe.

### 4. Primeiro boot no device (ADB)
- Empurrar o ELF para `/mnt/SDCARD/Tools/LinksAwakening.pak/` (confirmar o caminho real do
  SD no MiaUI via adb) e rodar pelo shell com `SDL_` logs no stderr.
- Verificar, nessa ordem: a janela abre (qual driver de vídeo o SDL escolheu, se o
  `SDL_CreateRenderer` pegou `opengles2`), a imagem aparece, o áudio toca, os botões respondem.
- Se o renderer falhar: escrever `platform_miaui.c` com o mesmo setup EGL/GLES2 do
  `platform.c` do MiaUI (textura 160x144, quad escalado) e trocar no build.

### 5. Ajustes para o RG35XX
- **Tela:** fullscreen 640x480; escala inteira 3x (480x432, centralizada) como padrão, com
  opção de esticar 4:3.
- **Controles:** mapear o joystick (índices acima): D-pad 13-16, A=0, B=1, START=9,
  SELECT=8. MENU (10) = salvar e sair. Se o `SDL_GameController` não reconhecer o
  dispositivo, o fallback de joystick cru do backend precisa usar esses índices (ou
  passar um mapping via `SDL_GAMECONTROLLERCONFIG` no `launch.sh`).
- **Saves:** `gb_platform_set_save_dir` apontando para `/mnt/SDCARD/Saves/LinksAwakening/`
  (ou dentro do `.pak`), para sobreviver a reinstalações.
- **Vsync/áudio:** swap interval 1; se o áudio estalar ou atrasar, corrigir a taxa de
  entrada para 60,46 Hz como fizemos no minarch (PANEL_HZ_X100).
- **CPU:** o jogo deve rodar folgado; fixar um clock mais baixo no `launch.sh` se o
  MiaUI expuser isso, para poupar bateria.

### 6. Pacote
- `ROMS/Tools/LinksAwakening.pak/`: `launch.sh` (cd na pasta, env do SDL, executa o ELF),
  `linksawakening.elf`, ícone se o MiaUI suportar.
- Zip com essa pasta para copiar no cartão. Nada de ROM ou código gerado no pacote
  público: quem for usar gera o próprio build a partir da ROM.

### 7. Teste final
- Título, criar save, entrar na primeira dungeon, salvar, sair pelo MENU, voltar e
  carregar o save. Medir fps/engasgos por alguns minutos.

## Riscos

1. **SDL2 do MiaUI sem `SDL_Renderer` GLES2 utilizável** → backend GLES2 próprio (etapa 4).
2. **Compilação do `rom.c` estourar a RAM do Docker** → `-O1`, ou dividir o arquivo
   (o recompilador pode gerar vários `.c`), ou aumentar a memória.
3. **Dependência de símbolo glibc novo** vinda da libstdc++ estática/dinâmica → linkar
   `-static-libstdc++ -static-libgcc` se o device não tiver a versão certa.
4. **Mapeamento de botões** diferente do esperado → logar os eventos de joystick na
   primeira execução.
