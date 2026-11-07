// Frontend libretro de la consola (POSIX): se compila una vez por núcleo (emu_gba, emu_nes, emu_gb, emu_snes)
// y lo usan la Luckfox y el PC; solo habla con el hardware por plataforma.h.
// Lo lanza el menú: emu <ruta_rom> <SISTEMA> <tipo nes|gb|gbc|gba|snes> [-m] [-n] [-t seg] [-c captura.ppm]
// Pruebas: -m sin límite de 60 fps, -n sin enviar a la pantalla (mide solo la emulación).
// Pausa: SELECT+START mantenidos ~0,25 s (sin A ni B), o Esc/P en el teclado.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "cfg.h"
#include "comun.h"
#include "dib.h"
#include "libretro.h"
#include "pantalla.h"
#include "pausa.h"
#include "plataforma.h"
#include "sonido.h"

static const char *ruta_rom, *sistema, *tipo;
static emulador_t emu;
static rgb acento;
static volatile bool terminar = false;

// ---------------------------------------------------------------- video

#define SRC_MAX (640 * 512) // PS1 entrega hasta 640x512 (SNES en alta resolución, 512x478)
static uint16_t cuadro[SRC_MAX]; // último cuadro del núcleo, RGB565
static int src_w = 0, src_h = 0;
static enum retro_pixel_format formato = RETRO_PIXEL_FORMAT_0RGB1555;

static uint16_t salida[ANCHO * ALTO], sombra[ANCHO * ALTO];
static int out_w, out_h, out_x, out_y, recorte;
static int mapa_x[ANCHO], mapa_y[ALTO];
static int mapa_src_w = -1, mapa_src_h = -1;

static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cnd = PTHREAD_COND_INITIALIZER;
static bool hay_cuadro = false, ocupado = false, video_salir = false, limpiar = true;
static unsigned long mostrados = 0;
static bool sin_video = false;

static const modo_pantalla *modo_actual(void) {
    if (emu == EMU_NES) return &PANTALLAS_NES[((CFG.nes_pantalla % N_PANT_NES) + N_PANT_NES) % N_PANT_NES];
    if (emu == EMU_SNES) return &PANTALLAS_SNES[((CFG.snes_pantalla % N_PANT_SNES) + N_PANT_SNES) % N_PANT_SNES];
    if (emu == EMU_GBA) return &PANTALLAS_GBA[((CFG.gba_pantalla % 2) + 2) % 2];
    if (emu == EMU_PS1) return &PANTALLAS_PS1[((CFG.ps1_pantalla % N_PANT_PS1) + N_PANT_PS1) % N_PANT_PS1];
    return &PANTALLAS_GB[((CFG.gb_pantalla % 3) + 3) % 3];
}

// Calcula tamaño de salida y tablas para el cuadro de w x h.
static void preparar_escala(int w, int h) {
    const modo_pantalla *m = modo_actual();
    recorte = m->recorte;
    int vis_h = h - 2 * recorte;
    out_w = m->w ? m->w : w;
    out_h = m->h ? m->h : vis_h;
    if (out_w > ANCHO) out_w = ANCHO;
    if (out_h > ALTO) out_h = ALTO;
    out_x = (ANCHO - out_w) / 2;
    out_y = (ALTO - out_h) / 2;
    for (int x = 0; x < out_w; x++) mapa_x[x] = x * w / out_w;
    for (int y = 0; y < out_h; y++) mapa_y[y] = recorte + y * vis_h / out_h;
    mapa_src_w = w;
    mapa_src_h = h;
    memset(sombra, 0xA5, sizeof sombra); // fuerza el envío completo
}

