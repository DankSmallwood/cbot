#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "message.h"

int main(int argc, char *argv[]) {
  Message m = message_new("@badge-info=subscriber/1;badges=test;another= xxxx");
  for(int i = 0; i < MAX_TAGS && m.tags[i].key; i++) {
    printf("%s=%s\n", m.tags[i].key, m.tags[i].value);
  }
  //message_new("@badge-info=subscriber/1 this is a test");
  return 0;
}
