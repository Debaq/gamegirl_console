# Plataforma PC (SDL2): la interfaz y los juegos en una ventana, para probar sin placa.
#   make          -> build/menu y build/emu_* (los de los núcleos compilados: ./compilar_nucleos.sh)
#   make correr   -> abre la ventana con sd_prueba/ (todas las consolas; SD=... para otra carpeta)
#   make sd       -> (re)arma sd_prueba/ con enlaces a las ROMs del proyecto (armar_sd.sh)
#   make paquete  -> dist/GameGirlColor-linux.tar.gz: carpeta portable (sin ROMs ni BIOS), ver paquete.sh
# Teclas (ventana): flechas = cruz, A, B (o S), X, Y, L, R = su botón, Enter = START, Espacio = SELECT,
#         Esc = pausa; B + arriba/abajo = saltar de letra.
UI     := ../..
CC     := gcc
CFLAGS := -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation \
          -I $(UI)/nucleo -I ../posix -I $(abspath ../../../luckfox_consola/consola/gpsp/libretro/libretro-common/include) \
          $(shell sdl2-config --cflags)
LDLIBS := $(shell sdl2-config --libs) -lm -lpthread
NUC    := $(abspath nucleos)
B      := build
NUCLEO := $(addprefix $(UI)/nucleo/,menu.c dib.c json.c cfg.c sonido.c pantalla.c pausa.c)
PLAT   := plat_pc.c ../posix/teclado.c
HDRS   := $(wildcard $(UI)/nucleo/*.h) $(wildcard ../posix/*.h)
SD     ?= $(abspath sd_prueba)

BINS := $(B)/menu
ifneq ($(wildcard $(NUC)/gpsp/gpsp_libretro.so),)
BINS += $(B)/emu_gba
endif
ifneq ($(wildcard $(NUC)/fceumm/fceumm_libretro.so),)
BINS += $(B)/emu_nes
endif
ifneq ($(wildcard $(NUC)/gambatte/gambatte_libretro.so),)
BINS += $(B)/emu_gb
endif
ifneq ($(wildcard $(NUC)/snes9x2005/snes9x2005_libretro.so),)
BINS += $(B)/emu_snes
endif
ifneq ($(wildcard $(NUC)/pcsx_rearmed/pcsx_rearmed_libretro.so),)
BINS += $(B)/emu_ps1
endif

all: $(BINS)

$(B)/menu: main_menu.c $(NUCLEO) $(PLAT) $(HDRS)
	@mkdir -p $(B)
	$(CC) $(CFLAGS) -o $@ main_menu.c $(NUCLEO) $(PLAT) $(LDLIBS)

# emu_<x>: el frontend libretro de la placa (../posix/emu.c) con el núcleo .so (ruta absoluta en el binario)
NUCLEO_EMU := $(filter-out $(UI)/nucleo/menu.c,$(NUCLEO))
define EMU
$(B)/emu_$(1): ../posix/emu.c $(NUCLEO_EMU) $(PLAT) $(HDRS) $(2)
	@mkdir -p $(B)
	$(CC) $(CFLAGS) -o $$@ ../posix/emu.c $(NUCLEO_EMU) $(PLAT) $(2) -Wl,-rpath,$(dir $(2)) $(LDLIBS)
endef
$(eval $(call EMU,gba,$(NUC)/gpsp/gpsp_libretro.so))
$(eval $(call EMU,nes,$(NUC)/fceumm/fceumm_libretro.so))
$(eval $(call EMU,gb,$(NUC)/gambatte/gambatte_libretro.so))
$(eval $(call EMU,snes,$(NUC)/snes9x2005/snes9x2005_libretro.so))
$(eval $(call EMU,ps1,$(NUC)/pcsx_rearmed/pcsx_rearmed_libretro.so))

# Versión portable: los emuladores buscan su núcleo en lib/ junto a ellos ($ORIGIN), no en este disco.
DIST := dist/GameGirlColor
define EMU_DIST
$(DIST)/emu_$(1): ../posix/emu.c $(NUCLEO_EMU) $(PLAT) $(HDRS) $(2)
	@mkdir -p $(DIST)/lib
	cp $(2) $(DIST)/lib/
	$(CC) $(CFLAGS) -o $$@ ../posix/emu.c $(NUCLEO_EMU) $(PLAT) -L$(DIST)/lib -l:$(notdir $(2)) \
		-Wl,-rpath,'$$$$ORIGIN/lib' $(LDLIBS)
endef
$(eval $(call EMU_DIST,gba,$(NUC)/gpsp/gpsp_libretro.so))
$(eval $(call EMU_DIST,nes,$(NUC)/fceumm/fceumm_libretro.so))
$(eval $(call EMU_DIST,gb,$(NUC)/gambatte/gambatte_libretro.so))
$(eval $(call EMU_DIST,snes,$(NUC)/snes9x2005/snes9x2005_libretro.so))
$(eval $(call EMU_DIST,ps1,$(NUC)/pcsx_rearmed/pcsx_rearmed_libretro.so))

$(DIST)/menu: main_menu.c $(NUCLEO) $(PLAT) $(HDRS)
	@mkdir -p $(DIST)
	$(CC) $(CFLAGS) -o $@ main_menu.c $(NUCLEO) $(PLAT) $(LDLIBS)

paquete: $(DIST)/menu $(patsubst $(B)/%,$(DIST)/%,$(filter $(B)/emu_%,$(BINS)))
	./paquete.sh $(DIST)

correr: all
	@[ -d sd_prueba ] || ./armar_sd.sh
	CONSOLA_SD=$(SD) ./$(B)/menu

sd:
	./armar_sd.sh

clean:
	rm -rf $(B)

.PHONY: all correr sd clean paquete