static void *hilo_video(void *arg) {
    (void)arg;
    static uint16_t local[SRC_MAX];
    for (;;) {
        pthread_mutex_lock(&mtx);
        while (!hay_cuadro && !video_salir) pthread_cond_wait(&cnd, &mtx);
        if (video_salir) { pthread_mutex_unlock(&mtx); break; }
        int w = src_w, h = src_h;
        memcpy(local, cuadro, (size_t)w * h * 2);
        hay_cuadro = false;
        ocupado = true;
        bool limpiar_ahora = limpiar;
        limpiar = false;
        pthread_mutex_unlock(&mtx);

        if (limpiar_ahora || w != mapa_src_w || h != mapa_src_h) {
            preparar_escala(w, h);
            memset(salida, 0, sizeof salida);
            plat_pantalla_enviar(salida, 0, 0, ANCHO, ALTO, ANCHO); // fondo negro
        }
        for (int y = 0; y < out_h; y++) {
            uint16_t *fila = &salida[y * out_w];
            if (y > 0 && mapa_y[y] == mapa_y[y - 1]) {
                memcpy(fila, fila - out_w, out_w * 2);
                continue;
            }
            const uint16_t *src = &local[mapa_y[y] * w];
            for (int x = 0; x < out_w; x++) fila[x] = src[mapa_x[x]];
        }
        pantalla_cambios(salida, sombra, out_x, out_y, out_w, out_h);

        pthread_mutex_lock(&mtx);
        ocupado = false;
        mostrados++;
        pthread_cond_broadcast(&cnd);
        pthread_mutex_unlock(&mtx);
    }
    return NULL;
}

// Espera a que el hilo de video termine lo que está enviando (antes de dibujar la pausa).
static void video_esperar(void) {
    pthread_mutex_lock(&mtx);
    hay_cuadro = false;
    while (ocupado) pthread_cond_wait(&cnd, &mtx);
    pthread_mutex_unlock(&mtx);
}

static void video_cb(const void *data, unsigned w, unsigned h, size_t pitch) {
    if (sin_video) return;
    if (!data || w * h > SRC_MAX || !w || !h) return; // cuadro repetido o saltado
    pthread_mutex_lock(&mtx);
    const uint8_t *s = data;
    for (unsigned y = 0; y < h; y++) {
        uint16_t *o = &cuadro[y * w];
        if (formato == RETRO_PIXEL_FORMAT_RGB565) {
            memcpy(o, s + y * pitch, w * 2);
        } else if (formato == RETRO_PIXEL_FORMAT_XRGB8888) {
            const uint32_t *p = (const uint32_t *)(s + y * pitch);
            for (unsigned x = 0; x < w; x++) {
                uint32_t c = p[x];
                o[x] = (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
            }
        } else {
            const uint16_t *p = (const uint16_t *)(s + y * pitch);
            for (unsigned x = 0; x < w; x++) {
                uint16_t c = p[x];
                o[x] = (uint16_t)(((c & 0x7C00) << 1) | ((c & 0x03E0) << 1) | (c & 0x001F));
            }
        }
    }
    src_w = w;
    src_h = h;
    hay_cuadro = true;
    pthread_cond_signal(&cnd);
    pthread_mutex_unlock(&mtx);
}

// ---------------------------------------------------------------- entrada

static uint16_t botones = 0;  // lo que ve el núcleo
static bool pausa_pedida = false;

// Diagnóstico: si existe /tmp/log_botones al arrancar, cada cambio de botones (lo que llega y lo que
// ve el núcleo) se anota en /tmp/botones.log con el número de llamada y la hora en ms.
static FILE *log_botones = NULL;
static unsigned long n_poll = 0;

static void input_poll_cb(void) {
    bool pedir;
    uint16_t crudo = plat_botones();
    uint16_t ant = botones;
    botones = pausa_gesto(crudo, &pedir);
    if (pedir) pausa_pedida = true;
    n_poll++;
    if (log_botones && botones != ant) {
        fprintf(log_botones, "%7lu %9u crudo %04X nucleo %04X\n", n_poll, plat_ms(), crudo, botones);
        fflush(log_botones);
    }
}

static int16_t input_state_cb(unsigned port, unsigned dev, unsigned idx, unsigned id) {
    (void)idx;
    if (port != 0 || dev != RETRO_DEVICE_JOYPAD) return 0;
    static const struct { unsigned id; uint16_t bt; } MAPA[] = {
        {RETRO_DEVICE_ID_JOYPAD_B, BT_B},         {RETRO_DEVICE_ID_JOYPAD_A, BT_A},
        {RETRO_DEVICE_ID_JOYPAD_SELECT, BT_SELECT}, {RETRO_DEVICE_ID_JOYPAD_START, BT_START},
        {RETRO_DEVICE_ID_JOYPAD_UP, BT_ARRIBA},   {RETRO_DEVICE_ID_JOYPAD_DOWN, BT_ABAJO},
        {RETRO_DEVICE_ID_JOYPAD_LEFT, BT_IZQ},    {RETRO_DEVICE_ID_JOYPAD_RIGHT, BT_DER},
        {RETRO_DEVICE_ID_JOYPAD_L, BT_L},         {RETRO_DEVICE_ID_JOYPAD_R, BT_R},
        {RETRO_DEVICE_ID_JOYPAD_X, BT_X},         {RETRO_DEVICE_ID_JOYPAD_Y, BT_Y},
    };
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK) {
        int16_t m = 0;
        for (unsigned i = 0; i < sizeof MAPA / sizeof MAPA[0]; i++)
            if (botones & MAPA[i].bt) m |= (int16_t)(1 << MAPA[i].id);
        return m;
    }
    for (unsigned i = 0; i < sizeof MAPA / sizeof MAPA[0]; i++)
        if (MAPA[i].id == id) return (botones & MAPA[i].bt) != 0;
    return 0;
}

