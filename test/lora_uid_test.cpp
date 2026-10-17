#include "../src/LoraBinaryProtocol.h"
#include <assert.h>
using namespace LoraBinary;
int main() {
 uint8_t packet[MAX_PACKET_SIZE];
 const uint8_t uid[6]={2,79,82,208,108,48};
 for(uint8_t ttl=0;ttl<=MAX_ROUTE_HOPS;++ttl){
  Request request;request.hasUid=true;memcpy(request.uid,uid,6);
  request.requestId=42;request.ttl=request.maxTtl=ttl;request.valueCount=64;
  for(uint8_t i=0;i<64;++i)request.values[i]=i*100;
  Request decoded;
  size_t n=encodeRequest(request,packet,sizeof(packet));assert(n);
  assert(decodeRequest(packet,n,decoded));assert(decoded.values[63]==6300);
  assert(memcmp(decoded.uid,uid,6)==0);assert(decoded.maxTtl==ttl);
  Response response;response.hasUid=true;memcpy(response.uid,uid,6);
  response.requestId=42;response.ttl=response.maxTtl=ttl;response.valueCount=64;
  for(uint8_t i=0;i<64;++i)response.values[i]=i;
  n=encodeResponse(response,packet,sizeof(packet));Response result;
  assert(n&&decodeResponse(packet,n,result));assert(result.values[63]==63);
  assert(responseMatches(request,result));result.uid[5]^=1;
  assert(!responseMatches(request,result)); // Another board must never satisfy this request.
  packet[n-1]^=1;assert(!decodeResponse(packet,n,result));
 }
 Request missing;assert(!encodeRequest(missing,packet,sizeof(packet)));
 for(uint8_t hops=1;hops<=MAX_ROUTE_HOPS+1;++hops){
  TopologyReport report;report.hasUid=report.hasDiagnostics=report.hasTemperature=true;
  memcpy(report.uid,uid,6);report.pathLength=hops;report.maxTtl=report.ttl=3;
  report.temperatureDeciCelsius=-123;report.supplyMillivolts=3290;
  for(uint8_t i=0;i<hops;++i){memcpy(report.pathUids[i],uid,6);report.pathUids[i][5]+=i;}
  size_t n=encodeTopologyReport(report,packet,sizeof(packet));TopologyReport decoded;
  assert(n&&decodeTopologyReport(packet,n,decoded));assert(decoded.pathLength==hops);
  assert(decoded.temperatureDeciCelsius==-123);assert(decoded.supplyMillivolts==3290);
  assert(!memcmp(decoded.pathUids,report.pathUids,sizeof(report.pathUids)));
 }
 TopologyDiscovery discovery;discovery.pathLength=3;discovery.maxTtl=3;
 for(uint8_t i=0;i<3;++i){memcpy(discovery.pathUids[i],uid,6);discovery.pathUids[i][5]+=i;}
 size_t n=encodeTopologyDiscovery(discovery,packet,sizeof(packet));TopologyDiscovery decoded;
 assert(n&&decodeTopologyDiscovery(packet,n,decoded));assert(!memcmp(decoded.pathUids,discovery.pathUids,sizeof(discovery.pathUids)));
 packet[1]=1;assert(!decodeTopologyDiscovery(packet,n,decoded));
}
