#!/bin/bash
# Completa la carpeta portable de la versión PC (la llama `make paquete`, que antes compila menu y emu_*
# con los núcleos en lib/) y la comprime en dist/GameGirlColor-linux.tar.gz.
# NO incluye ROMs ni la BIOS de PS1: quedan las carpetas sd/roms/<consola>/ vacías para ponerlas.
# Sí incluye lo que se usa solo cuando los nombres calzan: notas, fichas, categorías y la lista VALE LA PENA,
# y los sonidos del menú.
set -euo pipefail
cd "$(dirname "$0")"
DIST=${1:-dist/GameGirlColor}
PROY=$(cd ../../.. && pwd)
SDP="$PROY/luckfox_consola/sd/roms"

mkdir -p "$DIST/sd/roms"
for s in gba snes gbc gb nes homebrew ps1; do mkdir -p "$DIST/sd/roms/$s"; done
cp -r "$PROY/maix_dock/sd/sistema" "$DIST/sd/"
[ -f "$SDP/_vale_la_pena.json" ] && cp "$SDP/_vale_la_pena.json" "$DIST/sd/roms/"
for d in "$SDP"/*/; do
    c=$(basename "$d")
    [ -f "$d/_notas.json" ] && cp "$d/_notas.json" "$DIST/sd/roms/$c/"
    [ -f "$d/_categorias.json" ] && cp "$d/_categorias.json" "$DIST/sd/roms/$c/"
    [ -d "$d/_fichas" ] && rm -rf "$DIST/sd/roms/$c/_fichas" && cp -r "$d/_fichas" "$DIST/sd/roms/$c/"
done
for c in gb gbc nes homebrew; do # categorías de las consolas que vienen del proyecto base
    f="$PROY/maix_dock/sd/roms/$c/_categorias.json"
    [ -f "$f" ] && cp "$f" "$DIST/sd/roms/$c/"
done

cat > "$DIST/jugar.sh" <<'EOF'
#!/bin/bash
# Abre la consola. Las ROMs van en sd/roms/<consola>/ (ver LEEME.txt).
DIR=$(cd "$(dirname "$0")" && pwd)
cd "$DIR"
CONSOLA_SD="$DIR/sd" exec "$DIR/menu" "$@"
EOF
chmod +x "$DIST/jugar.sh"

cat > "$DIST/LEEME.txt" <<'EOF'
GAME GIRL COLOR - versión para PC (Linux)
==========================================

Abrir: doble clic en jugar.sh (o en una terminal: ./jugar.sh)

Necesita SDL2 instalado:
  Ubuntu / Mint / Debian:  sudo apt install libsdl2-2.0-0
  Fedora:                  sudo dnf install SDL2
  Arch:                    sudo pacman -S sdl2

Juegos: copiar las ROMs en sd/roms/<consola>/
  gba/       .gba
  snes/      .smc o .sfc
  gbc/       .gbc
  gb/        .gb
  nes/       .nes
  homebrew/  .gb .gbc .nes
  ps1/       .chd o .cue/.bin + una BIOS de 512 KB (PS1 está en beta: se activa en AJUSTES)
Las carátulas van junto a la ROM con el mismo nombre (.png). Las notas, fichas (tecla Y) y la lista
VALE LA PENA aparecen solas cuando el nombre del archivo es el mismo que en la consola original.

Teclas (también en el título de la ventana):
  Flechas        cruz
  A, B (o S)     botones A y B
  X, Y           X e Y  (Y sobre un juego = ficha)
  L, R           L y R  (en VALE LA PENA: grupo anterior / siguiente)
  Enter          START
  Espacio        SELECT (favorito)
  Esc            pausa dentro del juego
  B + arriba/abajo  saltar de letra en la lista

Partidas, configuración y registro se guardan dentro de sd/.

Emuladores (núcleos libretro, en lib/): gpSP (GBA), snes9x2005 (SNES), Gambatte (GB/GBC), FCEUmm (NES),
PCSX-ReARMed (PS1). gpSP, Gambatte, FCEUmm y PCSX-ReARMed son GPLv2: su código está en
https://github.com/libretro. snes9x2005 tiene licencia de uso no comercial.
Fichas: Wikipedia en español (CC BY-SA) y LaunchBox Games Database.
EOF

# Requisitos de glibc (para saber en qué distros corre)
MAXG=$(for f in "$DIST"/menu "$DIST"/emu_* "$DIST"/lib/*.so; do objdump -T "$f" 2>/dev/null; done |
       grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)
echo "glibc mínima: $MAXG"

tar -C "$(dirname "$DIST")" -czf "$(dirname "$DIST")/GameGirlColor-linux.tar.gz" "$(basename "$DIST")"
echo "Listo: $(dirname "$DIST")/GameGirlColor-linux.tar.gz ($(du -h "$(dirname "$DIST")/GameGirlColor-linux.tar.gz" | cut -f1))"
