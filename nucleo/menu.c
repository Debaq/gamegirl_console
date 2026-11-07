// Menú "Game Girl Color": port en C de maix_dock/menu.py (ver docs/ESPEC_MENU.md).
// Portable: el hardware se usa solo a través de plataforma.h. Cada plataforma llama a menu_principal().
//
// Opciones: [-c captura.ppm] [-t segundos]   (para probar sin mirar la pantalla)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cfg.h"
#include "comun.h"
#include "dib.h"
#include "json.h"
#include "menu.h"
#include "pantalla.h"
#include "plataforma.h"
#include "sonido.h"

#define FILA_H 30
#define FILAS 7
#define LISTA_Y 58
#define MAX_CHARS 17
// Lista de juegos: letra mediana (escala 2, 7 filas) o pequeña (escala 1, la de las fichas: 13 filas y nombres
// más largos). AJUSTES -> Letra.
static int lista_esc(void) { return CFG.letra_chica ? 1 : 2; }
static int lista_fila_h(void) { return CFG.letra_chica ? 16 : FILA_H; }
static int lista_filas(void) { return CFG.letra_chica ? 13 : FILAS; }
static int lista_chars(void) { return CFG.letra_chica ? 31 : MAX_CHARS; }
#define PORTADA_ESPERA 350
// En la K210 leer la SD mientras sonaba el audio hacía chicharrear: la carátula se leía recién al dejar de
// mover (PORTADA_ESPERA). En la Luckfox y el PC no pasa: se lee al momento de elegir el juego.
#define PORTADA_INMEDIATA 1
#define TAB_H 34
#define TAB_Y 5

static const char *const CATEGORIAS[] = {"En espanol", "Plataformas", "Accion", "Aventura", "RPG", "Puzles", "Deportes",
                                         "Carreras", "Disparos", "Lucha", "Estrategia", "Mesa y cartas", "Otros"};
#define N_CATEGORIAS 13

static volatile sig_atomic_t terminar = 0;
static void al_senal(int s) { (void)s; terminar = 1; }
static uint32_t t_fin_prueba = 0; // -t: salir de cualquier pantalla al llegar a este instante

static bool debe_terminar(void) {
    if (t_fin_prueba && (int32_t)(plat_ms() - t_fin_prueba) >= 0) terminar = 1;
    return terminar;
}

// ---------------------------------------------------------------- juegos

typedef struct {
    char *arch;    // nombre de archivo
    char *bajo;    // nombre sin extensión en minúsculas (búsqueda)
    int cat;       // índice en CATEGORIAS o -1
    short nota;    // nota de la comunidad x10 (46 = 4,6 de 5); 0 = sin nota. De _notas.json (herramientas/notas.py)
    int votos;
} juego;

typedef struct {
    bool presente;
    juego *todos;
    int n_todos;
    int *juegos; // índices en todos: visibles (sin nota -1 salvo ver_no_gustaron)
    int n_juegos;
    int *vis;    // resultado del filtro de búsqueda (NULL = sin filtro)
    int n_vis;
    char busqueda[24];
    int categoria; // índice en cats
    const char *cats[N_CATEGORIAS + 1];
    int n_cats;
} consola_t;

static consola_t SIS[N_SIS];

static bool ext_valida(sistema_t s, const char *nombre) {
    const char *p = strrchr(nombre, '.');
    if (!p) return false;
    switch (s) {
    case SIS_NES: return !strcasecmp(p, ".nes");
    case SIS_GB:
    case SIS_GBC: return !strcasecmp(p, ".gb") || !strcasecmp(p, ".gbc");
    case SIS_GBA: return !strcasecmp(p, ".gba");
    case SIS_HOMEBREW: return !strcasecmp(p, ".nes") || !strcasecmp(p, ".gb") || !strcasecmp(p, ".gbc");
    case SIS_SNES: return !strcasecmp(p, ".sfc") || !strcasecmp(p, ".smc");
    // PS1: la imagen se elige por su índice (.cue/.m3u); los .bin de las pistas no se listan.
    case SIS_PS1: return !strcasecmp(p, ".cue") || !strcasecmp(p, ".chd") || !strcasecmp(p, ".pbp") || !strcasecmp(p, ".m3u");
    default: return false;
    }
}

static int cmp_juego(const void *a, const void *b) {
    return strcmp(((const juego *)a)->bajo, ((const juego *)b)->bajo);
}

static char *minusculas_sin_ext(const char *arch) {
    char *s = strdup(arch);
    char *p = strrchr(s, '.');
    if (p) *p = 0;
    for (char *q = s; *q; q++) *q = (char)tolower((unsigned char)*q);
    return s;
}

static void listar(sistema_t s) {
    consola_t *c = &SIS[s];
    char dir[256], ruta[300];
    snprintf(dir, sizeof dir, "%s/%s", DIR_ROMS, SIS_CARPETA[s]);
    snprintf(ruta, sizeof ruta, "%s/_ocultos.json", dir);
    json *ocultos = json_leer_archivo(ruta);
    snprintf(ruta, sizeof ruta, "%s/_categorias.json", dir);
    json *cats = json_leer_archivo(ruta);
    snprintf(ruta, sizeof ruta, "%s/_notas.json", dir);
    json *notas = json_leer_archivo(ruta);

    DIR *d = opendir(dir);
    int cap = 0;
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (e->d_name[0] == '.' || !ext_valida(s, e->d_name)) continue;
            bool oculto = false;
            if (ocultos && ocultos->tipo == J_LISTA)
                for (int i = 0; i < ocultos->n && !oculto; i++) {
                    const char *t = json_texto(ocultos->hijos[i]);
                    oculto = t && !strcmp(t, e->d_name);
                }
            if (oculto) continue;
            if (c->n_todos == cap) {
                cap = cap ? cap * 2 : 256;
                c->todos = realloc(c->todos, sizeof *c->todos * cap);
            }
            juego *j = &c->todos[c->n_todos++];
            j->arch = strdup(e->d_name);
            j->bajo = minusculas_sin_ext(e->d_name);
            j->cat = -1;
            j->nota = 0;
            j->votos = 0;
            json *nv = json_campo(notas, e->d_name); // [nota 1-5, votos, año, "género"]
            if (nv && nv->tipo == J_LISTA && nv->n >= 2 && nv->hijos[0]->tipo == J_NUM) {
                j->nota = (short)(nv->hijos[0]->num * 10 + 0.5);
                j->votos = (int)nv->hijos[1]->num;
            }
            const char *cn = json_texto(json_campo(cats, e->d_name));
            for (int k = 0; cn && k < N_CATEGORIAS; k++)
                if (!strcmp(cn, CATEGORIAS[k])) j->cat = k;
        }
        closedir(d);
    }
    json_libre(ocultos);
    json_libre(cats);
    json_libre(notas);
    if (c->n_todos) qsort(c->todos, c->n_todos, sizeof *c->todos, cmp_juego);
    c->juegos = malloc(sizeof(int) * (c->n_todos + 1));
}

static int buscar_juego(sistema_t s, const char *arch) {
    consola_t *c = &SIS[s];
    for (int i = 0; i < c->n_todos; i++)
        if (!strcmp(c->todos[i].arch, arch)) return i;
    return -1;
}

static bool aplicar_filtro(sistema_t s, const char *texto, int categoria, int **res, int *n);

// Un sistema aparece si la plataforma tiene emulador para sus juegos.
static bool sistema_disponible(int s) {
    switch (s) {
    case SIS_NES: return plat_emulador_disponible(EMU_NES);
    case SIS_GB: return plat_emulador_disponible(EMU_GB);
    case SIS_GBC: return plat_emulador_disponible(EMU_GBC);
    case SIS_GBA: return plat_emulador_disponible(EMU_GBA);
    case SIS_SNES: return plat_emulador_disponible(EMU_SNES);
    case SIS_PS1: return plat_emulador_disponible(EMU_PS1);
    case SIS_HOMEBREW: return plat_emulador_disponible(EMU_NES) || plat_emulador_disponible(EMU_GB);
    default: return false;
    }
}

static const juego *orden_todos; // para cmp_por_nota
static int cmp_por_nota(const void *a, const void *b) {
    const juego *x = &orden_todos[*(const int *)a], *y = &orden_todos[*(const int *)b];
    if (x->nota != y->nota) return y->nota - x->nota;
    if (x->votos != y->votos) return y->votos - x->votos;
    return strcmp(x->bajo, y->bajo);
}

// Recalcula 'juegos' (por notas de gusto y orden elegido) y las categorías presentes; rehace el filtro.
static void refrescar_notas(void) {
    for (int s = 0; s < N_SIS; s++) {
        consola_t *c = &SIS[s];
        c->n_juegos = 0;
        for (int i = 0; i < c->n_todos; i++) {
            int v;
            if (!CFG.ver_no_gustaron && cfg_nota(SIS_NOMBRE[s], c->todos[i].arch, &v) && v < 0) continue;
            c->juegos[c->n_juegos++] = i;
        }
        if (CFG.orden_nota && c->n_juegos > 1) {
            orden_todos = c->todos;
            qsort(c->juegos, (size_t)c->n_juegos, sizeof(int), cmp_por_nota);
        }
        c->presente = c->n_juegos > 0 && sistema_disponible(s) && (s != SIS_PS1 || CFG.ps1_activa);
        const char *cat_actual = c->n_cats ? c->cats[c->categoria] : "Todas";
        c->n_cats = 0;
        c->cats[c->n_cats++] = "Todas";
        for (int k = 0; k < N_CATEGORIAS; k++)
            for (int i = 0; i < c->n_juegos; i++)
                if (c->todos[c->juegos[i]].cat == k) {
                    c->cats[c->n_cats++] = CATEGORIAS[k];
                    break;
                }
        c->categoria = 0;
        for (int k = 0; k < c->n_cats; k++)
            if (!strcmp(c->cats[k], cat_actual)) c->categoria = k;
        if (c->vis) {
            free(c->vis);
            c->vis = NULL;
            int *r = NULL, n = 0;
            if (aplicar_filtro(s, c->busqueda, c->categoria, &r, &n)) {
                c->vis = r;
                c->n_vis = n;
            }
        }
    }
}

// ---------------------------------------------------------------- pestañas

typedef enum { PT_RECIENTES, PT_FAVORITOS, PT_VALE, PT_SIS, PT_AJUSTES } pest_tipo;
typedef struct { pest_tipo tipo; sistema_t sis; int sel; } pestana;

