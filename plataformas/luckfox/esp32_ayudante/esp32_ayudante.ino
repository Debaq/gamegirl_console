// ESP32-S3 Super Mini (clon tipo XIAO): ayudante de audio y botones de la consola Luckfox.
//
// Luckfox --UART 1,5 Mbaud--> ESP32 --I2S--> MAX98357A --> parlante
//                          <--  botones + nivel del búfer cada 2 ms
//
// Protocolo (igual en los dos sentidos):  A5 5A  tipo  largo(LE16)  datos...  crc8
//   crc8 (polinomio 0x07) sobre tipo, largo y datos. Si se pierde un byte, se resincroniza
//   en la cabecera siguiente (los mensajes de arranque del ESP32 por estos pines se descartan).
// Luckfox -> ESP32:
//   0x01 AUDIO    muestras int16 LE, mono, 32768 Hz (hasta 1024 por paquete)
//   0x02 SILENCIO vacía el búfer (pausa, salir del juego)
//   0x03 VERSION  pide la versión (responde 0x82)
//   0x10 OTA_INICIO  tamaño (uint32), crc32 (uint32)      -> responde 0x90
//   0x11 OTA_DATOS   posición (uint32), hasta 1024 bytes   -> responde 0x90
//   0x12 OTA_FIN     (graba, verifica y reinicia)          -> responde 0x90 antes de reiniciar
// ESP32 -> Luckfox:
//   0x81 ESTADO   botones (uint16), nivel del búfer en muestras (uint16), vaciados (uint16)
//   0x82 VERSION  texto (fecha y hora de compilación)
//   0x90 OTA      código (uint8: 0 bien, 1 sin espacio, 2 posición fuera de orden, 3 error de escritura,
//                 4 crc distinto, 5 imagen inválida, 6 sin OTA iniciada), siguiente posición esperada (uint32)
//
// Actualización desde la Luckfox (OTA por esta misma UART): plataformas/luckfox/esp_actualizar.c, `make esp`.
// Se graba en la otra partición de la app y solo se cambia de partición si la imagen llegó entera y es válida;
// si la transferencia se corta, el ESP32 sigue con el firmware anterior.
//
// Botones a GND con pull-up interno; bits iguales a BT_* de gamegirl_console/nucleo/comun.h.
// Pines (ESP32-S3 Super Mini): ARRIBA 1, ABAJO 2, SELECT 3, IZQ 4, DER 5, A 6, B 10, L 11, R 12, START 13,
// X 14, Y 15 (pads de abajo). No usar 0 (BOOT), 19/20 (USB), 45/46 (arranque), 48 (LED) ni 26-32 (flash/PSRAM).
// Monitor USB (115200): estadísticas cada 5 s; 't' = tono de prueba, 'c'/'s' = tono continuo / parar.
//
// Compilar: arduino-cli compile -b esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=4M,PSRAM=enabled esp32_ayudante
#include <ESP_I2S.h>
#include <Update.h>
#include <atomic>
#include <esp_rom_crc.h>

static const char VERSION[] = "ayudante " __DATE__ " " __TIME__;

const int PIN_BCLK = 7, PIN_LRC = 8, PIN_DIN = 9;
const int PIN_RX = 44, PIN_TX = 43;
const long BAUDIOS = 1500000;
const int TASA = 32768;

struct Boton { const char *nombre; int pin; uint16_t bit; };
const Boton BOTONES[] = {
  {"B", 10, 1 << 0},     {"A", 6, 1 << 1},      {"SELECT", 3, 1 << 2}, {"START", 13, 1 << 3},
  {"ARRIBA", 1, 1 << 4}, {"ABAJO", 2, 1 << 5},  {"IZQ", 4, 1 << 6},    {"DER", 5, 1 << 7},
  {"L", 11, 1 << 8},     {"R", 12, 1 << 9},     {"X", 14, 1 << 10},    {"Y", 15, 1 << 11},
};
const int NBOTONES = sizeof(BOTONES) / sizeof(BOTONES[0]);

// ---------------------------------------------------------------- búfer de audio

const uint32_t CAP = 16384;      // 0,5 s
const uint32_t ARRANQUE = 1024;  // tras vaciarse, esperar ~31 ms de audio antes de volver a sonar
static int16_t anillo[CAP];
static std::atomic<uint32_t> escr{0}, lect{0};  // índices crecientes (un solo productor y un solo consumidor)
static std::atomic<uint32_t> vaciados{0};
static std::atomic<bool> pedir_vaciar{false};
static uint32_t descartadas = 0;

static void meter(const uint8_t *d, int bytes) {
  int n = bytes / 2;
  uint32_t e = escr.load(std::memory_order_relaxed);
  uint32_t l = lect.load(std::memory_order_acquire);
  for (int i = 0; i < n; i++) {
    if (e - l >= CAP) { descartadas += n - i; break; }  // lleno: el control de la Luckfox lo corrige
    anillo[e % CAP] = (int16_t)(d[2 * i] | (d[2 * i + 1] << 8));
    e++;
  }
  escr.store(e, std::memory_order_release);
}

