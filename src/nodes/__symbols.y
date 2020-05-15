#include "../magic.h"

enum {
NODE_NONE = 0,
#define XSTART() P(NODE_,XSYMNAME),
#include "_all.x"
};
