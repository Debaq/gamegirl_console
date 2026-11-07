#include "cfg.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h> // fsync (POSIX; la plataforma K210 dará el suyo)

#include "comun.h"
#include "json.h"
#include "plataforma.h"

config CFG;

const char *ruta_sd(const char *sub) {
    // Pocas rutas distintas: un búfer por cada 'sub' (son literales, se comparan por contenido).
    static struct { const char *sub; char ruta[160]; } cache[8];
    for (int i = 0; i < 8; i++) {
        if (cache[i].sub && !strcmp(cache[i].sub, sub)) return cache[i].ruta;
        if (!cache[i].sub) {
            snprintf(cache[i].ruta, sizeof cache[i].ruta, "%s%s", plat_raiz_sd(), sub);
            cache[i].sub = sub;
            return cache[i].ruta;
        }
    }
    return plat_raiz_sd();
}

static void por_defecto(void) {
    free(CFG.favoritos);
    free(CFG.notas);
    memset(&CFG, 0, sizeof CFG);
    CFG.volumen = 2;
    CFG.nes_pantalla = 2;
    CFG.gb_pantalla = 1;
    CFG.gb_paleta = 5;
    CFG.gba_pantalla = 1;
    CFG.snes_pantalla = 2;
    CFG.intro = true;
}

static int entero(const json *j, int def) {
    return j && (j->tipo == J_NUM || j->tipo == J_BOOL) ? (int)j->num : def;
}

static bool leer_ref(const json *j, ref_juego *r) {
    if (!j || j->tipo != J_LISTA || j->n != 2) return false;
    const char *s = json_texto(j->hijos[0]), *a = json_texto(j->hijos[1]);
    if (!s || !a) return false;
    snprintf(r->sis, sizeof r->sis, "%s", s);
    snprintf(r->arch, sizeof r->arch, "%s", a);
    return true;
}

void cfg_cargar(void) {
    por_defecto();
    json *j = json_leer_archivo(RUTA_CFG);
    if (!j) return;
    CFG.volumen = entero(json_campo(j, "volumen"), CFG.volumen);
    CFG.balance = entero(json_campo(j, "balance"), CFG.balance);
    CFG.nes_pantalla = entero(json_campo(j, "nes_pantalla"), CFG.nes_pantalla);
    CFG.gb_pantalla = entero(json_campo(j, "gb_pantalla"), CFG.gb_pantalla);
    CFG.gb_paleta = entero(json_campo(j, "gb_paleta"), CFG.gb_paleta);
    CFG.gba_pantalla = entero(json_campo(j, "gba_pantalla"), CFG.gba_pantalla);
    CFG.snes_pantalla = entero(json_campo(j, "snes_pantalla"), CFG.snes_pantalla);
    CFG.ps1_pantalla = entero(json_campo(j, "ps1_pantalla"), CFG.ps1_pantalla);
    CFG.intro = entero(json_campo(j, "intro"), CFG.intro) != 0;
    CFG.ver_no_gustaron = entero(json_campo(j, "ver_no_gustaron"), 0) != 0;
    CFG.orden_nota = entero(json_campo(j, "orden_nota"), 0) != 0;
    CFG.ps1_activa = entero(json_campo(j, "ps1_activa"), 0) != 0; // por defecto desactivada
    CFG.letra_chica = entero(json_campo(j, "letra_chica"), 0) != 0;
    json *r = json_campo(j, "recientes");
    if (r && r->tipo == J_LISTA)
        for (int i = 0; i < r->n && CFG.n_recientes < MAX_RECIENTES; i++)
            if (leer_ref(r->hijos[i], &CFG.recientes[CFG.n_recientes])) CFG.n_recientes++;
    json *f = json_campo(j, "favoritos");
    if (f && f->tipo == J_LISTA && f->n > 0) {
        CFG.favoritos = calloc(f->n, sizeof *CFG.favoritos);
        for (int i = 0; i < f->n; i++)
            if (leer_ref(f->hijos[i], &CFG.favoritos[CFG.n_favoritos])) CFG.n_favoritos++;
    }
    json *n = json_campo(j, "notas");
    if (n && n->tipo == J_OBJETO && n->n > 0) {
        CFG.notas = calloc(n->n, sizeof *CFG.notas);
        for (int i = 0; i < n->n; i++) {
            snprintf(CFG.notas[CFG.n_notas].clave, sizeof CFG.notas[0].clave, "%s", n->claves[i]);
            CFG.notas[CFG.n_notas].valor = entero(n->hijos[i], 0);
            CFG.n_notas++;
        }
    }
    json_libre(j);
}

