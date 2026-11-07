#include "dib.h"

#include <math.h>
#include <string.h>

#include "fuente.h"

uint16_t lienzo[ANCHO * ALTO];
static uint16_t *dst = lienzo;
static int dst_w = ANCHO, dst_h = ALTO;

void dib_destino(uint16_t *buf, int w, int h) {
    if (!buf) { buf = lienzo; w = ANCHO; h = ALTO; }
    dst = buf;
    dst_w = w;
    dst_h = h;
}

uint16_t c565(rgb c) { return (uint16_t)(((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3)); }

rgb mezcla(rgb a, rgb b, double k) {
    return (rgb){(uint8_t)(a.r + (b.r - a.r) * k), (uint8_t)(a.g + (b.g - a.g) * k), (uint8_t)(a.b + (b.b - a.b) * k)};
}

static void rect565(int x, int y, int w, int h, uint16_t c) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > dst_w) w = dst_w - x;
    if (y + h > dst_h) h = dst_h - y;
    if (w <= 0 || h <= 0) return;
    for (int j = 0; j < h; j++) {
        uint16_t *p = dst + (long)(y + j) * dst_w + x;
        for (int i = 0; i < w; i++) p[i] = c;
    }
}

void rect(int x, int y, int w, int h, rgb c) { rect565(x, y, w, h, c565(c)); }

void llenar(rgb c) { rect(0, 0, dst_w, dst_h, c); }

void rect_redondo(int x, int y, int w, int h, rgb c, int r) {
    rect(x + r, y, w - 2 * r, h, c);
    rect(x, y + r, w, h - 2 * r, c);
    if (r > 1) rect(x + 1, y + 1, w - 2, h - 2, c);
}

void rect_hueco(int x, int y, int w, int h, rgb c, int g) {
    rect(x, y, w, g, c);
    rect(x, y + h - g, w, g, c);
    rect(x, y, g, h, c);
    rect(x + w - g, y, g, h, c);
}

static inline void pixel(int x, int y, uint16_t c) {
    if ((unsigned)x < (unsigned)dst_w && (unsigned)y < (unsigned)dst_h) dst[(long)y * dst_w + x] = c;
}

static int rnd(double v) { return (int)lround(v); }

// --- fuente: mismo algoritmo que imlib_draw_ascii_string de MaixPy

static int col_primera(const uint8_t *g) {
    for (int x = 0; x < 8; x++)
        for (int y = 0; y < 12; y++)
            if (g[y] & (1 << (7 - x))) return x;
    return -1;
}

static int col_ultima(const uint8_t *g) {
    for (int x = 7; x >= 0; x--)
        for (int y = 11; y >= 0; y--)
            if (g[y] & (1 << (7 - x))) return x;
    return -1;
}

static void glifo(int x0, int y0, const uint8_t *g, uint16_t c, double esc) {
    int hh = rnd(12 * esc), ww = rnd(8 * esc);
    for (int y = 0; y < hh; y++) {
        int fila = rnd(y / esc);
        if (fila >= 12) continue; // MaixPy leería la fila 0 del glifo siguiente (casi siempre vacía)
        uint8_t bits = g[fila];
        if (!bits) continue;
        for (int x = 0; x < ww; x++) {
            int col = rnd(x / esc);
            if (col <= 7 && (bits & (1 << (7 - col)))) pixel(x0 + x, y0 + y, c);
        }
    }
}

