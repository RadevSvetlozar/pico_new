# Структура на firmware-а

Пълно ръководство за Access Point, конфигурация, MQTT topics и JSON примери:
`../USER_GUIDE_BG.md`.

Пълна документация за motherboard връзките и захранването:
`../HARDWARE_CONNECTIONS_BG.md`.

- `main.cpp` — само стартиране на компонентите и главен цикъл.
- `App.h` — общ интерфейс между модулите, пинове и споделени типове.
- `App.cpp` — споделени обекти, настройки и MQTT topic/log помощни функции.
- `Config.cpp` — Preferences, factory reset и web setup portal.
- `MqttService.cpp` — MQTT subscriptions и обработка на конфигурацията.
- `ModbusService.cpp` — Modbus RTU заявки и offline fallback задачи.
- `LoraService.cpp` — LoRa инициализация, сканиране, опашка и отговори.
- `OtaService.cpp` — сваляне и инсталиране на firmware update.
- `SystemHealth.cpp` — LED статус, heartbeat, температура и crash report.

## RGB статус

- Червено мигане: няма мрежова връзка.
- Жълто мигане: мрежата е свързана, но MQTT не е свързан.
- Постоянно зелено: мрежата и MQTT са свързани.
- Кратък син сигнал на всеки 2.4 секунди: BLE е достъпен (150 ms) или
  има активна BLE връзка (300 ms). Между сините сигнали се вижда мрежовият статус.
- Лилаво мигане: конфигурационен Access Point; бяло мигане: стартиране.

При вече свързана мрежа MQTT опитите продължават и при активна BLE връзка.
Serial логът показва broker адреса, порта и кода при неуспешен MQTT опит.

В Bluetooth конфигурацията успешният admin вход прочита и записаните Wi-Fi,
MQTT, GSM и admin пароли и SIM PIN. „Покажи паролите и SIM PIN“ позволява
сверяване на стойностите. Изходът и прекъсването на BLE изчистват паролите
от формата. Обикновеното `get_config` не връща тези тайни; необходимо е
обновяване както на frontend-а, така и на gateway firmware-а.

## Добавяне на функционалност

Публичните функции между модулите се декларират в `App.h`. Локални помощни
функции се поставят в анонимен `namespace` в съответния `.cpp`, за да не
замърсяват общия интерфейс.

## Routing на заявки

MQTT transport-ът определя кой модул на gateway-а приема заявката:
`modbus/request` изпълнява локалния RS485, а `lora/request` препраща към
отдалечения node. Полето `target` определя коя физическа услуга я изпълнява.

Съществуващите заявки без `target` остават валидни и се приемат като
`"target": "modbus"`. За Modbus през LoRa се използва:

```json
{
  "nodeId": 2,
  "target": "modbus",
  "address": 0,
  "slaveId": 1,
  "function": 2,
  "quantity": 1
}
```

Приемат се и routing имената `lora_modbus`, `lora_inputs`, `lora_outputs` и
`lora_can`. В момента Pro Mini изпълнява `modbus`; другите target-и връщат
`unsupported_function`, докато съответният хардуерен service бъде добавен.

При PING новият Pro Mini връща своя uptime в секунди, свободната SRAM като
`free_heap` и измереното захранване като `supply_mv`. Новият PONG е 16 байта,
а gateway-ът продължава да приема старите 8-, 12- и 14-байтови PONG пакети.

```json
[
  { "n": 2, "status": "pong", "rssi": -74, "snr": 8.25, "uptime": 3600, "free_heap": 1248, "supply_mv": 3290 }
]
```

Gateway-ът приема и стария `PONG` без uptime/free heap/напрежение, затова
по-стар Pro Mini остава съвместим.

`lora/scan_request` използва отделен `TOPOLOGY_REPORT`. Новият Pro Mini добавя
същите диагностични стойности и към всеки node в `lora/scan_results`:

```json
{
  "node_id": 1,
  "status": "reachable",
  "uptime": 180190,
  "free_heap": 1248,
  "supply_mv": 3290,
  "supply_v": 3.29,
  "temperature_c": 42.6
}
```

Старият topology report без тези полета продължава да се приема и препраща.

При topology scan Nano ESP32 проверява температурата на всеки Pro Mini:

