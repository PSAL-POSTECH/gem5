// gem5 timing probe for the multi-precision array: T trips of weight push, one input
// push of R rows, one pop of R rows. argv[1] picks the path -- 0 the original array,
// 1 the msa at full width, 2 the msa at a quarter width. gem5 times; values are unused.
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define T 16
#define XS(x) #x
#define S(x) XS(x)
// Original array: w_vpush v8, i_vpush v12, vpop v16.
#define W_VPUSH(vs)     ".word " S(0x2600305B + ((vs) << 20)) "\n\t"
#define I_VPUSH(vs)     ".word " S(0x2200305B + ((vs) << 20)) "\n\t"
#define VPOP(vd)        ".word " S(0x0800305B + ((vd) << 7)) "\n\t"
// msa: SIMM5 [4] weight, [3:2] width shift, [1:0] format.
#define M_VPUSH(vs, s5) ".word " S(0x2A00305B + ((vs) << 20) + ((s5) << 15)) "\n\t"
#define M_VPOP(vd)      ".word " S(0x0C00305B + ((vd) << 7)) "\n\t"

static uint32_t buf[64];

#define TRIP(WPUSH, IPUSH, POP)                                            \
  asm volatile(                                                            \
    "vsetvli %0, %1, e32, m4, ta, ma\n\t"                                  \
    "vle32.v v8, (%2)\n\t"                                                 \
    WPUSH                                                                  \
    "vle32.v v12, (%2)\n\t"                                                \
    IPUSH                                                                  \
    POP                                                                    \
    : "=&r"(t) : "r"(n), "r"(buf) : "memory")

int main(int argc, char **argv)
{
  int mode = argc > 1 ? atoi(argv[1]) : 0;
  long n = 32, t;
  for (int i = 0; i < T; i++) {
    if (mode == 0)      TRIP(W_VPUSH(8), I_VPUSH(12), VPOP(16));
    else if (mode == 1) TRIP(M_VPUSH(8, 16), M_VPUSH(12, 0), M_VPOP(16));
    else                TRIP(M_VPUSH(8, 24), M_VPUSH(12, 8), M_VPOP(16));
  }
  printf("mode %d done\n", mode);
  return 0;
}
