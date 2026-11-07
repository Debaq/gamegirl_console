#include "pausa.h"

#include <stdio.h>
#include <string.h>

#include "cfg.h"
#include "dib.h"
#include "pantalla.h"
#include "plataforma.h"
#include "sonido.h"

#define PW 300
#define PH 304
#define PX 90
#define PY 8
static uint16_t panel[PW * PH];

static const char *archivo(const pausa_ctx *c) {
    const char *b = strrchr(c->ruta_rom, '/');
    return b ? b + 1 : c->ruta_rom;
}

static void nombre_juego(const pausa_ctx *c, char *out, size_t n) {
    char t[300];
    titulo_completo(archivo(c), t, sizeof t);
    snprintf(out, n, "%s", t);
}

static void marco_panel(const pausa_ctx *c) {
    dib_destino(panel, PW, PH);
    llenar(c->acento);
    rect(3, 3, PW - 6, PH - 6, C_FONDO);
}

static void enviar_panel(const pausa_ctx *c) {
    dib_destino(NULL, 0, 0);
    plat_pantalla_enviar(panel, PX, PY, PW, PH, PW);
    if (pantalla_captura) { // la captura muestra el panel sobre el último cuadro
        static uint16_t todo[ANCHO * ALTO];
        memset(todo, 0, sizeof todo);
        if (c->fondo)
            for (int y = 0; y < c->fondo_h; y++)
                memcpy(&todo[(c->fondo_y + y) * ANCHO + c->fondo_x], &c->fondo[y * c->fondo_w], c->fondo_w * 2);
        for (int y = 0; y < PH; y++) memcpy(&todo[(PY + y) * ANCHO + PX], &panel[y * PW], PW * 2);
        pantalla_guardar_ppm(todo, ANCHO, ALTO, pantalla_captura);
    }
}

static void esperar_soltar(void) {
    uint32_t t0 = plat_ms();
    while (plat_botones() && plat_ms() - t0 < 1500) nucleo_dormir(10);
}

static bool terminar(const pausa_ctx *c) { return c->terminar && *c->terminar; }

typedef enum { OP_SEGUIR, OP_GUARDAR, OP_CARGAR, OP_VOLUMEN, OP_PANTALLA, OP_COLORES, OP_FAVORITO, OP_REINICIAR,
               OP_SALIR } opcion;

static const char *aviso_estado = "";

static void dibujar(const pausa_ctx *c, const opcion *ops, int n, int sel) {
    static const rgb blanco[3] = LOGO_BLANCO;
    marco_panel(c);
    const char *pausa = "PAUSA";
    for (int i = 0; i < 5; i++) letra_logo(pausa[i], 16 + i * 18, 12, 3, 1, blanco);
    char nom[300];
    nombre_juego(c, nom, sizeof nom);
    nom[34] = 0;
    texto(16, 40, nom, C_TENUE, 1, false);
    int paso = (PH - 24 - 58) / n;
    if (paso > 28) paso = 28;
    static const char *nombres[] = {"Continuar", "Guardar", "Cargar", "Volumen", "Pantalla", "Colores",
                                    "Favorito", "Reiniciar", "Salir al menu"};
    for (int i = 0; i < n; i++) {
        int y = 58 + i * paso;
        bool el = i == sel;
        if (el) rect_redondo(8, y, 284, paso - 2, c->acento, 3);
        texto(18, y + 5, nombres[ops[i]], el ? C_FONDO : C_TEXTO, 2, false);
        char v[40] = "";
        switch (ops[i]) {
        case OP_GUARDAR: snprintf(v, sizeof v, "%s", aviso_estado); break;
        case OP_CARGAR: snprintf(v, sizeof v, "%s", c->hay_estado && c->hay_estado() ? "Hay" : "Vacio"); break;
        case OP_PANTALLA: snprintf(v, sizeof v, "%s", cfg_modo_pantalla(c->emu)->nombre); break;
        case OP_COLORES: snprintf(v, sizeof v, "%s", cfg_nombre_paleta()); break;
        case OP_FAVORITO: snprintf(v, sizeof v, "%s", cfg_es_favorito(c->sistema, archivo(c)) ? "Si" : "No"); break;
        case OP_VOLUMEN:
            for (int j = 0; j < 10; j++) {
                int h = 4 + j;
                rgb col = j < CFG.volumen ? (el ? C_FONDO : C_A) : (el ? mezcla(c->acento, C_FONDO, 0.6) : C_BARRA_VACIA);
                rect(PW - 18 - (10 - j) * 9, y + 20 - h, 6, h, col);
            }
            break;
        default: break;
        }
        if (*v) texto(PW - 18 - (int)strlen(v) * 8, y + 9, v, el ? C_FONDO : C_TEXTO, 1, true);
    }
    texto(16, PH - 20, "A elegir  -  IZQ/DER  -  B seguir", C_TENUE, 1, false);
    enviar_panel(c);
}

