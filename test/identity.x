#ifdef XHEAD
#include <stdio.h>
#else
X(true_is_true, return true;)
X(not_false_is_true, return !false;)
#endif
