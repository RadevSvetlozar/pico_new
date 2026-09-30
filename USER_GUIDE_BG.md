# Arduino Nano ESP32 Gateway — ръководство за употреба

Този документ описва употребата на текущия firmware за Arduino Nano ESP32:

- първоначална настройка през Access Point;
- Wi-Fi, W5500 LAN и SIM7600 GSM/LTE връзка;
- MQTT комуникация;
- локален Modbus RTU;
- локални цифрови входове;
- LoRa комуникация с Pro Mini;
- системна диагностика, fallback и OTA.

Hardware връзките са описани отделно в `HARDWARE_CONNECTIONS_BG.md`.

## 1. Основен принцип

Nano ESP32 е gateway между MQTT и физическите интерфейси:

```text
MQTT
  |
  +-- Wi-Fi / W5500 LAN / SIM7600 GSM
  |
Nano ESP32
  +-- локален Modbus RTU
  +-- локални входове D4/D5/D6
  +-- LoRa -> Pro Mini -> Modbus или входове
```

Wi-Fi е мрежата по подразбиране. LAN и GSM се избират от setup портала.

## 2. Стартиране на Access Point

CONFIG бутонът е свързан между `D0` и `GND`.

За да влезеш в setup mode:

1. Натисни и задръж CONFIG бутона.
2. Натисни RESET или изключи и включи захранването.
3. Продължи да държиш бутона, докато платката стартира setup mode.
4. От телефон или компютър намери Wi-Fi мрежата:

```text
SSID: ESP32-Setup
Password: 147258369
```

5. Свържи се към нея.
6. Отвори:

```text
http://192.168.10.1
```

Пинът се проверява само при startup. Натискане на CONFIG без рестарт няма да
включи Access Point.

## 3. Настройки в Access Point портала

Началната страница е публична за всеки, който е свързан към setup Access
Point-а. На нея се виждат monitoring панелът и само:

- `WiFi SSID`;
- `WiFi password`.

При празно поле за Wi-Fi парола записаната парола не се променя. За
административните настройки натисни `Administrator`. Браузърът ще поиска:

```text
Username: admin
Password: admin147258
```

Това е началната администраторска парола след factory reset. Смени я от
защитената страница при първата настройка. Новата парола трябва да е поне 8
символа.

Администраторската проверка се извършва от ESP32 за `/admin`,
`/admin/save` и `/admin/reset`; скритите операции не могат да се извикат без
валидна парола.

### Primary network

Избира основния мрежов transport:

| Стойност | Употреба |
|---|---|
| WiFi | Вградената Wi-Fi връзка; default |
| LAN - W5500 | Ethernet чрез W5500 и DHCP |
| GSM/LTE - SIM7600 | Мобилни данни чрез SIM7600 |

След запис платката се рестартира и използва само избрания transport.

### Wi-Fi настройки

- `WiFi SSID` — името на локалната Wi-Fi мрежа.
- `WiFi password` — паролата за Wi-Fi.

Тези полета се използват само когато `Primary network` е `WiFi`.

### GSM настройки

- `GSM APN` — APN на мобилния оператор, например `internet`.
- `GSM user` — APN потребител, ако операторът изисква.
- `GSM password` — APN парола, ако операторът изисква.
- `SIM PIN` — PIN на SIM картата; остави празно, ако PIN е изключен.

Тези полета се използват само при `GSM/LTE - SIM7600`.

### MQTT настройки

- `MQTT server` — IP адрес или DNS име на MQTT broker-а.
- `MQTT port` — обикновено `1883`.

В защитената страница се настройват:

- `network_mode`, `ssid`, `pass` и `staticip`;
- `mqttip`, `mqttport`, `mqttuser` и `mqttpass`;
- `user_name`, `address`, `object_name` и `device_id`;
- `com1_baud_rate`;
- `gsm_apn`, `gsm_user`, `gsm_pass` и `gsm_pin`;
- нова администраторска парола.

