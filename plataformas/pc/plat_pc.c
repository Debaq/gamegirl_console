// Plataforma PC (Linux) con SDL2: ventana de 480x320 (ampliada), teclado y sonido.
// La interfaz y los juegos: el menú lanza build/emu_<x> (plataformas/posix/emu.c con el núcleo libretro
// compilado para el PC, ver compilar_nucleos.sh), que abre su propia ventana en el mismo lugar.
//
// Variables de entorno:
//   CONSOLA_SD      carpeta con roms/, sistema/ y consola.json (por defecto ./sd)
//   CONSOLA_ESCALA  ampliación de la ventana (por defecto 2)
//   CONSOLA_VENTANA "x,y" posición de la ventana (el menú se la pasa al emulador)
//   SDL_VIDEODRIVER=dummy para correr sin ventana (pruebas; teclas por la entrada estándar o
//                   la FIFO /tmp/consola_teclas, ver plataformas/posix/teclado.h)
#include <SDL.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <unistd.h>

#include "plataforma.h"
#include "teclado.h"

static SDL_Window *ventana = NULL;
static SDL_Renderer *rend = NULL;
static SDL_Texture *tex = NULL;
static uint16_t fb[ANCHO * ALTO];
// El emulador envía cuadros desde su hilo de video, pero SDL solo dibuja desde el hilo principal:
// el otro hilo copia a fb y deja 'sucio'; el principal presenta en eventos() (cada lectura de botones).
static pthread_mutex_t fb_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_t hilo_principal;
static volatile bool sucio = false;
static SDL_AudioDeviceID audio = 0;
static uint16_t teclas = 0;
static bool pausa = false;

// ---------------------------------------------------------------- eventos y teclado

// Teclas (pedido del usuario: claras): cada botón con la tecla de su mismo nombre, así el pie de pantalla
// ("A Jugar", "Y Ficha") dice directamente qué tecla apretar. S también es B (al lado de la A, como en los
// emuladores). Flechas = cruz, Enter = START, Espacio = SELECT, Esc = pausa del juego.
#define AYUDA_TECLAS "Flechas: cruz  |  A  B(o S)  X  Y  L  R: botones  |  Enter: START  |  Espacio: SELECT  |  Esc: pausa"

static uint16_t boton_de(SDL_Keycode k) {
    switch (k) {
    case SDLK_UP: return BT_ARRIBA;
    case SDLK_DOWN: return BT_ABAJO;
    case SDLK_LEFT: return BT_IZQ;
    case SDLK_RIGHT: return BT_DER;
    case SDLK_a: return BT_A;
    case SDLK_b: case SDLK_s: return BT_B;
    case SDLK_x: return BT_X;
    case SDLK_y: return BT_Y;
    case SDLK_l: return BT_L;
    case SDLK_r: return BT_R;
    case SDLK_SPACE: return BT_SELECT;
    case SDLK_RETURN: case SDLK_KP_ENTER: return BT_START;
    default: return 0;
    }
}

static void presentar(void);

static void eventos(void) {
    if (!ventana || !pthread_equal(pthread_self(), hilo_principal)) return;
    if (sucio) presentar();
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        // Cerrar la ventana = SIGTERM: el emulador lo atrapa y guarda la partida antes de salir.
        if (e.type == SDL_QUIT) raise(SIGTERM);
        if (e.type == SDL_KEYDOWN && !e.key.repeat) {
            if (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_p) pausa = true;
            teclas |= boton_de(e.key.keysym.sym);
        } else if (e.type == SDL_KEYUP) {
            teclas &= (uint16_t)~boton_de(e.key.keysym.sym);
        }
    }
}

// ---------------------------------------------------------------- pantalla

static void presentar(void) {
    if (!tex) return;
    pthread_mutex_lock(&fb_mtx);
    SDL_UpdateTexture(tex, NULL, fb, ANCHO * 2);
    sucio = false;
    pthread_mutex_unlock(&fb_mtx);
    SDL_RenderClear(rend);
    SDL_RenderCopy(rend, tex, NULL, NULL);
    SDL_RenderPresent(rend);
}

void plat_pantalla_abrir(bool reiniciar) {
    if (ventana) return;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "ERROR SDL: %s\n", SDL_GetError());
        exit(3);
    }
    hilo_principal = pthread_self();
    int esc = getenv("CONSOLA_ESCALA") ? atoi(getenv("CONSOLA_ESCALA")) : 2;
    if (esc < 1) esc = 1;
    int vx = SDL_WINDOWPOS_CENTERED, vy = SDL_WINDOWPOS_CENTERED;
    const char *pos = getenv("CONSOLA_VENTANA");
    if (pos && sscanf(pos, "%d,%d", &vx, &vy) != 2) vx = vy = SDL_WINDOWPOS_CENTERED;
    ventana = SDL_CreateWindow("Game Girl Color   -   " AYUDA_TECLAS, vx, vy, ANCHO * esc, ALTO * esc, 0);
    if (!ventana) { fprintf(stderr, "ERROR ventana: %s\n", SDL_GetError()); exit(3); }
    rend = SDL_CreateRenderer(ventana, -1, 0);
    SDL_RenderSetLogicalSize(rend, ANCHO, ALTO);
    tex = SDL_CreateTexture(rend, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, ANCHO, ALTO);
    if (reiniciar) memset(fb, 0, sizeof fb);
    presentar();
}

void plat_pantalla_cerrar(void) {}

