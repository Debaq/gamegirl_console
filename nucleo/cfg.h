// consola.json (mismo formato que maix_dock) y registro en la SD.
#pragma once
#include <stdbool.h>

#define MAX_RECIENTES 12

typedef struct {
    char sis[16];
    char arch[256];
} ref_juego;

typedef struct {
    char clave[280]; // "<SISTEMA>/<archivo>"
    int valor;       // 1 gustó, 0 sin opinión, -1 no gustó
} nota_t;

typedef struct {
    int volumen, balance, nes_pantalla, gb_pantalla, gb_paleta, gba_pantalla, snes_pantalla, ps1_pantalla;
    bool intro, ver_no_gustaron, orden_nota; // orden_nota: listas de consola por nota (si no, por nombre)
    bool ps1_activa;                         // PS1 es beta: oculta salvo que se active en AJUSTES
    bool letra_chica;                        // lista de juegos con letra pequeña (la de las fichas)
    ref_juego recientes[MAX_RECIENTES];
    int n_recientes;
    ref_juego *favoritos;
    int n_favoritos;
    nota_t *notas;
    int n_notas;
} config;

extern config CFG;

void cfg_cargar(void);
void cfg_guardar(void);

bool cfg_es_favorito(const char *sis, const char *arch);
bool cfg_alternar_favorito(const char *sis, const char *arch); // devuelve el nuevo estado
void cfg_agregar_reciente(const char *sis, const char *arch);
bool cfg_nota(const char *sis, const char *arch, int *valor); // false si no hay nota
void cfg_poner_nota(const char *sis, const char *arch, int valor);
int cfg_cuantas_no_gustaron(void);

// Modo de pantalla elegido para cada emulador, y cambiarlo (+1 / -1, con vuelta).
#include "comun.h"
const modo_pantalla *cfg_modo_pantalla(emulador_t e);
void cfg_cambiar_pantalla(emulador_t e, int d);
const char *cfg_nombre_paleta(void);

// Registro: "%7.1f texto" con segundos desde el arranque, rota a 256 KB.
void registrar(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