Wi-Fi, MQTT и GSM паролите не се показват обратно в HTML. Ако съответното поле
остане празно, записаната парола не се променя. Натисни
`Save advanced settings and restart`. Настройките се записват във flash
Preferences и платката се рестартира.

### Monitoring в портала

Порталът обновява на всеки две секунди:

- избран network transport;
- network status;
- MQTT status;
- uptime;
- free heap;
- температура от NTC термистора.

Monitoring панелът присъства и на публичната, и на администраторската страница.

## 4. MQTT base topic

Всички topics започват с:

```text
<user_name>/<address>/<object_name>/<device_id>
```

Пример:

```text
factory/sofia/pump_station/gateway_1
```

В примерите по-долу използваме:

```text
BASE=factory/sofia/pump_station/gateway_1
```

Полетата за идентичност могат да се зададат чрез MQTT `update-config`.

## 5. Общ списък на MQTT topics

### Команди към gateway-а

| Topic след BASE | Предназначение |
|---|---|
| `modbus/request` | Локална Modbus RTU заявка от Nano ESP32 |
| `inputs/request` | Четене на локалните D4/D5/D6 |
| `lora/request` | Заявка към Pro Mini през LoRa |
| `lora/scan_request` | Сканиране за LoRa nodes |
| `get-config` | Изискване на текущата конфигурация |
| `update-config` | Запис на конфигурация и рестарт |
| `set-fallback` | Запис на offline fallback задачи |
| `get-fallback` | Изискване на fallback конфигурацията |
| `update` | OTA firmware update |
| `restart` | Рестарт на Nano ESP32 |

### Отговори и статус

| Topic след BASE | Съдържание |
|---|---|
| `modbus/response` | Резултат от локален Modbus |
| `inputs/response` | Резултат от локалните входове |
| `lora/response` | Резултат от Pro Mini |
| `lora/scan_results` | Открити LoRa nodes |
| `config-response` | Текуща конфигурация |
| `fallback-state` | Записани fallback задачи |
| `system/diag` | Uptime, heap, температура и network |
| `system/devices` | Всички известни LoRa/Modbus устройства и локални входове |
| `system/crash_report` | Данни след възстановяване от crash |
| `online` | Стойност `1` при heartbeat |
| `version` | Firmware версия |
| `device_info` | Тип на устройството |
| `temperature` | Температура в °C |
| `log` | Текстови runtime логове |

## 6. Локален Modbus RTU

### Четене на holding register

Publish към:

```text
factory/sofia/pump_station/gateway_1/modbus/request
```

Payload:

```json
{
  "device": "inverter_1",
  "parameterId": 101,
  "slaveId": 1,
  "function": 3,
  "address": 100,
  "quantity": 2,
  "dataType": "float",
  "scale": 1.0,
  "unit": "V"
}
```

Примерен успешен response в `BASE/modbus/response`:

```json
{
  "device": "inverter_1",
  "parameterId": 101,
  "slaveId": 1,
  "function": 3,
  "address": 100,
  "quantity": 2,
  "dataType": "float",
  "scale": 1.0,
  "unit": "V",
  "data": [230.5],
  "status": "ok"
}
```

Пример за Modbus грешка:

```json
{
  "slaveId": 1,
  "function": 3,
  "address": 100,
  "quantity": 2,
  "status": "error",
  "code": 226
}
```

`code` е резултатът от ModbusMaster.

### Поддържани функции при локален Modbus

| Function | Операция |
|---:|---|
| 1 | Read Coils |
| 2 | Read Discrete Inputs |
| 3 | Read Holding Registers |
| 4 | Read Input Registers |
| 5 | Write Single Coil |
| 6 | Write Single Register |
| 16 | Write Multiple Registers |

### Запис на single register

```json
{
  "slaveId": 1,
  "function": 6,
  "address": 40125,
  "value": 1000,
  "parameterId": 102
}
```

### Запис на multiple registers

```json
{
  "slaveId": 1,
  "function": 16,
  "address": 200,
  "values": [10, 20, 30],
  "parameterId": 103
}
```

### Data types

