#include <assert.h>
#include "case_state.h"
int main(void) {
 struct case_state s={0};
 for(unsigned mode=1;mode<=4;mode++) {
  case_toggle(&s,mode);
  assert(s.mode==mode);
  uint32_t first=case_transform(&s,0x70004);
  assert(first==((mode==1||mode==3)?0x2070004:0x70004));
  assert(case_transform(&s,0x70005)==(mode==1?0x2070005:0x70005));
  assert(case_transform(&s,0x7002c)==(mode<=2?0x207002d:0));
  assert(case_transform(&s,0x70006)==(mode==2?0x70006:0x2070006));
  case_toggle(&s,mode);assert(s.mode==0);
 }
 uint32_t exits[]={0x70029,0x70028,0x7002b};
 for(unsigned i=0;i<3;i++) {case_toggle(&s,3);assert(case_transform(&s,exits[i])==0);assert(s.mode==0);assert(case_transform(&s,exits[i])==exits[i]);}
 case_toggle(&s,1);assert(case_transform(&s,0x1070004)==0x1070004);
 assert(case_transform(&s,0x7002a)==0x7002a);
 case_toggle(&s,4);assert(s.mode==4);assert(case_transform(&s,0x70004)==0x70004);
 return 0;
}
