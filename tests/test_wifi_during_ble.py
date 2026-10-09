"""Exercise the production network retry loop while Bluetooth stays connected."""
from pathlib import Path
import subprocess
import tempfile
source = (Path(__file__).resolve().parents[1] / 'src/NetworkService.cpp').read_text()
start = source.index('void NetworkMqttClient::loop()')
logic = source[start:source.index('bool NetworkMqttClient::publish', start)]
code = r"""
#include <cassert>
#include <string>
using String=std::string;
String networkMode="wifi";
unsigned long tick=10000;
unsigned long millis(){return tick;}
bool ble=true, resetting=false;
bool isBleConnected(){return ble;}
bool gatewayResetPending(){return resetting;}
String wifiFailureReason(){return "offline";}
constexpr int WARN=1;
void appLog(const char*,const String&,int){}
struct Client {bool online=false;int loops=0;bool connected(){return online;}void loop(){loops++;}};
struct NetworkMqttClient {
 Client client_;bool network=false,subscriptionsReady_=false;
 unsigned long lastNetworkAttempt_=0,lastMqttAttempt_=0;String networkFailureReason_;
 int attempts=0,mqttAttempts=0;
 bool isNetworkConnected(){return network;}
 void startTransport(){attempts++;lastNetworkAttempt_=tick;}
 void connectMqtt(){mqttAttempts++;lastMqttAttempt_=tick;}
 void loop();
};
"""+logic+r"""
int main(){
 NetworkMqttClient wifi;wifi.loop();assert(wifi.attempts==1);
 tick=15000;wifi.loop();assert(wifi.attempts==1);
 tick=20000;wifi.loop();assert(wifi.attempts==2);
 wifi.network=true;wifi.loop();assert(wifi.mqttAttempts==1);
 wifi.client_.online=true;wifi.loop();assert(wifi.client_.loops==1);
 for(auto mode:{"lan","gsm"}){networkMode=mode;NetworkMqttClient other;
 other.loop();assert(other.attempts==0);ble=false;other.loop();assert(other.attempts==1);ble=true;}
}
"""
with tempfile.TemporaryDirectory() as directory:
 root=Path(directory);(root/'test.cpp').write_text(code)
 subprocess.run(['c++','-std=c++11',str(root/'test.cpp'),'-o',str(root/'test')],check=True)
 subprocess.run([str(root/'test')],check=True)
print('PASS: Wi-Fi retries and server connection during BLE; blocking transports deferred')