pausa_res pausa_menu(const pausa_ctx *c, int *cambios) {
    *cambios = 0;
    plat_audio_vaciar();
    efecto("pausa");
    esperar_soltar();
    opcion ops[10];
    int n = 0;
    ops[n++] = OP_SEGUIR;
    if (c->guardar_estado && c->cargar_estado) { ops[n++] = OP_GUARDAR; ops[n++] = OP_CARGAR; }
    ops[n++] = OP_VOLUMEN;
    ops[n++] = OP_PANTALLA;
    if (c->emu == EMU_GB) ops[n++] = OP_COLORES;
    ops[n++] = OP_FAVORITO;
    ops[n++] = OP_REINICIAR;
    ops[n++] = OP_SALIR;
    int sel = 0;
    aviso_estado = "";
    dibujar(c, ops, n, sel);
    uint16_t antes = plat_botones();
    for (;;) {
        if (terminar(c)) return PAUSA_SALIR;
        nucleo_dormir(10);
        uint16_t b = plat_botones();
        uint16_t nuevo = b & ~antes;
        antes = b;
        if (plat_pausa_pedida()) nuevo |= BT_B;
        if (!nuevo) continue;
        int d = 0;
        if (nuevo & BT_ARRIBA) { sel = (sel - 1 + n) % n; efecto("mover"); }
        else if (nuevo & BT_ABAJO) { sel = (sel + 1) % n; efecto("mover"); }
        else if (nuevo & BT_IZQ) d = -1;
        else if (nuevo & BT_DER) d = 1;
        else if (nuevo & (BT_B | BT_START)) { efecto("seguir"); break; }
        else if (nuevo & BT_A) {
            switch (ops[sel]) {
            case OP_SEGUIR: efecto("seguir"); goto seguir;
            case OP_REINICIAR: efecto("abrir"); esperar_soltar(); sonido_esperar(); return PAUSA_REINICIAR;
            case OP_SALIR: efecto("volver"); esperar_soltar(); return PAUSA_SALIR;
            case OP_FAVORITO: efecto(cfg_alternar_favorito(c->sistema, archivo(c)) ? "ajuste" : "borrar"); break;
            case OP_GUARDAR: {
                bool ok = c->guardar_estado();
                aviso_estado = ok ? "Guardado" : "Error";
                efecto(ok ? "ajuste" : "error");
                break;
            }
            case OP_CARGAR:
                if (c->cargar_estado()) { efecto("seguir"); goto seguir; }
                efecto("error");
                break;
            default: d = 1;
            }
        }
        if (d) {
            switch (ops[sel]) {
            case OP_VOLUMEN: {
                int v = CFG.volumen + d;
                if (v < 0 || v > 10) { efecto("error"); break; }
                CFG.volumen = v;
                cfg_guardar();
                efecto("ajuste");
                break;
            }
            case OP_PANTALLA:
                cfg_cambiar_pantalla(c->emu, d);
                cfg_guardar();
                *cambios |= PAUSA_CAMBIO_PANTALLA;
                efecto("ajuste");
                break;
            case OP_COLORES:
                CFG.gb_paleta = ((CFG.gb_paleta + d) % 12 + 12) % 12;
                cfg_guardar();
                *cambios |= PAUSA_CAMBIO_PALETA;
                efecto("ajuste");
                break;
            default: break;
            }
        }
        dibujar(c, ops, n, sel);
    }
seguir:
    esperar_soltar();
    sonido_esperar(); // que el efecto no se mezcle con el audio del juego
    return PAUSA_SEGUIR;
}

void pausa_preguntar_gusto(const pausa_ctx *c) {
    const char *arch = archivo(c);
    if (cfg_nota(c->sistema, arch, NULL)) return;
    marco_panel(c);
    texto(18, 18, "Te gusto?", C_TEXTO, 3, false);
    char nom[300];
    nombre_juego(c, nom, sizeof nom);
    char l[35];
    snprintf(l, sizeof l, "%.34s", nom);
    texto(18, 58, l, C_TENUE, 1, false);
    if (strlen(nom) > 34) {
        snprintf(l, sizeof l, "%.34s", nom + 34);
        texto(18, 72, l, C_TENUE, 1, false);
    }
    static const char *b[3] = {"A", "B", "START"}, *t[3] = {"Si, me gusto", "No, ocultarlo", "No se"};
    for (int i = 0; i < 3; i++) {
        int y = 100 + i * 40;
        rect_redondo(12, y, 276, 32, C_PANEL, 4);
        int w = boton_pie(22, y + 8, b[i]);
        texto(22 + w + 12, y + 8, t[i], C_TEXTO, 2, false);
    }
    texto(18, PH - 22, "Los ocultos se ven en AJUSTES", C_TENUE, 1, false);
    enviar_panel(c);
    uint16_t antes = plat_botones();
    for (;;) {
        if (terminar(c)) return;
        nucleo_dormir(10);
        uint16_t bb = plat_botones(), nuevo = bb & ~antes;
        antes = bb;
        int nota;
        if (nuevo & BT_A) { nota = 1; efecto("ajuste"); }
        else if (nuevo & BT_B) { nota = -1; efecto("borrar"); }
        else if (nuevo & BT_START) { nota = 0; efecto("volver"); }
        else continue;
        cfg_poner_nota(c->sistema, arch, nota);
        registrar("nota: %s/%s %s", c->sistema, arch, nota > 0 ? "gusto" : nota < 0 ? "no gusto" : "sin opinion");
        esperar_soltar();
        sonido_esperar();
        return;
    }
}

uint16_t pausa_gesto(uint16_t b, bool *pedir) {
    static int armado = 0;
    *pedir = false;
    if ((b & BT_SELECT) && (b & BT_START) && !(b & (BT_A | BT_B))) {
        if (++armado >= 15) { armado = 0; *pedir = true; }
        return b & (uint16_t)~(BT_SELECT | BT_START); // mientras se arma, el juego no los ve
    }
    armado = 0;
    return b;
}
