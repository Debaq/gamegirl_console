#include "lcd.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "mux_mini.h"

#define GPIO_DC 52
#define GPIO_RST 51
#define SPI_HZ 50000000u
#define SPI_TROZO_MAX 65532 // el driver Rockchip acepta hasta 65535 por transferencia

static int spi_fd = -1, dc_fd = -1, rst_fd = -1;
static size_t trozo = 4096;
static uint16_t tx[480 * 320]; // big-endian para el SPI

static void morir(const char *msg) {
    fprintf(stderr, "ERROR pantalla %s: %s\n", msg, strerror(errno));
    exit(3);
}

static void escribir_archivo(const char *ruta, const char *valor) {
    int fd = open(ruta, O_WRONLY);
    if (fd < 0) return;
    if (write(fd, valor, strlen(valor)) < 0) {}
    close(fd);
}

// nivel: valor inicial. Ojo: escribir "out" en direction pone el pin en BAJO; con RST eso deja la
// pantalla en reset. "high"/"low" fijan salida y nivel juntos, sin pulso.
static int gpio_salida(int n, int nivel) {
    char ruta[64], num[8];
    snprintf(num, sizeof num, "%d", n);
    snprintf(ruta, sizeof ruta, "/sys/class/gpio/gpio%d", n);
    if (access(ruta, F_OK) != 0) escribir_archivo("/sys/class/gpio/export", num);
    snprintf(ruta, sizeof ruta, "/sys/class/gpio/gpio%d/direction", n);
    escribir_archivo(ruta, nivel ? "high" : "low");
    snprintf(ruta, sizeof ruta, "/sys/class/gpio/gpio%d/value", n);
    int fd = open(ruta, O_WRONLY);
    if (fd < 0) morir("abrir GPIO");
    return fd;
}

static void gpio_poner(int fd, int v) {
    if (pwrite(fd, v ? "1" : "0", 1, 0) != 1) morir("escribir GPIO");
}

static void spi_enviar(const void *datos, size_t n) {
    const uint8_t *d = datos;
    while (n > 0) {
        size_t m = n < trozo ? n : trozo;
        struct spi_ioc_transfer t = {
            .tx_buf = (unsigned long)d, .len = m, .speed_hz = SPI_HZ, .bits_per_word = 8,
        };
        if (ioctl(spi_fd, SPI_IOC_MESSAGE(1), &t) < 0) morir("SPI");
        d += m;
        n -= m;
    }
}

static void cmd(uint8_t c, const uint8_t *d, size_t n) {
    gpio_poner(dc_fd, 0);
    spi_enviar(&c, 1);
    if (n) {
        gpio_poner(dc_fd, 1);
        spi_enviar(d, n);
    }
}

#define CMD(c, ...)                                                        \
    do {                                                                   \
        const uint8_t _d[] = {__VA_ARGS__};                                \
        cmd(c, _d, sizeof _d);                                             \
    } while (0)

static void dormir_ms(int ms) { usleep(ms * 1000); }

void lcd_abrir(int reiniciar) {
    FILE *f = fopen("/sys/module/spidev/parameters/bufsiz", "r");
    if (f) {
        unsigned long b;
        if (fscanf(f, "%lu", &b) == 1 && b > 0) trozo = b;
        fclose(f);
    }
    if (trozo > SPI_TROZO_MAX) trozo = SPI_TROZO_MAX;
    if (mux_pantalla_a_gpio() != 0) {
        fprintf(stderr, "ERROR pantalla: no se pudo poner GPIO1_C3/C4 como GPIO (¿root?)\n");
        exit(3);
    }
    spi_fd = open("/dev/spidev0.0", O_RDWR);
    if (spi_fd < 0) morir("abrir /dev/spidev0.0");
    uint8_t modo = SPI_MODE_0, bits = 8;
    uint32_t hz = SPI_HZ;
    if (ioctl(spi_fd, SPI_IOC_WR_MODE, &modo) < 0) morir("SPI modo");
    if (ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) morir("SPI bits");
    if (ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &hz) < 0) morir("SPI velocidad");
    dc_fd = gpio_salida(GPIO_DC, 1);
    rst_fd = gpio_salida(GPIO_RST, 1);
    if (!reiniciar) {
        // Sin reset: reenviar la configuración por si la pantalla se apagó (por ejemplo, un emulador
        // lanzado a mano tras encender). Si ya estaba configurada no cambia nada ni parpadea.
        cmd(0x11, NULL, 0); dormir_ms(10); // SLPOUT (ignorado si ya estaba despierta)
        CMD(0xF0, 0xC3);
        CMD(0xF0, 0x96);
        CMD(0x36, 0x28);
        CMD(0x3A, 0x55);
        CMD(0xB4, 0x01);
        CMD(0xF0, 0x3C);
        CMD(0xF0, 0x69);
        cmd(0x21, NULL, 0);
        cmd(0x29, NULL, 0);
        return;
    }

    gpio_poner(rst_fd, 1); dormir_ms(10);
    gpio_poner(rst_fd, 0); dormir_ms(20);
    gpio_poner(rst_fd, 1); dormir_ms(150);
    cmd(0x01, NULL, 0); dormir_ms(150); // SWRESET
    cmd(0x11, NULL, 0); dormir_ms(150); // SLPOUT
    CMD(0xF0, 0xC3);
    CMD(0xF0, 0x96);
    CMD(0x36, 0x28); // MADCTL: horizontal 480x320, BGR
    CMD(0x3A, 0x55); // RGB565
    CMD(0xB4, 0x01);
    CMD(0xF0, 0x3C);
    CMD(0xF0, 0x69);
    cmd(0x21, NULL, 0); // INVON
    memset(tx, 0, sizeof tx);
    CMD(0x2A, 0, 0, (479 >> 8), 479 & 0xFF);
    CMD(0x2B, 0, 0, (319 >> 8), 319 & 0xFF);
    cmd(0x2C, (const uint8_t *)tx, sizeof tx); // negro antes de encender
    cmd(0x29, NULL, 0); // DISPON
    dormir_ms(20);
}

void lcd_cerrar(void) {
    if (spi_fd >= 0) close(spi_fd);
    if (dc_fd >= 0) close(dc_fd);
    if (rst_fd >= 0) close(rst_fd);
    spi_fd = dc_fd = rst_fd = -1;
}

static void ventana(int x, int y, int w, int h) {
    int x1 = x + w - 1, y1 = y + h - 1;
    CMD(0x2A, x >> 8, x & 0xFF, x1 >> 8, x1 & 0xFF);
    CMD(0x2B, y >> 8, y & 0xFF, y1 >> 8, y1 & 0xFF);
}

void lcd_enviar(const uint16_t *px, int x, int y, int w, int h, int paso) {
    if (w <= 0 || h <= 0) return;
    if (x < 0) { px -= x; w += x; x = 0; }
    if (y < 0) { px -= (long)y * paso; h += y; y = 0; }
    if (x + w > 480) w = 480 - x;
    if (y + h > 320) h = 320 - y;
    if (w <= 0 || h <= 0) return;
    uint16_t *o = tx;
    for (int j = 0; j < h; j++) {
        const uint16_t *s = px + (long)j * paso;
        for (int i = 0; i < w; i++) {
            uint16_t p = s[i];
            *o++ = (uint16_t)((p >> 8) | (p << 8));
        }
    }
    ventana(x, y, w, h);
    cmd(0x2C, (const uint8_t *)tx, (size_t)w * h * 2);
}