// Letras del español que la fuente de MaixPy no trae: se arman al vuelo con la letra base y la tilde encima.
// Se escriben como bytes Latin-1 (a_ascii los produce): á é í ó ú ü ñ Ñ ¿ ¡ (Á É Í Ó Ú se ven sin tilde:
// la mayúscula usa casi todo el alto del glifo).
static const struct { unsigned char cod; char base; uint8_t f0, f1, f2; bool borrar_punto, invertir; } EXTRA_DEF[] = {
    {0xE1, 'a', 0x00, 0x08, 0x10, false, false}, {0xE9, 'e', 0x00, 0x08, 0x10, false, false},
    {0xED, 'i', 0x08, 0x10, 0x00, true, false},  {0xF3, 'o', 0x00, 0x08, 0x10, false, false},
    {0xFA, 'u', 0x00, 0x08, 0x10, false, false}, {0xFC, 'u', 0x00, 0x44, 0x00, false, false},
    {0xF1, 'n', 0x00, 0x34, 0x58, false, false}, {0xD1, 'N', 0x34, 0x00, 0x00, false, false},
    {0xC1, 'A', 0, 0, 0, false, false}, {0xC9, 'E', 0, 0, 0, false, false}, {0xCD, 'I', 0, 0, 0, false, false},
    {0xD3, 'O', 0, 0, 0, false, false}, {0xDA, 'U', 0, 0, 0, false, false}, {0xDC, 'U', 0, 0, 0, false, false},
    {0xBF, '?', 0, 0, 0, false, true},   {0xA1, '!', 0, 0, 0, false, true},
};
#define N_EXTRA (int)(sizeof EXTRA_DEF / sizeof EXTRA_DEF[0])
static uint8_t EXTRA[N_EXTRA][12];

static const uint8_t *glifo_extra(unsigned char ch) {
    static bool hecho = false;
    if (!hecho) {
        for (int i = 0; i < N_EXTRA; i++) {
            const uint8_t *b = FUENTE_8X12[EXTRA_DEF[i].base - ' '];
            if (EXTRA_DEF[i].invertir) // ¿ ¡: el ? y el ! dados vuelta (filas 1-9)
                for (int r = 0; r < 12; r++) EXTRA[i][r] = (r >= 1 && r <= 9) ? b[10 - r] : 0;
            else {
                memcpy(EXTRA[i], b, 12);
                if (EXTRA_DEF[i].borrar_punto) EXTRA[i][1] = 0;
                EXTRA[i][0] |= EXTRA_DEF[i].f0;
                EXTRA[i][1] |= EXTRA_DEF[i].f1;
                EXTRA[i][2] |= EXTRA_DEF[i].f2;
            }
        }
        hecho = true;
    }
    for (int i = 0; i < N_EXTRA; i++)
        if (EXTRA_DEF[i].cod == ch) return EXTRA[i];
    return NULL;
}

static int avance(const char *s, double esc, bool mono, int x, int y, uint16_t c, bool dibujar) {
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        unsigned char ch = *p;
        const uint8_t *ge = ch >= 0x80 ? glifo_extra(ch) : NULL;
        if (!ge && (ch < ' ' || ch > '~')) {
            if (dibujar) {
                int m = rnd(esc * 3) / 2;
                int hh = rnd(12 * esc) - 2 * m, ww = rnd(8 * esc) - 2 * m;
                // MaixPy pasa alto y ancho cruzados: rectángulo hueco de (alto x ancho), grosor = escala
                int g = rnd(esc);
                for (int k = 0; k < g; k++) {
                    for (int i = 0; i < hh; i++) { pixel(x + m + i, y + m + k, c); pixel(x + m + i, y + m + ww - 1 - k, c); }
                    for (int i = 0; i < ww; i++) { pixel(x + m + k, y + m + i, c); pixel(x + m + hh - 1 - k, y + m + i, c); }
                }
            }
            continue; // MaixPy no avanza la x en caracteres desconocidos
        }
        const uint8_t *g = ge ? ge : FUENTE_8X12[ch - ' '];
        if (!mono) {
            int c0 = col_primera(g);
            if (c0 >= 0) x -= rnd(c0 * esc);
        }
        if (dibujar) glifo(x, y, g, c, esc);
        if (mono) {
            x += rnd(8 * esc);
        } else {
            int c1 = col_ultima(g);
            x += c1 >= 0 ? rnd((c1 + 2) * esc) : rnd(esc * 3);
        }
    }
    return x;
}

int texto(int x, int y, const char *s, rgb c, double esc, bool mono) {
    return avance(s, esc, mono, x, y, c565(c), true);
}

