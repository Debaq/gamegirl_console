# Armar el paquete PC completo (a mano)

`make paquete` deja la consola lista en `dist/GameGirlColor/` con las carpetas de juegos vacías.
Estos pasos le agregan tus juegos y lo comprimen. Todo se hace desde una terminal.

## 1. Paquete base

```bash
cd ~/Escritorio/Proyectos/gamegirl_console/plataformas/pc
make paquete
```

## 2. Copiar los juegos

Con `rsync` para no llevar partidas guardadas (`.sav`, estados). PS1 queda fuera: su carpeta queda vacía
y la pestaña no aparece (PS1 viene desactivada en AJUSTES).

```bash
P=~/Escritorio/Proyectos
D=$P/gamegirl_console/plataformas/pc/dist/GameGirlColor/sd/roms
X="--exclude=*.sav --exclude=*.srm --exclude=*.rtc --exclude=*.st* --exclude=*.tmp"

rsync -a $X $P/luckfox_consola/sd/roms/gba/  $D/gba/
rsync -a $X $P/luckfox_consola/sd/roms/snes/ $D/snes/
for c in gb gbc nes homebrew; do
    rsync -a $X $P/maix_dock/sd/roms/$c/ $D/$c/
done
```

Los nombres quedan iguales a los de tu SD, así que las notas, las fichas (Y) y VALE LA PENA funcionan solas.

## 3. Carátulas

La consola usa carátulas `.565`. GBA y SNES ya las traen; para GB, GBC, NES y homebrew se generan así
(solo convierte las que falten):

```bash
python3 -I $P/gamegirl_console/herramientas/portadas.py $D
```

## 4. Probar antes de comprimir

```bash
$P/gamegirl_console/plataformas/pc/dist/GameGirlColor/jugar.sh
```

## 5. Comprimir

Son ~10 GB (GBA 8,6 GB). Con zstd es mucho más rápido que gzip:

```bash
cd $P/gamegirl_console/plataformas/pc/dist
tar -I 'zstd -T0' -cf GameGirlColor-completo.tar.zst GameGirlColor
```

Tu colega lo abre con:

```bash
tar -xf GameGirlColor-completo.tar.zst
sudo pacman -S sdl2      # si no lo tiene
./GameGirlColor/jugar.sh
```

## Notas

- Si vuelves a correr `make paquete`, recompila la consola y actualiza notas y fichas, pero no borra los
  juegos copiados.
- Las partidas, la configuración y el registro de tu colega quedan dentro de su `GameGirlColor/sd/`.
