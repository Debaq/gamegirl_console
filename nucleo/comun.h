// Constantes compartidas por el menú y los emuladores (de maix_dock/menu.py, ver docs/ESPEC_MENU.md).
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define ANCHO 480
#define ALTO 320

// Rutas dentro de la SD (la raíz la da la plataforma, ver plat_raiz_sd).
const char *ruta_sd(const char *sub); // devuelve un búfer estático propio de cada 'sub'
#define SD_RAIZ ruta_sd("")
#define DIR_ROMS ruta_sd("/roms")
#define RUTA_CFG ruta_sd("/consola.json")
#define DIR_LOGS ruta_sd("/logs")
#define RUTA_LOG ruta_sd("/logs/consola.log")
#define RUTA_LOG_VIEJO ruta_sd("/logs/consola.log.1")
#define EXT_PORTADA ".565" // carátula pre-convertida en el PC (herramientas/portadas.py)

typedef struct { uint8_t r, g, b; } rgb;

#define C_FONDO ((rgb){10, 12, 30})
#define C_PANEL ((rgb){26, 30, 52})
#define C_TEXTO ((rgb){232, 234, 244})
#define C_TENUE ((rgb){120, 126, 156})
#define C_TEXTO_PIE ((rgb){190, 194, 214})
#define C_A ((rgb){235, 70, 110})
#define C_B ((rgb){250, 200, 40})
#define C_PILDORA ((rgb){78, 84, 120})
#define C_ROSA ((rgb){255, 105, 180})
#define C_LINEA_PIE ((rgb){40, 45, 75})
#define C_PEST_INACTIVA ((rgb){36, 41, 68})
#define C_BARRA_VACIA ((rgb){60, 64, 92})
#define C_FILA_AJUSTE ((rgb){44, 50, 86})
#define C_ERROR_CAJA ((rgb){170, 40, 40})
#define C_NINGUNO ((rgb){230, 90, 90})
#define C_BLANCO_PURO ((rgb){255, 255, 255})

// Logo
#define LOGO_BLANCO {{255, 255, 255}, {165, 175, 230}, {55, 60, 130}}
#define LOGO_ROSADO {{255, 175, 220}, {230, 50, 145}, {110, 20, 75}}

// Botones (misma máscara para teclado y, más adelante, el ESP32)
enum {
    BT_B = 1 << 0,
    BT_A = 1 << 1,
    BT_SELECT = 1 << 2,
    BT_START = 1 << 3,
    BT_ARRIBA = 1 << 4,
    BT_ABAJO = 1 << 5,
    BT_IZQ = 1 << 6,
    BT_DER = 1 << 7,
    BT_L = 1 << 8,
    BT_R = 1 << 9,
    BT_X = 1 << 10,
    BT_Y = 1 << 11,
};

// Sistemas (pestañas reales)
typedef enum { SIS_NES, SIS_GB, SIS_GBC, SIS_GBA, SIS_HOMEBREW, SIS_SNES, SIS_PS1, N_SIS } sistema_t;
// Emuladores
typedef enum { EMU_NES, EMU_GB, EMU_GBC, EMU_GBA, EMU_SNES, EMU_PS1, N_EMU } emulador_t;

static const char *const SIS_NOMBRE[N_SIS] = {"NES", "GAME BOY", "GB COLOR", "GBA", "HOMEBREW", "SNES", "PS1"};
static const char *const SIS_CARPETA[N_SIS] = {"nes", "gb", "gbc", "gba", "homebrew", "snes", "ps1"};
static const rgb SIS_COLOR[N_SIS] = {{230, 60, 60}, {140, 190, 40}, {200, 90, 220}, {105, 90, 220}, {70, 200, 170}, {90, 140, 240}, {235, 130, 50}};
// Orden de las pestañas de consolas en esta consola (pedido del usuario): GBA, SNES, GBC, GB, NES, homebrew;
// PS1 (experimento) después de SNES.
static const sistema_t ORDEN_PESTANAS[N_SIS] = {SIS_GBA, SIS_SNES, SIS_PS1, SIS_GBC, SIS_GB, SIS_NES, SIS_HOMEBREW};
#define C_RECIENTES ((rgb){250, 200, 40})
#define C_FAVORITOS C_ROSA
#define C_VALE ((rgb){80, 200, 120})
#define C_AJUSTES ((rgb){150, 156, 190})

static const char *const EMU_NOMBRE[N_EMU] = {"nes", "gb", "gbc", "gba", "snes", "ps1"};

// Modos de pantalla (nombre, ancho, alto; NES además recorte de overscan arriba/abajo)
typedef struct { const char *nombre; int w, h, recorte; } modo_pantalla;
#define N_PANT_NES 4
static const modo_pantalla PANTALLAS_NES[N_PANT_NES] = {{"Original", 0, 0, 0}, {"Llena", 366, 320, 8}, {"TV 4:3", 428, 320, 8}, {"Ancho", 480, 320, 8}};
#define N_PANT_SNES 4
static const modo_pantalla PANTALLAS_SNES[N_PANT_SNES] = {{"Original", 0, 0, 0}, {"Llena", 366, 320, 0}, {"TV 4:3", 427, 320, 0}, {"Ancho", 480, 320, 0}};
static const modo_pantalla PANTALLAS_GB[3] = {{"Original", 320, 288, 0}, {"Llena", 356, 320, 0}, {"Ancho", 480, 320, 0}};
static const modo_pantalla PANTALLAS_GBA[2] = {{"Original", 240, 160, 0}, {"Completa", 480, 320, 0}};
// PS1: el ancho cambia según el juego (256 a 640) y el alto (240 o 480 entrelazado): siempre se escala.
#define N_PANT_PS1 2
static const modo_pantalla PANTALLAS_PS1[N_PANT_PS1] = {{"TV 4:3", 427, 320, 0}, {"Ancho", 480, 320, 0}};

// Paletas de GB clásico: nombre en el menú y paleta interna equivalente de Gambatte.
static const char *const PALETAS_GB[12] = {"Verde original", "Grises", "GBC Cafe", "GBC Rojo", "GBC Azul",
                                           "GBC Azul oscuro", "GBC Verde", "GBC Verde oscuro", "GBC Pastel",
                                           "GBC Naranja", "GBC Amarillo", "GBC Invertido"};
static const char *const PALETAS_GAMBATTE[12] = {"GB - DMG", "GBC - Grayscale", "GBC - Brown", "GBC - Red",
                                                 "GBC - Blue", "GBC - Dark Blue", "GBC - Green",
                                                 "GBC - Dark Green", "GBC - Pastel Mix", "GBC - Orange",
                                                 "GBC - Yellow", "GBC - Inverted"};

static const int VOL_PCT[11] = {0, 3, 6, 10, 16, 24, 35, 50, 65, 80, 100};

#define ROM_GB_MAX (2 * 1024 * 1024)