// ---------------------------------------------------------------- audio
// El núcleo entrega estéreo a su tasa; se pasa a mono, se aplica el volumen y se convierte a
// 32768 Hz (interpolación lineal). La conversión se ajusta ±0,5 % según el nivel del búfer del
// ESP32 (control dinámico como RetroArch): el reloj del I2S manda y no se acumula desfase.

static double tasa_nucleo = 32768;
static double frac = 0;          // posición entre la muestra anterior y la actual
static int32_t previa = 0;
static int16_t audio_sal[4096];
static int n_sal = 0;
static double correccion = 1.0;

static void audio_vaciar(void) {
    if (n_sal) plat_audio_enviar(audio_sal, n_sal);
    n_sal = 0;
}

static void audio_muestra(int32_t x) {
    double paso = tasa_nucleo / (AUDIO_TASA * correccion); // muestras del núcleo por muestra de salida
    while (frac < 1.0) {
        int32_t y = previa + (int32_t)((x - previa) * frac);
        audio_sal[n_sal++] = (int16_t)y;
        if (n_sal == (int)(sizeof audio_sal / sizeof audio_sal[0])) audio_vaciar();
        frac += paso;
    }
    frac -= 1.0;
    previa = x;
}

static void audio_frames(const int16_t *d, size_t n) {
    int vol = VOL_PCT[CFG.volumen < 0 ? 0 : CFG.volumen > 10 ? 10 : CFG.volumen];
    if (!plat_audio_listo()) return;
    for (size_t i = 0; i < n; i++) audio_muestra(((int32_t)d[2 * i] + d[2 * i + 1]) * vol / 200);
}

static void audio_cb(int16_t l, int16_t r) {
    int16_t d[2] = {l, r};
    audio_frames(d, 1);
}

static size_t audio_batch_cb(const int16_t *d, size_t n) {
    audio_frames(d, n);
    return n;
}

// Después de cada cuadro: mandar lo juntado y recalcular la corrección.
static void audio_fin_cuadro(void) {
    audio_vaciar();
    if (!plat_audio_listo()) return;
    double d = (plat_audio_nivel() - AUDIO_OBJETIVO) / (double)AUDIO_OBJETIVO;
    if (d > 1) d = 1;
    if (d < -1) d = -1;
    correccion = 1.0 - 0.005 * d;
}

// ---------------------------------------------------------------- opciones del núcleo

static char dir_rom[512];
struct opcion { const char *clave; char valor[48]; };
static struct opcion OPC[24];
static int n_opc = 0;

static void opcion(const char *clave, const char *valor) {
    for (int i = 0; i < n_opc; i++)
        if (!strcmp(OPC[i].clave, clave)) { snprintf(OPC[i].valor, sizeof OPC[i].valor, "%s", valor); return; }
    if (n_opc < 24) {
        OPC[n_opc].clave = clave;
        snprintf(OPC[n_opc].valor, sizeof OPC[n_opc].valor, "%s", valor);
        n_opc++;
    }
}

// La pantalla SPI de la placa no alcanza a mostrar 60 cuadros grandes por segundo: ahí conviene que el
// núcleo salte cuadros. En el PC (x86) la ventana muestra todos.
#if defined(__arm__) || defined(__aarch64__)
#define PANTALLA_LENTA 1
#else
#define PANTALLA_LENTA 0
#endif

