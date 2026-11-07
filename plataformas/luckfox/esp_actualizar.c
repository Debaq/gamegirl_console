// Actualiza el firmware del ESP32 ayudante desde la Luckfox, por la misma UART del audio y los botones.
// Protocolo OTA en esp32_ayudante/esp32_ayudante.ino (paquetes 0x10-0x12, respuestas 0x90).
// Uso en la placa (con el menú detenido): esp_actualizar firmware.bin
//        esp_actualizar -v   (solo muestra la versión del ESP32)
// Desde el PC: make esp (compila, sube, detiene el menú, actualiza y lo relanza).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "esp.h"
#include "plataforma.h"

uint32_t plat_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec * 1000u + t.tv_nsec / 1000000u);
}

static uint32_t crc32(uint32_t c, const uint8_t *d, size_t n) {
    c = ~c;
    while (n--) {
        c ^= *d++;
        for (int i = 0; i < 8; i++) c = (c >> 1) ^ (0xEDB88320u & -(c & 1));
    }
    return ~c;
}

static void poner32(uint8_t *d, uint32_t v) {
    d[0] = (uint8_t)v;
    d[1] = (uint8_t)(v >> 8);
    d[2] = (uint8_t)(v >> 16);
    d[3] = (uint8_t)(v >> 24);
}

// Espera una respuesta del tipo pedido; devuelve su largo o -1 si no llegó a tiempo.
static int esperar(uint8_t tipo, uint8_t *d, int ms) {
    uint32_t t0 = plat_ms();
    while (plat_ms() - t0 < (uint32_t)ms) {
        esp_leer();
        uint8_t t;
        uint16_t n;
        if (esp_respuesta(&t, d, &n) && t == tipo) return n;
        usleep(500);
    }
    return -1;
}

static bool esperar_conectado(int ms) {
    uint32_t t0 = plat_ms();
    while (plat_ms() - t0 < (uint32_t)ms) {
        esp_leer();
        if (esp_conectado()) return true;
        usleep(2000);
    }
    return false;
}

static bool version(char *txt, int tam) {
    uint8_t d[64];
    for (int intento = 0; intento < 5; intento++) {
        esp_leer();
        esp_enviar(0x03, NULL, 0);
        int n = esperar(0x82, d, 300);
        if (n >= 0) {
            if (n >= tam) n = tam - 1;
            memcpy(txt, d, (size_t)n);
            txt[n] = 0;
            return true;
        }
    }
    return false;
}

static const char *CODIGOS[] = {"bien", "sin espacio", "posición fuera de orden", "error de escritura",
                                "crc distinto", "imagen inválida", "sin OTA iniciada"};

static const char *codigo_txt(uint8_t c) { return c < 7 ? CODIGOS[c] : "desconocido"; }

// Manda un paquete OTA y espera la respuesta (con reintentos). Devuelve el código, o -1 si no hubo respuesta.
static int pedir(uint8_t tipo, const uint8_t *d, uint16_t n, int ms, uint32_t *siguiente) {
    for (int intento = 0; intento < 6; intento++) {
        uint8_t r[64];
        esp_enviar(tipo, d, n);
        int k = esperar(0x90, r, ms);
        if (k >= 5) {
            *siguiente = r[1] | r[2] << 8 | r[3] << 16 | (uint32_t)r[4] << 24;
            return r[0];
        }
        fprintf(stderr, "  sin respuesta, reintento %d\n", intento + 1);
    }
    return -1;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 2) {
        fprintf(stderr, "Uso: %s firmware.bin | -v\n", argv[0]);
        return 1;
    }
    if (!esp_abrir()) { perror("no se pudo abrir la UART del ESP32"); return 1; }
    if (!esperar_conectado(1000)) { fprintf(stderr, "El ESP32 no responde (¿está el menú andando?)\n"); return 1; }
    char ver[64] = "?";
    if (!version(ver, sizeof ver)) snprintf(ver, sizeof ver, "sin respuesta (firmware viejo sin OTA: cargar por USB)");
    printf("ESP32 actual: %s\n", ver);
    if (strcmp(argv[1], "-v") == 0) return 0;

    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END);
    long tam = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *img = malloc((size_t)tam);
    if (!img || fread(img, 1, (size_t)tam, f) != (size_t)tam) { fprintf(stderr, "no se pudo leer %s\n", argv[1]); return 1; }
    fclose(f);
    if (tam < 1024 || img[0] != 0xE9) { fprintf(stderr, "%s no parece una imagen de app del ESP32\n", argv[1]); return 1; }
    uint32_t crc = crc32(0, img, (size_t)tam);
    printf("Imagen: %s, %ld bytes, crc32 %08x\n", argv[1], tam, crc);

    uint8_t p[4 + 1024];
    uint32_t sig = 0;
    poner32(p, (uint32_t)tam);
    poner32(p + 4, crc);
    int c = pedir(0x10, p, 8, 3000, &sig);
    if (c != 0) { fprintf(stderr, "No se pudo iniciar: %s\n", c < 0 ? "sin respuesta" : codigo_txt((uint8_t)c)); return 1; }

    uint32_t t0 = plat_ms(), pos = 0;
    int ultimo_pct = -1;
    while (pos < (uint32_t)tam) {
        uint16_t k = (uint16_t)((uint32_t)tam - pos > 1024 ? 1024 : (uint32_t)tam - pos);
        poner32(p, pos);
        memcpy(p + 4, img + pos, k);
        c = pedir(0x11, p, (uint16_t)(4 + k), 2000, &sig);
        if (c != 0) {
            fprintf(stderr, "\nFalló en %u: %s. El ESP32 sigue con el firmware anterior.\n", pos,
                    c < 0 ? "sin respuesta" : codigo_txt((uint8_t)c));
            return 1;
        }
        pos = sig; // lo que el ESP32 dice que espera (sirve si se perdió una respuesta)
        int pct = (int)(100.0 * pos / tam);
        if (pct / 10 != ultimo_pct / 10) { printf("  %3d %%\n", pct); ultimo_pct = pct; }
    }
    printf("Enviado en %.1f s, verificando...\n", (plat_ms() - t0) / 1000.0);
    c = pedir(0x12, NULL, 0, 5000, &sig);
    if (c != 0) {
        fprintf(stderr, "No se aplicó: %s. El ESP32 sigue con el firmware anterior.\n",
                c < 0 ? "sin respuesta" : codigo_txt((uint8_t)c));
        return 1;
    }
    printf("Verificado. Reiniciando el ESP32...\n");
    usleep(500000);
    esp_leer();
    if (!esperar_conectado(8000)) { fprintf(stderr, "El ESP32 no volvió después de reiniciar\n"); return 1; }
    if (!version(ver, sizeof ver)) { fprintf(stderr, "El ESP32 volvió pero no responde a VERSION\n"); return 1; }
    printf("ESP32 nuevo: %s\nListo.\n", ver);
    return 0;
}