static void escribir_ref(FILE *o, const ref_juego *r) {
    fputc('[', o);
    json_escribir_texto(o, r->sis);
    fputs(", ", o);
    json_escribir_texto(o, r->arch);
    fputc(']', o);
}

void cfg_guardar(void) {
    char tmp[200];
    snprintf(tmp, sizeof tmp, "%s.tmp", RUTA_CFG);
    FILE *o = fopen(tmp, "w");
    if (!o) return;
    fprintf(o, "{\"volumen\": %d, \"balance\": %d, \"nes_pantalla\": %d, \"gb_pantalla\": %d, \"gb_paleta\": %d, "
               "\"gba_pantalla\": %d, \"snes_pantalla\": %d, \"ps1_pantalla\": %d, \"intro\": %s, \"ver_no_gustaron\": %s, \"orden_nota\": %s, \"ps1_activa\": %s, \"letra_chica\": %s,\n \"recientes\": [",
            CFG.volumen, CFG.balance, CFG.nes_pantalla, CFG.gb_pantalla, CFG.gb_paleta, CFG.gba_pantalla, CFG.snes_pantalla, CFG.ps1_pantalla,
            CFG.intro ? "true" : "false", CFG.ver_no_gustaron ? "true" : "false", CFG.orden_nota ? "true" : "false", CFG.ps1_activa ? "true" : "false", CFG.letra_chica ? "true" : "false");
    for (int i = 0; i < CFG.n_recientes; i++) {
        if (i) fputs(", ", o);
        escribir_ref(o, &CFG.recientes[i]);
    }
    fputs("],\n \"favoritos\": [", o);
    for (int i = 0; i < CFG.n_favoritos; i++) {
        if (i) fputs(", ", o);
        escribir_ref(o, &CFG.favoritos[i]);
    }
    fputs("],\n \"notas\": {", o);
    for (int i = 0; i < CFG.n_notas; i++) {
        if (i) fputs(", ", o);
        json_escribir_texto(o, CFG.notas[i].clave);
        fprintf(o, ": %d", CFG.notas[i].valor);
    }
    fputs("}}\n", o);
    fflush(o);
    fsync(fileno(o));
    fclose(o);
    rename(tmp, RUTA_CFG);
}

bool cfg_es_favorito(const char *sis, const char *arch) {
    for (int i = 0; i < CFG.n_favoritos; i++)
        if (!strcmp(CFG.favoritos[i].sis, sis) && !strcmp(CFG.favoritos[i].arch, arch)) return true;
    return false;
}

bool cfg_alternar_favorito(const char *sis, const char *arch) {
    for (int i = 0; i < CFG.n_favoritos; i++)
        if (!strcmp(CFG.favoritos[i].sis, sis) && !strcmp(CFG.favoritos[i].arch, arch)) {
            memmove(&CFG.favoritos[i], &CFG.favoritos[i + 1], sizeof *CFG.favoritos * (CFG.n_favoritos - i - 1));
            CFG.n_favoritos--;
            cfg_guardar();
            return false;
        }
    CFG.favoritos = realloc(CFG.favoritos, sizeof *CFG.favoritos * (CFG.n_favoritos + 1));
    ref_juego *r = &CFG.favoritos[CFG.n_favoritos++];
    snprintf(r->sis, sizeof r->sis, "%s", sis);
    snprintf(r->arch, sizeof r->arch, "%s", arch);
    cfg_guardar();
    return true;
}

void cfg_agregar_reciente(const char *sis, const char *arch) {
    ref_juego nuevo;
    snprintf(nuevo.sis, sizeof nuevo.sis, "%s", sis);
    snprintf(nuevo.arch, sizeof nuevo.arch, "%s", arch);
    int o = 0;
    ref_juego lista[MAX_RECIENTES];
    lista[o++] = nuevo;
    for (int i = 0; i < CFG.n_recientes && o < MAX_RECIENTES; i++)
        if (strcmp(CFG.recientes[i].sis, sis) || strcmp(CFG.recientes[i].arch, arch)) lista[o++] = CFG.recientes[i];
    memcpy(CFG.recientes, lista, sizeof lista[0] * o);
    CFG.n_recientes = o;
    cfg_guardar();
}

