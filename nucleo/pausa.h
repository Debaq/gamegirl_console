// Menú de pausa y "¿Te gustó?" (ESPEC_MENU.md §5): panel de 300x304 en (90, 8) encima del juego.
// Lo usa el programa que corre el emulador en cada plataforma.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "comun.h"

typedef struct {
    const char *ruta_rom;  // para el nombre y el favorito
    const char *sistema;   // "GBA", "SNES", ... (clave de favoritos y notas)
    emulador_t emu;
    rgb acento;
    bool (*guardar_estado)(void); // NULL si el núcleo no guarda estados
    bool (*cargar_estado)(void);
    bool (*hay_estado)(void);
    volatile bool *terminar;      // opcional: salir de la pausa (señal)
    // Opcional, solo para capturas de prueba: el último cuadro del juego en la pantalla
    const uint16_t *fondo;
    int fondo_x, fondo_y, fondo_w, fondo_h;
} pausa_ctx;

typedef enum { PAUSA_SEGUIR, PAUSA_REINICIAR, PAUSA_SALIR } pausa_res;

// Cambios hechos en la pausa que el emulador tiene que aplicar al seguir.
enum { PAUSA_CAMBIO_PANTALLA = 1, PAUSA_CAMBIO_PALETA = 2 };

pausa_res pausa_menu(const pausa_ctx *c, int *cambios);
void pausa_preguntar_gusto(const pausa_ctx *c); // solo si el juego no tiene nota

// Detector del gesto SELECT+START (~0,25 s sin A ni B): llamarlo cada cuadro con los botones.
// Devuelve los botones que debe ver el juego (sin SELECT/START mientras se arma) y pone *pedir
// en true al completarse.
uint16_t pausa_gesto(uint16_t botones, bool *pedir);
