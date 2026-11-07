#include "pantalla.h"

#include <stdio.h>
#include <string.h>

#include "dib.h"
#include "plataforma.h"

const char *pantalla_captura = NULL;
static uint16_t sombra[ANCHO * ALTO]; // lo que ya está en la pantalla

int pantalla_cambios(const uint16_t *px, uint16_t *sh, int x, int y, int w, int h) {
    int y0 = -1, y1 = -1;
    for (int j = 0; j < h; j++)
        if (memcmp(px + (long)j * w, sh + (long)j * w, (size_t)w * 2)) {
            if (y0 < 0) y0 = j;
            y1 = j;
        }
    if (y0 < 0) return 0;
    int n = y1 - y0 + 1;
    memcpy(sh + (long)y0 * w, px + (long)y0 * w, (size_t)n * w * 2);
    plat_pantalla_enviar(px + (long)y0 * w, x, y + y0, w, n, w);
    return n;
}

void pantalla_mostrar(void) {
    pantalla_cambios(lienzo, sombra, 0, 0, ANCHO, ALTO);
    if (pantalla_captura) pantalla_guardar_ppm(lienzo, ANCHO, ALTO, pantalla_captura);
}

void pantalla_forzar(void) { memset(sombra, 0xA5, sizeof sombra); }

void pantalla_guardar_ppm(const uint16_t *px, int w, int h, const char *ruta) {
    FILE *f = fopen(ruta, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        uint16_t p = px[i];
        unsigned char c[3] = {(unsigned char)((p >> 11) << 3), (unsigned char)(((p >> 5) & 63) << 2),
                              (unsigned char)((p & 31) << 3)};
        fwrite(c, 1, 3, f);
    }
    fclose(f);
}
