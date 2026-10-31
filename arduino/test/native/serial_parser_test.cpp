#include "../../src/LifespanSerialParser.h"
#include <assert.h>
#include <vector>
#include <stdio.h>
using namespace treadspan;
struct Values { std::vector<uint16_t> steps, speeds; };
void step(void* p, uint16_t v) { static_cast<Values*>(p)->steps.push_back(v); }
void speed(void* p, uint16_t v) { static_cast<Values*>(p)->speeds.push_back(v); }
std::vector<uint8_t> packet(std::initializer_list<uint8_t> body) {
    std::vector<uint8_t> p(body); auto c=modbusCrc(p.data(),p.size());
    p.push_back(c & 255);p.push_back(c >> 8);return p;
}
void feed(LifespanSerialParser& p, const std::vector<uint8_t>& b, bool request, uint32_t t=1) {
    for (auto c:b) { if(request)p.requestByte(c,t++);else p.responseByte(c,t++); }
}
int main() {
    auto req=packet({1,3,0,15,0,1});
    auto reply=packet({1,3,2,0,24});
    auto target=packet({1,6,0,10,0,88});
    assert(req==std::vector<uint8_t>({1,3,0,15,0,1,0xb4,9}));
    { Values v;LifespanSerialParser p(step,speed,&v);
      feed(p,req,true);feed(p,reply,false,20);assert(v.steps==std::vector<uint16_t>({24})); }
    { Values v;LifespanSerialParser p(step,speed,&v);
      feed(p,req,true);for(size_t i=0;i<4;i++)p.responseByte(reply[i],20+i);
      assert(v.steps.empty());for(size_t i=4;i<reply.size();i++)p.responseByte(reply[i],20+i);
      assert(v.steps.size()==1); }
    { Values v;LifespanSerialParser p(step,speed,&v);
      feed(p,req,true);auto bad=reply;bad[4]^=1;feed(p,bad,false,20);assert(v.steps.empty()); }
    { Values v;LifespanSerialParser p(step,speed,&v);
      feed(p,req,true);feed(p,target,true,10);feed(p,reply,false,30);feed(p,target,false,40);
      assert(v.steps==std::vector<uint16_t>({24}));assert(v.speeds==std::vector<uint16_t>({88})); }
    { Values v;LifespanSerialParser p(step,speed,&v);
      feed(p,req,true);feed(p,target,true,10);feed(p,target,false,30);feed(p,reply,false,40);
      assert(v.steps.empty()); } // missing step reply must not interpret a write echo as steps
    { Values v;LifespanSerialParser p(step,speed,&v);
      feed(p,req,true);feed(p,packet({1,0x83,2}),false,20);feed(p,reply,false,30);assert(v.steps.empty()); }
    { Values v;LifespanSerialParser p(step,speed,&v);
      feed(p,req,true);feed(p,reply,false,600);assert(v.steps.empty()); }
    { Values v;LifespanSerialParser p(step,speed,&v);
      auto bad=target;bad.back()^=1;feed(p,bad,true);assert(v.speeds.empty());
      feed(p,target,true,30);assert(v.speeds.size()==1); }
    { Values v;LifespanSerialParser p(step,speed,&v);
      feed(p,packet({1,3,0xd1,10,0,1}),true);feed(p,reply,false,20);assert(v.steps.empty()); }
    { Values v;LifespanSerialParser p(step,speed,&v);
      feed(p,req,true,0xfffffff0);feed(p,reply,false,5);assert(v.steps.size()==1); }
    { Values v;LifespanSerialParser p(step,speed,&v);
      feed(p,req,true);feed(p,{1,3,250,99,99},false,20);feed(p,reply,false,30);
      assert(v.steps.empty());feed(p,req,true,60);feed(p,reply,false,80);assert(v.steps.size()==1); }
    { Values v;LifespanSerialParser p(step,speed,&v);
      for(int i=0;i<1000;++i)p.responseByte(0xff,i);feed(p,req,true,1010);feed(p,reply,false,1030);
      assert(v.steps.empty());feed(p,req,true,1100);feed(p,reply,false,1120);assert(v.steps.size()==1); }
    puts("PASS: serial framing, CRC, fragmentation, missing reply, exception, timeout, wrap, noise and resynchronization");
}