- от `60.0 °C` се записва `WARN` за висока температура;
- от `75.0 °C` се записва `ERROR` за критична температура;
- невалидно измерване (под `-100 °C` или над `150 °C`) се записва като
  `ERROR` за температурния сензор.

Тези съобщения се публикуват както в общия `<client-id>/log`, така и в
`<client-id>/system/log`, предназначен само за warnings и errors.

## Цифрови входове

И на Nano ESP32, и на Pro Mini са разрешени само свободните пинове `D4`, `D5`
и `D6`. Те се инициализират като `INPUT_PULLUP`:

- отворен вход се прочита като `1`;
- вход, свързан към `GND`, се прочита като `0`;
- към входовете не трябва да се подава напрежение над работното напрежение на
  платката.

Локално четене от Nano ESP32:

Topic: `<gateway>/inputs/request`

```json
{
  "target": "inputs",
  "function": 1,
  "address": 4,
  "quantity": 3,
  "parameterId": 10
}
```

Отговорът се публикува в `<gateway>/inputs/response`. Полето `data` съдържа
стойностите на `D4`, `D5` и `D6`.

Четене от Pro Mini през LoRa:

Topic: `<gateway>/lora/request`

```json
{
  "nodeId": 2,
  "target": "inputs",
  "function": 1,
  "address": 4,
  "quantity": 3,
  "parameterId": 11
}
```

Отговорът се публикува в `<gateway>/lora/response`. Може да се прочете само
един вход с `quantity: 1`, например `address: 5` за `D5`.

## Motherboard мрежови модули

В Access Point портала може да се избере основен transport:

- `wifi` — default;
- `lan` — W5500 с DHCP;
- `gsm` — SIM7600 с APN.

Изборът се записва в Preferences и се прилага след рестарт. MQTT topic-ите и
payload-ите не зависят от избрания transport.

### Pinout

| Функция | Nano ESP32 pin |
|---|---|
| LoRa DIO0 | D2 |
| LoRa RESET | D9 |
| LoRa CS | D10 |
| Shared SPI MOSI/MISO/SCK | D11/D12/D13 |
| W5500 CS | D7 |
| W5500 INT | D8 |
| SIM7600 power enable | A1 |
| SIM7600 ESP RX / modem TX | A2 |
| SIM7600 ESP TX / modem RX | A3 |
| RS485 RX / module RO/TXD | A5 |
| RS485 TX / module DI/RXD | A4 |
| Thermistor | A0 |
| Digital inputs | D4/D5/D6 |

W5500 и LoRa споделят SPI шината, но имат отделни CS линии. Всички слотове
трябва да държат своя CS в HIGH, когато модулът не използва шината.

SIM7600 трябва да има отделно стабилно захранване според конкретния carrier
board. GSM захранването не трябва да се взема директно от 3.3 V пина на Nano
ESP32.

### Setup portal monitoring

Страницата `http://192.168.10.1` показва на всеки две секунди:

- избран transport и connection status;
- MQTT status;
- uptime;
- free heap;
- температура от NTC термистора.

Публичната страница съдържа само monitoring и WiFi SSID/password. Advanced
network, GSM, MQTT и Factory Reset са в `/admin` и са защитени с HTTP Basic
Authentication. Началните данни след factory reset са `admin` /
`admin147258`; паролата се сменя от администраторската страница и се записва
в Preferences. Администраторската форма съдържа WiFi, static IP, MQTT
credentials, gateway identity, Modbus baud rate и GSM настройките. Monitoring
се вижда и в администраторската страница.

Heartbeat продължава да публикува status topics и `system/diag`, но не записва
периодични heartbeat съобщения в `<gateway>/log`.

Същите данни, плюс IP и signal quality, се публикуват в
`<gateway>/system/diag`.

От firmware 1.4.1 heartbeat публикува и retained `<gateway>/uid`. Стойността е
точно `mqttClientName`, използван от PubSubClient като MQTT Client ID. Същата
стойност присъства като `uid` и `mqtt_client_id` в `system/diag` и
`system/devices`.

### Provisioning режим през MQTT

Текущият режим се публикува при MQTT connect и при heartbeat в:

```text
<client-id>/system/provisioning/status
```

