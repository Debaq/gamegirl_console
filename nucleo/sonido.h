// Efectos de interfaz (sd/sistema/sonido): ui.json + ui_vN.raw e intro_vN.raw
// (PCM 16000 Hz, cuadros de 32 bits: 16 bajos izquierda, 16 altos derecha; un archivo por volumen).
// Se convierten a 32768 Hz mono y se entregan a plat_audio_enviar de a poco: sonido_tick() manda lo
// que entra sin pasarse del nivel máximo. Sin hilos (el K210 no tiene), así que los bucles que esperan
// usan nucleo_dormir(), que llama a sonido_tick().
// Reglas del proyecto base: un solo efecto a la vez y nunca cortar uno que suena (el nuevo se salta).
#pragma once
#include <stdbool.h>

void efecto(const char *nombre);
void sonido_intro(void);     // jingle de la intro (~3,2 s)
bool sonando(void);
void sonido_tick(void);      // llamar seguido (cada ~10 ms) mientras haya algo sonando
void sonido_esperar(void);   // hasta que termine lo que suena (máx. 4 s)
void sonido_cortar(void);    // corta la intro si se saltó (único caso en que se corta)

// Dormir alimentando el sonido.
void nucleo_dormir(int ms);
