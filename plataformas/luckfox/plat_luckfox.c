// Plataforma Luckfox Pico Mini (RV1103, Linux): pantalla ST7796S por SPI0 (lcd.c), botones y audio por
// el ESP32 en UART3 (esp.c), teclado de desarrollo (teclado.c) y emuladores libretro como procesos
// aparte (/oem/consola/emu_*).
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "esp.h"
#include "lcd.h"
#include "plataforma.h"
#include "teclado.h"

#define DIR_BIN "/oem/consola" // en /oem: la partición del sistema está casi llena

// ---------------------------------------------------------------- pantalla

void plat_pantalla_abrir(bool reiniciar) { lcd_abrir(reiniciar ? 1 : 0); }
void plat_pantalla_cerrar(void) { lcd_cerrar(); }
void plat_pantalla_enviar(const uint16_t *px, int x, int y, int w, int h, int paso) { lcd_enviar(px, x, y, w, h, paso); }

// ---------------------------------------------------------------- botones

void plat_entrada_abrir(void) {
    esp_abrir();
    teclado_abrir();
}

void plat_entrada_cerrar(void) {
    teclado_cerrar();
    esp_cerrar();
}

uint16_t plat_botones(void) {
    esp_leer();
    return esp_botones() | teclado_leer();
}

bool plat_pausa_pedida(void) { return teclado_pausa(); }

// ---------------------------------------------------------------- tiempo

uint32_t plat_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec * 1000u + t.tv_nsec / 1000000u);
}

void plat_dormir_ms(int ms) {
    if (ms > 0) usleep((useconds_t)ms * 1000);
}

// ---------------------------------------------------------------- audio (ESP32)

bool plat_audio_listo(void) {
    esp_leer();
    return esp_conectado();
}

void plat_audio_enviar(const int16_t *m, int n) { esp_audio(m, n); }

int plat_audio_nivel(void) {
    esp_leer();
    return esp_nivel();
}

void plat_audio_vaciar(void) { esp_silencio(); }

// ---------------------------------------------------------------- almacenamiento y juegos

const char *plat_raiz_sd(void) {
    const char *r = getenv("CONSOLA_SD");
    return r && *r ? r : "/mnt/sdcard";
}

static const char *binario(emulador_t e) {
    switch (e) {
    case EMU_NES: return DIR_BIN "/emu_nes";
    case EMU_GBA: return DIR_BIN "/emu_gba";
    case EMU_SNES: return DIR_BIN "/emu_snes";
    case EMU_PS1: return DIR_BIN "/emu_ps1";
    default: return DIR_BIN "/emu_gb"; // GB y GBC
    }
}

bool plat_emulador_disponible(emulador_t e) { return access(binario(e), X_OK) == 0; }

bool plat_jugar(const char *ruta, const char *sistema, emulador_t e, char *aviso, size_t n_aviso) {
    const char *bin = binario(e);
    static const char *const tipo[N_EMU] = {"nes", "gb", "gbc", "gba", "snes", "ps1"};
    // El emulador abre la pantalla y la UART por su cuenta: soltarlas mientras juega.
    lcd_cerrar();
    plat_entrada_cerrar();
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        int fd = open("/tmp/emu_error.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) { dup2(fd, 2); close(fd); }
        execl(bin, bin, ruta, sistema, tipo[e], (char *)NULL);
        fprintf(stderr, "ERROR no se pudo ejecutar %s: %s\n", bin, strerror(errno));
        _exit(127);
    }
    int st = 0;
    if (pid > 0) waitpid(pid, &st, 0);
    lcd_abrir(0);
    plat_entrada_abrir();
    aviso[0] = 0;
    if (pid < 0) snprintf(aviso, n_aviso, "fork: %s", strerror(errno));
    else if (WIFSIGNALED(st)) snprintf(aviso, n_aviso, "el emulador se cerro (senal %d)", WTERMSIG(st));
    else if (WEXITSTATUS(st) != 0) {
        FILE *f = fopen("/tmp/emu_error.txt", "r");
        char l[200];
        while (f && fgets(l, sizeof l, f))
            if (strstr(l, "ERROR")) {
                l[strcspn(l, "\n")] = 0;
                snprintf(aviso, n_aviso, "%s", l);
            }
        if (f) fclose(f);
        if (!*aviso) snprintf(aviso, n_aviso, "codigo de salida %d", WEXITSTATUS(st));
    }
    return !*aviso;
}

// ---------------------------------------------------------------- información

long plat_mem_libre_kb(void) {
    FILE *f = fopen("/proc/meminfo", "r");
    long kb = -1;
    char l[128];
    while (f && fgets(l, sizeof l, f))
        if (!strncmp(l, "MemAvailable:", 13)) kb = atol(l + 13);
    if (f) fclose(f);
    return kb;
}

void plat_info(char *out, size_t n) {
    struct statvfs sv;
    long libre = statvfs(plat_raiz_sd(), &sv) == 0 ? (long)(sv.f_bavail * (double)sv.f_frsize / 1048576) : -1;
    snprintf(out, n, "RV1103 1,2 GHz - %ld MB libres - SD libre %ld MB", plat_mem_libre_kb() / 1024, libre);
}