// VALE LA PENA: lista curada de varias consolas (roms/_vale_la_pena.json, armada en el PC con
// herramientas/vale_la_pena.py): grupos en orden ("Lo mejor" y sagas en orden de salida).
typedef struct { char *nombre; int n; sistema_t *sis; char **arch; } grupo_vale;
static grupo_vale *VALE = NULL;
static int n_vale = 0, vale_g = 0;

static int sistema_por_carpeta(const char *c) {
    for (int s = 0; s < N_SIS; s++)
        if (!strcmp(SIS_CARPETA[s], c)) return s;
    return -1;
}

static void cargar_vale(void) {
    char ruta[300];
    snprintf(ruta, sizeof ruta, "%s/_vale_la_pena.json", DIR_ROMS);
    json *j = json_leer_archivo(ruta);
    json *gs = json_campo(j, "grupos");
    if (gs && gs->tipo == J_LISTA && gs->n > 0) {
        VALE = calloc((size_t)gs->n, sizeof *VALE);
        for (int i = 0; VALE && i < gs->n; i++) {
            const char *nom = json_texto(json_campo(gs->hijos[i], "nombre"));
            json *js = json_campo(gs->hijos[i], "juegos");
            if (!nom || !js || js->tipo != J_LISTA) continue;
            grupo_vale *g = &VALE[n_vale];
            g->sis = malloc(sizeof *g->sis * (size_t)(js->n + 1));
            g->arch = malloc(sizeof *g->arch * (size_t)(js->n + 1));
            for (int k = 0; k < js->n; k++) {
                json *e = js->hijos[k];
                if (!e || e->tipo != J_LISTA || e->n < 2) continue;
                const char *c = json_texto(e->hijos[0]), *a = json_texto(e->hijos[1]);
                int s = c ? sistema_por_carpeta(c) : -1;
                if (s < 0 || !a) continue;
                g->sis[g->n] = (sistema_t)s;
                g->arch[g->n++] = strdup(a);
            }
            if (g->n) { g->nombre = strdup(nom); n_vale++; }
        }
    }
    json_libre(j);
}

static pestana PEST[N_SIS + 4];
static int n_pest = 0, actual = 0;

static bool hay_consolas(void);

static void armar_pestanas(void) {
    int sel_ant[N_SIS + 4] = {0};
    pest_tipo tipo_ant[N_SIS + 4];
    sistema_t sis_ant[N_SIS + 4];
    int n_ant = n_pest;
    for (int i = 0; i < n_pest; i++) { sel_ant[i] = PEST[i].sel; tipo_ant[i] = PEST[i].tipo; sis_ant[i] = PEST[i].sis; }
    n_pest = 0;
    PEST[n_pest++] = (pestana){PT_RECIENTES, 0, 0};
    PEST[n_pest++] = (pestana){PT_FAVORITOS, 0, 0};
    if (n_vale && hay_consolas()) PEST[n_pest++] = (pestana){PT_VALE, 0, 0};
    for (int i = 0; i < N_SIS; i++)
        if (SIS[ORDEN_PESTANAS[i]].presente) PEST[n_pest++] = (pestana){PT_SIS, ORDEN_PESTANAS[i], 0};
    PEST[n_pest++] = (pestana){PT_AJUSTES, 0, 0};
    for (int i = 0; i < n_pest; i++)
        for (int k = 0; k < n_ant; k++)
            if (tipo_ant[k] == PEST[i].tipo && (PEST[i].tipo != PT_SIS || sis_ant[k] == PEST[i].sis))
                PEST[i].sel = sel_ant[k];
}

static bool hay_consolas(void) {
    for (int s = 0; s < N_SIS; s++)
        if (SIS[s].presente) return true;
    return false;
}

static rgb color_pestana(const pestana *p) {
    switch (p->tipo) {
    case PT_RECIENTES: return C_RECIENTES;
    case PT_FAVORITOS: return C_FAVORITOS;
    case PT_VALE: return C_VALE;
    case PT_AJUSTES: return C_AJUSTES;
    default: return SIS_COLOR[p->sis];
    }
}

static const char *nombre_pestana(const pestana *p) {
    switch (p->tipo) {
    case PT_RECIENTES: return "RECIENTES";
    case PT_FAVORITOS: return "FAVORITOS";
    case PT_VALE: return "VALE LA PENA";
    case PT_AJUSTES: return "AJUSTES";
    default: return SIS_NOMBRE[p->sis];
    }
}

// Entradas de la pestaña actual: (sistema, índice en todos)
typedef struct { sistema_t sis; int idx; } entrada_t;
static entrada_t *ENT = NULL;
static int n_ent = 0, cap_ent = 0;

static void agregar_ent(sistema_t s, int idx) {
    if (n_ent == cap_ent) {
        cap_ent = cap_ent ? cap_ent * 2 : 512;
        ENT = realloc(ENT, sizeof *ENT * cap_ent);
    }
    ENT[n_ent++] = (entrada_t){s, idx};
}

static int sistema_por_nombre(const char *n) {
    for (int s = 0; s < N_SIS; s++)
        if (!strcmp(SIS_NOMBRE[s], n)) return s;
    return -1;
}

static int cmp_ent_nombre(const void *a, const void *b) {
    const entrada_t *x = a, *y = b;
    return strcmp(SIS[x->sis].todos[x->idx].bajo, SIS[y->sis].todos[y->idx].bajo);
}

static void armar_entradas(void) {
    n_ent = 0;
    pestana *p = &PEST[actual];
    if (p->tipo == PT_SIS) {
        consola_t *c = &SIS[p->sis];
        if (c->vis)
            for (int i = 0; i < c->n_vis; i++) agregar_ent(p->sis, c->vis[i]);
        else
            for (int i = 0; i < c->n_juegos; i++) agregar_ent(p->sis, c->juegos[i]);
    } else if (p->tipo == PT_RECIENTES || p->tipo == PT_FAVORITOS) {
        ref_juego *lista = p->tipo == PT_RECIENTES ? CFG.recientes : CFG.favoritos;
        int n = p->tipo == PT_RECIENTES ? CFG.n_recientes : CFG.n_favoritos;
        for (int i = 0; i < n; i++) {
            int s = sistema_por_nombre(lista[i].sis);
            if (s < 0 || !SIS[s].presente) continue;
            int idx = buscar_juego(s, lista[i].arch);
            if (idx >= 0) agregar_ent(s, idx);
        }
        if (p->tipo == PT_FAVORITOS && n_ent > 1) qsort(ENT, n_ent, sizeof *ENT, cmp_ent_nombre);
    } else if (p->tipo == PT_VALE && n_vale) {
        const grupo_vale *g = &VALE[vale_g];
        for (int i = 0; i < g->n; i++) {
            if (!SIS[g->sis[i]].presente) continue;
            int idx = buscar_juego(g->sis[i], g->arch[i]);
            if (idx >= 0) agregar_ent(g->sis[i], idx);
        }
    }
    if (p->sel >= n_ent) p->sel = n_ent ? n_ent - 1 : 0;
    if (p->sel < 0) p->sel = 0;
}

static const char *arch_de(const entrada_t *e) { return SIS[e->sis].todos[e->idx].arch; }

static void ruta_rom(const entrada_t *e, char *out, size_t n) {
    snprintf(out, n, "%s/%s/%s", DIR_ROMS, SIS_CARPETA[e->sis], arch_de(e));
}

static long tam_archivo(const char *ruta) {
    struct stat st;
    return stat(ruta, &st) == 0 ? (long)st.st_size : -1;
}

static emulador_t emulador_de(const entrada_t *e) {
    const char *a = arch_de(e), *ext = strrchr(a, '.');
    if (e->sis == SIS_GBA || (ext && !strcasecmp(ext, ".gba"))) return EMU_GBA;
    if (e->sis == SIS_PS1) return EMU_PS1;
    if (e->sis == SIS_SNES || (ext && (!strcasecmp(ext, ".sfc") || !strcasecmp(ext, ".smc")))) return EMU_SNES;
    if (e->sis == SIS_NES || (ext && !strcasecmp(ext, ".nes"))) return EMU_NES;
    if (e->sis == SIS_GBC) return EMU_GBC;
    char ruta[600];
    ruta_rom(e, ruta, sizeof ruta);
    if ((ext && !strcasecmp(ext, ".gbc")) || tam_archivo(ruta) > ROM_GB_MAX) return EMU_GBC;
    return EMU_GB;
}

static sistema_t sistema_real(const entrada_t *e) {
    // Para color y nombre bajo la carátula: el sistema de la carpeta.
    return e->sis;
}

// ---------------------------------------------------------------- carátulas

static uint16_t *portada_px = NULL;
static int portada_w = 0, portada_h = 0;
static char portada_ruta[600] = "";
static bool portada_ok = false;

static void ruta_portada(const entrada_t *e, char *out, size_t n) {
    char base[600];
    ruta_rom(e, base, sizeof base);
    char *p = strrchr(base, '.');
    if (p) *p = 0;
    snprintf(out, n, "%s%s", base, EXT_PORTADA);
}

static bool cargar_portada(const char *ruta) {
    if (!strcmp(ruta, portada_ruta)) return portada_ok;
    snprintf(portada_ruta, sizeof portada_ruta, "%s", ruta);
    portada_ok = false;
    FILE *f = fopen(ruta, "rb");
    if (!f) return false;
    uint16_t wh[2];
    if (fread(wh, 2, 2, f) == 2 && wh[0] && wh[1] && wh[0] <= 480 && wh[1] <= 320) {
        free(portada_px);
        portada_px = malloc((size_t)wh[0] * wh[1] * 2);
        if (portada_px && fread(portada_px, 2, (size_t)wh[0] * wh[1], f) == (size_t)wh[0] * wh[1]) {
            portada_w = wh[0];
            portada_h = wh[1];
            portada_ok = true;
        }
    }
    fclose(f);
    return portada_ok;
}

static void olvidar_portada(void) { portada_ruta[0] = 0; portada_ok = false; }

// ---------------------------------------------------------------- dibujo

static void mostrar(void) { pantalla_mostrar(); }
static void forzar_redibujo(void) { pantalla_forzar(); }

static int celda_logo(int n) { return n <= 4 ? 3 : 2; }