Полетата `provisioning_mode`, `legacy_provisioning` и
`self_service_onboarding` присъстват и в `system/diag`. Режимът присъства и в
root-а на `system/devices`.

За заявка на статуса публикувай произволен payload в:

```text
<client-id>/system/provisioning/get
```

За промяна използвай:

```text
<client-id>/system/provisioning/set
```

с JSON payload:

```json
{ "mode": "hybrid", "restart": false }
```

Разрешени са само `legacy`, `self_service` и `hybrid`. Настройката се записва
в NVS веднага. `restart:true` рестартира платката след публикуване на status;
по подразбиране рестарт не е необходим. Broker ACL трябва да разрешава write
към `system/provisioning/set` само на административния backend/client.

Проектът се проверява с:

```sh
platformio run
```

## Логове

Всички модули използват общ формат:

```text
[12345ms][INFO][MODBUS] Transaction completed successfully
```

Нивата са `DEBUG`, `INFO`, `WARN` и `ERROR`. Логовете винаги се изпращат към
Serial на `115200 baud`, а при активна MQTT връзка се публикуват и в topic
`<client-id>/log`. Само проблемите с ниво `WARN` или `ERROR` се публикуват
допълнително в `<client-id>/system/log`, така че dashboard-ът може да слуша
само този topic. За наблюдение на всички gateway-и може да се използва MQTT
wildcard `+/+/+/+/system/log`, понеже client id е съставен от четири сегмента.
WiFi и MQTT статусът се записват само при промяна, за да не се препълва
изходът във всеки `loop`.

За всяка LoRa заявка Serial Monitor показва node, request id, target,
`round_trip_ms`, RSSI и SNR. Времето започва непосредствено преди изпращането
към радиомодула и приключва при валиден отговор. При timeout също се показва
изминалото време. Успешният MQTT отговор в `<client-id>/lora/response`
съдържа същите полета `round_trip_ms`, `rssi` и `snr`.

## Мониторинг на LoRa устройствата

Gateway-ът публикува здравния статус на устройствата в:

```text
<client-id>/system/device/status
```

Payload пример:

```json
{
  "node_id": 2,
  "status": "online",
  "consecutive_failures": 0,
  "gateway_uptime_ms": 123456,
  "round_trip_ms": 438,
  "rssi": -74,
  "snr": 8.25
}
```

Статусите са:

- `online` — устройството е върнало валиден отговор;
- `degraded` — има един или два поредни timeout-а;
- `offline` — има три или повече поредни timeout-а.

При възстановяване броячът се нулира и устройството отново става `online`.
Преминаването към `offline` се публикува и в `<client-id>/system/log`.
Единичните timeout-и и възстановяването остават в Serial и status topic-а,
без да пълнят problem log-а. Статусът се определя от реалните LoRa заявки:
устройство, към което не се изпращат заявки, не може да бъде проверено.

## Аналогови входове (ADC)

На Nano ESP32 за потребителски ADC входове са разрешени само `A6` и `A7`.
Останалите аналогови пинове са заети от термистор, GSM и RS485. Локална
заявка за четене на двата входа:

Topic: `<gateway>/analog-inputs/request`

```json
{
  "target": "analog_inputs",
  "function": 1,
  "address": 6,
  "quantity": 2,
  "parameterId": 30
}
```

Отговорът е в `<gateway>/analog-inputs/response` и връща 12-bit сурови
стойности от 0 до 4095:

```json
{
  "target": "analog_inputs",
  "status": "ok",
  "adc_bits": 12,
  "adc_max": 4095,
  "data": [2048, 1024]
}
```

На Pro Mini са разрешени `A1` до `A7`; `A0` остава запазен за термистора.
Четене на `A1` през LoRa:

Topic: `<gateway>/lora/request`

```json
{
  "nodeId": 2,
  "target": "analog_inputs",
  "function": 1,
  "address": 1,
  "quantity": 1,
  "dataType": "uint16",
  "scale": 1,
  "parameterId": 31
}
```

Отговорът в `<gateway>/lora/response` съдържа 10-bit стойност от 0 до 1023,
`adc_bits`, `adc_max`, `round_trip_ms`, RSSI и SNR. Приемат се и target
имената `lora_analog_inputs`. Всички ADC входове са само за напрежение в
диапазона `0–3.3 V`; по-високо напрежение изисква външен делител и защита.