static void preparar_opciones(void) {
    switch (emu) {
    case EMU_GBA: {
        // Dibujar solo los cuadros que la pantalla alcanza a mostrar.
        bool grande = PANTALLA_LENTA && modo_actual()->w > 240;
        opcion("gpsp_frameskip", grande ? "fixed_interval" : "disabled");
        opcion("gpsp_frameskip_interval", grande ? "2" : "0");
        opcion("gpsp_color_correction", "disabled");
        opcion("gpsp_frame_mixing", "disabled");
        opcion("gpsp_sound_rate", "32768");
#if !PANTALLA_LENTA
        opcion("gpsp_drc", "disabled"); // su dynarec x86 es de 32 bits: en un PC de 64 se cae (intérprete)
#endif
        break;
    }
    case EMU_NES:
        opcion("fceumm_overscan_v_top", "0"); // el recorte lo hace el frontend según el modo
        opcion("fceumm_overscan_v_bottom", "0");
        opcion("fceumm_overscan_h_left", "0");
        opcion("fceumm_overscan_h_right", "0");
        opcion("fceumm_overscan", "disabled");
        break;
    case EMU_GB:
        opcion("gambatte_gb_hwmode", "Auto");
        opcion("gambatte_gb_colorization", "internal");
        opcion("gambatte_gb_internal_palette", PALETAS_GAMBATTE[((CFG.gb_paleta % 12) + 12) % 12]);
        opcion("gambatte_mix_frames", "disabled");
        break;
    case EMU_SNES: {
        // Como gpSP: en pantalla grande no tiene sentido dibujar más cuadros de los que el SPI muestra.
        bool grande = PANTALLA_LENTA && modo_actual()->w > 256;
        opcion("snes9x_2005_frameskip", grande ? "auto" : "disabled");
        opcion("snes9x_2005_frameskip_threshold", "33");
        opcion("snes9x_2005_reduce_sprite_flicker", "disabled");
        break;
    }
    case EMU_PS1:
        // Experimento: en la placa salta cuadros según el búfer de audio (como snes9x). La BIOS se busca
        // en la carpeta de la ROM (cualquier archivo de 512 KB; si no hay, usa la BIOS libre HLE).
        opcion("pcsx_rearmed_frameskip_type", PANTALLA_LENTA ? "auto" : "disabled");
        opcion("pcsx_rearmed_frameskip_threshold", "33");
        opcion("pcsx_rearmed_show_bios_bootlogo", "disabled");
        opcion("pcsx_rearmed_rgb32_output", "disabled");
        opcion("pcsx_rearmed_spu_thread", "disabled");
        opcion("pcsx_rearmed_gpu_thread_rendering", "disabled");
        break;
    case EMU_GBC:
        opcion("gambatte_gb_hwmode", "GBC");
        opcion("gambatte_gb_colorization", "disabled");
        opcion("gambatte_mix_frames", "disabled");
        break;
    default: break;
    }
}

static bool opciones_cambiaron = false;

// Estado del búfer de audio para el salto automático de cuadros de los núcleos (snes9x, gpSP):
// si el búfer del ESP32 baja, la emulación no llega a tiempo y conviene no dibujar ese cuadro.
static retro_audio_buffer_status_callback_t estado_audio_cb = NULL;

static void informar_audio(void) {
    if (!estado_audio_cb) return;
    if (!plat_audio_listo()) { estado_audio_cb(false, 0, false); return; }
    int nivel = plat_audio_nivel();
    unsigned ocupacion = (unsigned)(nivel * 100 / (2 * AUDIO_OBJETIVO));
    if (ocupacion > 100) ocupacion = 100;
    estado_audio_cb(true, ocupacion, nivel < AUDIO_OBJETIVO / 2);
}

static void log_cb(enum retro_log_level nivel, const char *fmt, ...) {
    if (nivel < RETRO_LOG_WARN) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[nucleo] ");
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

static bool env_cb(unsigned cmd, void *data) {
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        ((struct retro_log_callback *)data)->log = log_cb;
        return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        enum retro_pixel_format f = *(enum retro_pixel_format *)data;
        if (f != RETRO_PIXEL_FORMAT_RGB565 && f != RETRO_PIXEL_FORMAT_XRGB8888 && f != RETRO_PIXEL_FORMAT_0RGB1555)
            return false;
        formato = f;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char **)data = dir_rom;
        return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *(bool *)data = true;
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *v = data;
        for (int i = 0; i < n_opc; i++)
            if (!strcmp(OPC[i].clave, v->key)) { v->value = OPC[i].valor; return true; }
        v->value = NULL;
        return false;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *(bool *)data = opciones_cambiaron;
        opciones_cambiaron = false;
        return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        *(unsigned *)data = 0;
        return true;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
        return true;
    case RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK: {
        const struct retro_audio_buffer_status_callback *c = data;
        estado_audio_cb = c ? c->callback : NULL;
        return true;
    }
    default:
        return false;
    }
}