// ---------------------------------------------------------------- tono de prueba (solo monitor USB)

volatile bool pedir_tono = false, continuo = false;
I2SClass i2s;

void tarea_audio(void *) {
  static int16_t est[2 * 256];
  bool esperando = true;
  int32_t ultimo = 0;
  float fase = 0;
  int tono = 0;
  for (;;) {
    if (pedir_vaciar.exchange(false)) {
      lect.store(escr.load(std::memory_order_acquire), std::memory_order_release);
      esperando = true;
    }
    if (pedir_tono) { pedir_tono = false; tono = TASA; }
    if (continuo && tono < TASA / 2) tono = TASA;
    uint32_t l = lect.load(std::memory_order_relaxed);
    uint32_t e = escr.load(std::memory_order_acquire);
    if (esperando && e - l >= ARRANQUE) esperando = false;
    for (int i = 0; i < 256; i++) {
      int32_t m;
      if (!esperando && l != e) {
        m = anillo[l % CAP];
        l++;
      } else {
        if (!esperando) { esperando = true; vaciados++; }
        m = ultimo - ultimo / 16;  // bajar a cero sin clic; nunca repetir datos viejos
      }
      ultimo = m;
      if (tono > 0) {
        m += (int32_t)(sinf(fase) * 12000);
        fase += 2 * PI * 440.0f / TASA;
        if (fase > 2 * PI) fase -= 2 * PI;
        tono--;
        if (m > 32767) m = 32767;
        if (m < -32768) m = -32768;
      }
      est[2 * i] = est[2 * i + 1] = (int16_t)m;  // mismo sonido a los dos canales
    }
    lect.store(l, std::memory_order_release);
    i2s.write((uint8_t *)est, sizeof est);  // bloquea: el I2S marca el ritmo real
  }
}

// ---------------------------------------------------------------- protocolo

static uint8_t crc8(uint8_t c, uint8_t b) {
  c ^= b;
  for (int i = 0; i < 8; i++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1);
  return c;
}

enum { E_CAB0, E_CAB1, E_TIPO, E_LARGO0, E_LARGO1, E_DATOS, E_CRC };
static int estado = E_CAB0;
static uint8_t tipo, crc;
static uint16_t largo, pos;
static uint8_t datos[2048 + 4];
static uint32_t paquetes = 0, errores_crc = 0;

static void enviar_paquete(uint8_t t, const uint8_t *d, uint16_t n) {
  uint8_t cab[5] = {0xA5, 0x5A, t, (uint8_t)n, (uint8_t)(n >> 8)};
  uint8_t c = 0;
  for (int i = 2; i < 5; i++) c = crc8(c, cab[i]);
  for (int i = 0; i < n; i++) c = crc8(c, d[i]);
  Serial1.write(cab, 5);
  if (n) Serial1.write(d, n);
  Serial1.write(&c, 1);
}

// ---------------------------------------------------------------- actualización (OTA)

static bool ota_activa = false;
static uint32_t ota_tam = 0, ota_pos = 0, ota_crc_esperado = 0, ota_crc = 0;
static bool reiniciar = false;

static uint32_t leer32(const uint8_t *d) { return d[0] | d[1] << 8 | d[2] << 16 | (uint32_t)d[3] << 24; }

static void ota_responder(uint8_t codigo) {
  uint8_t r[5] = {codigo, (uint8_t)ota_pos, (uint8_t)(ota_pos >> 8), (uint8_t)(ota_pos >> 16), (uint8_t)(ota_pos >> 24)};
  enviar_paquete(0x90, r, sizeof r);
}

static void ota_paquete(uint8_t t, const uint8_t *d, uint16_t n) {
  if (t == 0x10 && n >= 8) {
    if (ota_activa) Update.abort();
    ota_tam = leer32(d);
    ota_crc_esperado = leer32(d + 4);
    ota_pos = ota_crc = 0;
    ota_activa = Update.begin(ota_tam);
    Serial.printf("OTA: inicio %lu bytes -> %s\n", (unsigned long)ota_tam, ota_activa ? "ok" : Update.errorString());
    ota_responder(ota_activa ? 0 : 1);
  } else if (t == 0x11 && n >= 4) {
    if (!ota_activa) { ota_responder(6); return; }
    uint32_t p = leer32(d);
    uint16_t k = n - 4;
    if (p != ota_pos) { ota_responder(p < ota_pos ? 0 : 2); return; }  // repetido (se perdió la respuesta): seguir
    if (Update.write((uint8_t *)d + 4, k) != k) {
      Serial.printf("OTA: error al escribir en %lu: %s\n", (unsigned long)p, Update.errorString());
      Update.abort();
      ota_activa = false;
      ota_responder(3);
      return;
    }
    ota_crc = esp_rom_crc32_le(ota_crc, d + 4, k);
    ota_pos += k;
    ota_responder(0);
  } else if (t == 0x12) {
    if (!ota_activa) { ota_responder(6); return; }
    ota_activa = false;
    if (ota_pos != ota_tam || ota_crc != ota_crc_esperado) {
      Serial.printf("OTA: crc %08lx, esperado %08lx\n", (unsigned long)ota_crc, (unsigned long)ota_crc_esperado);
      Update.abort();
      ota_responder(4);
      return;
    }
    bool ok = Update.end(true);  // verifica la imagen y la marca para arrancar
    Serial.printf("OTA: fin -> %s\n", ok ? "ok, reiniciando" : Update.errorString());
    ota_responder(ok ? 0 : 5);
    if (ok) reiniciar = true;
  }
}