int texto_ancho(const char *s, double esc, bool mono) { return avance(s, esc, mono, 0, 0, 0, false); }

// --- íconos (menu.py ICONOS), cada celda = 2x2 px

typedef struct { char k; rgb c; } entrada_paleta;
typedef struct {
    const char *nombre;
    entrada_paleta pal[8];
    int filas;
    const char *f[16];
} icono_t;

static const icono_t ICONOS[] = {
    {"NES", {{'g', {205, 205, 212}}, {'d', {45, 45, 55}}, {'r', {225, 45, 55}}, {'k', {110, 110, 125}}}, 8,
     {".gggggggggggggg.", "gggggggggggggggg", "ggdggggggggggggg", "gdddgggggggrrgrr", "ggdggggggggrrgrr",
      "gggggkkgkkgggggg", "gggggggggggggggg", ".gggggggggggggg."}},
    {"GAME BOY",
     {{'g', {205, 205, 212}}, {'d', {70, 72, 88}}, {'s', {150, 180, 60}}, {'k', {45, 45, 55}}, {'r', {175, 35, 95}}},
     14,
     {"gggggggggg", "gddddddddg", "gdssssssdg", "gdssssssdg", "gdssssssdg", "gddddddddg", "gggggggggg",
      "ggkgggggrg", "gkkkggrggg", "ggkggggggg", "gggggggggg", "gggkgkgggg", "gggggggggg", ".gggggggg."}},
    {"GB COLOR",
     {{'p', {160, 95, 215}}, {'d', {55, 40, 80}}, {'m', {255, 120, 190}}, {'y', {250, 210, 60}}, {'c', {80, 190, 240}},
      {'k', {45, 35, 60}}, {'w', {240, 230, 250}}},
     14,
     {"pppppppppp", "pddddddddp", "pdmmmmmmdp", "pdyyyyyydp", "pdccccccdp", "pddddddddp", "pppppppppp",
      "ppkppppppp", "pkkkppwppp", "ppkppppwpp", "pppppppppp", "pppkpkpppp", "pppppppppp", ".pppppppp."}},
    {"JUEGOS", {{'r', {235, 60, 80}}, {'k', {60, 60, 75}}, {'g', {205, 205, 212}}, {'y', {250, 200, 40}}}, 12,
     {"....rrrr....", "...rrrrrr...", "...rrrrrr...", "....rrrr....", ".....kk.....", ".....kk.....",
      ".....kk.....", ".gggggggggg.", "gggggggggggg", "ggyygggggggg", "gggggggggggg", ".gggggggggg."}},
    {"GBA",
     {{'p', {105, 90, 220}}, {'k', {35, 35, 50}}, {'s', {125, 175, 95}}, {'d', {40, 40, 60}}, {'y', {240, 80, 120}}},
     7,
     {"..pppppppppppp..", ".ppkkkkkkkkkkpp.", "pdpksssssssskppy", "dddksssssssskpyp", "pdpksssssssskppy",
      ".ppkkkkkkkkkkpp.", "..pppppppppppp.."}},
    {"SNES",
     {{'g', {205, 205, 212}}, {'d', {70, 70, 85}}, {'k', {110, 110, 125}}, {'b', {70, 120, 230}}, {'n', {60, 175, 90}},
      {'r', {225, 50, 60}}, {'y', {245, 200, 40}}},
     8,
     {"..gggggggggggg..", ".gggggggggggggg.", "ggdgggggggggbggg", "gdddgkkgkkgnggrg", "ggdggggggggggygg",
      ".gggggggggggggg.", "..gggg....gggg..", "...gg......gg..."}},
    // PS1: un CD plateado con reflejos de colores (un control quedaba igual al de SNES).
    {"PS1", {{'g', {200, 205, 215}}, {'k', {110, 115, 130}}, {'c', {110, 205, 240}}, {'p', {240, 140, 205}}}, 12,
     {"....gggg....", "..ggggggcc..", ".gggggggccc.", ".ggggggggpp.", "gggggkkggppg", "ggggk..kgppg",
      "gpggk..kgggg", "gppggkkggggg", ".ppgggggggg.", ".cccggggggg.", "..ccgggggg..", "....gggg...."}},
    {"HOMEBREW", {{'w', {245, 245, 250}}, {'g', {120, 255, 150}}}, 12,
     {"...wwwwww...", "....w..w....", "....w..w....", "....w..w....", "...w....w...", "..w..w...w..",
      ".w........w.", "wggggwgggggw", "wgggggggwggw", "wggwgggggggw", ".wggggggggw.", "..wwwwwwww.."}},
    {"RECIENTES", {{'k', {45, 45, 55}}, {'w', {245, 245, 250}}}, 12,
     {"...kkkkkk...", ".kkwwwwwwkk.", ".kwwwwkwwwk.", "kwwwwwkwwwwk", "kwwwwwkwwwwk", "kwwwwwkwwwwk",
      "kwwwwwkkkwwk", "kwwwwwwwwwwk", "kwwwwwwwwwwk", ".kwwwwwwwwk.", ".kkwwwwwwkk.", "...kkkkkk..."}},
    {"FAVORITOS", {{'y', {255, 236, 110}}, {'o', {235, 170, 40}}}, 12,
     {"......y......", ".....yyy.....", ".....yyy.....", "....yyyyy....", "yyyyyyyyyyyyy", ".yyyyyyyyyyy.",
      "..yyyyyyyyy..", "...yyyyyyy...", "...yyyoyyy...", "..yyyo.oyyy..", "..yyo...oyy..", ".yy.......yy."}},
    {"VALE LA PENA", {{'y', {250, 205, 60}}, {'o', {200, 140, 30}}, {'w', {255, 245, 200}}}, 12,
     {"..yyyyyyyy..", "yyyyyyyyyyyy", "y.ywyyyyyy.y", "y.ywyyyyyy.y", ".yyyyyyyyyy.", "..yyyyyyyy..",
      "...yyyyyy...", ".....yy.....", ".....yy.....", "....oooo....", "...oooooo...", "...oooooo..."}},
    {"AJUSTES", {{'g', {225, 228, 240}}, {'k', {45, 45, 55}}}, 12,
     {"....gggg....", ".gg.gggg.gg.", ".gggggggggg.", "..gggggggg..", "gggggkkggggg", "ggggkkkkgggg",
      "ggggkkkkgggg", "gggggkkggggg", "..gggggggg..", ".gggggggggg.", ".gg.gggg.gg.", "....gggg...."}},
};

