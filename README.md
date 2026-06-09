# ESP32 Joystick Menu & controller (Open Source)

Este proyecto implementa un controlador multifuncional interactivo basado en **ESP32** con pantalla TFT ILI9341, control por Joystick, funcionalidad de mouse por Bluetooth (BLE Mouse), control de luces inteligentes compatibles con Tuya Cloud, y un portal web cautivo integrado para configurar las credenciales locales sin tener que compilar el código de nuevo.

---

## Características principales

- **Menú Interactivo**: Interfaz gráfica en pantalla TFT ILI9341 controlada por hardware (Joystick analógico y botones).
- **Modo Mouse BLE**: Emulación de mouse Bluetooth de baja energía para controlar tablets, ordenadores o dispositivos móviles. Cuenta con suavizado de coordenadas y visualización de radar en pantalla.
- **Control de Domótica**: Integración directa con Tuya Cloud API mediante firma HMAC-SHA256 nativa para encendido/apagado de luces inteligentes y otros dispositivos compatibles.
- **Portal Cautivo de Configuración**: Levanta un Access Point local (`Joystick Config`) con servidor web en la IP `192.168.4.1` para que puedas configurar la red WiFi y el ID del dispositivo Tuya desde tu smartphone de forma inalámbrica y guardarlo en la memoria persistente NVS del microcontrolador.
- **Multitarea Real (FreeRTOS)**: Corre en múltiples hilos y cores del ESP32. El núcleo de red (Core 0) se encarga de las peticiones Web, conexión WiFi y API Tuya, mientras que el núcleo de UI (Core 1) mantiene la pantalla fluida a 60 FPS sin congelamientos.
- **Lectura de Batería**: Indicador visual dinámico del estado de carga de la batería en la pantalla principal.

---

## Conexiones de Hardware sugeridas

### Pantalla TFT ILI9341 (SPI)
- `TFT_CS`  -> GPIO 15
- `TFT_DC`  -> GPIO 4
- `TFT_RST` -> GPIO 2
- `TFT_MOSI`-> GPIO 16
- `TFT_SCK` -> GPIO 17
- `TFT_LED` -> GPIO 5 (Retroiluminación)

### Joystick Analógico
- `JOY_X`   -> GPIO 34
- `JOY_Y`   -> GPIO 35
- `JOY_BTN` -> GPIO 26 (Click del Joystick)

### Botones de Control
- `BTN_NAV` -> GPIO 25 (Navegación / Encendido y despertar de Deep Sleep)
- `PIN_BAT` -> GPIO 36 (Lectura de divisor resistivo para batería)

---

## Librerías Requeridas

Asegúrate de instalar las siguientes librerías desde el Gestor de Librerías de Arduino IDE:
1. **Adafruit GFX Library** (por Adafruit)
2. **Adafruit ILI9341** (por Adafruit)
3. **ESP32-BLE-Mouse** (por T-vK o similar para emulación HID Mouse BLE)
4. Librerías nativas de la placa ESP32 (WiFi, WebServer, HTTPClient, Preferences, SPI, mbedtls).

---

## Configuración y Despliegue

### 1. Clientes de Tuya Cloud (Desarrollador)
Para usar el control de luces inteligentes de Tuya, necesitarás configurar tus credenciales de desarrollador fijas en el código:
Abre [joystickmenu.ino](joystickmenu.ino) y reemplaza estas líneas con tus datos obtenidos en la plataforma [Tuya IoT Platform](https://iot.tuya.com/):
```cpp
#define TUYA_CLIENT_ID "TU_TUYA_CLIENT_ID"
#define TUYA_CLIENT_SEC "TU_TUYA_CLIENT_SECRET"
```

### 2. Configuración en marcha (Sin volver a programar)
Una vez programado el ESP32:
1. Enciende el dispositivo.
2. Ingresa al menú de **Ajustes / Portal Web WiFi**.
3. El dispositivo se pondrá en modo configuración y creará una red WiFi llamada **`Joystick Config`**.
4. Conéctate a ella desde tu teléfono móvil.
5. Abre un navegador web e ingresa a la dirección **`192.168.4.1`**.
6. Introduce el nombre y contraseña de tu red WiFi local, junto al **Device ID de Tuya** del foco o dispositivo que deseas controlar.
7. Haz clic en **Guardar y Reiniciar**. ¡El dispositivo guardará de forma permanente tus credenciales y se conectará automáticamente a tu red local!

---

## Licencia

Este proyecto está bajo la Licencia **MIT**. Puedes usarlo, modificarlo y distribuirlo libremente de forma comercial o personal. Consulta el archivo `LICENSE` para más información.
