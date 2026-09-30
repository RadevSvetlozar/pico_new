"""Exercise production advertising transition logic with simulated MQTT/time."""
from pathlib import Path
import subprocess
import tempfile
source = (Path(__file__).resolve().parents[1] / 'src/BleProvisioning.cpp').read_text()
start = source.index('  // Never disconnect an installer')
logic = source[start:source.index('\n}\n\nvoid blePublishLog', start)]
code = r'''
#include <cassert>
unsigned long clockMs=0, bleStartedAt=0;
constexpr unsigned long BLE_WINDOW_MS=30000;
unsigned long millis() { return clockMs; }
bool bleConnected=false, bleAdvertising=true;
struct Mqtt { bool online=false; bool isMqttConnected() { return online; } } mqttClient;
struct Advertiser { int starts=0, stops=0; void start(){++starts;} void stop(){++stops;} } advertiser;
struct BLEDevice { static Advertiser* getAdvertising(){return &advertiser;} };
void appLog(const char*, const char*) {}
void update() {
''' + logic + r'''
}
int main() {
  clockMs=600000; update(); assert(bleAdvertising && advertiser.stops==0);
  mqttClient.online=true; update(); assert(!bleAdvertising && advertiser.stops==1);
  update(); assert(advertiser.stops==1);
  mqttClient.online=false; update(); assert(bleAdvertising && advertiser.starts==1);
  update(); assert(advertiser.starts==1);
  bleConnected=true; bleAdvertising=false; mqttClient.online=true;
  update(); assert(bleConnected && advertiser.stops==1);
  bleConnected=false; mqttClient.online=false;
  update(); assert(bleAdvertising && advertiser.starts==2);
  clockMs=1000; mqttClient.online=true; update(); assert(bleAdvertising);
  clockMs=30000; update(); assert(!bleAdvertising);
}
'''
with tempfile.TemporaryDirectory() as directory:
    root=Path(directory)
    (root/'test.cpp').write_text(code)
    subprocess.run(['c++','-std=c++11',str(root/'test.cpp'),'-o',str(root/'test')],check=True)
    subprocess.run([str(root/'test')],check=True)
print('PASS: offline wait, MQTT recovery/loss, active session, reconnect, boot window')