static const icono_t *buscar_icono(const char *nombre) {
    for (unsigned i = 0; i < sizeof ICONOS / sizeof ICONOS[0]; i++)
        if (!strcmp(ICONOS[i].nombre, nombre)) return &ICONOS[i];
    return NULL;
}

int icono_alto(const char *nombre) {
    const icono_t *ic = buscar_icono(nombre);
    return ic ? ic->filas * 2 : 0;
}

void icono(const char *nombre, int cx, int cy, bool apagado, rgb fondo) {
    const icono_t *ic = buscar_icono(nombre);
    if (!ic) return;
    int ancho = (int)strlen(ic->f[0]);
    int x0 = cx - ancho * 2 / 2, y0 = cy - ic->filas * 2 / 2;
    for (int r = 0; r < ic->filas; r++)
        for (int c = 0; c < ancho; c++) {
            char k = ic->f[r][c];
            if (k == '.') continue;
            for (int i = 0; i < 8 && ic->pal[i].k; i++)
                if (ic->pal[i].k == k) {
                    rgb col = apagado ? mezcla(ic->pal[i].c, fondo, 0.55) : ic->pal[i].c;
                    rect(x0 + c * 2, y0 + r * 2, 2, 2, col);
                    break;
                }
        }
}

// --- logo (menu.py FUENTE 5x7)