// Pestañas desplazables (2026-11-07): ancho fijo cómodo; se ven las que caben y la fila se corre para que la
// activa quede a la vista con una vecina de cada lado. Las flechas se encienden si hay más hacia ese lado.
#define PEST_W 52
#define PEST_SEP 6
static int pest_off = 0; // primera pestaña visible

static void area_pestanas(int *x_min, int *x_max) {
    int c = celda_logo(n_pest);
    *x_min = 12 + 9 * c * 6 + 6 + 22;
    *x_max = ANCHO - 22;
}

static int pestanas_visibles(void) {
    int x_min, x_max;
    area_pestanas(&x_min, &x_max);
    int nv = (x_max - x_min + PEST_SEP) / (PEST_W + PEST_SEP);
    return nv < n_pest ? nv : n_pest;
}

static void ajustar_desplazamiento(void) {
    int nv = pestanas_visibles();
    int margen = nv >= 4 ? 1 : 0;
    if (actual - margen < pest_off) pest_off = actual - margen;
    if (actual + margen >= pest_off + nv) pest_off = actual + margen - nv + 1;
    if (pest_off > n_pest - nv) pest_off = n_pest - nv;
    if (pest_off < 0) pest_off = 0;
}

// Rectángulo de la pestaña i (en pantalla solo si está entre las visibles).
static void rect_pestana(int i, int *x, int *w) {
    int x_min, x_max, nv = pestanas_visibles();
    area_pestanas(&x_min, &x_max);
    int total = nv * PEST_W + (nv - 1) * PEST_SEP;
    int x0 = x_max - total; // pegadas a la derecha, como antes
    *x = x0 + (i - pest_off) * (PEST_W + PEST_SEP);
    *w = PEST_W;
}

static void dibujar_cabecera(void) {
    rgb acento = color_pestana(&PEST[actual]);
    rect(0, 0, ANCHO, 44, C_PANEL);
    rect(0, 44, ANCHO, 2, acento);
    int c = celda_logo(n_pest);
    logo_chico(12, 22 - c * 7 / 2, c);
    ajustar_desplazamiento();
    int nv = pestanas_visibles();
    for (int i = pest_off; i < pest_off + nv && i < n_pest; i++) {
        int x, w;
        rect_pestana(i, &x, &w);
        bool activa = i == actual;
        if (activa) rect_redondo(x, TAB_Y, w, TAB_H, acento, 4);
        else rect_redondo(x, TAB_Y, w, TAB_H, C_PEST_INACTIVA, 4);
        icono(nombre_pestana(&PEST[i]), x + w / 2, TAB_Y + TAB_H / 2, !activa, C_PANEL);
    }
    int x, w;
    rect_pestana(pest_off, &x, &w);
    flecha(x - 10, 22, -1, pest_off > 0 ? C_TEXTO : C_PEST_INACTIVA);
    rect_pestana(pest_off + nv - 1, &x, &w);
    flecha(x + w + 8, 22, 1, pest_off + nv < n_pest ? C_TEXTO : C_PEST_INACTIVA);
}

typedef struct { const char *b, *t; } item_pie;

static void dibujar_pie(const item_pie *it, int n, bool volumen) {
    rect(0, 296, ANCHO, 24, C_PANEL);
    rect(0, 296, ANCHO, 1, C_LINEA_PIE);
    int x = 10;
    for (int i = 0; i < n; i++) {
        if (!strcmp(it[i].b, "TXT")) {
            texto(x, 302, it[i].t, C_TEXTO_PIE, 1, false);
            x += (int)strlen(it[i].t) * 8 + 12;
            continue;
        }
        int w = boton_pie(x, 300, it[i].b);
        if (it[i].t && *it[i].t) {
            texto(x + w + 5, 302, it[i].t, C_TEXTO_PIE, 1, false);
            x += w + 5 + (int)strlen(it[i].t) * 8 + 10;
        } else {
            x += w + 3;
        }
    }
    if (volumen) {
        int vx = 430, y = 296;
        rect(vx, y + 9, 4, 6, C_TEXTO_PIE);
        for (int k = 0; k < 4; k++) rect(vx + 4 + k, y + 8 - k, 1, 8 + 2 * k, C_TEXTO_PIE);
        for (int k = 0; k < 5; k++) {
            int h = 4 + 2 * k;
            rect(vx + 12 + 6 * k, y + 18 - h, 4, h, CFG.volumen > 2 * k ? C_A : C_BARRA_VACIA);
        }
    }
}

static void titulo_corto(const char *arch, char *out, int n) {
    char t[300];
    titulo_completo(arch, t, sizeof t);
    int mc = lista_chars();
    if ((int)strlen(t) > mc) {
        t[mc - 1] = '~';
        t[mc] = 0;
    }
    snprintf(out, n, "%s", t);
}

static rgb marca_de(const pestana *p, const entrada_t *e, bool *hay) {
    *hay = true;
    if (p->tipo != PT_SIS) return SIS_COLOR[e->sis];
    if (cfg_es_favorito(SIS_NOMBRE[e->sis], arch_de(e))) return C_ROSA;
    int v;
    if (cfg_nota(SIS_NOMBRE[e->sis], arch_de(e), &v) && v < 0) return C_TENUE;
    *hay = false;
    return C_FONDO;
}

static void fila_lista(int fila, int idx, bool elegida, const char *txt) {
    pestana *p = &PEST[actual];
    rgb acento = color_pestana(p);
    int fh = lista_fila_h(), esc = lista_esc();
    int y = LISTA_Y + fila * fh;
    if (elegida) rect(8, y - 2, 270, fh - 2, acento);
    else rect(8, y - 2, 270, fh - 2, C_FONDO);
    texto(16, y - 1 + (fh - 2 - 12 * esc) / 2, txt, elegida ? C_FONDO : C_TEXTO, esc, false);
    bool hay;
    rgb m = marca_de(p, &ENT[idx], &hay);
    if (hay) rect(8, y - 2, 4, fh - 2, m);
}

static int primero_visible_de(int sel, int n, int filas) {
    int p = sel - filas / 2;
    if (p > n - filas) p = n - filas;
    return p < 0 ? 0 : p;
}
static int primero_visible(int sel, int n) { return primero_visible_de(sel, n, lista_filas()); }

static const char *pantalla_de(emulador_t em) { return cfg_modo_pantalla(em)->nombre; }

static char portada_pend[600] = "";
static uint32_t t_ultimo_boton = 0;

// Nota de la comunidad (LaunchBox) en la esquina inferior derecha de la carátula: estrella + "4,6".
// Con pocos votos (menos de 5) se ve más apagada: es poco confiable.
static void insignia_nota(const juego *j) {
    if (!j->nota) return;
    static const char *const ESTRELLA[7] = {"...#...", "..###..", "#######", ".#####.", "..###..", ".##.##.", "##...##"};
    char t[8];
    snprintf(t, sizeof t, "%d,%d", j->nota / 10, j->nota % 10);
    int w = 7 + 4 + texto_ancho(t, 1, false) + 8;
    int x = 294 + 174 - w - 3, y = 58 + 154 - 15;
    bool pocos = j->votos < 5;
    rgb col = pocos ? C_TENUE : (rgb){250, 205, 60};
    rect_redondo(x, y, w, 13, C_FONDO, 3);
    for (int r = 0; r < 7; r++)
        for (int c = 0; c < 7; c++)
            if (ESTRELLA[r][c] == '#') rect(x + 4 + c, y + 3 + r, 1, 1, col);
    texto(x + 4 + 7 + 4, y + 3, t, pocos ? C_TENUE : C_TEXTO, 1, false);
}

static void dibujar_caratula(const entrada_t *e, int sel, int n, bool leer_sd) {
    rgb acento = color_pestana(&PEST[actual]);
    sistema_t sr = sistema_real(e);
    rect(292, 56, 178, 158, acento);
    rect(294, 58, 174, 154, C_PANEL);
    char rp[600];
    ruta_portada(e, rp, sizeof rp);
    bool con = false;
    if (!strcmp(rp, portada_ruta)) con = portada_ok;
    else if (leer_sd) con = cargar_portada(rp);
    else snprintf(portada_pend, sizeof portada_pend, "%s", rp);
    if (con) {
        int x = 294 + (174 - portada_w > 0 ? (174 - portada_w) / 2 : 0);
        int y = 58 + (154 - portada_h > 0 ? (154 - portada_h) / 2 : 0);
        imagen(portada_px, portada_w, portada_h, x, y, 294, 58, 174, 154);
    } else {
        icono(SIS_NOMBRE[sr], 294 + 87, 58 + 77 - 8, false, C_PANEL);
        const char *nom = SIS_NOMBRE[sr];
        texto(294 + (174 - (int)strlen(nom) * 8) / 2, 58 + 154 / 2 + 22, nom, SIS_COLOR[sr], 1, true);
    }
    insignia_nota(&SIS[e->sis].todos[e->idx]);
    int y = 222;
    int fin = texto(294, y, SIS_NOMBRE[sr], SIS_COLOR[sr], 2, true);
    (void)fin;
    if (cfg_es_favorito(SIS_NOMBRE[e->sis], arch_de(e)))
        texto(294 + (int)strlen(SIS_NOMBRE[sr]) * 16 + 8, y + 4, "fav", C_ROSA, 1, false);
    char linea[64];
    if (leer_sd) {
        char ruta[600];
        ruta_rom(e, ruta, sizeof ruta);
        snprintf(linea, sizeof linea, "%d de %d  -  %ld KB", sel + 1, n, (tam_archivo(ruta) + 1023) / 1024);
    } else {
        snprintf(linea, sizeof linea, "%d de %d", sel + 1, n);
    }
    texto(294, y + 22, linea, C_TENUE, 1, false);
    emulador_t em = emulador_de(e);
    snprintf(linea, sizeof linea, "Pantalla: %s", pantalla_de(em));
    texto(294, y + 38, linea, C_TEXTO, 1, false);
    if (em == EMU_GB) texto(294, y + 52, PALETAS_GB[((CFG.gb_paleta % 12) + 12) % 12], C_TENUE, 1, false);
}

