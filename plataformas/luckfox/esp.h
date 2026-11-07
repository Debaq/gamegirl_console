// Enlace con el ESP32-S3 ayudante por UART3 (/dev/ttyS3, 1,5 Mbaud): audio hacia el ESP32,
// botones y nivel del búfer de audio desde el ESP32. Protocolo en esp32_ayudante/esp32_ayudante.ino.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define ESP_TASA 32768      // muestras por segundo que toca el ESP32
#define ESP_OBJETIVO 1638   // nivel buscado del búfer: ~50 ms

bool esp_abrir(void);
void esp_cerrar(void);
void esp_leer(void);              // procesa lo recibido (no bloquea)
bool esp_conectado(void);         // llegó un estado hace menos de 200 ms
uint16_t esp_botones(void);
int esp_nivel(void);              // muestras en el búfer del ESP32
void esp_audio(const int16_t *m, int n); // mono, ESP_TASA
void esp_silencio(void);          // vacía el búfer del ESP32
void esp_enviar(uint8_t t, const uint8_t *d, uint16_t n); // paquete cualquiera (hasta 2048 bytes)
bool esp_respuesta(uint8_t *t, uint8_t *d, uint16_t *n);  // última respuesta 0x82/0x90 (d: 64 bytes)
