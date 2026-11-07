// Teclado de desarrollo (los botones reales llegan del ESP32, ver esp.h): terminal (adb shell -t)
// o FIFO /tmp/consola_teclas (adb shell 'printf j > /tmp/consola_teclas').
//
// Teclado (igual que tools/control.py del proyecto base):
//   flechas o WASD = cruz, J o Z = A, K o X = B, N o Espacio = SELECT, M o Enter = START,
//   U = L, I = R, O = X, L = Y, Esc o P = pausa, RePág/AvPág = B+ARRIBA / B+ABAJO (saltar de letra).
#pragma once
#include <stdbool.h>
#include <stdint.h>

void teclado_abrir(void);
void teclado_cerrar(void);
uint16_t teclado_leer(void);  // botones "mantenidos" por el teclado ahora mismo (máscara BT_*)
bool teclado_pausa(void);     // se pidió la pausa con el teclado desde la última llamada
