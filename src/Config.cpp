#include "App.h"

namespace {
constexpr const char* ADMIN_USER = "admin";

String generateClientId() {
  const uint64_t chipId = ESP.getEfuseMac();
  char id[23];
  snprintf(id, sizeof(id), "esp32-%04X%08X",
           static_cast<uint16_t>(chipId >> 32),
           static_cast<uint32_t>(chipId));
  return String(id);
}

String htmlEscape(const String& input) {
  String output;
  output.reserve(input.length() + 16);
  for (size_t i = 0; i < input.length(); ++i) {
    switch (input[i]) {
      case '&': output += F("&amp;"); break;
      case '<': output += F("&lt;"); break;
      case '>': output += F("&gt;"); break;
      case '"': output += F("&quot;"); break;
      case '\'': output += F("&#39;"); break;
      default: output += input[i]; break;
    }
  }
  return output;
}

bool requireAdministrator() {
  if (server.authenticate(ADMIN_USER, adminPass.c_str())) return true;
  appLog("WEB", "Rejected unauthenticated administrator request from " +
                    server.client().remoteIP().toString(), WARN);
  server.requestAuthentication(BASIC_AUTH, "Nano ESP32 administration",
                               "Administrator password required");
  return false;
}

String pageStart(const String& title) {
  return String(
      "<!doctype html><html><head><meta name='viewport' "
      "content='width=device-width,initial-scale=1'><style>"
      "body{font-family:Arial;max-width:720px;margin:24px auto;padding:0 14px;"
      "background:#f4f6f8;color:#18212b}.card{background:white;padding:18px;"
      "margin:14px 0;border-radius:12px;box-shadow:0 2px 12px #0001}"
      "label{display:block;margin-top:10px;font-weight:600}input,select{width:"
      "100%;box-sizing:border-box;padding:9px;margin-top:4px}button,.button{"
      "display:inline-block;padding:10px 16px;margin-top:14px;background:"
      "#1769aa;color:white;border:0;border-radius:7px;text-decoration:none;"
      "cursor:pointer}.danger{background:#b3261e}.grid{display:grid;"
      "grid-template-columns:repeat(2,1fr);gap:10px}.value{font-size:1.2em;"
      "font-weight:700}.note{color:#55606b;font-size:.92em}</style></head>"
      "<body><h2>") + title + " v" + firmwareVersion + "</h2>";
}

String monitoringCard() {
  return String(
      "<div class='card'><h3>Monitoring</h3><div class='grid'>"
      "<div>Network<div id='network' class='value'>-</div></div>"
      "<div>Status<div id='netstatus' class='value'>-</div></div>"
      "<div>Uptime<div id='uptime' class='value'>-</div></div>"
      "<div>Free heap<div id='heap' class='value'>-</div></div>"
      "<div>Temperature<div id='temp' class='value'>-</div></div>"
      "<div>MQTT<div id='mqtt' class='value'>-</div></div></div></div>");
}

String monitoringScript() {
  return String(
      "<script>async function status(){try{let r=await fetch('/status');"
      "let s=await r.json();for(let k of ['network','netstatus','uptime',"
      "'heap','temp','mqtt'])document.getElementById(k).textContent=s[k];}"
      "catch(e){}}status();setInterval(status,2000);</script>");
}
}

bool eraseStoredConfiguration() {
  if (!preferences.begin("config", false)) return false;
  const bool cleared = preferences.clear();
  preferences.end();
  return cleared;
}

void clearPreferences() {
  appLog("CONFIG", "Factory reset: clearing stored configuration", WARN);
  if (!eraseStoredConfiguration()) {
    appLog("CONFIG", "Failed to clear configuration; restart cancelled", ERROR);
    return;
  }
  appLog("CONFIG", "Configuration cleared; restarting");
  delay(1000);
  ESP.restart();
}

void saveFallback(const String& json) {
  appLog("CONFIG", "Saving fallback configuration, bytes=" +
                       String(json.length()));
  preferences.begin("config", false);
  const size_t written = preferences.putString("fallback", json);
  preferences.end();
  fallbackJson = json;
  const DeserializationError error =
      deserializeJson(fallbackDoc, fallbackJson);
  if (error) {
    appLog("CONFIG", "Saved fallback cannot be parsed: " +
                         String(error.c_str()), ERROR);
    return;
  }
  appLog("CONFIG", "Fallback saved, flash bytes=" + String(written) +
                       ", tasks=" + String(fallbackDoc.size()));
}

