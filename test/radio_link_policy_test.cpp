#include "../src/RadioLinkPolicy.h"
#include <assert.h>
using namespace RadioLink;
int main(){
 Profile p;
 for(int i=0;i<3;++i){p.direct.observe(i==0,100,-100,5);p.relay.observe(true,500,-90,6);}
 p.select();assert(p.measured&&p.preferredTtl==2);assert(p.direct.percent()==33);assert(p.relay.percent()==100);
 assert(p.relay.latencySum/p.relay.replies==500);
 assert(!p.observeRuntime(false));assert(!p.observeRuntime(false));assert(p.observeRuntime(false));assert(p.preferredTtl==0&&p.needsRecheck);
 p.observeRuntime(true);assert(p.consecutiveMisses==0);
 Profile direct;for(int i=0;i<3;++i){direct.direct.observe(true);direct.relay.observe(true);}direct.select();assert(direct.preferredTtl==0);
 Profile lost;for(int i=0;i<3;++i){lost.direct.observe(false);lost.relay.observe(false);}lost.select();assert(lost.quality()[0]=='u');
 Profile partial;partial.direct.observe(true);partial.select();assert(!partial.measured);
}
