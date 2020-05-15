#include "../magic.h"

typedef union {
#define XSTART() P(Node,XTYPENAME) XIDNAME;
#include "_all.x"
} NodeUnion;
