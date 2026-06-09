#include "Adafruit_GFX.h"
#include "Adafruit_ILI9341.h"
#include "mbedtls/md.h"
#include "time.h"
#include <BleMouse.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <SPI.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

// ── Pines TFT ──
#define TFT_CS 15
#define TFT_RST 2
#define TFT_DC 4
#define TFT_MOSI 16
#define TFT_SCK 17
#define TFT_LED 5

// ── Pines Joystick ──
#define JOY_X 34
#define JOY_Y 35
#define JOY_BTN 26

// ── Pines de Navegación y Energía ──
#define BTN_NAV 25
#define PIN_BAT 36

// ── Umbrales joystick ──
#define JOY_THRESHOLD_HIGH 3000
#define JOY_THRESHOLD_LOW 1000
#define DEBOUNCE_MS 300

// ---- CREDENCIALES CARGADAS POR NVS ----
String sys_wifi_ssid;
String sys_wifi_pass;
String sys_tuya_dev_id;
String sys_wifi_options = ""; // Lista HTML dinámica de redes
int sys_joy_speed = 15;
int sys_joy_deadzone = 3;

Preferences prefs;
WebServer server(80);

// ---- TUYA CLOUD API FIJOS ----
#define TUYA_CLIENT_ID "YOUR_TUYA_CLIENT_ID"
#define TUYA_CLIENT_SEC "YOUR_TUYA_CLIENT_SECRET"
#define TUYA_ENDPOINT "openapi.tuyaus.com"

String tuya_access_token = "";
unsigned long token_expire_time = 0;

// ---- BLE MOUSE ----
BleMouse bleMouse("Raton BLE", "OpenSource", 100);

// Instancias
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

// ── Datos Menú ──
const char *opciones[] = {"TV", "Luz ", "Mouse", "Llamar",
                           "Configuracion", "Apagar"};
int totalOpciones = 6;
int seleccionActual = 0;
int seleccionLuz = 0; // 0 = Encendida, 1 = Apagada

enum Pantalla {
  MENU,
  PANTALLA_LUZ,
  PANTALLA_TABLET,
  PANTALLA_CONFIRMACION,
  PANTALLA_AJUSTES,
  PANTALLA_CONFIG
};
Pantalla pantallaActual = MENU;
int seleccionAjustes = 0;

// Variables de tiempo / suavizado
unsigned long ultimoMovimiento = 0;
unsigned long tiempoBotonPresionado = 0;
bool botonMantenido = false;
bool anteriorWiFiConectado = false;

unsigned long tiempoBtnNavMantenido = 0;
bool btnNavBloqueado = false;
bool nav2sTriggered = false;
int xSuavizado = 2048;
int ySuavizado = 2048;

int offsetX = 0;
int offsetY = 0;

int leerJoyX() {
  int val = (4095 - analogRead(JOY_X)) - offsetX;
  return constrain(val, 0, 4095);
}

int leerJoyY() {
  int val = analogRead(JOY_Y) - offsetY;
  return constrain(val, 0, 4095);
}

// Variables HUD Joystick (Tablet)
int antJoyXDisp = 160;
int antJoyYDisp = 128;

// Colores personalizados Modo Tablet
#define COLOR_BG 0x0841     // Azul muy oscuro (casi negro)
#define COLOR_RING1 0x4A8C  // Gris azulado (anillo exterior)
#define COLOR_CROSS 0x2104  // Gris oscuro (cruceta)
#define COLOR_DOT 0x07FF    // Cian puro (bola cursor cuando BLE activo)
#define COLOR_ACCENT 0x05FF // Azul brillante (acentos UI)
#define COLOR_HEADER 0x001F // Azul ILI9341 (header)
#define RADAR_CX 160        // Centro X del radar
#define RADAR_CY 128        // Centro Y — mismo en ambas pantallas
#define RADAR_R 65          // Radio anillo exterior (MAS GRANDE)
#define DOT_R 5             // Radio bola cursor
#define DOT_MAX_R 48

// ── VARIABLES VOLÁTILES FREERTOS ──
volatile int estadoNube =
    0; // 0=Idle, 1=Conectando, 2=Exito, 3=Fallo, 4=ConfigAP
volatile int requestLuz = 0; // 0=Ninguno, 1=Encender, 2=Apagar
volatile bool requestTabletInit = false;
volatile bool modoTabletActivo = false;
volatile bool requestConfigInit = false;
volatile bool modoConfigActivo = false;
volatile bool ultimaLuzEncendida = false;

// Task Handles
TaskHandle_t TaskUIHandle = NULL;
TaskHandle_t TaskNetHandle = NULL;

void dibujarMenu(bool completo = true);
void dibujarPantallaLuz(bool completo = true);
void dibujarPantallaTablet();
void dibujarTabletConectado();
void dibujarRadar();
void restaurarRadarEnPunto(int px, int py);
void dibujarPantallaAjustes(bool completo = true);
void dibujarPantallaConfig();
void dibujarConfirmacion(bool encendida, bool exito);
void volverAlMenu();

