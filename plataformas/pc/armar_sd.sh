#!/bin/bash
# Arma la SD de prueba del PC (sd_prueba/) juntando, con enlaces simbólicos archivo por archivo:
#   - GB, GBC, NES, homebrew y sonidos: ../../../maix_dock/sd
#   - GBA, SNES y PS1 (con su BIOS):     ../../../luckfox_consola/sd/roms
# Las carátulas .565 se generan dentro de sd_prueba/ (no ensucian las carpetas de origen).
# consola.json y logs/ que escribe el menú quedan solo en sd_prueba/. Se puede volver a correr:
# agrega lo nuevo y convierte solo las carátulas que falten.
set -euo pipefail
cd "$(dirname "$0")"
PROY=$(cd ../../.. && pwd)
SD=sd_prueba

enlazar_carpeta() { # origen destino
    local o=$1 d=$2
    [ -d "$o" ] || return 0
    mkdir -p "$d"
    for f in "$o"/*; do
        [ -f "$f" ] || continue
        case "$f" in *.565) continue ;; esac
        ln -sfn "$f" "$d/$(basename "$f")"
    done
}

mkdir -p "$SD/roms"
find "$SD/roms" -xtype l -delete   # enlaces a archivos que ya no existen (p. ej. las GBA en inglés reemplazadas)
for s in gb gbc nes homebrew; do enlazar_carpeta "$PROY/maix_dock/sd/roms/$s" "$SD/roms/$s"; done
for s in gba snes ps1; do enlazar_carpeta "$PROY/luckfox_consola/sd/roms/$s" "$SD/roms/$s"; done
ln -sfn "$PROY/maix_dock/sd/sistema" "$SD/sistema"
# notas de la comunidad (herramientas/notas.py) y fichas (herramientas/fichas.py), guardadas en este proyecto
for f in "$PROY"/luckfox_consola/sd/roms/*/_fichas; do
    [ -d "$f" ] && mkdir -p "$SD/roms/$(basename "$(dirname "$f")")" && ln -sfn "$f" "$SD/roms/$(basename "$(dirname "$f")")/_fichas"
done
for f in "$PROY"/luckfox_consola/sd/roms/*/_notas.json; do
    [ -f "$f" ] && mkdir -p "$SD/roms/$(basename "$(dirname "$f")")" && ln -sfn "$f" "$SD/roms/$(basename "$(dirname "$f")")/_notas.json"
done
[ -f "$PROY/luckfox_consola/sd/roms/_vale_la_pena.json" ] && ln -sfn "$PROY/luckfox_consola/sd/roms/_vale_la_pena.json" "$SD/roms/"
# GBA y SNES ya traen .565 propios en origen: se enlazan tal cual
for s in gba snes; do
    for f in "$PROY/luckfox_consola/sd/roms/$s"/*.565; do
        [ -f "$f" ] && ln -sfn "$f" "$SD/roms/$s/$(basename "$f")"
    done
done
python3 -I ../../herramientas/portadas.py "$SD/roms"
for s in "$SD"/roms/*/; do echo "$(basename "$s"): $(ls "$s" | grep -cvE '\.(jpg|jpeg|png|565|json|txt)$') juegos"; done
