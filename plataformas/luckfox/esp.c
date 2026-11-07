#include "esp.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "plataforma.h"

#define PUERTO "/dev/ttyS3"

static int fd = -1;
// El menú usa el enlace desde dos hilos (botones y efectos de sonido).
static pthread_mutex_t mtx_rx = PTHREAD_MUTEX_INITIALIZER, mtx_tx = PTHREAD_MUTEX_INITIALIZER;
static uint16_t botones = 0;
static int nivel = 0;
static uint32_t t_estado = 0;

static uint8_t crc8(uint8_t c, uint8_t b) {
    c ^= b;
    for (int i = 0; i < 8; i++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1);
    return c;
}

bool esp_abrir(void) {
    if (fd >= 0) return true;
    fd = open(PUERTO, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return false;
    struct termios t;
    memset(&t, 0, sizeof t);
    t.c_cflag = CS8 | CLOCAL | CREAD;
    cfsetispeed(&t, B1500000);
    cfsetospeed(&t, B1500000);
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    tcflush(fd, TCIOFLUSH);
    if (tcsetattr(fd, TCSANOW, &t) != 0) {
        close(fd);
        fd = -1;
        return false;
    }
    return true;
}

void esp_cerrar(void) {
    pthread_mutex_lock(&mtx_rx);
    pthread_mutex_lock(&mtx_tx);
    if (fd >= 0) close(fd);
    fd = -1;
    pthread_mutex_unlock(&mtx_tx);
    pthread_mutex_unlock(&mtx_rx);
}

// ---------------------------------------------------------------- recepción

enum { E_CAB0, E_CAB1, E_TIPO, E_LARGO0, E_LARGO1, E_DATOS, E_CRC };
static int estado = E_CAB0;
static uint8_t tipo, crc, datos[64];
static uint16_t largo, pos;
// Última respuesta a un pedido (versión, OTA); la consume esp_respuesta().
static uint8_t resp_tipo, resp_datos[64];
static uint16_t resp_largo;
static bool resp_hay = false;

static void paquete(void) {
    if (tipo == 0x81 && largo >= 4) {
        botones = (uint16_t)(datos[0] | datos[1] << 8);
        nivel = datos[2] | datos[3] << 8;
        t_estado = plat_ms();
    } else if (tipo == 0x82 || tipo == 0x90) {
        resp_tipo = tipo;
        resp_largo = largo;
        memcpy(resp_datos, datos, largo);
        resp_hay = true;
    }
}

bool esp_respuesta(uint8_t *t, uint8_t *d, uint16_t *n) {
    pthread_mutex_lock(&mtx_rx);
    bool hay = resp_hay;
    if (hay) {
        *t = resp_tipo;
        *n = resp_largo;
        memcpy(d, resp_datos, resp_largo);
        resp_hay = false;
    }
    pthread_mutex_unlock(&mtx_rx);
    return hay;
}

static void recibir(uint8_t b) {
    switch (estado) {
    case E_CAB0: if (b == 0xA5) estado = E_CAB1; break;
    case E_CAB1: estado = b == 0x5A ? E_TIPO : (b == 0xA5 ? E_CAB1 : E_CAB0); break;
    case E_TIPO: tipo = b; crc = crc8(0, b); estado = E_LARGO0; break;
    case E_LARGO0: largo = b; crc = crc8(crc, b); estado = E_LARGO1; break;
    case E_LARGO1:
        largo |= b << 8;
        crc = crc8(crc, b);
        pos = 0;
        estado = largo > sizeof datos ? E_CAB0 : (largo ? E_DATOS : E_CRC);
        break;
    case E_DATOS:
        datos[pos++] = b;
        crc = crc8(crc, b);
        if (pos == largo) estado = E_CRC;
        break;
    case E_CRC:
        if (b == crc) paquete();
        estado = E_CAB0;
        break;
    }
}

void esp_leer(void) {
    pthread_mutex_lock(&mtx_rx);
    if (fd >= 0) {
        uint8_t b[512];
        ssize_t n;
        while ((n = read(fd, b, sizeof b)) > 0)
            for (ssize_t i = 0; i < n; i++) recibir(b[i]);
    }
    pthread_mutex_unlock(&mtx_rx);
}

bool esp_conectado(void) { return fd >= 0 && t_estado && plat_ms() - t_estado < 200; }

uint16_t esp_botones(void) { return esp_conectado() ? botones : 0; }

int esp_nivel(void) { return nivel; }

// ---------------------------------------------------------------- envío

static void escribir(const uint8_t *d, size_t n) {
    uint32_t t0 = plat_ms();
    while (n > 0) {
        ssize_t w = write(fd, d, n);
        if (w > 0) {
            d += w;
            n -= (size_t)w;
            continue;
        }
        if (w < 0 && errno != EAGAIN && errno != EINTR) return;
        if (plat_ms() - t0 > 50) return; // el ESP32 no está o la UART se trabó: no colgar el juego
        struct pollfd p = {fd, POLLOUT, 0};
        poll(&p, 1, 5);
    }
}

static void enviar(uint8_t t, const uint8_t *d, uint16_t n) {
    pthread_mutex_lock(&mtx_tx);
    if (fd < 0) { pthread_mutex_unlock(&mtx_tx); return; }
    uint8_t cab[5] = {0xA5, 0x5A, t, (uint8_t)n, (uint8_t)(n >> 8)};
    uint8_t c = 0;
    for (int i = 2; i < 5; i++) c = crc8(c, cab[i]);
    for (uint16_t i = 0; i < n; i++) c = crc8(c, d[i]);
    uint8_t paq[5 + 2048 + 1];
    memcpy(paq, cab, 5);
    if (n) memcpy(paq + 5, d, n);
    paq[5 + n] = c;
    escribir(paq, 5 + n + 1);
    pthread_mutex_unlock(&mtx_tx);
}

void esp_audio(const int16_t *m, int n) {
    while (n > 0) {
        int k = n > 1024 ? 1024 : n;
        uint8_t d[2048];
        for (int i = 0; i < k; i++) {
            d[2 * i] = (uint8_t)m[i];
            d[2 * i + 1] = (uint8_t)((uint16_t)m[i] >> 8);
        }
        enviar(0x01, d, (uint16_t)(k * 2));
        m += k;
        n -= k;
    }
}

void esp_silencio(void) { enviar(0x02, NULL, 0); }

void esp_enviar(uint8_t t, const uint8_t *d, uint16_t n) { enviar(t, d, n); }