// ══════════════════════════════════════════════
// RUTINAS DE ROM Y WEBSERVER
// ══════════════════════════════════════════════

void cargarPreferencias() {
  prefs.begin("joystick", true);
  sys_wifi_ssid = prefs.getString("ssid", "YOUR_WIFI_SSID");
  sys_wifi_pass = prefs.getString("pass", "YOUR_WIFI_PASSWORD");
  sys_tuya_dev_id = prefs.getString("tuyaid", "YOUR_TUYA_DEVICE_ID");
  sys_joy_speed = prefs.getInt("jspeed", 15);
  sys_joy_deadzone = prefs.getInt("jdead", 3);
  prefs.end();
}

void handleConfigRoot() {
  String html =
      "<html><head><meta name='viewport' content='width=device-width, "
      "initial-scale=1.0'>"
      "<style>body{font-family:sans-serif; background:#222; color:#fff; "
      "padding:20px;}"
      "input, select{width:100%; padding:10px; margin:5px 0 20px; "
      "border-radius:5px; border:none; box-sizing: border-box; "
      "font-size:16px;}</style></head>"
      "<body><h2>Configuracion</h2><form method='POST' action='/save'>"
      "<b>Selecciona tu WiFi:</b><br><select name='ssid'>" +
      sys_wifi_options +
      "</select><br>"
      "<b>WiFi Contrasena:</b><br><input type='password' name='pass' value='" +
      sys_wifi_pass +
      "'>"
      "<b>Tuya Device ID:</b><br><input name='tuya' value='" +
      sys_tuya_dev_id +
      "'><br>"
      "<input type='submit' value='Guardar y Reiniciar' "
      "style='background:#10b981; color:#fff; font-weight:bold;'></form>"
      "</body></html>";
  server.send(200, "text/html", html);
}

void handleConfigSave() {
  String s = server.arg("ssid");
  String p = server.arg("pass");
  String t = server.arg("tuya");

  prefs.begin("joystick", false);
  prefs.putString("ssid", s);
  prefs.putString("pass", p);
  prefs.putString("tuyaid", t);
  prefs.end();

  String res = "<html><head><meta name='viewport' content='width=device-width, "
               "initial-scale=1.0'><style>body{font-family:sans-serif; "
               "background:#222; color:#10b981; text-align:center; "
               "padding-top:50px;}</style></head><body><h2>Guardado Con "
               "Exito!</h2><p>El joystick se reiniciara ahora para aplicar los "
               "cambios.</p></body></html>";
  server.send(200, "text/html", res);

  delay(1000);
  esp_restart();
}

// ══════════════════════════════════════════════
// RUTINAS DE RED (SE EJECUTAN EN CORE 0)
// ══════════════════════════════════════════════

void conectarWiFi() {
  if (WiFi.getMode() != WIFI_OFF) {
    WiFi.disconnect(true);
    delay(100);
  }

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(WIFI_PS_NONE);
  WiFi.begin(sys_wifi_ssid.c_str(), sys_wifi_pass.c_str());

  int w_retry = 0;
  while (WiFi.status() != WL_CONNECTED && w_retry < 24) {
    vTaskDelay(pdMS_TO_TICKS(500));
    w_retry++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    configTime(0, 0, "pool.ntp.org", "time.windows.com", "time.google.com");
  }
}

String generateHMAC(String message) {
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (info == NULL)
    return "";
  mbedtls_md_setup(&ctx, info, 1);
  mbedtls_md_hmac_starts(&ctx, (const unsigned char *)TUYA_CLIENT_SEC,
                         strlen(TUYA_CLIENT_SEC));
  mbedtls_md_hmac_update(&ctx, (const unsigned char *)message.c_str(),
                         message.length());
  unsigned char mac[32];
  mbedtls_md_hmac_finish(&ctx, mac);
  mbedtls_md_free(&ctx);
  char output[65];
  for (int i = 0; i < 32; i++)
    sprintf(output + (i * 2), "%02X", mac[i]);
  return String(output);
}

String generateSHA256(String payload) {
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (info == NULL)
    return "";
  mbedtls_md_setup(&ctx, info, 0);
  mbedtls_md_starts(&ctx);
  mbedtls_md_update(&ctx, (const unsigned char *)payload.c_str(),
                    payload.length());
  unsigned char hash[32];
  mbedtls_md_finish(&ctx, hash);
  mbedtls_md_free(&ctx);
  char output[65];
  for (int i = 0; i < 32; i++)
    sprintf(output + (i * 2), "%02x", hash[i]);
  return String(output);
}

unsigned long long getTimeMs() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return ((unsigned long long)tv.tv_sec * 1000ULL) +
         ((unsigned long long)tv.tv_usec / 1000ULL);
}