static void dibujar_vacia(void) {
    pestana *p = &PEST[actual];
    const char *l1 = "", *l2 = "";
    if (p->tipo == PT_RECIENTES) { l1 = "Todavia no jugaste nada"; l2 = "Los ultimos juegos van a aparecer aqui"; }
    else if (p->tipo == PT_FAVORITOS) { l1 = "Sin favoritos"; l2 = "SELECT sobre un juego lo marca como favorito"; }
    else if (p->tipo == PT_VALE) { l1 = "Grupo sin juegos"; l2 = "START, L o R: otro grupo"; }
    else { l1 = "Sin juegos"; l2 = "Revisa la carpeta de ROMs"; }
    icono(nombre_pestana(p), 240, 120, false, C_FONDO);
    texto((ANCHO - (int)strlen(l1) * 16) / 2, 152, l1, C_TEXTO, 2, true);
    texto((ANCHO - (int)strlen(l2) * 8) / 2, 180, l2, C_TENUE, 1, true);
}

// AJUSTES
typedef enum { AJ_VOLUMEN, AJ_PANT_NES, AJ_PANT_GB, AJ_COLORES, AJ_PANT_GBA, AJ_PANT_SNES, AJ_PANT_PS1, AJ_INTRO, AJ_RECIENTES, AJ_FAVORITOS,
               AJ_NO_GUSTARON, AJ_REGISTRO, AJ_ORDEN, AJ_PS1, AJ_LETRA } ajuste_t;
static ajuste_t AJ[15];
static int n_aj = 0, aj_sel = 0, confirmar = -1;

static void armar_ajustes(void) {
    n_aj = 0;
    AJ[n_aj++] = AJ_VOLUMEN;
    if (SIS[SIS_NES].presente) AJ[n_aj++] = AJ_PANT_NES;
    if (SIS[SIS_GB].presente || SIS[SIS_GBC].presente || SIS[SIS_HOMEBREW].presente) AJ[n_aj++] = AJ_PANT_GB;
    if (SIS[SIS_GB].presente) AJ[n_aj++] = AJ_COLORES;
    if (SIS[SIS_GBA].presente) AJ[n_aj++] = AJ_PANT_GBA;
    if (SIS[SIS_SNES].presente) AJ[n_aj++] = AJ_PANT_SNES;
    if (SIS[SIS_PS1].presente) AJ[n_aj++] = AJ_PANT_PS1;
    AJ[n_aj++] = AJ_LETRA;
    AJ[n_aj++] = AJ_ORDEN;
    if (plat_emulador_disponible(EMU_PS1) && SIS[SIS_PS1].n_juegos) AJ[n_aj++] = AJ_PS1;
    AJ[n_aj++] = AJ_INTRO;
    AJ[n_aj++] = AJ_RECIENTES;
    AJ[n_aj++] = AJ_FAVORITOS;
    AJ[n_aj++] = AJ_NO_GUSTARON;
    AJ[n_aj++] = AJ_REGISTRO;
    if (aj_sel >= n_aj) aj_sel = n_aj - 1;
}

static const char *nombre_ajuste(ajuste_t a) {
    static const char *n[] = {"Volumen", "Pantalla NES", "Pantalla GB", "Colores GB", "Pantalla GBA", "Pantalla SNES", "Pantalla PS1", "Intro",
                              "Recientes", "Favoritos", "No gustaron", "Registro", "Orden", "PS1 (beta)", "Letra de la lista"};
    return n[a];
}

static void valor_ajuste(ajuste_t a, char *out, size_t n) {
    switch (a) {
    case AJ_VOLUMEN: snprintf(out, n, "%d", CFG.volumen); break;
    case AJ_PANT_NES: snprintf(out, n, "%s", pantalla_de(EMU_NES)); break;
    case AJ_PANT_SNES: snprintf(out, n, "%s", pantalla_de(EMU_SNES)); break;
    case AJ_PANT_PS1: snprintf(out, n, "%s", pantalla_de(EMU_PS1)); break;
    case AJ_PANT_GB: snprintf(out, n, "%s", PANTALLAS_GB[((CFG.gb_pantalla % 3) + 3) % 3].nombre); break;
    case AJ_COLORES: snprintf(out, n, "%s", PALETAS_GB[((CFG.gb_paleta % 12) + 12) % 12]); break;
    case AJ_PANT_GBA: snprintf(out, n, "%s", PANTALLAS_GBA[((CFG.gba_pantalla % 2) + 2) % 2].nombre); break;
    case AJ_INTRO: snprintf(out, n, "%s", CFG.intro ? "Si" : "No"); break;
    case AJ_ORDEN: snprintf(out, n, "%s", CFG.orden_nota ? "Por nota" : "Por nombre"); break;
    case AJ_PS1: snprintf(out, n, "%s", CFG.ps1_activa ? "Activada" : "Desactivada"); break;
    case AJ_LETRA: snprintf(out, n, "%s", CFG.letra_chica ? "Peque\xf1" "a" : "Mediana"); break;
    case AJ_RECIENTES:
        if (confirmar == AJ_RECIENTES) snprintf(out, n, "A: borrar todo");
        else snprintf(out, n, "Borrar (%d)", CFG.n_recientes);
        break;
    case AJ_FAVORITOS:
        if (confirmar == AJ_FAVORITOS) snprintf(out, n, "A: borrar todo");
        else snprintf(out, n, "Borrar (%d)", CFG.n_favoritos);
        break;
    case AJ_NO_GUSTARON:
        snprintf(out, n, "%s (%d)", CFG.ver_no_gustaron ? "Visibles" : "Ocultos", cfg_cuantas_no_gustaron());
        break;
    case AJ_REGISTRO: snprintf(out, n, "Ver"); break;
    }
}

// Filas del mismo alto que la lista de juegos (el texto a escala 2 mide 24 px): si no entran todas,
// la lista se desplaza con la elegida y aparece una barra a la derecha.
static void dibujar_ajustes(void) {
    const int y0 = 56, der = n_aj > FILAS ? 450 : 458; // borde derecho de los valores
    int prim = primero_visible_de(aj_sel, n_aj, FILAS);
    for (int f = 0; f < FILAS && prim + f < n_aj; f++) {
        int i = prim + f, y = y0 + f * FILA_H;
        bool el = i == aj_sel;
        rgb col = el ? C_TEXTO : C_TENUE;
        if (el) {
            rect_redondo(8, y, der, FILA_H - 2, C_FILA_AJUSTE, 3);
            rect(8, y, 4, FILA_H - 2, C_AJUSTES);
        }
        texto(20, y + 2, nombre_ajuste(AJ[i]), col, 2, false);
        if (AJ[i] == AJ_VOLUMEN) {
            int x0 = der - 136;
            char num[8];
            snprintf(num, sizeof num, "%d", CFG.volumen);
            texto(x0 - 12 - texto_ancho(num, 2, false), y + 2, num, col, 2, false);
            for (int j = 0; j < 10; j++) {
                int hh = 6 + j;
                rect(x0 + j * 14, y + 21 - hh, 10, hh, j < CFG.volumen ? C_A : C_BARRA_VACIA);
            }
            continue;
        }
        char v[48];
        valor_ajuste(AJ[i], v, sizeof v);
        if (confirmar == (int)AJ[i]) col = C_A;
        texto(der - texto_ancho(v, 2, false), y + 2, v, col, 2, false);
    }
    if (n_aj > FILAS) {
        int alto = FILAS * FILA_H - 2, th = alto * FILAS / n_aj;
        rect(464, y0, 4, alto, C_PANEL);
        rect(464, y0 + (alto - th) * prim / (n_aj - FILAS), 4, th, C_AJUSTES);
    }
    item_pie it[3];
    int n = 0;
    it[n++] = (item_pie){"UD", "Elegir"};
    switch (AJ[aj_sel]) {
    case AJ_REGISTRO: it[n++] = (item_pie){"A", "Ver registro"}; break;
    case AJ_RECIENTES:
    case AJ_FAVORITOS: it[n++] = (item_pie){"A", "Borrar"}; it[n++] = (item_pie){"B", "Cancelar"}; break;
    case AJ_VOLUMEN: it[n++] = (item_pie){"A", "Subir"}; it[n++] = (item_pie){"B", "Bajar"}; break;
    case AJ_NO_GUSTARON: it[n++] = (item_pie){"A", CFG.ver_no_gustaron ? "Ocultar" : "Mostrar"}; break;
    default: it[n++] = (item_pie){"A", "Siguiente"}; it[n++] = (item_pie){"B", "Anterior"};
    }
    dibujar_pie(it, n, true);
}

// Marquesina
static int marq_idx = -1, marq_pos = 0;
static uint32_t marq_t = 0;
static bool marq_esperando = true;

static void dibujar(bool leer_sd) {
    marq_idx = -1;
    llenar(C_FONDO);
    if (!hay_consolas() && n_pest <= 3) {
        logo_chico(12, 11, 3);
        texto(30, 120, "No hay juegos en la SD", C_TEXTO, 2, false);
        texto(30, 150, "/mnt/sdcard/roms/gba snes ps1 gbc gb nes homebrew", C_TENUE, 1, false);
        mostrar();
        return;
    }
    dibujar_cabecera();
    pestana *p = &PEST[actual];
    if (p->tipo == PT_AJUSTES) {
        dibujar_ajustes();
        mostrar();
        return;
    }
    armar_entradas();
    if (!n_ent) {
        dibujar_vacia();
        if (p->tipo == PT_VALE) {
            item_pie it[] = {{"START", VALE[vale_g].nombre}};
            dibujar_pie(it, 1, true);
        } else
            dibujar_pie(NULL, 0, true);
        mostrar();
        return;
    }
    rgb acento = color_pestana(p);
    int prim = primero_visible(p->sel, n_ent), nf = lista_filas();
    for (int f = 0; f < nf && prim + f < n_ent; f++) {
        char t[64];
        titulo_corto(arch_de(&ENT[prim + f]), t, sizeof t);
        fila_lista(f, prim + f, prim + f == p->sel, t);
    }
    if (n_ent > nf) {
        rect(282, 56, 4, 208, C_PANEL);
        int th = 208 * nf / n_ent;
        if (th < 12) th = 12;
        int ty = 56 + (208 - th) * prim / (n_ent - nf);
        rect(282, ty, 4, th, acento);
    }
    dibujar_caratula(&ENT[p->sel], p->sel, n_ent, leer_sd);
    const entrada_t *e = &ENT[p->sel];
    bool fav = cfg_es_favorito(SIS_NOMBRE[e->sis], arch_de(e));
    if (p->tipo == PT_SIS) {
        consola_t *c = &SIS[p->sis];
        if (c->vis) {
            char f[80] = "Filtro:";
            if (c->categoria) { strcat(f, " "); strncat(f, c->cats[c->categoria], 20); }
            if (c->busqueda[0]) { strcat(f, " \""); strncat(f, c->busqueda, 24); strcat(f, "\""); }
            char nn[16];
            snprintf(nn, sizeof nn, " (%d)", c->n_vis);
            strcat(f, nn);
            item_pie it[] = {{"TXT", f}, {"START", "Cambiar"}, {"SELECT", fav ? "Quitar" : "Fav"}};
            dibujar_pie(it, 3, true);
        } else {
            item_pie it[] = {{"A", "Jugar"}, {"Y", "Ficha"}, {"START", "Buscar"}, {"B", ""}, {"+", ""}, {"UD", "Letra"},
                             {"SEL", fav ? "Quitar" : "Fav"}}; // "SEL": con "SELECT" no cabe junto al volumen
            dibujar_pie(it, 7, true);
        }
    } else if (p->tipo == PT_VALE) {
        char g[48];
        snprintf(g, sizeof g, "%s (%d/%d)", VALE[vale_g].nombre, vale_g + 1, n_vale);
        item_pie it[] = {{"A", "Jugar"}, {"Y", "Ficha"}, {"START", g}, {"SELECT", fav ? "Quitar" : "Fav"}};
        dibujar_pie(it, 4, true);
    } else {
        item_pie it[] = {{"A", "Jugar"}, {"Y", "Ficha"}, {"SELECT", fav ? "Quitar" : "Favorito"}};
        dibujar_pie(it, 3, true);
    }
    mostrar();
    // Marquesina: si el nombre no entra
    char t[300];
    titulo_completo(arch_de(e), t, sizeof t);
    if ((int)strlen(t) > lista_chars()) {
        marq_idx = p->sel;
        marq_pos = 0;
        marq_t = plat_ms();
        marq_esperando = true;
    }
}