void loadPreferences() {
  appLog("CONFIG", "Reading configuration from Preferences", DEBUG);
  preferences.begin("config", true);
  mqttClientName = preferences.getString("mqttClientName", "");
  if (mqttClientName.isEmpty()) {
    mqttClientName = generateClientId();
  }

  userName = preferences.getString("user_name", "test");
  address = preferences.getString("address", "test");
  objectName = preferences.getString("object_name", "test");
  deviceId = preferences.getString("device_id", "test");
  com1BaudRate = preferences.getString("com1_baud_rate", "9600");
  wifiSsid = preferences.getString("ssid", "");
  wifiPass = preferences.getString("pass", "");
  mqttServer = preferences.getString("mqttip", "srdashboard.website");
  mqttPort = preferences.getString("mqttport", "1883");
  mqttUser = preferences.getString("mqttuser", "");
  mqttPass = preferences.getString("mqttpass", "");
  staticIp = preferences.getString("staticip", "192.168.0.55");
  networkMode = preferences.getString("network_mode", "wifi");
  if (networkMode != "wifi" && networkMode != "lan" &&
      networkMode != "gsm") {
    networkMode = "wifi";
  }
  gsmApn = preferences.getString("gsm_apn", "internet");
  gsmUser = preferences.getString("gsm_user", "");
  gsmPass = preferences.getString("gsm_pass", "");
  gsmPin = preferences.getString("gsm_pin", "");
  adminPass = preferences.getString("admin_pass", "admin147258");
  if (adminPass.length() < 8) adminPass = "admin147258";
  provisioningMode = preferences.getString("prov_mode", "hybrid");
  if (provisioningMode != "legacy" &&
      provisioningMode != "self_service" &&
      provisioningMode != "hybrid") {
    provisioningMode = "hybrid";
  }
  gsmBaudRate = preferences.getUInt("gsm_baud", 115200);
  fallbackJson = preferences.getString("fallback", "[]");
  preferences.end();

  const DeserializationError error =
      deserializeJson(fallbackDoc, fallbackJson);
  appLog("CONFIG", "Configuration loaded: client=" + getClientId() +
                       ", SSID=" + wifiSsid + ", broker=" + mqttServer +
                       ":" + mqttPort + ", network=" + networkMode +
                       ", Modbus baud=" + com1BaudRate);
  appLog("CONFIG", "Secrets loaded: WiFi password=" +
                       String(wifiPass.isEmpty() ? "missing" : "configured") +
                       ", MQTT password=" +
                       String(mqttPass.isEmpty() ? "missing" : "configured"),
         DEBUG);
  if (error) {
    appLog("CONFIG", "Fallback JSON parse error: " + String(error.c_str()),
           ERROR);
  } else {
    appLog("CONFIG", "Fallback tasks loaded: " + String(fallbackDoc.size()),
           DEBUG);
  }
}

