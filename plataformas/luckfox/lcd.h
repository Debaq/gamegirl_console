// ST7796S por SPI0 en la Luckfox Pico Mini (480x320 horizontal, RGB565).
#pragma once
#include <stdint.h>

// reiniciar=1: reset por hardware y secuencia de arranque (al encender).
// reiniciar=0: solo abre SPI/GPIO (la pantalla ya quedó configurada por otro programa).
void lcd_abrir(int reiniciar);
void lcd_cerrar(void);

// Envía un rectángulo de píxeles RGB565 nativos (little-endian); 'paso' en píxeles.
void lcd_enviar(const uint16_t *px, int x, int y, int w, int h, int paso);