// ---------------------------------------------------------------- partidas y estados

static char ruta_sav[600], ruta_rtc[600], ruta_est[600];

static void cargar_mem(unsigned id, const char *ruta) {
    size_t n = retro_get_memory_size(id);
    uint8_t *m = retro_get_memory_data(id);
    if (!n || !m) return;
    FILE *f = fopen(ruta, "rb");
    if (!f) return;
    if (fread(m, 1, n, f) == 0) {}
    fclose(f);
}

static int64_t ahora_ns(void);
static uint8_t *copia_sav = NULL;

// Guardado de la partida en segundo plano: escribir y hacer fsync en la microSD tarda 20-200 ms y, hecho en el
// hilo del juego, se notaba como un freeze corto cada vez que el juego cambiaba su memoria de guardado.
// El juego solo copia los datos (memcpy) y este hilo los escribe.
static pthread_t th_guardar;
static pthread_mutex_t mtx_g = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cnd_g = PTHREAD_COND_INITIALIZER;
static uint8_t *g_buf = NULL;     // datos a escribir
static size_t g_n = 0;
static char g_ruta[600];
static bool g_pendiente = false, g_escribiendo = false, g_salir = false, g_hilo = false;

static bool escribir_archivo(const char *ruta, const uint8_t *m, size_t n) {
    char tmp[620];
    snprintf(tmp, sizeof tmp, "%s.tmp", ruta);
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    bool ok = fwrite(m, 1, n, f) == n;
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    return ok && rename(tmp, ruta) == 0;
}

static void *hilo_guardar(void *arg) {
    (void)arg;
    pthread_mutex_lock(&mtx_g);
    for (;;) {
        while (!g_pendiente && !g_salir) pthread_cond_wait(&cnd_g, &mtx_g);
        if (!g_pendiente && g_salir) break;
        g_pendiente = false;
        g_escribiendo = true;
        static uint8_t *local = NULL;
        static size_t cap = 0;
        if (cap < g_n) { free(local); local = malloc(g_n); cap = local ? g_n : 0; }
        size_t n = local ? g_n : 0;
        if (n) memcpy(local, g_buf, n);
        char ruta[600];
        snprintf(ruta, sizeof ruta, "%s", g_ruta);
        pthread_mutex_unlock(&mtx_g);
        int64_t t0 = ahora_ns();
        bool ok = n && escribir_archivo(ruta, local, n);
        registrar("partida guardada (fondo, %.0f ms): %s", (ahora_ns() - t0) / 1e6, ok ? "ok" : "ERROR");
        pthread_mutex_lock(&mtx_g);
        g_escribiendo = false;
        pthread_cond_broadcast(&cnd_g);
    }
    pthread_mutex_unlock(&mtx_g);
    return NULL;
}

// Espera a que termine lo que esté escribiendo el hilo (antes de un guardado directo o de salir).
static void guardar_esperar(void) {
    if (!g_hilo) return;
    pthread_mutex_lock(&mtx_g);
    while (g_pendiente || g_escribiendo) pthread_cond_wait(&cnd_g, &mtx_g);
    pthread_mutex_unlock(&mtx_g);
}

static void guardar_terminar(void) {
    if (!g_hilo) return;
    pthread_mutex_lock(&mtx_g);
    g_salir = true;
    pthread_cond_broadcast(&cnd_g);
    pthread_mutex_unlock(&mtx_g);
    pthread_join(th_guardar, NULL);
    g_hilo = false;
    free(g_buf);
    g_buf = NULL;
}

// Guardado periódico de la SRAM sin frenar el juego.
static void guardar_sav_fondo(void) {
    size_t n = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    uint8_t *m = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    if (!n || !m || (copia_sav && !memcmp(copia_sav, m, n))) return;
    if (!g_hilo) {
        if (pthread_create(&th_guardar, NULL, hilo_guardar, NULL) != 0) return;
        g_hilo = true;
    }
    pthread_mutex_lock(&mtx_g);
    if (g_n < n) { free(g_buf); g_buf = malloc(n); g_n = g_buf ? n : 0; }
    if (g_buf) {
        memcpy(g_buf, m, n);
        snprintf(g_ruta, sizeof g_ruta, "%s", ruta_sav);
        g_pendiente = true;
        pthread_cond_signal(&cnd_g);
    }
    pthread_mutex_unlock(&mtx_g);
    if (g_buf) {
        if (!copia_sav) copia_sav = malloc(n);
        if (copia_sav) memcpy(copia_sav, m, n);
    }
}

