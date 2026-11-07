#include "sonido.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "comun.h"
#include "json.h"
#include "plataforma.h"

#define TASA_ORIGEN 16000
#define NIVEL_MAX 6000 // muestras esperando en la salida a partir de las que no se manda más
#define RELLENO 1100   // ceros al final: algunas salidas arrancan con ~1000 muestras y así terminan limpias

static int16_t *cola = NULL; // muestras a AUDIO_TASA pendientes de entregar
static int n_cola = 0, pos_cola = 0, cap_cola = 0;
static uint32_t fin_estimado = 0; // ms en que termina de sonar lo ya entregado

static uint8_t *raw = NULL; // ui_vN.raw del volumen cargado
static long n_raw = 0;
static int vol_raw = -1;
static json *indice = NULL;

static bool leer_archivo(const char *ruta, uint8_t **datos, long *n) {
    FILE *f = fopen(ruta, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    *n = ftell(f);
    fseek(f, 0, SEEK_SET);
    *datos = malloc(*n > 0 ? *n : 1);
    bool ok = *datos && fread(*datos, 1, *n, f) == (size_t)*n;
    fclose(f);
    if (!ok) { free(*datos); *datos = NULL; *n = 0; }
    return ok;
}

// Cuadros de 32 bits a 16 kHz -> mono AUDIO_TASA, agregados a la cola.
static void encolar(const uint8_t *d, long bytes) {
    if (pos_cola >= n_cola) pos_cola = n_cola = 0;
    long cuadros = bytes / 4;
    int n_sal = (int)(cuadros * (double)AUDIO_TASA / TASA_ORIGEN) + RELLENO;
    if (n_cola + n_sal > cap_cola) {
        cap_cola = n_cola + n_sal;
        cola = realloc(cola, sizeof *cola * cap_cola);
    }
    double paso = (double)TASA_ORIGEN / AUDIO_TASA;
    int o = 0;
    for (double p = 0; o < n_sal - RELLENO; p += paso, o++) {
        long i = (long)p;
        if (i >= cuadros) break;
        int16_t a = (int16_t)(d[4 * i] | d[4 * i + 1] << 8);
        int16_t b = i + 1 < cuadros ? (int16_t)(d[4 * i + 4] | d[4 * i + 5] << 8) : 0;
        double f = p - i;
        cola[n_cola + o] = (int16_t)(a + (b - a) * f);
    }
    memset(cola + n_cola + o, 0, sizeof *cola * (n_sal - o));
    n_cola += n_sal;
    sonido_tick();
}

void sonido_tick(void) {
    if (pos_cola >= n_cola) return;
    if (!plat_audio_listo()) { pos_cola = n_cola = 0; return; }
    int nivel = plat_audio_nivel();
    if (nivel >= NIVEL_MAX) return;
    int k = n_cola - pos_cola;
    if (k > NIVEL_MAX - nivel) k = NIVEL_MAX - nivel;
    plat_audio_enviar(cola + pos_cola, k);
    pos_cola += k;
    fin_estimado = plat_ms() + (uint32_t)((nivel + k) * 1000 / AUDIO_TASA);
}

void nucleo_dormir(int ms) {
    uint32_t fin = plat_ms() + (uint32_t)ms;
    for (;;) {
        sonido_tick();
        int32_t falta = (int32_t)(fin - plat_ms());
        if (falta <= 0) break;
        plat_dormir_ms(falta < 8 ? falta : 8);
    }
}

static void preparar(void) {
    if (!indice) {
        char ruta[200];
        snprintf(ruta, sizeof ruta, "%s/sistema/sonido/ui.json", SD_RAIZ);
        indice = json_leer_archivo(ruta);
    }
    if (vol_raw != CFG.volumen) {
        free(raw);
        raw = NULL;
        n_raw = 0;
        vol_raw = CFG.volumen;
        if (CFG.volumen > 0) {
            char ruta[200];
            snprintf(ruta, sizeof ruta, "%s/sistema/sonido/ui_v%d.raw", SD_RAIZ, CFG.volumen);
            leer_archivo(ruta, &raw, &n_raw);
        }
    }
}

bool sonando(void) { return pos_cola < n_cola || (int32_t)(fin_estimado - plat_ms()) > 0; }

void efecto(const char *nombre) {
    if (CFG.volumen <= 0 || !plat_audio_listo() || sonando()) return;
    preparar();
    if (!indice || !raw) return;
    json *r = json_campo(indice, nombre);
    if (!r || r->tipo != J_LISTA || r->n != 2) return;
    long ini = (long)r->hijos[0]->num, largo = (long)r->hijos[1]->num;
    if (ini < 0 || largo <= 0 || ini + largo > n_raw) return;
    encolar(raw + ini, largo);
}

void sonido_intro(void) {
    if (CFG.volumen <= 0 || !plat_audio_listo()) return;
    char ruta[200];
    snprintf(ruta, sizeof ruta, "%s/sistema/sonido/intro_v%d.raw", SD_RAIZ, CFG.volumen);
    uint8_t *d;
    long n;
    if (!leer_archivo(ruta, &d, &n)) return;
    encolar(d, n);
    free(d);
}

void sonido_esperar(void) {
    uint32_t t0 = plat_ms();
    while (sonando() && plat_ms() - t0 < 4000) nucleo_dormir(10);
}

void sonido_cortar(void) {
    pos_cola = n_cola = 0;
    fin_estimado = 0;
    plat_audio_vaciar();
}
