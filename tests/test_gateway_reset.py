"""Exercise the reset state machine natively with real ArduinoJson and fake I/O."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = (root / 'src/GatewayReset.cpp').read_text().replace('#include "App.h"', '').replace('#include "esp_system.h"', '').replace('.isEmpty()', '.empty()')
include = root / '.pio/libdeps/arduino_nano_esp32/ArduinoJson/src'
harness = r'''
#include <ArduinoJson.h>
#include <string>
#include <vector>
#include <cassert>
#include <cstdio>
using String = std::string;
unsigned long clockMs = 0;
unsigned long millis() { return clockMs; }
unsigned long esp_random() { static unsigned long v = 1; return v++; }
void delay(int) {}
String mqttClientName = "esp32-TEST";
String getTopic(const String& suffix) { return "a/b/c/d/" + suffix; }
struct Mqtt { bool connected = true; std::vector<String> payloads;
 bool isMqttConnected() { return connected; }
 void publish(const String&, const String& p, bool retain) { assert(!retain); payloads.push_back(p); }
} mqttClient;
struct Device { int restarts = 0; void restart() { restarts++; } } ESP;
int erases = 0, bleSuccess = 0; bool eraseOk = true;
bool eraseStoredConfiguration() { erases++; return eraseOk; }
void notifyBleGatewayReset(bool success, const String&) { if(success) bleSuccess++; }
'''
harness += source
harness += r'''
String ack(const String& id, bool success=true) {
 return "{\"uid\":\"esp32-TEST\",\"command_id\":\""+id+"\",\"phase\":\"server_reset_complete\",\"success\":"+(success?"true":"false")+"}";
}
int main() {
 mqttClient.connected=false;assert(!beginGatewayReset("mqtt"));assert(erases==0);
 assert(beginGatewayReset("bluetooth"));processGatewayReset();assert(mqttClient.payloads.empty());assert(erases==0);
 mqttClient.connected=true;assert(!beginGatewayReset("mqtt"));
 processGatewayReset();assert(mqttClient.payloads.size()==1);assert(erases==0);
 StaticJsonDocument<512> req;deserializeJson(req,mqttClient.payloads.back());String id=req["command_id"].as<String>();
 handleGatewayResetAcknowledgement(ack("wrong"));processGatewayReset();assert(erases==0);
 clockMs+=2000;processGatewayReset();StaticJsonDocument<512> retry;deserializeJson(retry,mqttClient.payloads.back());assert(id==retry["command_id"].as<String>());
 handleGatewayResetAcknowledgement(ack(id));processGatewayReset();assert(erases==1&&ESP.restarts==1&&bleSuccess==1);assert(!gatewayResetPending());
 assert(beginGatewayReset("mqtt"));clockMs+=30001;processGatewayReset();assert(erases==1&&!gatewayResetPending());
 assert(beginGatewayReset("mqtt"));processGatewayReset();deserializeJson(req,mqttClient.payloads.back());id=req["command_id"].as<String>();
 handleGatewayResetAcknowledgement(ack(id,false));processGatewayReset();assert(erases==1&&!gatewayResetPending());
 assert(beginGatewayReset("bluetooth"));processGatewayReset();deserializeJson(req,mqttClient.payloads.back());id=req["command_id"].as<String>();
 eraseOk=false;handleGatewayResetAcknowledgement(ack(id));processGatewayReset();assert(erases==2&&ESP.restarts==1&&bleSuccess==1);
}
'''
with tempfile.TemporaryDirectory() as folder:
    path = Path(folder)
    (path / 'test.cpp').write_text(harness)
    subprocess.run(['c++', '-std=c++11', '-I'+str(include), str(path/'test.cpp'), '-o', str(path/'test')], check=True)
    subprocess.run([str(path/'test')], check=True)
print('PASS: offline refusal, ACK correlation, retries, timeout, server failure and flash failure')