static void guardar_mem(unsigned id, const char *ruta, bool solo_si_cambio) {
    size_t n = retro_get_memory_size(id);
    uint8_t *m = retro_get_memory_data(id);
    if (!n || !m) return;
    guardar_esperar(); // que una escritura en segundo plano vieja no pise a esta
    if (id == RETRO_MEMORY_SAVE_RAM) {
        if (solo_si_cambio && copia_sav && !memcmp(copia_sav, m, n)) return;
        if (!copia_sav) copia_sav = malloc(n);
    }
    if (escribir_archivo(ruta, m, n) && id == RETRO_MEMORY_SAVE_RAM && copia_sav) memcpy(copia_sav, m, n);
}

static bool guardar_estado(void) {
    size_t n = retro_serialize_size();
    if (!n) return false;
    void *buf = malloc(n);
    bool ok = buf && retro_serialize(buf, n);
    if (ok) {
        FILE *f = fopen(ruta_est, "wb");
        ok = f && fwrite(buf, 1, n, f) == n;
        if (f) { fflush(f); fsync(fileno(f)); fclose(f); }
    }
    free(buf);
    registrar("estado guardado: %s %s", ruta_est, ok ? "ok" : "ERROR");
    return ok;
}

static bool cargar_estado(void) {
    size_t n = retro_serialize_size();
    FILE *f = fopen(ruta_est, "rb");
    if (!f || !n) { if (f) fclose(f); return false; }
    void *buf = malloc(n);
    bool ok = buf && fread(buf, 1, n, f) == n && retro_unserialize(buf, n);
    fclose(f);
    free(buf);
    registrar("estado cargado: %s %s", ruta_est, ok ? "ok" : "ERROR");
    return ok;
}

static bool hay_estado(void) {
    struct stat st;
    return stat(ruta_est, &st) == 0 && st.st_size > 0;
}

// ---------------------------------------------------------------- pausa (núcleo: pausa.c)

static pausa_ctx contexto_pausa(void) {
    pausa_ctx c = {
        .ruta_rom = ruta_rom, .sistema = sistema, .emu = emu, .acento = acento,
        .guardar_estado = retro_serialize_size() > 0 ? guardar_estado : NULL,
        .cargar_estado = retro_serialize_size() > 0 ? cargar_estado : NULL,
        .hay_estado = hay_estado, .terminar = &terminar,
        .fondo = salida, .fondo_x = out_x, .fondo_y = out_y, .fondo_w = out_w, .fondo_h = out_h,
    };
    return c;
}

// Devuelve 0 seguir, 1 reiniciar, 2 salir.
static int menu_pausa(void) {
    video_esperar();
    pausa_ctx c = contexto_pausa();
    int cambios = 0;
    pausa_res r = pausa_menu(&c, &cambios);
    if (r != PAUSA_SEGUIR) return r == PAUSA_REINICIAR ? 1 : 2;
    if (cambios) {
        preparar_opciones();
        opciones_cambiaron = true;
    }
    pthread_mutex_lock(&mtx);
    limpiar = true; // borra el panel y vuelve a dibujar el juego entero
    pthread_mutex_unlock(&mtx);
    return 0;
}

static void preguntar_gusto(void) {
    pausa_ctx c = contexto_pausa();
    pausa_preguntar_gusto(&c);
}

// ---------------------------------------------------------------- principal

static void al_senal(int s) { (void)s; terminar = true; }

static int64_t ahora_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000000LL + t.tv_nsec;
}

static long rss_kb(void) {
    FILE *f = fopen("/proc/self/status", "r");
    char l[128];
    long kb = -1;
    while (f && fgets(l, sizeof l, f))
        if (!strncmp(l, "VmRSS:", 6)) kb = atol(l + 6);
    if (f) fclose(f);
    return kb;
}

