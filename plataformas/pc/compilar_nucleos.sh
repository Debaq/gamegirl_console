#!/bin/bash
# Compila los núcleos libretro (código de terceros) para el PC, como bibliotecas compartidas.
# Clona copias limpias de los repositorios de ../../../luckfox_consola/consola (sin red), para no mezclar
# objetos ARM de la placa con los del PC:
#   nucleos/gpsp/gpsp_libretro.so               GBA
#   nucleos/fceumm/fceumm_libretro.so           NES
#   nucleos/gambatte/gambatte_libretro.so       GB y GBC
#   nucleos/snes9x2005/snes9x2005_libretro.so   SNES
#   nucleos/pcsx_rearmed/pcsx_rearmed_libretro.so  PS1 (experimento)
# Después: make (arma build/emu_*). No necesita sudo. Uso: ./compilar_nucleos.sh
set -e
cd "$(dirname "$0")"
ORIGEN=$(realpath ../../../luckfox_consola/consola)
J=$(nproc)
mkdir -p nucleos

compilar() { # nombre makefile .so
    local d=nucleos/$1
    [ -d "$d" ] || { git clone -q "$ORIGEN/$1" "$d" && git -C "$d" submodule -q update --init --depth 1 2>/dev/null || true; }
    if [ ! -f "$d/$3" ]; then
        echo "== $1 (registro en nucleos/$1.log)"
        make -C "$d" -f "$2" -j"$J" platform=unix > "nucleos/$1.log" 2>&1 || { tail -20 "nucleos/$1.log"; exit 1; }
    fi
    echo "OK $1"
}

compilar gpsp Makefile gpsp_libretro.so
compilar fceumm Makefile.libretro fceumm_libretro.so
compilar gambatte Makefile.libretro gambatte_libretro.so
compilar snes9x2005 Makefile snes9x2005_libretro.so
compilar pcsx_rearmed Makefile.libretro pcsx_rearmed_libretro.so

make
echo "Núcleos listos: make correr"