## Опционално LoRa препредаване (TTL)

Всички LoRa заявки остават директни по подразбиране. Ако полето `ttl` липсва
или е `0`, gateway-ът изпраща стария бинарен формат и никоя чужда платка не
препредава пакета. Relay режимът се включва единствено с `ttl` от 1 до 3:

```json
{
  "nodeId": 8,
  "target": "modbus",
  "slaveId": 1,
  "function": 2,
  "address": 3,
  "quantity": 1,
  "ttl": 1
}
```

`ttl: 1` разрешава един relay, `ttl: 2` — до два, а `ttl: 3` — до три.
Pro Mini пази cache за последните 16 request/response пакета. Вече видян
пакет не се изпълнява или препредава повторно. Преди relay има случаен jitter
между 150 и 800 ms. Препредаването работи и за заявката, и за отговора.

Gateway timeout-ът се пресмята автоматично:

```text
timeout_ms = 5000 + ttl * 3500
```

Gateway опашката е ограничена до 20 заявки. Write командите и заявките с
`command_id` са с висок приоритет, всяка заявка има срок (60 секунди по
подразбиране), а node с timeout получава отделен експоненциален backoff до
30 секунди. Така проблемен node не блокира комуникацията с останалите nodes.

Успешният отговор при TTL съдържа `hops` и `relayed`. Ако целевата платка се
чува директно, тя може да отговори преди relay-а; тогава `hops` е `0` и
`relayed` е `false`. Всеки relay увеличава airtime и трябва да се отчита при
планирането на заявките и допустимия duty cycle.

## Topology scan

Сканирането се стартира само при MQTT команда и не работи автоматично:

Topic: `<gateway>/lora/scan_request`

```json
{ "ttl": 2 }
```

При празен payload се използва `ttl: 2`. Допустимите стойности са от 0 до 3.
Scan-ът е неблокиращ за MQTT и продължава `8000 + ttl * 7000 ms`. Докато е
активен, нормалните LoRa заявки остават в опашката.

Всяка достигната Pro Mini връща пътя, по който discovery пакетът е стигнал
до нея. Node `0` означава gateway-а. Примерен `lora/scan_results`:

```json
{
  "scan_id": 42,
  "ttl": 2,
  "gateway_node_id": 0,
  "nodes": [
    {"node_id": 5, "radio_hops": 1, "relay_count": 0, "path": [5]},
    {"node_id": 8, "radio_hops": 2, "relay_count": 1, "path": [5, 8]}
  ],
  "links": [
    {"from": 0, "to": 5},
    {"from": 5, "to": 8}
  ],
  "node_count": 2
}
```

Това е rooted карта на достигнатите пътища от gateway-а, а не RF измерване
на всички алтернативни връзки между всяка възможна двойка платки. Поради
колизии повторен scan може да открие друг валиден път. `rssi_last_hop` и
`snr_last_hop` са качеството на report пакета, който gateway-ът е получил.

Serial мониторът се стартира с:

```sh
platformio device monitor
```

### 1.4.5 — временните onboarding тестове не са устройства

Заявки с `onboarding_test: true` се изпълняват и връщат обичайните корелирани отговори и логове, но не регистрират Modbus Slave ID / LoRa Node ID в `system/devices` и не променят health статуса на вече познато устройство. LoRa тестовете не публикуват и `system/device/status`. Топологичният scan остава отделен в `lora/scan_results`.

Устройство влиза в инвентара при успешен отговор на нормална (нетестова) заявка — например редовното четене след запис на устройството в базата. Само timeout към неизвестен адрес не създава видим запис. Познато устройство продължава да показва грешките от нормалните заявки.

Инвентарът е в RAM: обновяването на firmware и рестартът изчистват старите временни записи, без factory reset и без изтриване на настройки или устройства от базата. След това редовният трафик отново попълва реалните устройства. Не е необходима промяна на frontend/SQL/Node-RED за тази версия — текущите onboarding заявки вече съдържат флага.

Проверка: стартирай и прекрати тест към непотвърден Node/Slave ID; следващият `system/devices` не трябва да го съдържа. Успешен тест без натискане на „Запази“ също не добавя устройство. След запис и първия успешен редовен отговор устройството се появява.