void setupAccessPoint() {
  appLog("WEB", "Starting configuration access point");
  const IPAddress localIp(192, 168, 10, 1);
  const IPAddress subnet(255, 255, 255, 0);
  const bool apStarted = WiFi.softAP("ESP32-Setup", "147258369");
  const bool networkConfigured = WiFi.softAPConfig(localIp, localIp, subnet);
  appLog("WEB", "AP start=" + String(apStarted) +
                    ", network config=" + String(networkConfigured));

  server.on("/", HTTP_GET, []() {
    appLog("WEB", "GET / from " + server.client().remoteIP().toString(),
           DEBUG);
    String html = pageStart("Gateway Setup") + monitoringCard() +
        "<div class='card'><h3>WiFi</h3><form method='post' action='/save'>"
        "<label>WiFi SSID</label><input name='ssid' maxlength='32' value='" +
        htmlEscape(wifiSsid) +
        "' required><label>WiFi password</label><input type='password' "
        "name='pass' minlength='8' maxlength='63' placeholder='Unchanged when "
        "empty'><button type='submit'>Save WiFi and restart</button></form>"
        "<p class='note'>Advanced network, GSM, MQTT and factory reset require "
        "administrator access.</p><a class='button' href='/admin'>"
        "Administrator</a></div>" + monitoringScript() + "</body></html>";
    server.send(200, "text/html", html);
  });

  server.on("/status", HTTP_GET, []() {
    StaticJsonDocument<384> status;
    status["network"] = networkMode;
    status["netstatus"] = mqttClient.networkStatus();
    status["uptime"] = String(millis() / 1000) + " s";
    status["heap"] = String(ESP.getFreeHeap()) + " B";
    status["temp"] = String(readBoardTemperature(), 1) + " C";
    status["mqtt"] = mqttClient.isMqttConnected() ? "connected" : "offline";
    status["ip"] = WiFi.softAPIP().toString();
    String output;
    serializeJson(status, output);
    server.send(200, "application/json", output);
  });

  server.on("/save", HTTP_POST, []() {
    appLog("WEB", "POST /save received");
    const String ssid = server.arg("ssid");
    const String pass = server.arg("pass");
    if (ssid.isEmpty() || (!pass.isEmpty() && pass.length() < 8)) {
      server.send(400, "text/plain",
                  "SSID is required; WiFi password must be at least 8 chars");
      return;
    }
    preferences.begin("config", false);
    preferences.putString("ssid", ssid);
    if (!pass.isEmpty()) preferences.putString("pass", pass);
    preferences.end();
    appLog("WEB", "WiFi settings saved: SSID=" + ssid + ", password=" +
                      String(pass.isEmpty() ? "missing" : "configured"));
    server.send(200, "text/html", "Settings saved! ESP32 will reboot...");
    appLog("WEB", "HTTP response sent; restarting");
    delay(1500);
    ESP.restart();
  });

  server.on("/admin", HTTP_GET, []() {
    if (!requireAdministrator()) return;
    const String wifiSelected = networkMode == "wifi" ? " selected" : "";
    const String lanSelected = networkMode == "lan" ? " selected" : "";
    const String gsmSelected = networkMode == "gsm" ? " selected" : "";
    const String legacySelected = provisioningMode == "legacy" ? " selected" : "";
    const String selfServiceSelected =
        provisioningMode == "self_service" ? " selected" : "";
    const String hybridSelected = provisioningMode == "hybrid" ? " selected" : "";
    String html = pageStart("Administrator") +
        monitoringCard() +
        "<div class='card'><h3>Advanced connection</h3>"
        "<form method='post' action='/admin/save'>"
        "<label>Provisioning function</label><select name='prov_mode'>"
        "<option value='legacy'" + legacySelected +
        ">Legacy configuration (current behavior)</option>"
        "<option value='self_service'" + selfServiceSelected +
        ">Self-service onboarding only</option>"
        "<option value='hybrid'" + hybridSelected +
        ">Hybrid (legacy + self-service)</option></select>"
        "<p class='note'>Self-service accepts claim_device over BLE. Legacy "
        "keeps the existing save_config command. Default is Legacy.</p>"
        "<label>Primary network</label><select name='network_mode'>"
        "<option value='wifi'" + wifiSelected + ">WiFi (default)</option>"
        "<option value='lan'" + lanSelected + ">LAN - W5500</option>"
        "<option value='gsm'" + gsmSelected +
        ">GSM/LTE - SIM7600/A7670</option></select>"
        "<label>WiFi SSID</label><input name='ssid' maxlength='32' value='" +
        htmlEscape(wifiSsid) +
        "' required><label>WiFi password</label><input type='password' "
        "name='pass' minlength='8' maxlength='63' placeholder='Unchanged when "
        "empty'><label>Static IP</label><input name='staticip' value='" +
        htmlEscape(staticIp) +
        "' placeholder='192.168.1.55'><label>MQTT server</label><input "
        "name='mqttip' value='" + htmlEscape(mqttServer) +
        "' required><label>MQTT port</label><input type='number' min='1' "
        "max='65535' name='mqttport' value='" + htmlEscape(mqttPort) +
        "' required><label>MQTT user</label><input name='mqttuser' value='" +
        htmlEscape(mqttUser) +
        "'><label>MQTT password</label><input type='password' name='mqttpass' "
        "placeholder='Unchanged when empty'><label>User name</label><input "
        "name='user_name' value='" + htmlEscape(userName) +
        "' required><label>Address</label><input name='address' value='" +
        htmlEscape(address) +
        "' required><label>Object name</label><input name='object_name' "
        "value='" + htmlEscape(objectName) +
        "' required><label>Device ID</label><input name='device_id' value='" +
        htmlEscape(deviceId) +
        "' required><label>COM1 / Modbus baud rate</label><input type='number' "
        "min='1200' max='921600' name='com1_baud_rate' value='" +
        htmlEscape(com1BaudRate) + "' required>"
        "<label>GSM APN</label><input name='gsm_apn' value='" +
        htmlEscape(gsmApn) + "'><label>GSM user</label><input name='gsm_user' "
        "value='" + htmlEscape(gsmUser) +
        "'><label>GSM password</label><input type='password' name='gsm_pass' "
        "placeholder='Unchanged when empty'><label>SIM PIN (optional)</label>"
        "<input type='password' name='gsm_pin' placeholder='Unchanged when "
        "empty'><label>New administrator password</label><input "
        "type='password' minlength='8' maxlength='63' name='admin_pass' "
        "placeholder='Unchanged when empty'><button type='submit'>"
        "Save advanced settings and restart</button></form></div>"
        "<div class='card'><h3>Factory reset</h3><p>Deletes WiFi, network, "
        "MQTT, GSM and administrator configuration.</p>"
        "<form method='post' action='/admin/reset' onsubmit=\"return confirm("
        "'Factory reset? All settings will be deleted.')\"><button "
        "class='danger' type='submit'>Factory Reset</button></form></div>"
        "<a class='button' href='/'>Back</a>" + monitoringScript() +
        "</body></html>";
    server.send(200, "text/html", html);
  });

  server.on("/admin/save", HTTP_POST, []() {
    if (!requireAdministrator()) return;
    String selectedMode = server.arg("network_mode");
    if (selectedMode != "lan" && selectedMode != "gsm") selectedMode = "wifi";
    String selectedProvisioningMode = server.arg("prov_mode");
    if (selectedProvisioningMode != "self_service" &&
        selectedProvisioningMode != "hybrid") {
      selectedProvisioningMode = "legacy";
    }
    const String newAdminPass = server.arg("admin_pass");
    const String ssid = server.arg("ssid");
    const String wifiPassword = server.arg("pass");
    const String mqttPassword = server.arg("mqttpass");
    const String gsmPassword = server.arg("gsm_pass");
    const String simPin = server.arg("gsm_pin");
    if (!newAdminPass.isEmpty() && newAdminPass.length() < 8) {
      server.send(400, "text/plain",
                  "Administrator password must be at least 8 chars");
      return;
    }
    if (ssid.isEmpty() ||
        (!wifiPassword.isEmpty() && wifiPassword.length() < 8) ||
        server.arg("mqttip").isEmpty() || server.arg("mqttport").isEmpty() ||
        server.arg("user_name").isEmpty() || server.arg("address").isEmpty() ||
        server.arg("object_name").isEmpty() ||
        server.arg("device_id").isEmpty() ||
        server.arg("com1_baud_rate").isEmpty()) {
      server.send(400, "text/plain",
                  "Missing required field or WiFi password is too short");
      return;
    }
    preferences.begin("config", false);
    preferences.putString("network_mode", selectedMode);
    preferences.putString("prov_mode", selectedProvisioningMode);
    preferences.putString("ssid", ssid);
    if (!wifiPassword.isEmpty())
      preferences.putString("pass", wifiPassword);
    preferences.putString("mqttip", server.arg("mqttip"));
    preferences.putString("mqttport", server.arg("mqttport"));
    preferences.putString("mqttuser", server.arg("mqttuser"));
    if (!mqttPassword.isEmpty())
      preferences.putString("mqttpass", mqttPassword);
    preferences.putString("user_name", server.arg("user_name"));
    preferences.putString("address", server.arg("address"));
    preferences.putString("object_name", server.arg("object_name"));
    preferences.putString("device_id", server.arg("device_id"));
    preferences.putString("com1_baud_rate", server.arg("com1_baud_rate"));
    preferences.putString("staticip", server.arg("staticip"));
    preferences.putString("gsm_apn", server.arg("gsm_apn"));
    preferences.putString("gsm_user", server.arg("gsm_user"));
    if (!gsmPassword.isEmpty())
      preferences.putString("gsm_pass", gsmPassword);
    if (!simPin.isEmpty())
      preferences.putString("gsm_pin", simPin);
    if (!newAdminPass.isEmpty())
      preferences.putString("admin_pass", newAdminPass);
    preferences.end();
    appLog("WEB", "Administrator settings saved: network=" + selectedMode +
                      ", provisioning=" + selectedProvisioningMode);
    server.send(200, "text/html",
                "Administrator settings saved! ESP32 will reboot...");
    delay(1500);
    ESP.restart();
  });

  server.on("/admin/reset", HTTP_POST, []() {
    if (!requireAdministrator()) return;
    appLog("WEB", "POST /admin/reset received", WARN);
    if (!beginGatewayReset("web")) {
      server.send(409, "text/plain", "MQTT connection required, or reset already pending");
      return;
    }
    server.send(202, "text/plain", "Reset requested. Waiting for server cleanup; monitor reset/response on MQTT.");
  });

  server.on("/reset", HTTP_ANY, []() {
    server.send(404, "text/plain", "Not found");
  });

  server.begin();
  appLog("WEB", "Setup portal ready at http://192.168.10.1");
}