| `dataType` | Обработка |
|---|---|
| `raw` | Unsigned 16-bit register |
| `int16` | Signed 16-bit |
| `float` | 32-bit float от два последователни register-а |
| `int32` | Signed 32-bit от два register-а |
| `uint32` | Unsigned 32-bit от два register-а |

`scale` се умножава върху прочетената стойност.

## 7. Локални входове на Nano ESP32

Разрешени са само:

```text
D4, D5, D6
```

Те са конфигурирани като `INPUT_PULLUP`:

- отворен вход → `1`;
- вход към GND → `0`.

### Четене на един вход

Publish към `BASE/inputs/request`:

```json
{
  "target": "inputs",
  "function": 1,
  "address": 4,
  "quantity": 1,
  "parameterId": 201
}
```

Response в `BASE/inputs/response`:

```json
{
  "target": "inputs",
  "function": 1,
  "address": 4,
  "quantity": 1,
  "parameterId": 201,
  "data": [0],
  "status": "ok"
}
```

### Четене на трите входа

```json
{
  "target": "inputs",
  "function": 1,
  "address": 4,
  "quantity": 3,
  "parameterId": 202
}
```

Примерен response:

```json
{
  "target": "inputs",
  "function": 1,
  "address": 4,
  "quantity": 3,
  "parameterId": 202,
  "data": [1, 0, 1],
  "status": "ok"
}
```

При неразрешен пин:

```json
{
  "target": "inputs",
  "function": 1,
  "address": 7,
  "quantity": 1,
  "status": "invalid_pin",
  "allowedPins": "D4,D5,D6"
}
```

## 8. LoRa сканиране

Publish празен payload към:

```text
BASE/lora/scan_request
```

Nano ESP32 изпраща broadcast `PING` и чака отговори около 12 секунди.

Резултатът се публикува в:

```text
BASE/lora/scan_results
```

Пример:

```json
[
  {
    "n": 2,
    "status": "pong",
    "rssi": -74,
    "snr": 8.25,
    "uptime": 3600
  },
  {
    "n": 3,
    "status": "pong",
    "rssi": -81,
    "snr": 5.5,
    "uptime": 125
  }
]
```

Полета:

- `n` — Pro Mini `NODE_ID`;
- `rssi` — сила на получения сигнал;
- `snr` — signal-to-noise ratio;
- `uptime` — uptime на Pro Mini в секунди.

Стар Pro Mini без uptime пак може да се покаже, но без поле `uptime`.

## 9. Modbus през LoRa и Pro Mini

Publish към:

```text
BASE/lora/request
```

Пример за read discrete inputs от Modbus slave зад Pro Mini:

```json
{
  "nodeId": 2,
  "target": "modbus",
  "parameterId": 301,
  "slaveId": 1,
  "function": 2,
  "address": 0,
  "quantity": 1,
  "dataType": "raw",
  "scale": 1.0
}
```

Приема се и:

```json
"target": "lora_modbus"
```

Примерен response в `BASE/lora/response`:

```json
{
  "nodeId": 2,
  "target": "modbus",
  "parameterId": 301,
  "slaveId": 1,
  "function": 2,
  "address": 0,
  "quantity": 1,
  "dataType": "raw",
  "scale": 1.0,
  "status": "ok",
  "data": [1]
}
```

Pro Mini поддържа Modbus функции `1`, `2`, `3`, `4`, `5`, `6`, `15` и `16`.

При timeout Nano ESP32 записва warning в `BASE/log` и публикува корелиран
response в `BASE/lora/response`. Ако заявката съдържа `command_id`, той се
връща без промяна:

```json
{
  "command_id": "cmd-example",
  "nodeId": 2,
  "status": "timeout",
  "error": "no_response_from_lora_node",
  "round_trip_ms": 5000
}
```

Command widget-ът изпраща команда до три пъти в рамките на една минута.
Успех се показва само след `status: "ok"`; при Modbus write функции `5`, `6`,
`15` и `16` стойността се прочита обратно преди крайното потвърждение. След
изтичането на минутата командата получава статус `expired` и не се изпраща
повече, така че стара команда не може да се изпълни по-късно.