bool renovarTokenTuya() {
  if (millis() < token_expire_time && tuya_access_token != "")
    return true;

  unsigned long long t = getTimeMs();
  if (t < 1600000000000ULL)
    return false;

  char t_buf[32];
  sprintf(t_buf, "%llu", t);
  String t_str = String(t_buf);

  String stringToSign = "GET\ne3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca"
                        "495991b7852b855\n\n/v1.0/token?grant_type=1";
  String message = String(TUYA_CLIENT_ID) + t_str + stringToSign;
  String sign = generateHMAC(message);

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(10);
  HTTPClient http;
  bool exito = false;

  if (http.begin(client, "https://" + String(TUYA_ENDPOINT) +
                             "/v1.0/token?grant_type=1")) {
    http.addHeader("client_id", TUYA_CLIENT_ID);
    http.addHeader("sign", sign);
    http.addHeader("sign_method", "HMAC-SHA256");
    http.addHeader("t", t_str);

    int code = http.GET();
    if (code > 0) {
      String res = http.getString();
      int index = res.indexOf("\"access_token\":\"");
      if (index > 0) {
        tuya_access_token =
            res.substring(index + 16, res.indexOf("\"", index + 16));
        token_expire_time = millis() + (1800 * 1000);
        exito = true;
      }
    }
    http.end();
  }
  return exito;
}

bool realizarPeticionTuya(bool encender) {
  if (WiFi.status() != WL_CONNECTED) {
    conectarWiFi();
  }
  if (WiFi.status() != WL_CONNECTED)
    return false;

  unsigned long long time_ms = getTimeMs();
  int intentos = 0;
  while (time_ms < 1600000000000ULL && intentos < 20) {
    vTaskDelay(pdMS_TO_TICKS(500));
    time_ms = getTimeMs();
    intentos++;
  }

  if (time_ms >= 1600000000000ULL && renovarTokenTuya()) {
    time_ms = getTimeMs();
    char t_buf3[32];
    sprintf(t_buf3, "%llu", time_ms);
    String t_str = String(t_buf3);

    String payload = "{\"commands\":[{\"code\":\"switch_led\",\"value\":" +
                     String(encender ? "true" : "false") + "}]}";
    String url = "/v1.0/iot-03/devices/" + sys_tuya_dev_id + "/commands";

    String contentHash = generateSHA256(payload);
    String stringToSign = "POST\n" + contentHash + "\n\n" + url;
    String message =
        String(TUYA_CLIENT_ID) + tuya_access_token + t_str + stringToSign;
    String sign = generateHMAC(message);

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(10);
    HTTPClient http;

    if (http.begin(client, "https://" + String(TUYA_ENDPOINT) + url)) {
      http.addHeader("client_id", TUYA_CLIENT_ID);
      http.addHeader("access_token", tuya_access_token);
      http.addHeader("sign", sign);
      http.addHeader("sign_method", "HMAC-SHA256");
      http.addHeader("t", t_str);
      http.addHeader("Content-Type", "application/json");

      int code = http.POST(payload);
      http.end();
      if (code == 200)
        return true;
    }
  }
  return false;
}

// ══════════════════════════════════════════════
// TAREAS FREERTOS
// ══════════════════════════════════════════════

