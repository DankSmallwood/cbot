#include "../magic.h"

#define XSTART() typedef struct {
#define XEND() } P(Node,XTYPENAME);
#include "_all.x"