## 10. Входове на Pro Mini през LoRa

Разрешени са `D4`, `D5` и `D6` на Pro Mini.

Publish към `BASE/lora/request`:

```json
{
  "nodeId": 2,
  "target": "inputs",
  "function": 1,
  "address": 4,
  "quantity": 3,
  "parameterId": 401,
  "dataType": "raw",
  "scale": 1.0
}
```

Приема се и:

```json
"target": "lora_inputs"
```

Примерен response:

```json
{
  "nodeId": 2,
  "target": "inputs",
  "function": 1,
  "address": 4,
  "quantity": 3,
  "parameterId": 401,
  "dataType": "raw",
  "scale": 1.0,
  "status": "ok",
  "data": [1, 1, 0]
}
```

## 11. Системна диагностика

На приблизително две секунди gateway-ът публикува:

```text
BASE/system/diag
```

Пример при Wi-Fi:

```json
{
  "temp": "25.4",
  "free_heap": 265120,
  "min_free_heap": 250840,
  "cpu_freq": 240,
  "uptime": 7200,
  "network": "wifi",
  "network_status": "connected",
  "ip": "192.168.1.55",
  "signal": -61,
  "lora_queue_size": 3,
  "lora_queue_max": 20,
  "lora_busy": true,
  "lora_active_node_id": 2,
  "lora_active_request_ms": 1840,
  "lora_queue_peak": 4,
  "lora_dropped_requests": 0,
  "lora_expired_requests": 0,
  "lora_backoff_deferrals": 2
}
```

`lora_queue_size` е броят чакащи LoRa заявки, без заявката, която в момента
се обработва. `lora_queue_max` е максималният капацитет; при достигането му
новите заявки се отказват и се записва warning в системния log.

`lora_busy` показва дали се обработва заявка в момента. При активна заявка
`lora_active_node_id` е крайният node, а `lora_active_request_ms` е изминалото
време. Когато няма активна заявка, двете стойности са `0`.
`lora_queue_peak` пази най-големия брой чакащи заявки след рестарта, а
`lora_dropped_requests` брои отказаните заявки при пълна опашка. Тези два
брояча се нулират при рестарт. `lora_expired_requests` брои заявки, които са
чакали по-дълго от допустимото и не са изпратени със стара стойност.
`lora_backoff_deferrals` показва колко пъти заявка е била отложена, защото
нейният node е в кратък backoff след timeout.

Опашката подрежда write командите и payload-и с `command_id` пред обикновените
read заявки. По желание заявката може да зададе `priority: 0..2` (0 е
най-висок) и `queue_ttl_ms` между 5000 и 300000 ms. По подразбиране срокът е
60000 ms. След timeout gateway-ът прилага отделен експоненциален backoff за
конкретния node (до 30 секунди), без да блокира останалите LoRa устройства.

### Подписан OTA manifest

Съществуващият URL OTA остава активен. Допълнително firmware 1.4.0 приема
подписан manifest на `BASE/update-manifest` със структура:

```json
{
  "board": "arduino_nano_esp32",
  "version": "1.4.0",
  "url": "https://example.com/firmware.bin",
  "sha256": "64 hexadecimal characters",
  "signature": "HMAC-SHA256(version|url|sha256, admin_password)"
}
```

Nano проверява подписа преди download и SHA-256 преди активиране. При грешка
update-ът се прекратява и старата версия продължава да работи.

При GSM `signal` е CSQ стойността от модема. При LAN стойността е `0`.

На същия heartbeat се публикува и категоризиран inventory:

```text
BASE/system/devices
```

Пример:

