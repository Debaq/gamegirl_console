// Envío a la pantalla de lo que cambió (sobre plat_pantalla_enviar) y capturas para pruebas.
#pragma once
#include <stdint.h>

// Manda 'px' (w x h, paso w) a (x, y), solo la franja de filas distinta de 'sombra', y la actualiza.
// Devuelve las filas enviadas.
int pantalla_cambios(const uint16_t *px, uint16_t *sombra, int x, int y, int w, int h);

// El lienzo del menú (dib.h) a la pantalla, solo lo que cambió.
void pantalla_mostrar(void);
void pantalla_forzar(void); // la próxima vez manda todo

// Si está definida, cada pantalla_mostrar guarda el lienzo en este PPM (pruebas sin pantalla).
extern const char *pantalla_captura;
void pantalla_guardar_ppm(const uint16_t *px, int w, int h, const char *ruta);
