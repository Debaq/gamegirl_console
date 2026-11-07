// Lo único que cada chip/placa implementa para usar la interfaz de la consola.
// El núcleo (menú, pausa, sonidos, configuración) solo habla con el hardware a través de esto.
// Implementaciones: plataformas/luckfox (Luckfox Pico + ESP32), plataformas/pc (SDL2).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "comun.h"

// ---------------------------------------------------------------- pantalla (480x320 RGB565)

// reiniciar: true al encender (reset y configuración); false si otro programa ya la configuró.
void plat_pantalla_abrir(bool reiniciar);
void plat_pantalla_cerrar(void);
// Rectángulo de píxeles RGB565 nativos; 'paso' en píxeles. Bloquea hasta que se envió.
void plat_pantalla_enviar(const uint16_t *px, int x, int y, int w, int h, int paso);

// ---------------------------------------------------------------- botones

void plat_entrada_abrir(void);
void plat_entrada_cerrar(void);
uint16_t plat_botones(void);     // máscara BT_* de los botones mantenidos ahora
bool plat_pausa_pedida(void);    // atajo de pausa propio de la plataforma (tecla); el gesto
                                 // SELECT+START lo detecta el núcleo

// ---------------------------------------------------------------- tiempo

uint32_t plat_ms(void);          // milisegundos monótonos
void plat_dormir_ms(int ms);

// ---------------------------------------------------------------- audio (mono, 32768 Hz)

#define AUDIO_TASA 32768
#define AUDIO_OBJETIVO 1638      // nivel buscado del búfer de salida: ~50 ms

bool plat_audio_listo(void);     // hay por dónde sonar
void plat_audio_enviar(const int16_t *m, int n);
int plat_audio_nivel(void);      // muestras que esperan sonar
void plat_audio_vaciar(void);    // descarta lo que espera (pausa, salir)

// ---------------------------------------------------------------- almacenamiento y juegos

const char *plat_raiz_sd(void);  // carpeta con roms/, sistema/, consola.json (stdio/dirent POSIX)

// ¿Hay emulador para este tipo en esta plataforma? (las pestañas sin emulador no aparecen)
bool plat_emulador_disponible(emulador_t e);

// Juega la ROM y vuelve cuando el usuario sale. Devuelve false y llena 'aviso' si falló.
// El emulador puede cambiar consola.json (volumen, favoritos, notas): el menú lo vuelve a leer.
bool plat_jugar(const char *ruta, const char *sistema, emulador_t e, char *aviso, size_t n_aviso);

// ---------------------------------------------------------------- información (pantalla Registro)

void plat_info(char *out, size_t n);   // p. ej. "RV1103 1,2 GHz - 64 MB RAM"
long plat_mem_libre_kb(void);