static void avanzar_marquesina(void) {
    if (marq_idx < 0 || PEST[actual].tipo == PT_AJUSTES || marq_idx >= n_ent) return;
    uint32_t t = plat_ms();
    if (marq_esperando) {
        if (t - marq_t < 1000) return;
        marq_esperando = false;
        marq_t = t;
    }
    if (t - marq_t < 160) return;
    marq_t = t;
    char full[300], vuelta[700], vis[64];
    titulo_completo(arch_de(&ENT[marq_idx]), full, sizeof full);
    snprintf(vuelta, sizeof vuelta, "%s     %s", full, full);
    int ciclo = (int)strlen(full) + 5;
    marq_pos++;
    if (marq_pos >= ciclo) {
        marq_pos = 0;
        marq_esperando = true;
    }
    snprintf(vis, sizeof vis, "%.*s", lista_chars(), vuelta + marq_pos);
    int fila = marq_idx - primero_visible(PEST[actual].sel, n_ent);
    fila_lista(fila, marq_idx, true, vis);
    mostrar();
}

// ---------------------------------------------------------------- búsqueda

static const char TECLAS_BUSQ[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ";
#define N_TECLAS 37
#define N_CELDAS (N_TECLAS + 2)

static bool aplicar_filtro(sistema_t s, const char *texto, int categoria, int **res, int *n) {
    consola_t *c = &SIS[s];
    char palabras[8][24];
    int np = 0;
    char buf[32];
    snprintf(buf, sizeof buf, "%s", texto);
    for (char *q = buf; *q; q++) *q = (char)tolower((unsigned char)*q);
    for (char *tok = strtok(buf, " "); tok && np < 8; tok = strtok(NULL, " ")) snprintf(palabras[np++], 24, "%s", tok);
    if (!np && categoria == 0) return false;
    *res = malloc(sizeof(int) * (c->n_juegos + 1));
    *n = 0;
    const char *cat = c->cats[categoria];
    for (int i = 0; i < c->n_juegos; i++) {
        juego *j = &c->todos[c->juegos[i]];
        bool ok = true;
        for (int k = 0; k < np && ok; k++) ok = strstr(j->bajo, palabras[k]) != NULL;
        if (ok && categoria) {
            const char *jc = j->cat >= 0 ? CATEGORIAS[j->cat] : "Otros";
            ok = !strcmp(jc, cat);
        }
        if (ok) (*res)[(*n)++] = c->juegos[i];
    }
    return true;
}

static void dibujar_busqueda(sistema_t s, const char *txt, int cat, int cur, const int *res, int nres, bool filtro) {
    consola_t *c = &SIS[s];
    rgb acento = SIS_COLOR[s];
    llenar(C_FONDO);
    rect(0, 0, ANCHO, 44, C_PANEL);
    char t[64];
    snprintf(t, sizeof t, "BUSCAR EN %s", SIS_NOMBRE[s]);
    texto(12, 12, t, acento, 2, true);
    rect(12, 54, 456, 34, C_PANEL);
    snprintf(t, sizeof t, "%s_", txt);
    texto(20, 60, t, C_TEXTO, 2, true);
    snprintf(t, sizeof t, "%d juegos", filtro ? nres : c->n_juegos);
    texto(330, 66, t, C_TENUE, 1, false);
    rect(12, 94, 268, 26, cur == -1 ? acento : C_PANEL);
    snprintf(t, sizeof t, "< Categoria: %s >", c->cats[cat]);
    texto(18, 100, t, cur == -1 ? C_FONDO : C_TEXTO, 1.5, false);
    for (int i = 0; i < N_CELDAS; i++) {
        int fila = i / 8, col = i % 8, x, y = 126 + fila * 27, w;
        const char *et;
        char uno[2] = {0, 0};
        if (i < N_TECLAS) {
            x = 12 + col * 34;
            w = 30;
            uno[0] = TECLAS_BUSQ[i] == ' ' ? '_' : TECLAS_BUSQ[i];
            et = uno;
        } else {
            fila = (N_TECLAS + 7) / 8;
            y = 126 + fila * 27;
            x = i == N_TECLAS ? 12 : 148;
            w = 4 * 34 - 4;
            et = i == N_TECLAS ? "BORRAR" : "OK";
        }
        bool el = i == cur;
        rect(x, y, w, 23, el ? acento : C_PANEL);
        texto(x + (w - (int)strlen(et) * 12) / 2, y + 4, et, el ? C_FONDO : C_TEXTO, 1.5, true);
    }
    int px = 12 + 8 * 34 + 10;
    texto(px, 100, "Resultados:", C_TENUE, 1, false);
    int mostrados = 0;
    if (filtro) {
        if (!nres) texto(px, 116, "(ninguno)", C_NINGUNO, 1, false);
        for (int k = 0; k < nres && k < 12; k++) {
            char tt[300];
            titulo_completo(c->todos[res[k]].arch, tt, sizeof tt);
            tt[24] = 0;
            texto(px, 116 + k * 14, tt, C_TEXTO, 1, false);
            mostrados++;
        }
    } else {
        for (int k = 0; k < c->n_juegos && k < 12; k++) {
            char tt[300];
            titulo_completo(c->todos[c->juegos[k]].arch, tt, sizeof tt);
            tt[24] = 0;
            texto(px, 116 + k * 14, tt, C_TEXTO, 1, false);
        }
    }
    (void)mostrados;
    item_pie it[] = {{"A", "Escribir"}, {"B", "Borrar"}, {"U", "Categoria"}, {"START", "Listo"}};
    dibujar_pie(it, 4, false);
    mostrar();
}

static void esperar_soltar(void) {
    uint32_t t0 = plat_ms();
    while (plat_botones() && plat_ms() - t0 < 1500) nucleo_dormir(10);
}

static void busqueda(sistema_t s) {
    consola_t *c = &SIS[s];
    char txt[24];
    snprintf(txt, sizeof txt, "%s", c->busqueda);
    int cat = c->categoria, cur = 0;
    efecto("buscar");
    int *res = NULL, nres = 0;
    bool filtro = aplicar_filtro(s, txt, cat, &res, &nres);
    dibujar_busqueda(s, txt, cat, cur, res, nres, filtro);
    uint16_t antes = plat_botones();
    uint32_t rep = 0;
    for (;;) {
        nucleo_dormir(10);
        if (debe_terminar()) { free(res); return; }
        uint16_t b = plat_botones();
        uint16_t nuevo = b & ~antes;
        uint32_t t = plat_ms();
        uint16_t flechas = BT_ARRIBA | BT_ABAJO | BT_IZQ | BT_DER;
        if (nuevo & flechas) rep = t + 300;
        else if ((b & flechas) && rep && (int32_t)(t - rep) >= 0) { nuevo |= b & flechas; rep = t + 90; }
        antes = b;
        if (!nuevo) continue;
        char viejo[24];
        snprintf(viejo, sizeof viejo, "%s", txt);
        int cat_v = cat;
        bool aceptar = false;
        if (nuevo & BT_START) aceptar = true;
        else if (cur == -1 && (nuevo & (BT_DER | BT_A))) cat = (cat + 1) % c->n_cats;
        else if (cur == -1 && (nuevo & BT_IZQ)) cat = (cat - 1 + c->n_cats) % c->n_cats;
        else if (cur == -1 && (nuevo & BT_ABAJO)) cur = 0;
        else if (nuevo & BT_DER) cur = (cur + 1) % N_CELDAS;
        else if (nuevo & BT_IZQ) cur = (cur - 1 + N_CELDAS) % N_CELDAS;
        else if (nuevo & BT_ABAJO) cur = cur + 8 > N_CELDAS - 1 ? N_CELDAS - 1 : cur + 8;
        else if (nuevo & BT_ARRIBA) cur = cur < 8 ? -1 : cur - 8;
        else if (nuevo & BT_A) {
            if (cur < N_TECLAS) {
                size_t l = strlen(txt);
                if (l < 20) { txt[l] = TECLAS_BUSQ[cur]; txt[l + 1] = 0; }
            } else if (cur == N_TECLAS) {
                if (*txt) txt[strlen(txt) - 1] = 0;
            } else aceptar = true;
        } else if (nuevo & BT_B) {
            if (*txt) txt[strlen(txt) - 1] = 0;
        }
        free(res);
        res = NULL;
        filtro = aplicar_filtro(s, txt, cat, &res, &nres);
        if (aceptar) {
            if (filtro && !nres) {
                efecto("error");
                continue;
            }
            // mantener el juego elegido si sigue en la lista
            pestana *p = &PEST[actual];
            armar_entradas();
            int elegido = n_ent ? ENT[p->sel].idx : -1;
            snprintf(c->busqueda, sizeof c->busqueda, "%s", txt);
            c->categoria = cat;
            free(c->vis);
            c->vis = filtro ? res : NULL;
            c->n_vis = filtro ? nres : 0;
            if (!filtro) free(res);
            armar_entradas();
            p->sel = 0;
            for (int i = 0; i < n_ent; i++)
                if (ENT[i].idx == elegido) p->sel = i;
            efecto("volver");
            esperar_soltar();
            return;
        }
        if (strlen(txt) < strlen(viejo)) efecto("borrar");
        else if (strcmp(txt, viejo)) efecto("tecla");
        else if (cat != cat_v) efecto("ajuste");
        else efecto("mover");
        dibujar_busqueda(s, txt, cat, cur, res, nres, filtro);
    }
}

// ---------------------------------------------------------------- registro

static void ver_registro(void) {
    // Últimas 300 líneas, ajustadas a 57 columnas.
    enum { MAXL = 1200 };
    static char lineas[MAXL][60];
    static rgb colores[MAXL];
    static bool cuelgue[MAXL];
    int n = 0, n_cuelgues = 0;
    FILE *f = fopen(RUTA_LOG, "r");
    char *todas[300] = {0};
    int nt = 0;
    if (f) {
        char l[512];
        while (fgets(l, sizeof l, f)) {
            l[strcspn(l, "\n")] = 0;
            if (!*l) continue;
            free(todas[nt % 300]);
            todas[nt % 300] = strdup(l);
            nt++;
        }
        fclose(f);
    }
    int ini = nt > 300 ? nt - 300 : 0;
    rgb col_ant = C_TEXTO;
    bool cuel_ant = false;
    for (int k = ini; k < nt && n < MAXL; k++) {
        char *l = todas[k % 300];
        char limpio[512];
        // quitar el prefijo de las ROMs
        char *pos;
        snprintf(limpio, sizeof limpio, "%s", l);
        char pref[200];
        snprintf(pref, sizeof pref, "%s/", DIR_ROMS);
        size_t lp = strlen(pref);
        while ((pos = strstr(limpio, pref))) memmove(pos, pos + lp, strlen(pos + lp) + 1);
        rgb col;
        bool cu = false;
        if (!strncmp(limpio, "        ", 8) && !isdigit((unsigned char)limpio[8])) { col = col_ant; cu = cuel_ant; }
        else if (strstr(limpio, "CUELGUE")) { col = C_A; cu = true; n_cuelgues++; }
        else if (strstr(limpio, "arranque")) col = strstr(limpio, "watchdog") ? C_B : C_TENUE;
        else if (strstr(limpio, "error:")) col = C_B;
        else if (strstr(limpio, "salio:")) col = C_TENUE;
        else col = C_TEXTO;
        col_ant = col;
        cuel_ant = cu;
        int len = (int)strlen(limpio), p = 0;
        bool primera = true;
        while (p < len && n < MAXL) {
            int ancho = primera ? 57 : 55;
            snprintf(lineas[n], sizeof lineas[0], "%s%.*s", primera ? "" : "  ", ancho, limpio + p);
            colores[n] = col;
            cuelgue[n] = cu;
            n++;
            p += ancho;
            primera = false;
        }
    }
    for (int k = 0; k < 300; k++) free(todas[k]);

    char info[160], plat[100];
    plat_info(plat, sizeof plat);
    int off = snprintf(info, sizeof info, "%s -", plat);
    for (int s = 0; s < N_SIS && off < (int)sizeof info; s++)
        if (SIS[s].presente) off += snprintf(info + off, sizeof info - off, " %s %d", SIS_NOMBRE[s], SIS[s].n_juegos);
    info[58] = 0;

    bool solo = false;
    int nf = (ALTO - 30 - 50) / 12;
    int idx[MAXL], ni = 0, top = 0;
    uint16_t antes = plat_botones();
    uint32_t rep = 0;
    bool redibujar = true, rearmar = true;
    efecto("abrir");
    for (;;) {
        if (rearmar) {
            ni = 0;
            for (int i = 0; i < n; i++)
                if (!solo || cuelgue[i]) idx[ni++] = i;
            top = ni > nf ? ni - nf : 0;
            rearmar = false;
        }
        if (redibujar) {
            llenar(C_FONDO);
            rect(0, 0, ANCHO, 44, C_PANEL);
            texto(12, 6, "REGISTRO", C_ROSA, 2, true);
            char cc[32];
            snprintf(cc, sizeof cc, "%d cuelgue%s", n_cuelgues, n_cuelgues == 1 ? "" : "s");
            texto(468 - (int)strlen(cc) * 8, 6, cc, n_cuelgues ? C_A : C_TENUE, 1, false);
            if (solo) texto(364, 18, "solo cuelgues", C_B, 1, false);
            texto(12, 30, info, C_TENUE, 1, false);
            rect(0, 44, ANCHO, 2, C_ROSA);
            if (!n) texto(12, 50, "(todavia no hay registro)", C_TENUE, 1, false);
            else if (!ni) texto(12, 50, "(no hay cuelgues registrados)", C_TENUE, 1, false);
            for (int k = 0; k < nf && top + k < ni; k++)
                texto(12, 50 + k * 12, lineas[idx[top + k]], colores[idx[top + k]], 1, false);
            if (ni > nf) {
                rect(474, 50, 3, 240, C_PANEL);
                int th = 240 * nf / ni;
                if (th < 10) th = 10;
                int ty = 50 + (240 - th) * top / (ni - nf);
                rect(474, ty, 3, th, C_ROSA);
            }
            item_pie it[] = {{"UD", "Mover"}, {"A", solo ? "Todo" : "Solo cuelgues"}, {"B", "Volver"}};
            dibujar_pie(it, 3, false);
            mostrar();
            redibujar = false;
        }
        nucleo_dormir(10);
        if (debe_terminar()) return;
        uint16_t b = plat_botones();
        uint16_t nuevo = b & ~antes;
        uint32_t t = plat_ms();
        uint16_t flechas = BT_ARRIBA | BT_ABAJO | BT_IZQ | BT_DER;
        if (nuevo & flechas) rep = t + 300;
        else if ((b & flechas) && rep && (int32_t)(t - rep) >= 0) { nuevo |= b & flechas; rep = t + 60; }
        antes = b;
        if (!nuevo) continue;
        int maxtop = ni > nf ? ni - nf : 0, viejo = top;
        if (nuevo & BT_ABAJO) top++;
        if (nuevo & BT_ARRIBA) top--;
        if (nuevo & BT_DER) top += nf;
        if (nuevo & BT_IZQ) top -= nf;
        if (top > maxtop) top = maxtop;
        if (top < 0) top = 0;
        if (top != viejo) redibujar = true;
        if (nuevo & BT_A) { solo = !solo; rearmar = redibujar = true; efecto("ajuste"); }
        if (nuevo & BT_B) { efecto("volver"); esperar_soltar(); return; }
    }
}

// FICHA del juego (Y sobre un juego): roms/<carpeta>/_fichas/<rom>.txt, armada en el PC con
// herramientas/fichas.py. Cabecera "Clave: valor", línea vacía y el resumen. Se lee solo al abrirla.
static void lanzar(const entrada_t *e0);

static int envolver(const char *txt, int ancho, char (*out)[60], rgb *col, rgb c, int n, int max) {
    // Agrega 'txt' partido en líneas de hasta 'ancho' columnas (cortando entre palabras).
    int len = (int)strlen(txt), p = 0;
    while (p < len && n < max) {
        while (txt[p] == ' ') p++;
        int fin = p + ancho;
        if (fin >= len) fin = len;
        else {
            int k = fin;
            while (k > p && txt[k] != ' ') k--;
            if (k > p) fin = k;
        }
        snprintf(out[n], 60, "%.*s", fin - p, txt + p);
        col[n++] = c;
        p = fin;
    }
    return n;
}

static void ver_ficha(const entrada_t *e) {
    enum { MAXL = 400 };
    static char lineas[MAXL][60];
    static rgb colores[MAXL];
    int n = 0;
    char ruta[700], base[300], titulo[64] = "";
    snprintf(base, sizeof base, "%s", arch_de(e));
    char *punto = strrchr(base, '.');
    if (punto) *punto = 0;
    snprintf(ruta, sizeof ruta, "%s/%s/_fichas/%s.txt", DIR_ROMS, SIS_CARPETA[e->sis], base);
    FILE *f = fopen(ruta, "r");
    titulo_completo(arch_de(e), titulo, sizeof titulo);
    if (f) {
        char l[4096], a[4096];
        bool cuerpo = false;
        while (fgets(l, sizeof l, f) && n < MAXL) {
            l[strcspn(l, "\r\n")] = 0;
            a_ascii(l, a, sizeof a);
            if (!cuerpo) {
                if (!*a) { cuerpo = true; if (n < MAXL) { lineas[n][0] = 0; colores[n++] = C_TEXTO; } continue; }
                char *dos = strstr(a, ": ");
                if (!dos) continue;
                *dos = 0;
                const char *clave = a, *valor = dos + 2;
                if (!strcmp(clave, "Titulo")) { snprintf(titulo, sizeof titulo, "%s", valor); continue; }
                char fila[600];
                snprintf(fila, sizeof fila, "%-10s %s", clave, valor);
                bool tenue = !strcmp(clave, "Fuente");
                int ini = n;
                n = envolver(fila, 57, lineas, colores, tenue ? C_TENUE : C_TEXTO, n, MAXL);
                // la clave en color: se marca la primera línea (se dibuja aparte)
                if (!tenue && n > ini) colores[ini] = C_B;
            } else if (!*a) {
                if (n < MAXL) { lineas[n][0] = 0; colores[n++] = C_TEXTO; }
            } else {
                n = envolver(a, 57, lineas, colores, C_TEXTO_PIE, n, MAXL);
            }
        }
        fclose(f);
    }
    if (!n) {
        n = envolver("Este juego no tiene ficha (no se encontro en la base de datos).", 57, lineas, colores, C_TENUE, 0, MAXL);
    }
    int nf = (ALTO - 30 - 66) / 12, top = 0;
    rgb acento = SIS_COLOR[e->sis];
    uint16_t antes = plat_botones();
    uint32_t rep = 0;
    bool redibujar = true;
    efecto("abrir");
    for (;;) {
        if (redibujar) {
            llenar(C_FONDO);
            rect(0, 0, ANCHO, 60, C_PANEL);
            char t1[32], t2[64];
            int lt = (int)strlen(titulo);
            if (lt <= 28) { snprintf(t1, sizeof t1, "%s", titulo); t2[0] = 0; }
            else { // en dos renglones, cortando en un espacio
                int k = 28;
                while (k > 10 && titulo[k] != ' ') k--;
                snprintf(t1, sizeof t1, "%.*s", k, titulo);
                snprintf(t2, sizeof t2, "%s", titulo + k + (titulo[k] == ' '));
                if ((int)strlen(t2) > 56) { t2[55] = '~'; t2[56] = 0; }
            }
            texto(12, 6, t1, C_TEXTO, 2, false);
            if (*t2) texto(12, 30, t2, C_TEXTO, strlen(t2) <= 28 ? 2 : 1, false);
            texto(468 - (int)strlen(SIS_NOMBRE[e->sis]) * 8, 46, SIS_NOMBRE[e->sis], acento, 1, false);
            rect(0, 60, ANCHO, 2, acento);
            for (int k = 0; k < nf && top + k < n; k++) {
                const char *l = lineas[top + k];
                int y = 68 + k * 12;
                if (colores[top + k].r == C_B.r && colores[top + k].g == C_B.g && colores[top + k].b == C_B.b) {
                    char clave[12];
                    snprintf(clave, sizeof clave, "%.10s", l);
                    texto(12, y, clave, C_B, 1, false);
                    texto(12 + 11 * 8, y, l + (strlen(l) > 11 ? 11 : strlen(l)), C_TEXTO, 1, false);
                } else
                    texto(12, y, l, colores[top + k], 1, false);
            }
            if (n > nf) {
                rect(474, 68, 3, 222, C_PANEL);
                int th = 222 * nf / n;
                if (th < 10) th = 10;
                int ty = 68 + (222 - th) * top / (n - nf);
                rect(474, ty, 3, th, acento);
            }
            item_pie it[] = {{"UD", "Mover"}, {"A", "Jugar"}, {"B", "Volver"}};
            dibujar_pie(it, 3, false);
            mostrar();
            redibujar = false;
        }
        nucleo_dormir(10);
        if (debe_terminar()) return;
        uint16_t b = plat_botones();
        uint16_t nuevo = b & ~antes;
        uint32_t t = plat_ms();
        uint16_t flechas = BT_ARRIBA | BT_ABAJO | BT_IZQ | BT_DER;
        if (nuevo & flechas) rep = t + 300;
        else if ((b & flechas) && rep && (int32_t)(t - rep) >= 0) { nuevo |= b & flechas; rep = t + 60; }
        antes = b;
        if (!nuevo) continue;
        int maxtop = n > nf ? n - nf : 0, viejo = top;
        if (nuevo & BT_ABAJO) top++;
        if (nuevo & BT_ARRIBA) top--;
        if (nuevo & BT_DER) top += nf;
        if (nuevo & BT_IZQ) top -= nf;
        if (top > maxtop) top = maxtop;
        if (top < 0) top = 0;
        if (top != viejo) redibujar = true;
        if (nuevo & (BT_B | BT_Y)) { efecto("volver"); esperar_soltar(); return; }
        if (nuevo & BT_A) { esperar_soltar(); lanzar(e); return; }
    }
}

// ---------------------------------------------------------------- intro

static double suave(double x) {
    if (x < 0) x = 0;
    if (x > 1) x = 1;
    return 1 - (1 - x) * (1 - x) * (1 - x);
}

static void intro(void) {
    static const rgb blanco[3] = LOGO_BLANCO, rosado[3] = LOGO_ROSADO;
    static const rgb colores[4] = {{230, 60, 60}, {140, 190, 40}, {70, 150, 240}, {250, 200, 40}};
    static const rgb col_logo[5] = {{235, 60, 70}, {190, 90, 230}, {120, 200, 60}, {250, 200, 40}, {60, 150, 245}};
    const double T_LETRAS[4] = {0.55, 0.75, 0.95, 1.15}, T_DOCK = 1.35, T_ACORDE = 1.60;
    const double T_COLOR[5] = {1.95, 2.07, 2.19, 2.31, 2.43}, T_BRILLO[4] = {2.62, 2.70, 2.78, 2.86}, T_FIN = 3.1;
    const int X_LOGO = 34, Y_LOGO = 104, X_GIRL = 34 + 4 * 48 + 24, X_COLOR = 34 + 400 - 150 + 5, Y_COLOR = 104 + 56 + 18;
    sonido_intro();
    uint32_t t0 = plat_ms();
    uint16_t antes = plat_botones();
    bool saltada = false;
    for (;;) {
        double t = (plat_ms() - t0) / 1000.0;
        if (t >= T_FIN || debe_terminar()) break;
        uint16_t b = plat_botones();
        if ((b & ~antes) || plat_pausa_pedida()) { saltada = true; break; }
        antes = b;
        llenar(C_FONDO);
        if (t < 0.25) {
            int w = (int)(480 * t / 0.25);
            if (w < 2) w = 2;
            rect((ANCHO - w) / 2, 159, w, 3, C_BLANCO_PURO);
        } else if (t < 0.45) {
            int h = (int)(320 * (t - 0.25) / 0.2);
            if (h < 3) h = 3;
            rect(0, (ALTO - h) / 2, ANCHO, h, (rgb){40, 50, 100});
        } else {
            for (int i = 0; i < 45; i++) {
                int x0 = (i * 197) % 480, y = (i * 89 + 13) % 320, v = 1 + i % 3;
                int x = ((int)(x0 - v * t * 60) % 480 + 480) % 480;
                int c = 90 + v * 50;
                rgb col = {(uint8_t)c, (uint8_t)c, (uint8_t)(c + 40 > 255 ? 255 : c + 40)};
                int s = v > 1 ? 2 : 1;
                rect(x, y, s, s, col);
            }
            const char *game = "GAME", *girl = "GIRL", *color = "COLOR";
            for (int i = 0; i < 4; i++) {
                double dt = t - T_LETRAS[i];
                if (dt < 0) continue;
                int y;
                if (dt < 0.2) y = (int)(-70 + (Y_LOGO + 70) * suave(dt / 0.2));
                else if (dt < 0.45) y = Y_LOGO - (int)(14 * (1 - (dt - 0.2) / 0.25) * fabs(sin((dt - 0.2) * 12)));
                else y = Y_LOGO;
                letra_logo(game[i], X_LOGO + i * 48, y, 8, 2, blanco);
            }
            double dt = t - T_DOCK;
            if (dt >= 0) {
                int dx = (int)(480 * (1 - suave(dt / 0.3)));
                for (int i = 0; i < 4; i++) letra_logo(girl[i], X_GIRL + i * 48 + dx, Y_LOGO, 8, 2, rosado);
            }
            dt = t - T_ACORDE;
            if (dt >= 0) {
                int w = (int)((X_COLOR - 18 - X_LOGO) * suave(dt / 0.35));
                for (int i = 0; i < 4; i++) rect(X_LOGO + i * w / 4, Y_COLOR + 14, w / 4 + 1, 7, colores[i]);
            }
            for (int i = 0; i < 5; i++) {
                double d = t - T_COLOR[i];
                if (d < 0) break;
                rgb cl[3] = {mezcla(col_logo[i], C_BLANCO_PURO, 0.45), col_logo[i], mezcla(col_logo[i], C_FONDO, 0.65)};
                letra_logo(color[i], X_COLOR + i * 30, Y_COLOR - (int)(10 * (1 - suave(d / 0.15))), 5, 1, cl);
            }
            const int bx[4] = {X_LOGO + 4, X_GIRL + 190, X_LOGO + 120, X_COLOR + 150};
            const int by[4] = {Y_LOGO - 6, Y_LOGO + 4, Y_COLOR + 30, Y_COLOR - 4};
            for (int i = 0; i < 4; i++) {
                double d = t - T_BRILLO[i];
                if (d < 0 || d >= 0.3) continue;
                int r = (int)(12 * (1 - d / 0.3)) + 2;
                rect(bx[i] - r, by[i] - 1, 2 * r, 3, (rgb){255, 255, 220});
                rect(bx[i] - 1, by[i] - r, 3, 2 * r, (rgb){255, 255, 220});
            }
        }
        mostrar();
    }
    if (saltada) sonido_cortar(); // único caso en que se corta un sonido
    else sonido_esperar();        // el jingle no se corta
}

// ---------------------------------------------------------------- lanzar

static void pantalla_carga(const entrada_t *e) {
    rgb acento = SIS_COLOR[e->sis];
    llenar(C_FONDO);
    rect_redondo(137, 37, 206, 246, acento, 6);
    rect_redondo(140, 40, 200, 240, C_PANEL, 5);
    char rp[600];
    ruta_portada(e, rp, sizeof rp);
    if (cargar_portada(rp)) imagen(portada_px, portada_w, portada_h, 140 + (200 - portada_w) / 2, 52, 140, 40, 200, 240);
    else icono(SIS_NOMBRE[e->sis], 240, 125, false, C_PANEL);
    char t[300];
    titulo_completo(arch_de(e), t, sizeof t);
    for (int k = 0; k < 2 && (int)strlen(t) > k * 22; k++) {
        char l[23];
        snprintf(l, sizeof l, "%.22s", t + k * 22);
        texto(140 + (200 - (int)strlen(l) * 8) / 2, 212 + k * 14, l, C_TEXTO, 1, false);
    }
    texto(140 + (200 - 11 * 8) / 2, 250, "Cargando...", acento, 1, false);
    mostrar();
}

static void caja_error(const char *msg) {
    rect(30, 110, 420, 100, C_ERROR_CAJA);
    texto(44, 120, "No se pudo jugar:", C_TEXTO, 2, false);
    int len = (int)strlen(msg);
    for (int k = 0; k < 3 && k * 60 < len; k++) {
        char l[61];
        snprintf(l, sizeof l, "%.60s", msg + k * 60);
        texto(44, 152 + k * 16, l, C_TEXTO, 1, false);
    }
    mostrar();
}

static void lanzar(const entrada_t *e0) {
    entrada_t e = *e0;
    emulador_t em = emulador_de(&e);
    char ruta[600];
    ruta_rom(&e, ruta, sizeof ruta);
    cfg_agregar_reciente(SIS_NOMBRE[e.sis], arch_de(&e));
    efecto("abrir");
    pantalla_carga(&e);
    olvidar_portada();
    esperar_soltar();
    sonido_esperar();
    registrar("juego: %s (%s) mem libre %ld", ruta, EMU_NOMBRE[em], plat_mem_libre_kb() * 1024);

    char aviso[200] = "";
    plat_jugar(ruta, SIS_NOMBRE[e.sis], em, aviso, sizeof aviso);
    cfg_cargar(); // el emulador pudo cambiar volumen, pantalla, favoritos o notas
    refrescar_notas();
    armar_pestanas();
    armar_ajustes();
    if (*aviso) registrar("error: %s %s", ruta, aviso);
    else registrar("salio: %s", ruta);
    efecto(*aviso ? "error" : "volver");
    if (PEST[actual].tipo == PT_RECIENTES) PEST[actual].sel = 0;
    forzar_redibujo();
    esperar_soltar();
    dibujar(PORTADA_INMEDIATA);
    if (*aviso) {
        caja_error(aviso);
        nucleo_dormir(3000);
        dibujar(PORTADA_INMEDIATA);
    }
}

// ---------------------------------------------------------------- bucle

static char inicial(int i) {
    char c = (char)toupper((unsigned char)arch_de(&ENT[i])[0]);
    return c >= 'A' && c <= 'Z' ? c : '#';
}

// Igual que saltar_letra de menu.py: adelante = primer juego con otra inicial;
// atrás = el primero de la letra anterior.
static int saltar_letra(int sel, int paso) {
    if (n_ent < 2) return sel;
    char act = inicial(sel);
    if (paso > 0) {
        for (int k = 1; k < n_ent; k++) {
            int i = (sel + k) % n_ent;
            if (inicial(i) != act) return i;
        }
        return sel;
    }
    int i = -1;
    for (int k = 1; k < n_ent; k++) {
        int j = (sel - k + n_ent) % n_ent;
        if (inicial(j) != act) { i = j; break; }
    }
    if (i < 0) return sel;
    char l = inicial(i);
    for (int k = 1; k < n_ent; k++) {
        int j = (i - 1 + n_ent) % n_ent;
        if (inicial(j) != l) break;
        i = j;
    }
    return i;
}

static void cambiar_ajuste(int d) {
    ajuste_t a = AJ[aj_sel];
    switch (a) {
    case AJ_VOLUMEN: {
        int v = CFG.volumen + d;
        if (v < 0 || v > 10) { efecto("error"); return; }
        CFG.volumen = v;
        break;
    }
    case AJ_PANT_NES: CFG.nes_pantalla = ((CFG.nes_pantalla + d) % N_PANT_NES + N_PANT_NES) % N_PANT_NES; break;
    case AJ_PANT_SNES: CFG.snes_pantalla = ((CFG.snes_pantalla + d) % N_PANT_SNES + N_PANT_SNES) % N_PANT_SNES; break;
    case AJ_PANT_PS1: cfg_cambiar_pantalla(EMU_PS1, d); break;
    case AJ_PANT_GB: CFG.gb_pantalla = ((CFG.gb_pantalla + d) % 3 + 3) % 3; break;
    case AJ_COLORES: CFG.gb_paleta = ((CFG.gb_paleta + d) % 12 + 12) % 12; break;
    case AJ_PANT_GBA: CFG.gba_pantalla = ((CFG.gba_pantalla + d) % 2 + 2) % 2; break;
    case AJ_INTRO: CFG.intro = !CFG.intro; break;
    case AJ_LETRA: CFG.letra_chica = !CFG.letra_chica; break;
    case AJ_ORDEN:
        CFG.orden_nota = !CFG.orden_nota;
        refrescar_notas();
        for (int i = 0; i < n_pest; i++) PEST[i].sel = 0;
        break;
    case AJ_PS1:
        CFG.ps1_activa = !CFG.ps1_activa;
        cfg_guardar();
        refrescar_notas();
        armar_pestanas();
        armar_ajustes();
        for (int i = 0; i < n_pest; i++)
            if (PEST[i].tipo == PT_AJUSTES) actual = i;
        efecto("ajuste");
        return;
    case AJ_NO_GUSTARON:
        if (d < 0) return;
        CFG.ver_no_gustaron = !CFG.ver_no_gustaron;
        cfg_guardar();
        refrescar_notas();
        armar_pestanas();
        armar_ajustes();
        for (int i = 0; i < n_pest; i++)
            if (PEST[i].tipo == PT_AJUSTES) actual = i;
        efecto("ajuste");
        return;
    case AJ_RECIENTES:
    case AJ_FAVORITOS: {
        bool rec = a == AJ_RECIENTES;
        if (d < 0) { efecto(confirmar >= 0 ? "volver" : "error"); confirmar = -1; return; }
        if ((rec ? CFG.n_recientes : CFG.n_favoritos) == 0) { efecto("error"); return; }
        if (confirmar != (int)a) { confirmar = a; efecto("ajuste"); return; }
        if (rec) CFG.n_recientes = 0;
        else CFG.n_favoritos = 0;
        confirmar = -1;
        cfg_guardar();
        efecto("borrar");
        return;
    }
    case AJ_REGISTRO:
        if (d < 0) { efecto("error"); return; }
        ver_registro();
        forzar_redibujo();
        return;
    }
    cfg_guardar();
    efecto("ajuste");
}


int menu_principal(int argc, char **argv) {
    int op;
    while ((op = getopt(argc, argv, "c:t:")) != -1) {
        if (op == 'c') pantalla_captura = optarg;
        else if (op == 't') t_fin_prueba = plat_ms() + (uint32_t)(atof(optarg) * 1000);
    }
    signal(SIGINT, al_senal);
    signal(SIGTERM, al_senal);

    cfg_cargar();
    registrar("== arranque ==");
    plat_pantalla_abrir(true);
    plat_entrada_abrir();
    forzar_redibujo();
    for (int i = 0; i < 20 && !plat_audio_listo(); i++) { // la salida de audio puede tardar un poco
        plat_botones();
        plat_dormir_ms(5);
    }
    if (plat_botones() & BT_START) return 0;

    for (int s = 0; s < N_SIS; s++) listar(s);
    cargar_vale();
    refrescar_notas();
    armar_pestanas();
    armar_ajustes();
    if (CFG.intro) intro();
    if (terminar) return 0;

    actual = 0;
    armar_entradas();
    if (!n_ent && hay_consolas()) actual = 2;
    dibujar(PORTADA_INMEDIATA);

    uint16_t antes = plat_botones();
    uint32_t repetir_en = 0;
    while (!debe_terminar()) {
        uint16_t b = plat_botones();
        uint16_t nuevo = b & ~antes;
        uint32_t t = plat_ms();
        if (b) t_ultimo_boton = t;
        if (nuevo) repetir_en = t + 350;
        else if ((b & (BT_ARRIBA | BT_ABAJO)) && repetir_en && (int32_t)(t - repetir_en) >= 0) {
            nuevo |= b & (BT_ARRIBA | BT_ABAJO);
            repetir_en = t + 70;
        }
        antes = b;
        if (!hay_consolas()) { nucleo_dormir(10); continue; }

        if (!nuevo) {
            if (*portada_pend && t - t_ultimo_boton >= PORTADA_ESPERA && !sonando()) {
                portada_pend[0] = 0;
                dibujar(true);
            }
            avanzar_marquesina();
            nucleo_dormir(10);
            continue;
        }
        portada_pend[0] = 0;
        pestana *p = &PEST[actual];
        bool redibujar = false;
        if (nuevo & (BT_IZQ | BT_DER)) {
            actual = (actual + ((nuevo & BT_DER) ? 1 : -1) + n_pest) % n_pest;
            confirmar = -1;
            efecto("pestana");
            redibujar = true;
        } else if ((nuevo & (BT_START | BT_L | BT_R)) && p->tipo == PT_VALE && n_vale) {
            vale_g = (vale_g + ((nuevo & BT_L) ? -1 : 1) + n_vale) % n_vale; // START y R: siguiente; L: anterior
            p->sel = 0;
            efecto("pestana");
            redibujar = true;
        } else if ((nuevo & BT_START) && p->tipo == PT_SIS) {
            busqueda(p->sis);
            forzar_redibujo();
            redibujar = true;
        } else if (p->tipo == PT_AJUSTES) {
            if (nuevo & (BT_ARRIBA | BT_ABAJO)) {
                aj_sel = (aj_sel + ((nuevo & BT_ABAJO) ? 1 : -1) + n_aj) % n_aj;
                confirmar = -1;
                efecto("mover");
                redibujar = true;
            } else if (nuevo & BT_A) { cambiar_ajuste(1); redibujar = true; }
            else if (nuevo & BT_B) { cambiar_ajuste(-1); redibujar = true; }
        } else {
            armar_entradas();
            if (n_ent) {
                if ((b & BT_B) && (nuevo & (BT_ARRIBA | BT_ABAJO)) && p->tipo == PT_SIS) {
                    p->sel = saltar_letra(p->sel, (nuevo & BT_ABAJO) ? 1 : -1);
                    efecto("letra");
                    redibujar = true;
                } else if (nuevo & (BT_ARRIBA | BT_ABAJO)) {
                    p->sel = (p->sel + ((nuevo & BT_ABAJO) ? 1 : -1) + n_ent) % n_ent;
                    efecto("mover");
                    redibujar = true;
                } else if (nuevo & BT_A) {
                    lanzar(&ENT[p->sel]);
                    antes = plat_botones();
                    continue;
                } else if (nuevo & BT_Y) {
                    entrada_t e = ENT[p->sel];
                    ver_ficha(&e);
                    forzar_redibujo();
                    antes = plat_botones();
                    redibujar = true;
                } else if (nuevo & BT_SELECT) {
                    bool f = cfg_alternar_favorito(SIS_NOMBRE[ENT[p->sel].sis], arch_de(&ENT[p->sel]));
                    efecto(f ? "ajuste" : "borrar");
                    redibujar = true;
                }
            }
        }
        if (terminar) break; // -t dentro de una sub-pantalla: conservar su captura
        if (redibujar) dibujar(PORTADA_INMEDIATA);
        nucleo_dormir(10);
    }
    plat_entrada_cerrar();
    return 0;
}