```json
{
  "gateway_uptime_ms": 14644976,
  "lora_devices": [
    {
      "node_id": 1,
      "status": "online",
      "consecutive_failures": 0,
      "last_request_ms": 14644081,
      "last_seen_ms": 14644976,
      "round_trip_ms": 895,
      "rssi": -55,
      "snr": 9.75,
      "services": ["modbus", "inputs"]
    },
    {
      "node_id": 2,
      "status": "offline",
      "consecutive_failures": 3,
      "last_request_ms": 14643000,
      "services": ["modbus"]
    }
  ],
  "modbus_devices": [
    {
      "slave_id": 1,
      "status": "online",
      "last_code": 0,
      "last_request_ms": 14644000,
      "last_seen_ms": 14644000
    }
  ],
  "digital_inputs": {
    "status": "configured",
    "last_request_ms": 14642000,
    "channels": [
      {"pin": "D4", "value": 1},
      {"pin": "D5", "value": 0},
      {"pin": "D6", "value": 1}
    ]
  },
  "analog_inputs": {
    "status": "configured",
    "adc_bits": 12,
    "adc_max": 4095,
    "last_request_ms": 14641000,
    "channels": [
      {"pin": "A6", "value": 2048},
      {"pin": "A7", "value": 1024}
    ]
  }
}
```

`lora_devices` съдържа node IDs, към които gateway-ът изпраща актуални заявки.
Ако към даден node няма заявка 60 секунди, той автоматично се премахва от
inventory списъка. При възстановяване на заявките се появява отново с нов
статус. Така node, преместен към друг gateway, не остава записан до рестарт.
`services` показва типовете заявки към съответния node. `modbus_devices`
съдържа локалните RS-485 slave IDs, към които е имало заявки. Старият
`BASE/system/device/status` остава активен за съвместимост.

Допълнително се публикуват:

```text
BASE/online       -> 1
BASE/version      -> 1.0.1
BASE/device_info  -> arduino nano esp32
BASE/temperature  -> 25.4
```

Ако free heap падне под 15000 bytes, firmware-ът записва crash информация и
рестартира gateway-а.

## 12. Изискване на конфигурацията

Publish празен payload към:

```text
BASE/get-config
```

Response в `BASE/config-response`:

```json
{
  "com1_baud_rate": "9600",
  "wifi_ssid": "MyWiFi",
  "wifi_pass": "configured-password",
  "mqtt_server": "broker.example.com",
  "mqtt_port": "1883",
  "mqtt_user": "device-user",
  "mqtt_pass": "configured-password",
  "static_ip": "192.168.0.55",
  "network_mode": "wifi",
  "gsm_apn": "internet",
  "gsm_user": "",
  "gsm_pin": "",
  "user_name": "factory",
  "address": "sofia",
  "object_name": "pump_station",
  "device_id": "gateway_1",
  "version": "1.0.1",
  "device_info": "arduino nano esp32"
}
```

Забележка: текущият firmware връща и записаните пароли. Използвай защитен и
контролиран MQTT broker и не давай достъп до config topics на обикновени
клиенти.

## 13. Обновяване на конфигурацията през MQTT

Publish към:

```text
BASE/update-config
```

Пример:

```json
{
  "network_mode": "wifi",
  "ssid": "MyWiFi",
  "pass": "MyWiFiPassword",
  "mqttip": "broker.example.com",
  "mqttport": "1883",
  "mqttuser": "device-user",
  "mqttpass": "device-password",
  "user_name": "factory",
  "address": "sofia",
  "object_name": "pump_station",
  "device_id": "gateway_1",
  "com1_baud_rate": "9600",
  "staticip": "192.168.1.55",
  "gsm_apn": "internet",
  "gsm_user": "",
  "gsm_pass": "",
  "gsm_pin": ""
}
```

Допустими `network_mode` стойности:

```text
wifi
lan
gsm
```

След запис gateway-ът автоматично се рестартира. Ако промениш identity
полетата, след рестарта `BASE` topic също се променя.

## 14. Restart

Publish произволен payload към:

```text
BASE/restart
```

Пример:

```text
1
```

Gateway-ът се рестартира след около една секунда.

## 15. Обновяване на firmware (OTA)

Firmware-ът поддържа два независими OTA начина:

1. директно качване от PlatformIO през локалната Wi-Fi мрежа;
2. изтегляне на `firmware.bin` от URL след MQTT команда.