static bool cargar_juego(void) {
    retro_set_environment(env_cb);
    retro_set_video_refresh(video_cb);
    retro_set_audio_sample(audio_cb);
    retro_set_audio_sample_batch(audio_batch_cb);
    retro_set_input_poll(input_poll_cb);
    retro_set_input_state(input_state_cb);
    retro_init();
    struct retro_system_info si;
    retro_get_system_info(&si);
    struct retro_game_info info = {.path = ruta_rom};
    void *datos = NULL;
    if (!si.need_fullpath) {
        FILE *f = fopen(ruta_rom, "rb");
        if (!f) return false;
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        datos = malloc(n > 0 ? n : 1);
        if (!datos || fread(datos, 1, n, f) != (size_t)n) { fclose(f); free(datos); return false; }
        fclose(f);
        info.data = datos;
        info.size = n;
    }
    if (!retro_load_game(&info)) return false;
    // datos: algunos núcleos guardan el puntero, no se libera.
    cargar_mem(RETRO_MEMORY_SAVE_RAM, ruta_sav);
    cargar_mem(RETRO_MEMORY_RTC, ruta_rtc);
    size_t n = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    if (n) {
        copia_sav = malloc(n);
        if (copia_sav) memcpy(copia_sav, retro_get_memory_data(RETRO_MEMORY_SAVE_RAM), n);
    }
    return true;
}

static void descargar_juego(void) {
    guardar_mem(RETRO_MEMORY_SAVE_RAM, ruta_sav, true);
    guardar_mem(RETRO_MEMORY_RTC, ruta_rtc, false);
    guardar_terminar();
    retro_unload_game();
    retro_deinit();
    free(copia_sav);
    copia_sav = NULL;
}

