// Fuerza GPIO1_C3 (RST) y GPIO1_C4 (DC) a función GPIO en la Luckfox Pico Mini (RV1103).
// El device tree de fábrica los deja como SPI0_MISO y UART4_RX, y exportarlos por sysfs NO cambia
// el mux. Registros GPIO1_IOC (0xFF538000): cada pin usa 4 bits; los 16 bits altos son máscara de
// escritura, así que se cambia solo el pin pedido.
#ifndef MUX_MINI_H
#define MUX_MINI_H

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#define GPIO1_IOC_BASE 0xFF538000u
#define GPIO1C_SEL_0 0x10 // C0..C3
#define GPIO1C_SEL_1 0x14 // C4..C7

// Devuelve 0 si quedó bien.
static int mux_pantalla_a_gpio(void) {
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) { perror("abrir /dev/mem"); return -1; }
    volatile uint32_t *ioc = (volatile uint32_t *)mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, GPIO1_IOC_BASE);
    close(fd);
    if (ioc == MAP_FAILED) { perror("mmap GPIO1_IOC"); return -1; }
    ioc[GPIO1C_SEL_0 / 4] = 0xF0000000u; // C3 (bits 15:12) = 0 -> GPIO
    ioc[GPIO1C_SEL_1 / 4] = 0x000F0000u; // C4 (bits 3:0)   = 0 -> GPIO
    uint32_t c3 = (ioc[GPIO1C_SEL_0 / 4] >> 12) & 0xF;
    uint32_t c4 = ioc[GPIO1C_SEL_1 / 4] & 0xF;
    munmap((void *)ioc, 4096);
    if (c3 || c4) {
        fprintf(stderr, "mux: C3=%u C4=%u (esperado 0)\n", c3, c4);
        return -1;
    }
    return 0;
}

#endif