OTA от URL не се заменя или изключва при активиране на PlatformIO Wi-Fi OTA.
Двата начина могат да се използват независимо.

### 15.1. Първоначално активиране на Wi-Fi OTA

Поддръжката на Wi-Fi upload трябва първо да попадне във firmware-а на
платката. Затова първото качване се прави еднократно през USB:

```sh
cd "/Users/svetlozarradev/Desktop/Projects/Dashboard/whole-dashboard-project/ardiono nanp esp32"

"$HOME/.platformio/penv/bin/pio" run \
  -e arduino_nano_esp32 \
  -t upload
```

След рестарта Nano ESP32 трябва да бъде в режим `network_mode=wifi` и да се
свърже към същата локална мрежа, в която е компютърът. В Serial/MQTT лога се
появява:

```text
[OTA] PlatformIO WiFi upload ready: nano-esp32.local:3232
```

Wi-Fi OTA използва:

```text
Host: nano-esp32.local
Port: 3232
Password: стойността на adminPass
```

Паролата по подразбиране е `admin147258`. Стойността на `--auth` в
`platformio.ini` трябва да съвпада с текущата администраторска парола на
устройството. Ако `adminPass` е променена през setup портала, промени и:

```ini
[env:arduino_nano_esp32_ota]
upload_flags =
    --auth=НОВАТА_ADMIN_ПАРОЛА
    --port=3232
```

### 15.2. Качване през Wi-Fi с PlatformIO

След еднократното USB качване следващите версии могат да се качват с:

```sh
cd "/Users/svetlozarradev/Desktop/Projects/Dashboard/whole-dashboard-project/ardiono nanp esp32"

"$HOME/.platformio/penv/bin/pio" run \
  -e arduino_nano_esp32_ota \
  -t upload
```

Ако mDNS името `nano-esp32.local` не се открива, използвай IP адреса, който
се вижда в network лога или monitoring панела:

```sh
"$HOME/.platformio/penv/bin/pio" run \
  -e arduino_nano_esp32_ota \
  -t upload \
  --upload-port 192.168.1.50
```

За Wi-Fi upload:

- компютърът и Nano ESP32 трябва да имат директна свързаност в една LAN/VPN;
- firewall-ът трябва да допуска UDP порт `3232` към платката и обратната TCP
  връзка от платката към компютъра;
- `network_mode` трябва да бъде `wifi` — PlatformIO OTA не се стартира през
  W5500 или SIM7600;
- не прекъсвай захранването, докато upload-ът не завърши и платката не се
  рестартира;
- ако платката още няма firmware с `ArduinoOTA`, първо отново качи през USB.

Във VS Code може да се избере environment `arduino_nano_esp32_ota` от
PlatformIO и след това `Upload`. За нормално USB качване се избира
`arduino_nano_esp32`.

### 15.3. Build за OTA от URL

Компилирай стандартния environment:

```sh
cd "/Users/svetlozarradev/Desktop/Projects/Dashboard/whole-dashboard-project/ardiono nanp esp32"

"$HOME/.platformio/penv/bin/pio" run -e arduino_nano_esp32
```

Готовият файл за URL OTA е:

```text
.pio/build/arduino_nano_esp32/firmware.bin
```

Не използвай `bootloader.bin`, `partitions.bin` или `firmware.elf`. Качи
`firmware.bin` на HTTP/HTTPS сървър, който връща директно файла с валиден
`Content-Length`, например:

```text
https://server.example.com/firmware/nano-esp32-1.3.2.bin
```

Провери URL адреса преди update:

```sh
curl -I https://server.example.com/firmware/nano-esp32-1.3.2.bin
```

Очаква се успешен HTTP status и размерът на файла да присъства като
`Content-Length`.

### 15.4. Стартиране на OTA от URL чрез MQTT

Publish към:

```text
BASE/update
```

```json
{
  "cmd": "update",
  "url": "https://raw.githubusercontent.com/RadevSvetlozar/pico_new/main/firmware.bin"
}
```

Пълният topic например е:

