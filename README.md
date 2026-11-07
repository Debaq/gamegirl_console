# gamegirl_console: interfaz "Game Girl Color" para cualquier placa

Menú, pausa, "¿te gustó?", sonidos de interfaz, configuración y registro de la consola, en C portable.
Nació como port de `../maix_dock/menu.py` (K210, MicroPython); la especificación está en `docs/ESPEC_MENU.md`.

## Estructura

```
nucleo/          portable, no conoce el hardware
  plataforma.h   lo ÚNICO que cada placa implementa (pantalla, botones, tiempo, audio, juegos, info)
  menu.c         menú principal (menu_principal)
  pausa.c        menú de pausa, "¿te gustó?" y gesto SELECT+START (lo usan los emuladores)
  sonido.c       efectos e intro (sin hilos: sonido_tick / nucleo_dormir)
  pantalla.c     envío solo de lo que cambió, capturas PPM para pruebas
  dib.c fuente.h dibujo, fuente 8x12 de MaixPy, íconos y logo
  cfg.c json.c   consola.json, modos de pantalla, registro
plataformas/
  luckfox/       Luckfox Pico Mini (RV1103): SPI ST7796S, ESP32 por UART (audio y botones),
                 emuladores libretro como procesos (../posix/emu.c) y firmware del ESP32 (esp32_ayudante/)
  pc/            Linux con SDL2: la interfaz y los juegos en una ventana para probar sin placa
  posix/         emu.c (frontend libretro: emu_gba, emu_nes...), teclado de terminal y FIFO
                 /tmp/consola_teclas (luckfox y pc)
herramientas/    portadas.py: carátulas jpg/png -> .565 (RGB565 crudo)
```

## Uso

- PC: `plataformas/pc/compilar_nucleos.sh` una vez (núcleos libretro como .so en `plataformas/pc/nucleos/`:
  gpSP, FCEUmm, Gambatte, snes9x2005 y PCSX-ReARMed) y `make -C plataformas/pc correr`. Usa `plataformas/pc/sd_prueba/` (la arma `armar_sd.sh` / `make sd`:
  enlaces a las ROMs de `maix_dock/sd` y `luckfox_consola/sd` + carátulas .565); otra SD: `SD=carpeta`.
  Sin ventana (pruebas): `SDL_VIDEODRIVER=dummy ... build/menu -c captura.ppm -t 3` con teclas por stdin.
- Luckfox: `make -C plataformas/luckfox subir` / `instalar`. Los núcleos libretro (terceros) se compilan en
  `../luckfox_consola/consola/compilar_nucleos.sh`.

Teclado en la ventana del PC: flechas, A = A, S = B, X/Y/L/R = su botón (también J o Z = A, B o K = B, Q/E = L/R),
Espacio o N = SELECT, Enter o M = START, Esc o P = pausa, RePág/AvPág = saltar de letra.
En terminal/FIFO (`posix/teclado.c`, placa y pruebas) sigue: WASD, J/K = A/B, U/I = L/R, O/L = X/Y.

## Consolas

GBA (gpSP), SNES (snes9x2005), PS1 (PCSX-ReARMed, experimento), GBC y GB (Gambatte), NES (FCEUmm) y homebrew.
PS1: juegos .chd/.cue/.pbp/.m3u y la BIOS (archivo de 512 KB) en la misma carpeta `roms/ps1`.

## Agregar una plataforma

Implementar `nucleo/plataforma.h` y un `main()` que llame a `menu_principal()`. Si la placa corre
emuladores, el programa del emulador usa `pausa_menu()`, `pausa_preguntar_gusto()` y `pausa_gesto()`.
Pendiente: `plataformas/maix` (K210, como módulo del firmware MaixPy).