static void paquete(uint8_t t, const uint8_t *d, uint16_t n) {
  paquetes++;
  if (t == 0x01) meter(d, n);
  else if (t == 0x02) pedir_vaciar = true;
  else if (t == 0x03) enviar_paquete(0x82, (const uint8_t *)VERSION, sizeof VERSION - 1);
  else if (t >= 0x10 && t <= 0x12) ota_paquete(t, d, n);
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
    if (b == crc) paquete(tipo, datos, largo);
    else errores_crc++;
    estado = E_CAB0;
    break;
  }
}

static void enviar_estado(uint16_t botones) {
  uint32_t n = escr.load() - lect.load();
  uint16_t nivel = n > 65535 ? 65535 : n, vac = (uint16_t)vaciados.load();
  uint8_t p[12] = {0xA5, 0x5A, 0x81, 6, 0, (uint8_t)botones, (uint8_t)(botones >> 8),
                   (uint8_t)nivel, (uint8_t)(nivel >> 8), (uint8_t)vac, (uint8_t)(vac >> 8), 0};
  uint8_t c = 0;
  for (int i = 2; i < 11; i++) c = crc8(c, p[i]);
  p[11] = c;
  Serial1.write(p, sizeof p);
}

// ---------------------------------------------------------------- botones con antirrebote (8 ms)

static uint16_t estable = 0, crudo_ant = 0;
static uint8_t cuenta = 0;

static uint16_t leer_botones() {
  uint16_t m = 0;
  for (int i = 0; i < NBOTONES; i++)
    if (digitalRead(BOTONES[i].pin) == LOW) m |= BOTONES[i].bit;
  return m;
}

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < NBOTONES; i++) pinMode(BOTONES[i].pin, INPUT_PULLUP);
  Serial1.setRxBufferSize(16384);
  Serial1.setTxBufferSize(1024);
  Serial1.begin(BAUDIOS, SERIAL_8N1, PIN_RX, PIN_TX);
  i2s.setPins(PIN_BCLK, PIN_LRC, PIN_DIN);
  if (!i2s.begin(I2S_MODE_STD, TASA, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO))
    Serial.println("ERROR: no se pudo iniciar I2S");
  xTaskCreatePinnedToCore(tarea_audio, "audio", 4096, nullptr, 10, nullptr, 1);
  Serial.printf("ESP32 ayudante listo (%s): UART1 1,5 Mbaud (RX 44, TX 43), I2S 32768 Hz (7, 8, 9)\n", VERSION);
}

uint32_t t_boton = 0, t_estado = 0, t_info = 0;
uint16_t botones_ant = 0;

void loop() {
  static uint8_t buf[1024];
  int n = Serial1.available();
  if (n > 0) {
    n = Serial1.readBytes(buf, n > (int)sizeof buf ? (int)sizeof buf : n);
    for (int i = 0; i < n; i++) recibir(buf[i]);
  }

  if (reiniciar) {
    Serial1.flush();  // que la respuesta llegue antes de reiniciar
    delay(50);
    ESP.restart();
  }

  uint32_t ahora = micros();
  if (ahora - t_boton >= 1000) {  // muestreo cada 1 ms; estable tras 8 lecturas iguales
    t_boton = ahora;
    uint16_t c = leer_botones();
    if (c == crudo_ant) {
      if (cuenta < 8 && ++cuenta == 8) estable = c;
    } else {
      cuenta = 0;
      crudo_ant = c;
    }
  }
  if (ahora - t_estado >= 2000) {
    t_estado = ahora;
    enviar_estado(estable);
  }

  while (Serial.available()) {
    char c = Serial.read();
    if (c == 't') pedir_tono = true;
    if (c == 'c') continuo = true;
    if (c == 's') continuo = false;
  }
  if (estable != botones_ant) {
    for (int i = 0; i < NBOTONES; i++)
      if ((estable ^ botones_ant) & BOTONES[i].bit)
        Serial.printf("%s %s\n", BOTONES[i].nombre, (estable & BOTONES[i].bit) ? "APRETADO" : "suelto");
    botones_ant = estable;
  }
  if (millis() - t_info >= 5000) {
    t_info = millis();
    Serial.printf("paquetes %lu, errores crc %lu, nivel %lu, vaciados %lu, descartadas %lu\n", paquetes,
                  errores_crc, (unsigned long)(escr.load() - lect.load()), (unsigned long)vaciados.load(),
                  descartadas);
  }
}