```text
factory/sofia/pump_station/gateway_1/update
```

При успешен download firmware-ът се записва и Nano ESP32 се рестартира.
Следи `BASE/log` за HTTP status, размер, записани bytes и съобщението за
завършено обновяване.

В текущата версия HTTP OTA трябва да се използва през Wi-Fi. MQTT transport-ът
работи през Wi-Fi, W5500 и SIM7600, но HTTP download слоят все още не избира
автоматично EthernetClient/TinyGsmClient.

Пример с `mosquitto_pub`:

```sh
mosquitto_pub -h broker.example.com \
  -u device-user -P device-password \
  -t 'factory/sofia/pump_station/gateway_1/update' \
  -m '{"cmd":"update","url":"https://server.example.com/firmware/nano-esp32-1.3.2.bin"}'
```

URL трябва да бъде достъпен от самото Nano ESP32, а не само от компютъра.
Не прекъсвай захранването по време на download или flash запис.

## 16. Offline fallback

Fallback задачите се изпълняват приблизително на 10 секунди, когато MQTT не е
свързан.

### Запис на fallback

Publish JSON array към:

```text
BASE/set-fallback
```

Пример за локален Modbus write:

```json
[
  {
    "enabled": true,
    "protocol": "modbus_rtu",
    "payload": {
      "slaveId": 1,
      "function": 6,
      "address": 40125,
      "value": 0
    }
  }
]
```

Поддържани fallback protocol стойности:

- `modbus_rtu`;
- `mqtt`;
- `http`.

MQTT fallback няма да успее, ако MQTT връзката действително е offline, но е
оставен за съвместимост със съществуващи конфигурации.

HTTP fallback в текущата версия трябва да се използва само при Wi-Fi
transport.

### Прочитане на fallback

Publish към:

```text
BASE/get-fallback
```

Записаният array се връща в:

```text
BASE/fallback-state
```

## 17. Factory reset

Отвори `Administrator`, въведи администраторските данни и натисни
`Factory Reset`. Браузърът показва и допълнително потвърждение.

Операцията изтрива Preferences конфигурацията и рестартира устройството.
Следващото стартиране използва firmware default стойностите, включително
администраторска парола `admin147258`. Старият публичен адрес `/reset` е
деактивиран.

## 18. Пример с Mosquitto

Абониране за всички topics:

```sh
mosquitto_sub -h broker.example.com \
  -u device-user -P device-password \
  -t 'factory/sofia/pump_station/gateway_1/#' -v
```

Локален вход D4:

```sh
mosquitto_pub -h broker.example.com \
  -u device-user -P device-password \
  -t 'factory/sofia/pump_station/gateway_1/inputs/request' \
  -m '{"target":"inputs","function":1,"address":4,"quantity":1,"parameterId":201}'
```

LoRa scan:

```sh
mosquitto_pub -h broker.example.com \
  -u device-user -P device-password \
  -t 'factory/sofia/pump_station/gateway_1/lora/scan_request' \
  -m '{}'
```

## 19. Най-чести проблеми

### Access Point не се появява

- CONFIG бутонът трябва да е задържан по време на reset/startup.
- Провери D0 към GND.
- Провери захранването и Serial log-а.

### MQTT не се свързва

- Провери избрания network mode.
- Провери broker адрес, port, user и password.
- При LAN провери W5500 link LED и DHCP.
- При GSM провери SIM PIN, APN, антена и GSM захранването.

### Pro Mini се вижда при scan, но не отговаря на заявки

- Качи съвместимия текущ firmware и на Pro Mini.
- Провери еднакви frequency, sync word и encryption key.
- Провери `nodeId`.

### LoRa timeout

- Провери 868 MHz антените.
- Провери захранването и LoRa bulk capacitor-а.
- Провери `nodeId`.
- Провери дали Pro Mini не е зает с дълга Modbus операция.

### Входът винаги е `1`

- При `INPUT_PULLUP` входът става `0` само когато е свързан към GND.
- Не подавай 12/24 V директно към D4/D5/D6.