static const struct { char ch; const char *f[7]; } LOGO[] = {
    {'G', {".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".###."}},
    {'A', {".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'M', {"#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"}},
    {'E', {"#####", "#....", "#....", "####.", "#....", "#....", "#####"}},
    {'I', {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "#####"}},
    {'R', {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"}},
    {'L', {"#....", "#....", "#....", "#....", "#....", "#....", "#####"}},
    {'C', {".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."}},
    {'O', {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'P', {"####.", "#...#", "#...#", "####.", "#....", "#....", "#...."}},
    {'U', {"#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'S', {".####", "#....", "#....", ".###.", "....#", "....#", "####."}},
};

void letra_logo(char ch, int x, int y, int celda, int incl, const rgb col[3]) {
    const char *const *f = NULL;
    for (unsigned i = 0; i < sizeof LOGO / sizeof LOGO[0]; i++)
        if (LOGO[i].ch == ch) f = LOGO[i].f;
    if (!f) return;
    for (int pasada = 0; pasada < 2; pasada++)
        for (int r = 0; r < 7; r++) {
            int c = 0;
            while (c < 5) {
                if (f[r][c] != '#') { c++; continue; }
                int c0 = c;
                while (c < 5 && f[r][c] == '#') c++;
                int n = c - c0;
                int xx = x + (6 - r) * incl + c0 * celda, yy = y + r * celda;
                if (pasada == 0) {
                    rect(xx + celda / 2, yy + celda / 2, n * celda, celda, col[2]);
                } else {
                    int m = celda / 2;
                    rect(xx, yy, n * celda, m, mezcla(col[0], col[1], r / 7.0));
                    rect(xx, yy + m, n * celda, celda - m, mezcla(col[0], col[1], (r + 0.5) / 7.0));
                }
            }
        }
}

void logo_chico(int x, int y, int celda) {
    static const rgb blanco[3] = LOGO_BLANCO, rosado[3] = LOGO_ROSADO;
    int paso = celda * 6;
    const char *game = "GAME", *girl = "GIRL";
    for (int i = 0; i < 4; i++) letra_logo(game[i], x + i * paso, y, celda, 1, blanco);
    int x2 = x + 4 * paso + celda * 3;
    for (int i = 0; i < 4; i++) letra_logo(girl[i], x2 + i * paso, y, celda, 1, rosado);
}

void flecha(int x, int y, int dx, rgb c) {
    for (int k = 0; k < 4; k++) rect(x - dx * k, y - k, 1, 2 * k + 1, c);
}

int boton_pie(int x, int y, const char *b) {
    static const int anchos[16] = {6, 10, 12, 14, 14, 16, 16, 16, 16, 16, 16, 14, 14, 12, 10, 6};
    if (!strcmp(b, "A") || !strcmp(b, "B")) {
        rgb c = b[0] == 'A' ? C_A : C_B;
        for (int f = 0; f < 16; f++) rect(x + (16 - anchos[f]) / 2, y + f, anchos[f], 1, c);
        texto(x + 4, y + 2, b, C_FONDO, 1, true);
        return 16;
    }
    if (!strcmp(b, "UD") || !strcmp(b, "U")) {
        bool ud = b[1] == 'D';
        for (int k = 0; k < 4; k++) {
            rect(x + 4 - k, y + 1 + k, 2 * k + 1, 1, C_TEXTO_PIE);
            if (ud) rect(x + 4 - k, y + 14 - k, 2 * k + 1, 1, C_TEXTO_PIE);
        }
        rect(x + 3, y + 5, 3, ud ? 6 : 9, C_TEXTO_PIE);
        return 9;
    }
    if (!strcmp(b, "+")) {
        texto(x, y + 2, "+", C_TENUE, 1, false);
        return 8;
    }
    int w = (int)strlen(b) * 8 + 10;
    rect_redondo(x, y + 1, w, 14, C_PILDORA, 3);
    texto(x + 5, y + 2, b, C_TEXTO, 1, true);
    return w;
}

void imagen(const uint16_t *px, int w, int h, int x, int y, int cx, int cy, int cw, int ch) {
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if (yy < cy || yy >= cy + ch || yy < 0 || yy >= dst_h) continue;
        for (int i = 0; i < w; i++) {
            int xx = x + i;
            if (xx < cx || xx >= cx + cw || xx < 0 || xx >= dst_w) continue;
            dst[(long)yy * dst_w + xx] = px[(long)j * w + i];
        }
    }
}

// UTF-8 latino común a ASCII (la fuente solo tiene ASCII).
static char sin_acento(unsigned cp) {
    static const struct { unsigned cp; char a; } T[] = {
        {0xE1, 'a'}, {0xE0, 'a'}, {0xE2, 'a'}, {0xE4, 'a'}, {0xE3, 'a'}, {0xE5, 'a'}, {0xC1, 'A'}, {0xC0, 'A'},
        {0xC2, 'A'}, {0xC4, 'A'}, {0xC3, 'A'}, {0xE9, 'e'}, {0xE8, 'e'}, {0xEA, 'e'}, {0xEB, 'e'}, {0xC9, 'E'},
        {0xC8, 'E'}, {0xCA, 'E'}, {0xED, 'i'}, {0xEC, 'i'}, {0xEE, 'i'}, {0xEF, 'i'}, {0xCD, 'I'}, {0xF3, 'o'},
        {0xF2, 'o'}, {0xF4, 'o'}, {0xF6, 'o'}, {0xF5, 'o'}, {0xD3, 'O'}, {0xD6, 'O'}, {0xFA, 'u'}, {0xF9, 'u'},
        {0xFB, 'u'}, {0xFC, 'u'}, {0xDA, 'U'}, {0xDC, 'U'}, {0xF1, 'n'}, {0xD1, 'N'}, {0xE7, 'c'}, {0xC7, 'C'},
        {0xDF, 's'},
    };
    for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++)
        if (T[i].cp == cp) return T[i].a;
    return 0;
}

// Texto UTF-8 -> ASCII de la fuente: tildes sin acento, comillas y rayas tipográficas a su forma simple.
void a_ascii(const char *s, char *out, int n) {
    int o = 0;
    for (int i = 0; s[i] && o < n - 1;) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { out[o++] = (char)c; i++; continue; }
        int k = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
        unsigned cp = 0;
        if (k == 2 && s[i + 1]) cp = ((c & 0x1F) << 6) | (s[i + 1] & 0x3F);
        else if (k == 3 && s[i + 1] && s[i + 2]) cp = ((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F);
        char a = sin_acento(cp);
        if (cp >= 0x80 && cp <= 0xFF && glifo_extra((unsigned char)cp)) out[o++] = (char)cp; // á é í ó ú ü ñ ¿ ¡
        else if (a) out[o++] = a;
        else if (cp == 0x2018 || cp == 0x2019 || cp == 0xB4) out[o++] = '\'';
        else if (cp == 0x201C || cp == 0x201D || cp == 0xAB || cp == 0xBB) out[o++] = '"';
        else if (cp == 0x2013 || cp == 0x2014) out[o++] = '-';
        else if (cp == 0x2026 && o < n - 3) { out[o++] = '.'; out[o++] = '.'; out[o++] = '.'; }
        else if (cp == 0xA0) out[o++] = ' ';
        else out[o++] = '?';
        for (int j = 0; j < k && s[i]; j++) i++;
    }
    out[o] = 0;
}

void titulo_completo(const char *archivo, char *out, int n) {
    const char *punto = strrchr(archivo, '.');
    int len = punto ? (int)(punto - archivo) : (int)strlen(archivo);
    int o = 0;
    for (int i = 0; i < len && o < n - 1;) {
        unsigned char c = (unsigned char)archivo[i];
        if (c < 0x80) {
            out[o++] = c == '_' ? ' ' : (char)c;
            i++;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < len) {
            unsigned cp = ((c & 0x1F) << 6) | (archivo[i + 1] & 0x3F);
            char a = sin_acento(cp);
            out[o++] = a ? a : '?';
            i += 2;
        } else {
            int k = (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
            out[o++] = '?';
            i += k;
        }
    }
    out[o] = 0;
}
