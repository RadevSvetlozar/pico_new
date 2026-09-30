"""Run the production inventory recorders and LoRa call guards without hardware."""
from pathlib import Path
import subprocess
import tempfile
import re
root=Path(__file__).resolve().parents[1]
modbus=(root/'src/ModbusService.cpp').read_text()
lora=(root/'src/LoraService.cpp').read_text()
modbus_logic=modbus[modbus.index('struct ModbusDeviceHealth'):modbus.index('}  // namespace')]
lora_struct=lora[lora.index('struct NodeHealth'):lora.index('StaticJsonDocument<4096>')]
lora_logic=lora[lora.index('void markNodeOnline'):lora.index('LoraBinary::DataType parseDataType')]
guards=re.findall(r'if \(!\(requestJson\["onboarding_test"\] \| false\)\) \{\s*markNode(?:Online|Timeout)\([^;]+;\s*\}',lora)
assert len(guards)==2
assert 'recordModbusDevice(slaveId, result, request["onboarding_test"] | false);' in modbus
assert 'if (!health.confirmed) continue;' in lora
code=r'''
#include <cassert>
#include <cstdint>
#include <string>
unsigned long tick=100;
unsigned long millis(){return tick;}
struct Modbus { static constexpr uint8_t ku8MBSuccess=0; } modbusNode;
namespace LoraBinary {enum Target{MODBUS=0};}
constexpr int INFO=1,ERROR=2;
template<class T> std::string String(T v){return std::to_string(v);}
template<class... T> void appLog(T... args){}
int published=0;
void publishNodeHealth(uint8_t,const char*,uint8_t,unsigned long=0,int=0,float=0){++published;}
struct Flag {bool value;bool operator|(bool)const{return value;}};
struct Json {bool value;Flag operator[](const char*)const{return {value};}};
struct Request {uint8_t nodeId=2;LoraBinary::Target target=LoraBinary::MODBUS;} request;
unsigned long roundTripMs=10;int packetRssi=-60;float packetSnr=5;
'''+modbus_logic+lora_struct+lora_logic+'\nvoid success(bool temporary){Json requestJson{temporary};'+guards[0]+'}\nvoid failure(bool temporary){Json requestJson{temporary};'+guards[1]+r'''}
int main(){
 for(int i=1;i<=10;i++){recordModbusDevice(i,0,true);recordModbusDevice(i,226,true);assert(!modbusDevices[i].known);}
 recordModbusDevice(3,226,false);assert(!modbusDevices[3].known);
 recordModbusDevice(3,0,false);assert(modbusDevices[3].known&&modbusDevices[3].online);
 tick=200;recordModbusDevice(3,226,true);assert(modbusDevices[3].online&&modbusDevices[3].lastRequestMs==100);
 recordModbusDevice(3,226,false);assert(modbusDevices[3].known&&!modbusDevices[3].online);
 success(true);for(int i=0;i<4;i++)failure(true);
 assert(!nodeHealth[2].confirmed&&published==0&&nodeNextEligibleMs[2]==0);
 for(int i=0;i<4;i++)failure(false);assert(!nodeHealth[2].confirmed);
 success(false);assert(nodeHealth[2].confirmed&&nodeHealth[2].online);
 int before=published;auto seen=nodeHealth[2].lastSeenMs;tick=500;
 success(true);failure(true);assert(published==before&&nodeHealth[2].online&&nodeHealth[2].lastSeenMs==seen);
 for(int i=0;i<3;i++)failure(false);assert(nodeHealth[2].confirmed&&!nodeHealth[2].online);
}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'test.cpp').write_text(code)
 subprocess.run(['c++','-std=c++11',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS: temporary success/failure never registers or alters devices; normal traffic confirms and tracks health')
