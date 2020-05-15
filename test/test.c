#include <stdio.h>
#include <stdbool.h>
#include "macro_magic.h"

#define XHEAD
#include "_all.x"
#undef XHEAD

#define X(n,b) bool P(test_,n)(void) { b }
#include "_all.x"
#undef X

int main(int argc, char *argv[]) {
#define X(n,...) \
  if(P(test_,n)()) printf("."); \
  else printf("\n" PS(test_,n) " failed\n");
#include "_all.x"
#undef X

  printf("\n");
  return 0;
}