void plat_pantalla_enviar(const uint16_t *px, int x, int y, int w, int h, int paso) {
    pthread_mutex_lock(&fb_mtx);
    for (int j = 0; j < h; j++)
        if (y + j >= 0 && y + j < ALTO)
            for (int i = 0; i < w; i++)
                if (x + i >= 0 && x + i < ANCHO) fb[(y + j) * ANCHO + x + i] = px[(long)j * paso + i];
    sucio = true;
    pthread_mutex_unlock(&fb_mtx);
    eventos(); // en el hilo principal presenta ya; desde el hilo de video, en la próxima lectura de botones
}

// ---------------------------------------------------------------- botones y tiempo

void plat_entrada_abrir(void) { teclado_abrir(); }
void plat_entrada_cerrar(void) { teclado_cerrar(); }

uint16_t plat_botones(void) {
    eventos();
    return teclas | teclado_leer();
}

bool plat_pausa_pedida(void) {
    eventos();
    bool p = pausa || teclado_pausa();
    pausa = false;
    return p;
}

uint32_t plat_ms(void) { return SDL_GetTicks(); }

void plat_dormir_ms(int ms) {
    eventos();
    if (ms > 0) SDL_Delay((Uint32)ms);
}

// ---------------------------------------------------------------- audio

static void abrir_audio(void) {
    static bool intentado = false;
    if (intentado) return;
    intentado = true;
    SDL_AudioSpec quiero = {.freq = AUDIO_TASA, .format = AUDIO_S16SYS, .channels = 1, .samples = 512};
    audio = SDL_OpenAudioDevice(NULL, 0, &quiero, NULL, 0);
    if (audio) SDL_PauseAudioDevice(audio, 0);
}

bool plat_audio_listo(void) {
    abrir_audio();
    return audio != 0;
}

void plat_audio_enviar(const int16_t *m, int n) {
    if (audio) SDL_QueueAudio(audio, m, (Uint32)n * 2);
}

int plat_audio_nivel(void) { return audio ? (int)(SDL_GetQueuedAudioSize(audio) / 2) : 0; }

void plat_audio_vaciar(void) {
    if (audio) SDL_ClearQueuedAudio(audio);
}

// ---------------------------------------------------------------- almacenamiento y juegos

const char *plat_raiz_sd(void) {
    const char *r = getenv("CONSOLA_SD");
    return r && *r ? r : "sd";
}

// Los emuladores van junto al menú (build/emu_*). Sin núcleo compilado la pestaña igual se muestra,
// para revisar la interfaz completa, y al jugar avisa qué falta.
static const char *binario(emulador_t e, char *out, size_t n) {
    static const char *const nombre[N_EMU] = {"emu_nes", "emu_gb", "emu_gb", "emu_gba", "emu_snes", "emu_ps1"};
    char exe[512];
    ssize_t k = readlink("/proc/self/exe", exe, sizeof exe - 1);
    exe[k > 0 ? k : 0] = 0;
    char *barra = strrchr(exe, '/');
    if (barra) *barra = 0;
    snprintf(out, n, "%s/%s", k > 0 ? exe : ".", nombre[e]);
    return nombre[e];
}

bool plat_emulador_disponible(emulador_t e) {
    (void)e;
    return true;
}

bool plat_jugar(const char *ruta, const char *sistema, emulador_t e, char *aviso, size_t n) {
    static const char *const tipo[N_EMU] = {"nes", "gb", "gbc", "gba", "snes", "ps1"};
    char bin[600];
    const char *nombre = binario(e, bin, sizeof bin);
    aviso[0] = 0;
    if (access(bin, X_OK) != 0) {
        snprintf(aviso, n, "falta %s: correr plataformas/pc/compilar_nucleos.sh", nombre);
        return false;
    }
    // El emulador abre su propia ventana donde está la del menú; el menú se esconde mientras.
    char pos[32] = "";
    if (ventana) {
        int vx, vy;
        SDL_GetWindowPosition(ventana, &vx, &vy);
        snprintf(pos, sizeof pos, "%d,%d", vx, vy);
        SDL_HideWindow(ventana);
        SDL_PumpEvents();
    }
    plat_audio_vaciar();
    plat_entrada_cerrar();
    fflush(stdout);
    char err[64];
    snprintf(err, sizeof err, "/tmp/consola_emu_error_%d.txt", (int)getpid());
    pid_t pid = fork();
    if (pid == 0) {
        int fd = open(err, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) { dup2(fd, 2); close(fd); }
        if (*pos) setenv("CONSOLA_VENTANA", pos, 1);
        execl(bin, bin, ruta, sistema, tipo[e], (char *)NULL);
        fprintf(stderr, "ERROR no se pudo ejecutar %s: %s\n", bin, strerror(errno));
        _exit(127);
    }
    int st = 0;
    if (pid > 0) while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    plat_entrada_abrir();
    teclas = 0; // lo soltado mientras jugaba no llegó a esta ventana
    pausa = false;
    if (ventana) {
        SDL_ShowWindow(ventana);
        SDL_RaiseWindow(ventana);
        SDL_FlushEvents(SDL_KEYDOWN, SDL_KEYUP);
        presentar();
    }
    if (pid < 0) snprintf(aviso, n, "fork: %s", strerror(errno));
    else if (WIFSIGNALED(st)) snprintf(aviso, n, "el emulador se cerro (senal %d)", WTERMSIG(st));
    else if (WEXITSTATUS(st) != 0) {
        FILE *f = fopen(err, "r");
        char l[200];
        while (f && fgets(l, sizeof l, f))
            if (strstr(l, "ERROR")) {
                l[strcspn(l, "\n")] = 0;
                snprintf(aviso, n, "%s", l);
            }
        if (f) fclose(f);
        if (!*aviso) snprintf(aviso, n, "codigo de salida %d", WEXITSTATUS(st));
    }
    unlink(err);
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
    snprintf(out, n, "PC (SDL) - disco libre %ld MB", libre);
}
