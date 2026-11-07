#!/usr/bin/env python3
"""Convierte las carátulas (.jpg/.png junto a cada ROM) a .565 para la consola.

Formato .565: ancho (uint16 LE), alto (uint16 LE) y los píxeles RGB565 little-endian.
La placa no decodifica JPG: lee el archivo tal cual. Tamaño máximo 174x154 (interior del marco).

Uso: python3 portadas.py /run/media/$USER/ROMS/roms     (solo convierte lo nuevo o cambiado)
"""
import os
import struct
import sys

from PIL import Image

MAX_W, MAX_H = 174, 154
EXT = (".jpg", ".jpeg", ".png", ".gif", ".bmp")


def convertir(origen, destino):
    im = Image.open(origen).convert("RGB")
    if im.width > MAX_W or im.height > MAX_H:
        im.thumbnail((MAX_W, MAX_H), Image.LANCZOS)
    w, h = im.size
    datos = bytearray(struct.pack("<HH", w, h))
    rgb = im.tobytes()
    for i in range(0, len(rgb), 3):
        r, g, b = rgb[i], rgb[i + 1], rgb[i + 2]
        datos += struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
    tmp = destino + ".tmp"
    with open(tmp, "wb") as f:
        f.write(datos)
    os.replace(tmp, destino)


def main():
    raiz = sys.argv[1] if len(sys.argv) > 1 else "."
    hechos = saltados = errores = 0
    for carpeta, _, archivos in os.walk(raiz):
        for a in sorted(archivos):
            base, ext = os.path.splitext(a)
            if ext.lower() not in EXT:
                continue
            origen = os.path.join(carpeta, a)
            destino = os.path.join(carpeta, base + ".565")
            if os.path.exists(destino) and os.path.getmtime(destino) >= os.path.getmtime(origen):
                saltados += 1
                continue
            try:
                convertir(origen, destino)
                hechos += 1
            except Exception as e:  # imagen dañada: se sigue con las demás
                errores += 1
                print("ERROR", origen, e)
            if hechos and hechos % 200 == 0:
                print(hechos, "convertidas...", flush=True)
    print(f"Listo: {hechos} convertidas, {saltados} ya estaban, {errores} con error")


if __name__ == "__main__":
    main()
