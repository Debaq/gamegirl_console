// Dibujo sobre un búfer RGB565 (por defecto el lienzo de 480x320), imitando a MaixPy image.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "comun.h"

extern uint16_t lienzo[ANCHO * ALTO];

// Cambia el destino del dibujo (por ejemplo el panel de pausa). NULL vuelve al lienzo.
void dib_destino(uint16_t *buf, int w, int h);

uint16_t c565(rgb c);
rgb mezcla(rgb a, rgb b, double k);

void rect(int x, int y, int w, int h, rgb c);
void rect_redondo(int x, int y, int w, int h, rgb c, int r);
void rect_hueco(int x, int y, int w, int h, rgb c, int grosor);
void llenar(rgb c);

// Texto con la fuente 8x12 de MaixPy. Devuelve la x final.
int texto(int x, int y, const char *s, rgb c, double escala, bool mono);
int texto_ancho(const char *s, double escala, bool mono);

// Ícono de pestaña/sistema centrado en (cx, cy). apagado: colores mezclados 55% hacia 'fondo'.
void icono(const char *nombre, int cx, int cy, bool apagado, rgb fondo);
int icono_alto(const char *nombre);

// Letra del logo (5x7) en cursiva con degradado y sombra. col = {arriba, abajo, sombra}.
void letra_logo(char ch, int x, int y, int celda, int incl, const rgb col[3]);
void logo_chico(int x, int y, int celda);

void flecha(int x, int y, int dx, rgb c);

// Botón del pie: "A", "B", "UD", "U", "+", o pastilla con texto ("START", "SELECT"). Devuelve el ancho.
int boton_pie(int x, int y, const char *b);

// Copia una imagen RGB565 nativa recortándola al rectángulo (cx, cy, cw, ch).
void imagen(const uint16_t *px, int w, int h, int x, int y, int cx, int cy, int cw, int ch);

// Título de una ROM: sin extensión, "_" a espacio y sin acentos (UTF-8 a ASCII).
void titulo_completo(const char *archivo, char *out, int n);
void a_ascii(const char *s, char *out, int n); // UTF-8 -> texto de la fuente (ASCII + á é í ó ú ü ñ ¿ ¡ en Latin-1)
