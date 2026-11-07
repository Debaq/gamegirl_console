// JSON mínimo (lo justo para consola.json, _categorias.json y _ocultos.json).
#pragma once
#include <stdbool.h>
#include <stdio.h>

typedef enum { J_NULO, J_BOOL, J_NUM, J_TEXTO, J_LISTA, J_OBJETO } json_tipo;

typedef struct json {
    json_tipo tipo;
    double num;          // J_NUM y J_BOOL
    char *texto;         // J_TEXTO
    int n;               // elementos (lista u objeto)
    struct json **hijos; // lista/objeto
    char **claves;       // objeto
} json;

json *json_leer(const char *texto);
json *json_leer_archivo(const char *ruta);
void json_libre(json *j);
json *json_campo(const json *obj, const char *clave);
const char *json_texto(const json *j); // NULL si no es texto

// Escritura
void json_escribir_texto(FILE *f, const char *s);
