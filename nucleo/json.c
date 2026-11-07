#include "json.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const char *p; } lector;

static void blancos(lector *l) {
    while (*l->p && isspace((unsigned char)*l->p)) l->p++;
}

static json *nuevo(json_tipo t) {
    json *j = calloc(1, sizeof *j);
    if (j) j->tipo = t;
    return j;
}

static void agregar(json *j, char *clave, json *h) {
    j->hijos = realloc(j->hijos, sizeof *j->hijos * (j->n + 1));
    if (j->tipo == J_OBJETO) j->claves = realloc(j->claves, sizeof *j->claves * (j->n + 1));
    j->hijos[j->n] = h;
    if (j->tipo == J_OBJETO) j->claves[j->n] = clave;
    j->n++;
}

static void utf8(char **o, unsigned cp) {
    char *s = *o;
    if (cp < 0x80) *s++ = (char)cp;
    else if (cp < 0x800) { *s++ = (char)(0xC0 | (cp >> 6)); *s++ = (char)(0x80 | (cp & 0x3F)); }
    else { *s++ = (char)(0xE0 | (cp >> 12)); *s++ = (char)(0x80 | ((cp >> 6) & 0x3F)); *s++ = (char)(0x80 | (cp & 0x3F)); }
    *o = s;
}

static char *leer_cadena(lector *l) {
    if (*l->p != '"') return NULL;
    l->p++;
    // Medir hasta la comilla de cierre (los escapes nunca agrandan el texto: \uXXXX da como mucho 3 bytes).
    size_t cap = 1;
    for (const char *q = l->p; *q && *q != '"'; q++, cap++)
        if (*q == '\\' && q[1]) { q++; cap++; }
    char *out = malloc(cap), *o = out;
    while (*l->p && *l->p != '"') {
        char c = *l->p++;
        if (c != '\\') { *o++ = c; continue; }
        c = *l->p++;
        switch (c) {
        case 'n': *o++ = '\n'; break;
        case 't': *o++ = '\t'; break;
        case 'r': *o++ = '\r'; break;
        case 'b': *o++ = '\b'; break;
        case 'f': *o++ = '\f'; break;
        case 'u': {
            unsigned cp = 0;
            for (int i = 0; i < 4 && isxdigit((unsigned char)*l->p); i++, l->p++)
                cp = cp * 16 + (isdigit((unsigned char)*l->p) ? *l->p - '0' : (tolower(*l->p) - 'a' + 10));
            utf8(&o, cp);
            break;
        }
        default: *o++ = c;
        }
    }
    if (*l->p == '"') l->p++;
    *o = 0;
    return out;
}

static json *valor(lector *l, int prof) {
    if (prof > 32) return NULL;
    blancos(l);
    char c = *l->p;
    if (c == '{' || c == '[') {
        json *j = nuevo(c == '{' ? J_OBJETO : J_LISTA);
        char fin = c == '{' ? '}' : ']';
        l->p++;
        blancos(l);
        if (*l->p == fin) { l->p++; return j; }
        for (;;) {
            char *clave = NULL;
            if (j->tipo == J_OBJETO) {
                blancos(l);
                clave = leer_cadena(l);
                if (!clave) { json_libre(j); return NULL; }
                blancos(l);
                if (*l->p != ':') { free(clave); json_libre(j); return NULL; }
                l->p++;
            }
            json *h = valor(l, prof + 1);
            if (!h) { free(clave); json_libre(j); return NULL; }
            agregar(j, clave, h);
            blancos(l);
            if (*l->p == ',') { l->p++; continue; }
            if (*l->p == fin) { l->p++; return j; }
            json_libre(j);
            return NULL;
        }
    }
    if (c == '"') {
        json *j = nuevo(J_TEXTO);
        j->texto = leer_cadena(l);
        return j;
    }
    if (!strncmp(l->p, "true", 4)) { l->p += 4; json *j = nuevo(J_BOOL); j->num = 1; return j; }
    if (!strncmp(l->p, "false", 5)) { l->p += 5; return nuevo(J_BOOL); }
    if (!strncmp(l->p, "null", 4)) { l->p += 4; return nuevo(J_NULO); }
    char *fin;
    double d = strtod(l->p, &fin);
    if (fin == l->p) return NULL;
    l->p = fin;
    json *j = nuevo(J_NUM);
    j->num = d;
    return j;
}

json *json_leer(const char *texto) {
    lector l = {texto};
    return valor(&l, 0);
}

json *json_leer_archivo(const char *ruta) {
    FILE *f = fopen(ruta, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 8 * 1024 * 1024) { fclose(f); return NULL; }
    char *buf = malloc(n + 1);
    size_t leidos = fread(buf, 1, n, f);
    fclose(f);
    buf[leidos] = 0;
    json *j = json_leer(buf);
    free(buf);
    return j;
}

void json_libre(json *j) {
    if (!j) return;
    for (int i = 0; i < j->n; i++) {
        json_libre(j->hijos[i]);
        if (j->claves) free(j->claves[i]);
    }
    free(j->hijos);
    free(j->claves);
    free(j->texto);
    free(j);
}

json *json_campo(const json *obj, const char *clave) {
    if (!obj || obj->tipo != J_OBJETO) return NULL;
    for (int i = 0; i < obj->n; i++)
        if (!strcmp(obj->claves[i], clave)) return obj->hijos[i];
    return NULL;
}

const char *json_texto(const json *j) { return j && j->tipo == J_TEXTO ? j->texto : NULL; }

void json_escribir_texto(FILE *f, const char *s) {
    fputc('"', f);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p == '"' || *p == '\\') { fputc('\\', f); fputc(*p, f); }
        else if (*p == '\n') fputs("\\n", f);
        else if (*p < 0x20) fprintf(f, "\\u%04x", *p);
        else fputc(*p, f);
    }
    fputc('"', f);
}
