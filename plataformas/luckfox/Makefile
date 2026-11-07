# Plataforma Luckfox Pico Mini (RV1103, uClibc) + ESP32-S3 (audio y botones).
#   make            -> build/menu y build/emu_* (los de los núcleos que estén compilados)
#   make subir      -> copia a /oem/consola de la placa por adb
#   make instalar   -> subir + arranque automático (/etc/init.d/S95consola)
# Los núcleos libretro (código de terceros) se compilan en el proyecto de la placa:
#   ../luckfox_consola/consola/compilar_nucleos.sh
UI      := ../..
LUCKFOX ?= $(abspath ../../../luckfox_consola)
NUC     ?= $(LUCKFOX)/consola
TC      ?= $(LUCKFOX)/sdk/luckfox-pico/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf/bin/arm-rockchip830-linux-uclibcgnueabihf
CC      := $(TC)-gcc
CPU     := -marm -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard
CFLAGS  := $(CPU) -O2 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation \
           -I $(UI)/nucleo -I . -I ../posix -I $(NUC)/gpsp/libretro/libretro-common/include
LDLIBS  := -static -s -lstdc++ -lpthread -lm
B       := build

NUCLEO  := $(addprefix $(UI)/nucleo/,dib.c json.c cfg.c sonido.c pantalla.c pausa.c)
PLAT    := plat_luckfox.c lcd.c esp.c ../posix/teclado.c
HDRS    := $(wildcard $(UI)/nucleo/*.h) $(wildcard *.h) $(wildcard ../posix/*.h)

# FCEUmm, Gambatte y snes9x estáticos no traen libretro-common (lo pone RetroArch): se reusan los
# objetos que ya compiló gpSP.
LRC_OBJS := $(wildcard $(NUC)/gpsp/libretro/libretro-common/*/*.o)

BINS := $(B)/menu
ifneq ($(wildcard $(NUC)/gpsp/libgpsp.a),)
BINS += $(B)/emu_gba
endif
ifneq ($(wildcard $(NUC)/fceumm/libfceumm.a),)
BINS += $(B)/emu_nes
endif
ifneq ($(wildcard $(NUC)/gambatte/libgambatte.a),)
BINS += $(B)/emu_gb
endif
ifneq ($(wildcard $(NUC)/snes9x2005/libsnes.a),)
BINS += $(B)/emu_snes
endif
ifneq ($(wildcard $(NUC)/pcsx_rearmed/libpcsx.a),)
BINS += $(B)/emu_ps1
endif

all: $(BINS)

$(B)/libretro_comun.a: $(LRC_OBJS)
	@mkdir -p $(B)
	$(TC)-ar rcs $@ $(LRC_OBJS)

$(B)/menu: main_menu.c $(UI)/nucleo/menu.c $(NUCLEO) $(PLAT) $(HDRS)
	@mkdir -p $(B)
	$(CC) $(CFLAGS) -o $@ main_menu.c $(UI)/nucleo/menu.c $(NUCLEO) $(PLAT) $(LDLIBS)

# emu_<x>: el frontend libretro (../posix/emu.c) con el núcleo correspondiente
define EMU
$(B)/emu_$(1): ../posix/emu.c $(NUCLEO) $(PLAT) $(HDRS) $(2) $(B)/libretro_comun.a
	@mkdir -p $(B)
	$(CC) $(CFLAGS) -o $$@ ../posix/emu.c $(NUCLEO) $(PLAT) $(2) $(B)/libretro_comun.a $(LDLIBS)
endef
$(eval $(call EMU,gba,$(NUC)/gpsp/libgpsp.a))
$(eval $(call EMU,nes,$(NUC)/fceumm/libfceumm.a))
$(eval $(call EMU,gb,$(NUC)/gambatte/libgambatte.a))
$(eval $(call EMU,snes,$(NUC)/snes9x2005/libsnes.a))

# PS1 (experimento): PCSX-ReARMed trae su propio libretro-common (no se le agrega libretro_comun.a), salvo
# dos piezas que su lector de CD usa y su Makefile no incluye en la biblioteca estática.
PCSX_LRC := $(NUC)/pcsx_rearmed/deps/libretro-common
PCSX_EXTRA := $(PCSX_LRC)/lists/dir_list.c $(PCSX_LRC)/compat/compat_strcasestr.c
$(B)/emu_ps1: ../posix/emu.c $(NUCLEO) $(PLAT) $(HDRS) $(NUC)/pcsx_rearmed/libpcsx.a
	@mkdir -p $(B)
	$(CC) -I $(PCSX_LRC)/include $(CFLAGS) -o $@ ../posix/emu.c $(NUCLEO) $(PLAT) $(PCSX_EXTRA) \
		$(NUC)/pcsx_rearmed/libpcsx.a $(LDLIBS)

# Firmware del ESP32 ayudante por OTA desde la Luckfox (el ESP32 ya debe tener un firmware con OTA;
# la primera vez se carga por USB: make esp_usb PUERTO=/dev/ttyACM0).
FQBN    := esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=4M,PSRAM=enabled
ESP_BIN := $(B)/esp32/esp32_ayudante.ino.bin
PUERTO  ?= /dev/ttyACM0

$(B)/esp_actualizar: esp_actualizar.c esp.c esp.h
	@mkdir -p $(B)
	$(CC) $(CFLAGS) -o $@ esp_actualizar.c esp.c $(LDLIBS)

$(ESP_BIN): esp32_ayudante/esp32_ayudante.ino
	arduino-cli compile -b $(FQBN) --output-dir $(B)/esp32 esp32_ayudante

esp: $(B)/esp_actualizar $(ESP_BIN)
	adb push $(B)/esp_actualizar $(ESP_BIN) /tmp/
	adb shell 'chmod +x /tmp/esp_actualizar; /etc/init.d/S95consola stop; sleep 1; \
		/tmp/esp_actualizar /tmp/esp32_ayudante.ino.bin; r=$$?; rm -f /tmp/esp32_ayudante.ino.bin; exit $$r'; \
		r=$$?; timeout 5 adb shell /etc/init.d/S95consola start; exit $$r

esp_version: $(B)/esp_actualizar
	adb push $(B)/esp_actualizar /tmp/
	adb shell 'chmod +x /tmp/esp_actualizar; /etc/init.d/S95consola stop; sleep 1; /tmp/esp_actualizar -v'; \
		timeout 5 adb shell /etc/init.d/S95consola start; true

esp_usb: $(ESP_BIN)
	arduino-cli upload -b $(FQBN) -p $(PUERTO) --input-dir $(B)/esp32 esp32_ayudante

subir: all
	adb shell 'rm -rf /root/consola /root/roms /root/jugar.sh; mkdir -p /oem/consola'
	adb push $(BINS) /oem/consola/

instalar: subir
	adb push S95consola /etc/init.d/S95consola
	adb shell 'chmod +x /etc/init.d/S95consola; sync'

clean:
	rm -rf $(B)

.PHONY: all subir instalar clean esp esp_version esp_usb