int main(int argc, char **argv) {
    bool medir = false;
    double limite = 0;
    int op;
    while ((op = getopt(argc, argv, "mnt:c:")) != -1) {
        if (op == 'm') medir = true;
        else if (op == 'n') sin_video = true;
        else if (op == 't') limite = atof(optarg);
        else if (op == 'c') pantalla_captura = optarg;
    }
    if (argc - optind < 3) {
        fprintf(stderr, "Uso: %s [-m] [-n] [-t seg] [-c captura.ppm] ruta_rom SISTEMA nes|gb|gbc|gba\n", argv[0]);
        return 2;
    }
    ruta_rom = argv[optind];
    sistema = argv[optind + 1];
    tipo = argv[optind + 2];
    emu = !strcmp(tipo, "nes")    ? EMU_NES
          : !strcmp(tipo, "gba")  ? EMU_GBA
          : !strcmp(tipo, "snes") ? EMU_SNES
          : !strcmp(tipo, "ps1")  ? EMU_PS1
          : !strcmp(tipo, "gbc")  ? EMU_GBC
                                  : EMU_GB;
    acento = SIS_COLOR[SIS_GB];
    for (int s = 0; s < N_SIS; s++)
        if (!strcmp(SIS_NOMBRE[s], sistema)) acento = SIS_COLOR[s];

    snprintf(dir_rom, sizeof dir_rom, "%s", ruta_rom);
    char *barra = strrchr(dir_rom, '/');
    if (barra) *barra = 0;
    else snprintf(dir_rom, sizeof dir_rom, ".");
    snprintf(ruta_sav, sizeof ruta_sav, "%s", ruta_rom);
    char *punto = strrchr(ruta_sav, '.');
    if (punto && (!strrchr(ruta_sav, '/') || punto > strrchr(ruta_sav, '/'))) *punto = 0;
    snprintf(ruta_rtc, sizeof ruta_rtc, "%s.rtc", ruta_sav);
    strncat(ruta_sav, ".sav", sizeof ruta_sav - strlen(ruta_sav) - 1);
    snprintf(ruta_est, sizeof ruta_est, "%s.est", ruta_rom);

    signal(SIGINT, al_senal);
    signal(SIGTERM, al_senal);
    signal(SIGHUP, al_senal);

    cfg_cargar();
    if (access("/tmp/log_botones", F_OK) == 0) {
        log_botones = fopen("/tmp/botones.log", "a");
        if (log_botones) fprintf(log_botones, "== %s\n", ruta_rom);
    }
    plat_pantalla_abrir(false);
    plat_entrada_abrir();
    for (int i = 0; i < 20 && !plat_audio_listo(); i++) { // el ESP32 manda su estado cada 2 ms
        plat_botones();
        plat_dormir_ms(5);
    }
    preparar_opciones();
    if (!cargar_juego()) {
        fprintf(stderr, "ERROR no se pudo cargar %s\n", ruta_rom);
        return 2;
    }
    struct retro_system_av_info av;
    retro_get_system_av_info(&av);
    double fps = av.timing.fps > 1 ? av.timing.fps : 60;
    if (av.timing.sample_rate > 1000) tasa_nucleo = av.timing.sample_rate;
    printf("%s cargado (%dx%d, %.4f fps, audio %.0f Hz, salida de audio %s, RSS %ld KB)\n", ruta_rom, av.geometry.base_width,
           av.geometry.base_height, fps, tasa_nucleo, plat_audio_listo() ? "conectado" : "sin respuesta", rss_kb());

    pthread_t th;
    pthread_create(&th, NULL, hilo_video, NULL);

    const int64_t periodo = (int64_t)(1e9 / fps);
    int64_t limite_t = ahora_ns(), t_stat = limite_t, t_sav = limite_t, ns_emul = 0, ns_max = 0;
    const int64_t t_fin = limite > 0 ? limite_t + (int64_t)(limite * 1e9) : 0;
    unsigned long cuadros = 0, mostrados_ant = 0, tirones = 0;
    int64_t t_ant = limite_t;
    bool salir = false;

    while (!salir && !terminar) {
        if (pausa_pedida || plat_pausa_pedida()) {
            pausa_pedida = false;
            int r = menu_pausa();
            if (r == 2) {
                preguntar_gusto();
                salir = true;
                break;
            }
            if (r == 1) {
                guardar_mem(RETRO_MEMORY_SAVE_RAM, ruta_sav, true);
                retro_reset();
                pthread_mutex_lock(&mtx);
                limpiar = true;
                pthread_mutex_unlock(&mtx);
            }
            limite_t = ahora_ns();
            continue;
        }
        int64_t t0 = ahora_ns();
        informar_audio();
        retro_run();
        audio_fin_cuadro();
        int64_t dt = ahora_ns() - t0;
        ns_emul += dt;
        if (dt > ns_max) ns_max = dt;
        cuadros++;
        if (!medir) {
            limite_t += periodo;
            int64_t ahora = ahora_ns();
            if (ahora > limite_t + 5 * periodo) limite_t = ahora;
            else if (limite_t > ahora) {
                // Repetir: pcsx_rearmed usa señales que cortan la espera (EINTR) y sin esto corre al doble.
                struct timespec ts = {limite_t / 1000000000LL, limite_t % 1000000000LL};
                while (ahora_ns() < limite_t) clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
            }
        }
        int64_t ahora = ahora_ns();
        if (ahora - t_ant > 2 * periodo) tirones++; // un cuadro que tardó más de dos: se nota como freeze
        t_ant = ahora;
        if (ahora - t_stat >= 5000000000LL) {
            double s = (ahora - t_stat) / 1e9;
            pthread_mutex_lock(&mtx);
            unsigned long m = mostrados;
            pthread_mutex_unlock(&mtx);
            printf("emu %5.1f fps | pantalla %4.1f fps | cuadro prom %5.2f ms, max %5.2f ms | tirones %lu | audio nivel %d (x%.4f) | RSS %ld KB\n",
                   cuadros / s, (m - mostrados_ant) / s, ns_emul / 1e6 / (cuadros ? cuadros : 1), ns_max / 1e6,
                   tirones, plat_audio_nivel(), correccion, rss_kb());
            tirones = 0;
            fflush(stdout);
            t_stat = ahora;
            cuadros = 0;
            ns_emul = ns_max = 0;
            mostrados_ant = m;
        }
        if (ahora - t_sav >= 30000000000LL) {
            guardar_sav_fondo();
            t_sav = ahora;
        }
        if (t_fin && ahora >= t_fin) {
            video_esperar();
            break;
        }
    }

    pthread_mutex_lock(&mtx);
    video_salir = true;
    pthread_cond_signal(&cnd);
    pthread_mutex_unlock(&mtx);
    pthread_join(th, NULL);
    if (pantalla_captura && !salir) {
        static uint16_t todo[ANCHO * ALTO];
        memset(todo, 0, sizeof todo);
        for (int y = 0; y < out_h; y++) memcpy(&todo[(out_y + y) * ANCHO + out_x], &salida[y * out_w], out_w * 2);
        pantalla_guardar_ppm(todo, ANCHO, ALTO, pantalla_captura);
    }
    sonido_esperar(); // el "volver" de salir al menú
    plat_audio_vaciar();
    descargar_juego();
    plat_entrada_cerrar();
    return 0;
}
