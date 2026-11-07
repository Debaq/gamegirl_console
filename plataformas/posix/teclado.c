#include "teclado.h"

#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "comun.h"
#include "plataforma.h"

// Una tecla de terminal no avisa cuando se suelta: cada pulsación "mantiene" el botón un rato.
// La repetición automática del terminal (cada ~33 ms tras ~500 ms) lo mantiene apretado.
#define MANTENER_MS 140

// También se leen teclas de una FIFO, para manejar la consola sin terminal (menú lanzado al arrancar):
//   adb shell 'printf j > /tmp/consola_teclas'
#define FIFO_TECLAS "/tmp/consola_teclas"
static int fifo_fd = -1, fifo_escritor = -1;

static struct termios orig;
static bool raw = false, abierta = false, pausa_pedida = false;
static uint32_t hasta[16];

void teclado_cerrar(void) {
    if (raw) tcsetattr(0, TCSANOW, &orig);
    raw = false;
    if (fifo_fd >= 0) close(fifo_fd);
    if (fifo_escritor >= 0) close(fifo_escritor);
    fifo_fd = fifo_escritor = -1;
    abierta = false;
}

void teclado_abrir(void) {
    if (abierta) return;
    abierta = true;
    mkfifo(FIFO_TECLAS, 0666);
    fifo_fd = open(FIFO_TECLAS, O_RDONLY | O_NONBLOCK);
    fifo_escritor = open(FIFO_TECLAS, O_WRONLY | O_NONBLOCK); // evita EOF cuando no hay quien escriba
    fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
    static bool atexit_puesto = false;
    if (!atexit_puesto) { atexit(teclado_cerrar); atexit_puesto = true; }
    if (isatty(0) && tcgetattr(0, &orig) == 0) {
        struct termios t = orig;
        t.c_lflag &= ~(ICANON | ECHO); // ISIG queda: Ctrl-C sigue funcionando
        t.c_cc[VMIN] = 0;
        t.c_cc[VTIME] = 0;
        tcsetattr(0, TCSANOW, &t);
        raw = true;
    }
}

static void apretar(uint16_t m) {
    uint32_t t = plat_ms() + MANTENER_MS;
    for (int i = 0; i < 16; i++)
        if (m & (1u << i)) hasta[i] = t;
}

static void procesar(const unsigned char *b, ssize_t n);

static void leer_teclas(void) {
    unsigned char b[64];
    ssize_t n = read(0, b, sizeof b);
    if (n > 0) procesar(b, n);
    if (fifo_fd >= 0 && (n = read(fifo_fd, b, sizeof b)) > 0) procesar(b, n);
}

static void procesar(const unsigned char *b, ssize_t n) {
    for (ssize_t i = 0; i < n; i++) {
        unsigned char c = b[i];
        if (c == 27) {
            if (i + 2 < n && b[i + 1] == '[') {
                unsigned char k = b[i + 2];
                if (k == 'A') apretar(BT_ARRIBA);
                else if (k == 'B') apretar(BT_ABAJO);
                else if (k == 'C') apretar(BT_DER);
                else if (k == 'D') apretar(BT_IZQ);
                else if ((k == '5' || k == '6') && i + 3 < n && b[i + 3] == '~') {
                    apretar(BT_B | (k == '5' ? BT_ARRIBA : BT_ABAJO));
                    i++;
                }
                i += 2;
            } else {
                pausa_pedida = true; // Esc solo
            }
            continue;
        }
        switch (c) {
        case 'w': case 'W': apretar(BT_ARRIBA); break;
        case 's': case 'S': apretar(BT_ABAJO); break;
        case 'a': case 'A': apretar(BT_IZQ); break;
        case 'd': case 'D': apretar(BT_DER); break;
        case 'j': case 'J': case 'z': case 'Z': apretar(BT_A); break;
        case 'k': case 'K': case 'x': case 'X': apretar(BT_B); break;
        case 'n': case 'N': case ' ': apretar(BT_SELECT); break;
        case 'm': case 'M': case '\n': case '\r': apretar(BT_START); break;
        case 'u': case 'U': apretar(BT_L); break;
        case 'i': case 'I': apretar(BT_R); break;
        case 'o': case 'O': apretar(BT_X); break;
        case 'l': case 'L': apretar(BT_Y); break;
        case 'p': case 'P': pausa_pedida = true; break;
        }
    }
}

uint16_t teclado_leer(void) {
    if (!abierta) teclado_abrir();
    leer_teclas();
    uint32_t t = plat_ms();
    uint16_t m = 0;
    for (int i = 0; i < 16; i++)
        if ((int32_t)(hasta[i] - t) > 0) m |= 1u << i;
    return m;
}

bool teclado_pausa(void) {
    if (!abierta) teclado_abrir();
    leer_teclas();
    bool p = pausa_pedida;
    pausa_pedida = false;
    return p;
}