void Task_Network(void *pvParameters) {
  cargarPreferencias();
  conectarWiFi();

  for (;;) {
    if (requestLuz != 0) {
      estadoNube = 1;
      bool encender = (requestLuz == 1);
      ultimaLuzEncendida = encender;
      requestLuz = 0;

      bool exito = realizarPeticionTuya(encender);

      if (exito)
        estadoNube = 2;
      else
        estadoNube = 3;

      vTaskDelay(pdMS_TO_TICKS(3000));
      estadoNube = 0;
    }

    if (requestTabletInit) {
      requestTabletInit = false;
      WiFi.disconnect(true);
      vTaskDelay(pdMS_TO_TICKS(500));
      bleMouse.begin();
      modoTabletActivo = true;
    }

    if (requestConfigInit) {
      requestConfigInit = false;

      // 1. Escaneo previo de Redes
      WiFi.disconnect(true);
      WiFi.mode(WIFI_STA);
      int n = WiFi.scanNetworks();

      sys_wifi_options = "<option value='" + sys_wifi_ssid + "'>[Guardado] " +
                         sys_wifi_ssid + "</option>";
      if (n > 0) {
        for (int i = 0; i < n; ++i) {
          if (WiFi.SSID(i) != sys_wifi_ssid) {
            sys_wifi_options += "<option value='" + WiFi.SSID(i) + "'>" +
                                WiFi.SSID(i) + " (" + String(WiFi.RSSI(i)) +
                                "dBm)</option>";
          }
        }
      }

      // 2. Levantar el Punto de Acceso Local
      WiFi.mode(WIFI_AP);
      WiFi.softAP("Joystick Config");

      server.on("/", handleConfigRoot);
      server.on("/save", handleConfigSave);
      server.begin();

      modoConfigActivo = true;
      estadoNube = 4;

      while (true) {
        server.handleClient();
        vTaskDelay(pdMS_TO_TICKS(20));
      }
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void Task_UI(void *pvParameters) {
  pinMode(TFT_LED, OUTPUT);
  digitalWrite(TFT_LED, HIGH);

  SPI.begin(TFT_SCK, -1, TFT_MOSI, TFT_CS);

  tft.begin();
  tft.setRotation(3);
  dibujarMenu();
  int anteriorEstadoNube = 0;
  bool anteriorBleConectado = false;

  for (;;) {
    if (estadoNube != anteriorEstadoNube) {
      if (estadoNube == 1) {
        tft.fillScreen(ILI9341_BLACK);
        tft.setCursor(65, 100);
        tft.setTextColor(ILI9341_WHITE);
        tft.setTextSize(2);
        tft.println("Conectando...");
      } else if (estadoNube == 2) {
        dibujarConfirmacion(ultimaLuzEncendida, true);
      } else if (estadoNube == 3) {
        dibujarConfirmacion(ultimaLuzEncendida, false);
      } else if (estadoNube == 4) {
        dibujarPantallaConfig();
      } else if (estadoNube == 0) {
        volverAlMenu();
      }
      anteriorEstadoNube = estadoNube;
    }

    if (estadoNube == 0 || estadoNube == 4) {
      void leerJoystick();
      void leerBoton();
      void leerBotonNav();
      leerJoystick();
      leerBoton();
      leerBotonNav();

      if (pantallaActual == MENU) {
        bool wifiConectado = (WiFi.status() == WL_CONNECTED);
        if (wifiConectado != anteriorWiFiConectado) {
          anteriorWiFiConectado = wifiConectado;
          if (wifiConectado)
            tft.fillCircle(305, 15, 6, ILI9341_GREEN);
          else
            tft.fillCircle(305, 15, 6, ILI9341_RED);
        }
      } else if (pantallaActual == PANTALLA_TABLET && modoTabletActivo) {
        bool actualBle = bleMouse.isConnected();
        if (actualBle != anteriorBleConectado) {
          anteriorBleConectado = actualBle;
          if (actualBle) {
            dibujarTabletConectado();
          } else {
            dibujarPantallaTablet();
          }
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(15));
  }
}

// ══════════════════════════════════════════════
// DIBUJO DE PANTALLAS (LLAMADAS DESDE TASK_UI)
// ══════════════════════════════════════════════

void dibujarBateria(int x, int y, uint16_t bgColor) {
  int raw = analogRead(PIN_BAT);
  int maxADC = 2600;
  int minADC = 1900;
  int pct = map(raw, minADC, maxADC, 0, 100);
  if (pct > 100) pct = 100;
  if (pct < 0) pct = 0;

  uint16_t color = ILI9341_GREEN;
  if (pct < 40) color = ILI9341_YELLOW;
  if (pct < 15) color = ILI9341_RED;

  tft.drawRect(x, y, 22, 10, ILI9341_WHITE);
  tft.fillRect(x + 22, y + 2, 2, 6, ILI9341_WHITE);
  
  int fillWidth = map(pct, 0, 100, 0, 20);
  if (fillWidth > 0) {
    tft.fillRect(x + 1, y + 1, fillWidth, 8, color);
  }
  if (fillWidth < 20) {
    tft.fillRect(x + 1 + fillWidth, y + 1, 20 - fillWidth, 8, bgColor);
  }
}

void dibujarMenu(bool completo) {
  if (completo) {
    tft.fillScreen(ILI9341_BLACK);
    tft.setCursor(60, 20);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(3);
    tft.println("MENU");
    tft.drawFastHLine(20, 52, 280, ILI9341_BLUE);

    dibujarBateria(260, 12, ILI9341_BLACK);

    if (WiFi.status() == WL_CONNECTED) {
      tft.fillCircle(305, 15, 6, ILI9341_GREEN);
      anteriorWiFiConectado = true;
    } else {
      tft.fillCircle(305, 15, 6, ILI9341_RED);
      anteriorWiFiConectado = false;
    }
  }

  for (int i = 0; i < totalOpciones; i++) {
    int yPos = 58 + (i * 30);
    if (i == seleccionActual) {
      tft.fillRoundRect(30, yPos - 3, 260, 28, 8, ILI9341_YELLOW);
      tft.setTextColor(ILI9341_BLACK);
    } else {
      tft.fillRoundRect(30, yPos - 3, 260, 28, 8, ILI9341_BLACK);
      tft.drawRoundRect(30, yPos - 3, 260, 28, 8, ILI9341_WHITE);
      tft.setTextColor(ILI9341_WHITE);
    }
    tft.setCursor(45, yPos);
    tft.setTextSize(3);
    tft.println(opciones[i]);
  }
}

void dibujarRadar() {
  tft.drawCircle(RADAR_CX, RADAR_CY, RADAR_R, COLOR_RING1);
  tft.drawCircle(RADAR_CX, RADAR_CY, RADAR_R - 1, COLOR_RING1);
  tft.drawCircle(RADAR_CX, RADAR_CY, RADAR_R - 2, COLOR_RING1);
  tft.drawFastHLine(RADAR_CX - RADAR_R + 4, RADAR_CY, (RADAR_R - 4) * 2,
                    COLOR_CROSS);
  tft.drawFastVLine(RADAR_CX, RADAR_CY - RADAR_R + 4, (RADAR_R - 4) * 2,
                    COLOR_CROSS);
}

void restaurarRadarEnPunto(int px, int py) {
  if (abs(py - RADAR_CY) <= DOT_R + 1) {
    tft.drawFastHLine(RADAR_CX - RADAR_R + 4, RADAR_CY, (RADAR_R - 4) * 2,
                      COLOR_CROSS);
  }
  if (abs(px - RADAR_CX) <= DOT_R + 1) {
    tft.drawFastVLine(RADAR_CX, RADAR_CY - RADAR_R + 4, (RADAR_R - 4) * 2,
                      COLOR_CROSS);
  }
}

void dibujarPantallaTablet() {
  tft.fillScreen(COLOR_BG);

  tft.fillRect(0, 0, 320, 44, COLOR_HEADER);
  tft.setCursor(58, 5);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.print("MODO TABLET");
  tft.setCursor(58, 24);
  tft.setTextColor(0xFD20);
  tft.setTextSize(1);
  tft.print("Buscando dispositivo BLE...");
  
  dibujarBateria(270, 15, COLOR_HEADER);

  tft.drawFastHLine(0, 44, 320, COLOR_ACCENT);

  dibujarRadar();

  tft.fillCircle(RADAR_CX, RADAR_CY, DOT_R, 0xC618);

  tft.drawFastHLine(0, 215, 320, COLOR_RING1);
  tft.setCursor(10, 219);
  tft.setTextColor(0x39CF);
  tft.setTextSize(1);
  tft.print("Manten boton 2s para salir");

  antJoyXDisp = RADAR_CX;
  antJoyYDisp = RADAR_CY;
}

void dibujarTabletConectado() {
  tft.fillScreen(COLOR_BG);

  tft.fillRect(0, 0, 320, 38, COLOR_HEADER);
  tft.setCursor(18, 10);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.print("MODO TABLET");
  tft.fillRoundRect(196, 6, 108, 26, 6, 0x0460);
  tft.setCursor(205, 13);
  tft.setTextColor(0x07E0);
  tft.setTextSize(1);
  tft.print("BLE CONECTADO");
  
  dibujarBateria(280, 15, 0x0460);

  tft.drawFastHLine(0, 38, 320, COLOR_ACCENT);

  dibujarRadar();

  tft.fillCircle(RADAR_CX, RADAR_CY, DOT_R, COLOR_DOT);

  tft.drawFastHLine(0, 215, 320, COLOR_RING1);
  tft.setCursor(10, 219);
  tft.setTextColor(0x39CF);
  tft.setTextSize(1);
  tft.print("Manten boton 2s para salir");

  antJoyXDisp = RADAR_CX;
  antJoyYDisp = RADAR_CY;
}

void dibujarPantallaAjustes(bool completo) {
  if (completo) {
    tft.fillScreen(ILI9341_BLACK);
    tft.setCursor(35, 15);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(3);
    tft.println("AJUSTES MOUSE");
    tft.drawFastHLine(20, 45, 280, ILI9341_BLUE);
    tft.setCursor(30, 225);
    tft.setTextColor(ILI9341_CYAN);
    tft.setTextSize(1);
    tft.println("Usa el Joystick. Boton Ext para salir.");
  }

  const char *nombres[] = {"Velocidad", "Zona Muerta", "Portal Web WiFi"};
  for (int i = 0; i < 3; i++) {
    int yPos = 65 + (i * 45);
    if (i == seleccionAjustes) {
      tft.fillRoundRect(20, yPos - 5, 280, 38, 8, ILI9341_YELLOW);
      tft.setTextColor(ILI9341_BLACK);
    } else {
      tft.fillRoundRect(20, yPos - 5, 280, 38, 8, ILI9341_BLACK);
      tft.drawRoundRect(20, yPos - 5, 280, 38, 8, ILI9341_WHITE);
      tft.setTextColor(ILI9341_WHITE);
    }

    tft.setCursor(30, yPos + 4);
    tft.setTextSize(2);
    if (i == 0) {
      tft.print("["); 
      if (sys_joy_speed < 10) tft.print(" ");
      tft.print(sys_joy_speed); 
      tft.print("] ");
      tft.print(nombres[i]);
    } else if (i == 1) {
      tft.print("["); 
      if (sys_joy_deadzone < 10) tft.print(" ");
      tft.print(sys_joy_deadzone); 
      tft.print("] ");
      tft.print(nombres[i]);
    } else {
      tft.print("  -> "); tft.print(nombres[i]);
    }
  }
}

void dibujarPantallaConfig() {
  tft.fillScreen(ILI9341_BLACK);
  tft.setCursor(50, 20);
  tft.setTextColor(ILI9341_CYAN);
  tft.setTextSize(3);
  tft.println("CONFIGURACION");
  tft.drawFastHLine(20, 50, 280, ILI9341_BLUE);

  tft.setCursor(20, 80);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.println("1. Conectate al WiFi:");
  tft.setTextColor(ILI9341_YELLOW);
  tft.setCursor(20, 105);
  tft.println("   Joystick Config");

  tft.setCursor(20, 140);
  tft.setTextColor(ILI9341_WHITE);
  tft.println("2. Abre el navegador:");
  tft.setTextColor(ILI9341_GREEN);
  tft.setCursor(20, 165);
  tft.println("   192.168.4.1");

  tft.setCursor(20, 225);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(1);
  tft.println("MANTEN PRESIONADO EL BOTON para cancelar.");
}

void dibujarPantallaLuz(bool completo) {
  if (completo) {
    tft.fillScreen(ILI9341_BLACK);
    tft.setCursor(75, 15);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(3);
    tft.println("CONTROL LUZ");
    tft.drawFastHLine(20, 50, 280, ILI9341_BLUE);

    tft.setCursor(30, 228);
    tft.setTextColor(ILI9341_CYAN);
    tft.setTextSize(1);
    tft.println("< Izq: volver   Der: confirmar");
  }

  int y0 = 75;
  if (seleccionLuz == 0) {
    tft.fillRoundRect(20, y0, 280, 60, 10, ILI9341_YELLOW);
    tft.setTextColor(ILI9341_BLACK);
  } else {
    tft.fillRoundRect(20, y0, 280, 60, 10, ILI9341_BLACK);
    tft.drawRoundRect(20, y0, 280, 60, 10, ILI9341_WHITE);
    tft.setTextColor(ILI9341_WHITE);
  }
  tft.setCursor(85, y0 + 18);
  tft.setTextSize(2);
  tft.println("LUZ ENCENDIDA");

  int y1 = 155;
  if (seleccionLuz == 1) {
    tft.fillRoundRect(20, y1, 280, 60, 10, ILI9341_CYAN);
    tft.setTextColor(ILI9341_BLACK);
  } else {
    tft.fillRoundRect(20, y1, 280, 60, 10, ILI9341_BLACK);
    tft.drawRoundRect(20, y1, 280, 60, 10, ILI9341_WHITE);
    tft.setTextColor(ILI9341_WHITE);
  }
  tft.setCursor(85, y1 + 18);
  tft.setTextSize(2);
  tft.println("LUZ APAGADA");
}

void dibujarConfirmacion(bool encendida, bool exito) {
  tft.fillScreen(ILI9341_BLACK);

  if (!exito) {
    tft.fillCircle(160, 85, 45, ILI9341_RED);
    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(143, 73);
    tft.setTextSize(4);
    tft.println("!");
    tft.setCursor(55, 148);
    tft.setTextColor(ILI9341_RED);
    tft.setTextSize(2);
    tft.println("Error al enviar");
  } else if (encendida) {
    tft.fillCircle(160, 90, 50, ILI9341_YELLOW);
    tft.setCursor(35, 185);
    tft.setTextColor(ILI9341_YELLOW);
    tft.setTextSize(3);
    tft.println("LUZ ENCENDIDA");
  } else {
    tft.drawCircle(160, 90, 50, ILI9341_WHITE);
    tft.setCursor(45, 185);
    tft.setTextColor(0x7BEF);
    tft.setTextSize(3);
    tft.println("LUZ APAGADA");
  }

  tft.setCursor(50, 225);
  tft.setTextColor(ILI9341_CYAN);
  tft.setTextSize(1);
  tft.println("< Espera por favor...");
}

// ══════════════════════════════════════════════
// NAVEGACIÓN E INPUTS (LLAMADOS POR TASK_UI)
// ══════════════════════════════════════════════

void volverAlMenu() {
  if (pantallaActual == PANTALLA_AJUSTES) {
    prefs.begin("joystick", false);
    prefs.putInt("jspeed", sys_joy_speed);
    prefs.putInt("jdead", sys_joy_deadzone);
    prefs.end();
  }
  pantallaActual = MENU;
  dibujarMenu(true);
}

void ejecutarOpcion() {
  if (seleccionActual == 1) {
    pantallaActual = PANTALLA_LUZ;
    seleccionLuz = 0;
    dibujarPantallaLuz();
  } else if (seleccionActual == 2) {
    pantallaActual = PANTALLA_TABLET;
    if (!modoTabletActivo) {
      tft.fillScreen(ILI9341_BLACK);
      tft.setCursor(20, 150);
      tft.setTextColor(ILI9341_WHITE);
      tft.setTextSize(2);
      tft.println("Iniciando Bluetooth...");
      requestTabletInit = true;

      while (!modoTabletActivo) {
        vTaskDelay(pdMS_TO_TICKS(100));
      }
    }
    dibujarPantallaTablet();
  } else if (seleccionActual == 4) {
    pantallaActual = PANTALLA_AJUSTES;
    seleccionAjustes = 0;
    dibujarPantallaAjustes(true);
  } else if (seleccionActual == 5) {
    void apagarDispositivo();
    apagarDispositivo();
  } else {
    int yPos = 58 + (seleccionActual * 30);
    tft.fillRoundRect(30, yPos - 3, 260, 28, 8, ILI9341_DARKGREY);
    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(45, yPos);
    tft.setTextSize(3);
    tft.println(opciones[seleccionActual]);
    vTaskDelay(pdMS_TO_TICKS(300));
    dibujarMenu();
  }
}

void confirmarLuz() {
  pantallaActual = PANTALLA_CONFIRMACION;
  requestLuz = (seleccionLuz == 0) ? 1 : 2;
}

void controlarTabletBLE() {
  int rawX = leerJoyX();
  int rawY = leerJoyY();

  int joyXDisp = map(rawX, 0, 4095, RADAR_CX - DOT_MAX_R, RADAR_CX + DOT_MAX_R);
  int joyYDisp = map(rawY, 0, 4095, RADAR_CY - DOT_MAX_R, RADAR_CY + DOT_MAX_R);

  float dx = joyXDisp - RADAR_CX;
  float dy = joyYDisp - RADAR_CY;
  float dist = sqrt(dx * dx + dy * dy);
  if (dist > DOT_MAX_R) {
    joyXDisp = RADAR_CX + (int)(dx * DOT_MAX_R / dist);
    joyYDisp = RADAR_CY + (int)(dy * DOT_MAX_R / dist);
  }

  if (joyXDisp != antJoyXDisp || joyYDisp != antJoyYDisp) {
    tft.fillCircle(antJoyXDisp, antJoyYDisp, DOT_R, COLOR_BG);
    restaurarRadarEnPunto(antJoyXDisp, antJoyYDisp);
    uint16_t colorBola = bleMouse.isConnected() ? COLOR_DOT : 0xC618;
    tft.fillCircle(joyXDisp, joyYDisp, DOT_R, colorBola);
    antJoyXDisp = joyXDisp;
    antJoyYDisp = joyYDisp;
  }

  if (bleMouse.isConnected()) {
    xSuavizado = (xSuavizado * 8 + rawX * 2) / 10;
    ySuavizado = (ySuavizado * 8 + rawY * 2) / 10;
    int vx = map(xSuavizado, 0, 4095, -sys_joy_speed, sys_joy_speed);
    int vy = map(ySuavizado, 0, 4095, -sys_joy_speed, sys_joy_speed);
    
    if (abs(vx) <= sys_joy_deadzone)
      vx = 0;
    if (abs(vy) <= sys_joy_deadzone)
      vy = 0;
      
    if (vx != 0 || vy != 0) {
      bleMouse.move(vx, vy, 0);
    }
  }
}

void leerJoystick() {
  unsigned long ahora = millis();
  if (pantallaActual == PANTALLA_TABLET) {
    controlarTabletBLE();
    return;
  }
  if (pantallaActual == PANTALLA_CONFIG) {
    return;
  }

  if (ahora - ultimoMovimiento < DEBOUNCE_MS)
    return;

  int valorX = leerJoyX();
  int valorY = leerJoyY();

  if (pantallaActual == MENU) {
    if (valorY < JOY_THRESHOLD_LOW) {
      seleccionActual = (seleccionActual - 1 + totalOpciones) % totalOpciones;
      dibujarMenu(false);
      ultimoMovimiento = ahora;
    } else if (valorY > JOY_THRESHOLD_HIGH) {
      seleccionActual = (seleccionActual + 1) % totalOpciones;
      dibujarMenu(false);
      ultimoMovimiento = ahora;
    } else if (valorX > JOY_THRESHOLD_HIGH) {
      ejecutarOpcion();
      ultimoMovimiento = ahora;
    }
  } else if (pantallaActual == PANTALLA_LUZ) {
    if (valorY < JOY_THRESHOLD_LOW) {
      seleccionLuz = 0;
      dibujarPantallaLuz(false);
      ultimoMovimiento = ahora;
    } else if (valorY > JOY_THRESHOLD_HIGH) {
      seleccionLuz = 1;
      dibujarPantallaLuz(false);
      ultimoMovimiento = ahora;
    } else if (valorX > JOY_THRESHOLD_HIGH) {
      confirmarLuz();
      ultimoMovimiento = ahora;
    } else if (valorX < JOY_THRESHOLD_LOW) {
      volverAlMenu();
      ultimoMovimiento = ahora;
    }
  } else if (pantallaActual == PANTALLA_AJUSTES) {
    if (valorY < JOY_THRESHOLD_LOW) {
      seleccionAjustes = (seleccionAjustes - 1 + 3) % 3;
      dibujarPantallaAjustes(false);
      ultimoMovimiento = ahora;
    } else if (valorY > JOY_THRESHOLD_HIGH) {
      seleccionAjustes = (seleccionAjustes + 1) % 3;
      dibujarPantallaAjustes(false);
      ultimoMovimiento = ahora;
    } else if (valorX > JOY_THRESHOLD_HIGH) {
      if (seleccionAjustes == 0) {
        if (sys_joy_speed < 50) sys_joy_speed++;
        dibujarPantallaAjustes(false);
      } else if (seleccionAjustes == 1) {
        if (sys_joy_deadzone < 20) sys_joy_deadzone++;
        dibujarPantallaAjustes(false);
      } else if (seleccionAjustes == 2) {
        pantallaActual = PANTALLA_CONFIG;
        if (!modoConfigActivo) {
          tft.fillScreen(ILI9341_BLACK);
          tft.setCursor(20, 150);
          tft.setTextColor(ILI9341_WHITE);
          tft.setTextSize(2);
          tft.println("Iniciando Portal WiFi...");
          requestConfigInit = true;
          while (!modoConfigActivo) {
            vTaskDelay(pdMS_TO_TICKS(100));
          }
        }
      }
      ultimoMovimiento = ahora - 150;
    } else if (valorX < JOY_THRESHOLD_LOW) {
      if (seleccionAjustes == 0) {
        if (sys_joy_speed > 1) sys_joy_speed--;
        dibujarPantallaAjustes(false);
      } else if (seleccionAjustes == 1) {
        if (sys_joy_deadzone > 0) sys_joy_deadzone--;
        dibujarPantallaAjustes(false);
      }
      ultimoMovimiento = ahora - 150;
    }
  }
}

void leerBoton() {
  bool presionado = (digitalRead(JOY_BTN) == LOW);
  if (presionado) {
    if (tiempoBotonPresionado == 0)
      tiempoBotonPresionado = millis();

    if (pantallaActual == PANTALLA_CONFIG) {
      if (!botonMantenido && (millis() - tiempoBotonPresionado > 2000)) {
        botonMantenido = true;
        tft.fillScreen(ILI9341_BLACK);
        tft.setCursor(20, 150);
        tft.setTextColor(ILI9341_CYAN);
        tft.setTextSize(2);
        tft.println("Restableciendo Sistema...");
        vTaskDelay(pdMS_TO_TICKS(800));
        esp_restart();
      }
    } else if (pantallaActual == PANTALLA_TABLET) {
      if (bleMouse.isConnected() && !bleMouse.isPressed(MOUSE_LEFT)) {
        bleMouse.press(MOUSE_LEFT);
      }
    }
  } else {
    if (tiempoBotonPresionado > 0) {
      tiempoBotonPresionado = 0;
      if (!botonMantenido) {
        if (pantallaActual == MENU) {
          ejecutarOpcion();
        } else if (pantallaActual == PANTALLA_TABLET) {
          if (bleMouse.isConnected() && bleMouse.isPressed(MOUSE_LEFT)) {
            bleMouse.release(MOUSE_LEFT);
          }
        } else if (pantallaActual == PANTALLA_LUZ) {
          volverAlMenu();
        }
      }
      botonMantenido = false;
    }
  }
}

void apagarDispositivo() {
  tft.fillScreen(ILI9341_BLACK);
  tft.setCursor(80, 110);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(3);
  tft.println("APAGANDO");
  vTaskDelay(pdMS_TO_TICKS(1000));
  
  digitalWrite(TFT_LED, LOW);
  
  esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_NAV, 0);
  esp_deep_sleep_start();
}

void leerBotonNav() {
  bool presionado = (digitalRead(BTN_NAV) == LOW);
  if (presionado) {
    if (tiempoBtnNavMantenido == 0) {
      tiempoBtnNavMantenido = millis();
      nav2sTriggered = false;
    } else if (!btnNavBloqueado) {
      unsigned long mant = millis() - tiempoBtnNavMantenido;
      if (mant >= 5000) {
        btnNavBloqueado = true;
        apagarDispositivo();
      } else if (mant >= 2000 && !nav2sTriggered) {
        nav2sTriggered = true;
        if (pantallaActual != MENU) {
          volverAlMenu();
        }
      }
    }
  } else {
    tiempoBtnNavMantenido = 0;
    btnNavBloqueado = false;
    nav2sTriggered = false;
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(JOY_BTN, INPUT_PULLUP);
  pinMode(BTN_NAV, INPUT_PULLUP);

  long sumX = 0, sumY = 0;
  for (int i = 0; i < 20; i++) {
    sumX += (4095 - analogRead(JOY_X));
    sumY += analogRead(JOY_Y);
    delay(10);
  }
  offsetX = (sumX / 20) - 2048;
  offsetY = (sumY / 20) - 2048;

  xTaskCreatePinnedToCore(Task_Network, "Redes", 16384, NULL, 1, &TaskNetHandle,
                          0);
  xTaskCreatePinnedToCore(Task_UI, "Pantallas", 8192, NULL, 2, &TaskUIHandle,
                          1);
}

void loop() {
  vTaskDelete(NULL);
}