static void clave_nota(char *out, size_t n, const char *sis, const char *arch) { snprintf(out, n, "%s/%s", sis, arch); }

bool cfg_nota(const char *sis, const char *arch, int *valor) {
    char k[280];
    clave_nota(k, sizeof k, sis, arch);
    for (int i = 0; i < CFG.n_notas; i++)
        if (!strcmp(CFG.notas[i].clave, k)) {
            if (valor) *valor = CFG.notas[i].valor;
            return true;
        }
    return false;
}

void cfg_poner_nota(const char *sis, const char *arch, int valor) {
    char k[280];
    clave_nota(k, sizeof k, sis, arch);
    for (int i = 0; i < CFG.n_notas; i++)
        if (!strcmp(CFG.notas[i].clave, k)) {
            CFG.notas[i].valor = valor;
            cfg_guardar();
            return;
        }
    CFG.notas = realloc(CFG.notas, sizeof *CFG.notas * (CFG.n_notas + 1));
    snprintf(CFG.notas[CFG.n_notas].clave, sizeof CFG.notas[0].clave, "%s", k);
    CFG.notas[CFG.n_notas].valor = valor;
    CFG.n_notas++;
    cfg_guardar();
}

int cfg_cuantas_no_gustaron(void) {
    int n = 0;
    for (int i = 0; i < CFG.n_notas; i++)
        if (CFG.notas[i].valor < 0) n++;
    return n;
}

void registrar(const char *fmt, ...) {
    mkdir(DIR_LOGS, 0755);
    struct stat st;
    if (stat(RUTA_LOG, &st) == 0 && st.st_size > 256 * 1024) {
        remove(RUTA_LOG_VIEJO);
        rename(RUTA_LOG, RUTA_LOG_VIEJO);
    }
    va_list ap;
    FILE *f = fopen(RUTA_LOG, "a");
    va_start(ap, fmt);
    if (f) {
        fprintf(f, "%7.1f ", plat_ms() / 1000.0);
        vfprintf(f, fmt, ap);
        fputc('\n', f);
        fclose(f);
    } else {
        vfprintf(stderr, fmt, ap);
        fputc('\n', stderr);
    }
    va_end(ap);
}

// ---------------------------------------------------------------- modos de pantalla

static int envolver(int v, int n) { return ((v % n) + n) % n; }

const modo_pantalla *cfg_modo_pantalla(emulador_t e) {
    switch (e) {
    case EMU_NES: return &PANTALLAS_NES[envolver(CFG.nes_pantalla, N_PANT_NES)];
    case EMU_SNES: return &PANTALLAS_SNES[envolver(CFG.snes_pantalla, N_PANT_SNES)];
    case EMU_GBA: return &PANTALLAS_GBA[envolver(CFG.gba_pantalla, 2)];
    case EMU_PS1: return &PANTALLAS_PS1[envolver(CFG.ps1_pantalla, N_PANT_PS1)];
    default: return &PANTALLAS_GB[envolver(CFG.gb_pantalla, 3)];
    }
}

void cfg_cambiar_pantalla(emulador_t e, int d) {
    switch (e) {
    case EMU_NES: CFG.nes_pantalla = envolver(CFG.nes_pantalla + d, N_PANT_NES); break;
    case EMU_SNES: CFG.snes_pantalla = envolver(CFG.snes_pantalla + d, N_PANT_SNES); break;
    case EMU_GBA: CFG.gba_pantalla = envolver(CFG.gba_pantalla + d, 2); break;
    case EMU_PS1: CFG.ps1_pantalla = envolver(CFG.ps1_pantalla + d, N_PANT_PS1); break;
    default: CFG.gb_pantalla = envolver(CFG.gb_pantalla + d, 3); break;
    }
}

const char *cfg_nombre_paleta(void) { return PALETAS_GB[envolver(CFG.gb_paleta, 12)]; }
