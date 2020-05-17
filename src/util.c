#include <string.h>
#include <stddef.h>
#include <stdlib.h>

char *strdup(const char *s) {
  size_t len = strlen(s);
  char *new = malloc(len+1);
  memcpy(new, s, len);
  new[len] = 0;
  return new;
}

char *strndup(const char *s, size_t n) {
  char *end = memchr(s,0,n);
  if(end) n = end - s;

  char *new = malloc(n+1);
  memcpy(new, s, n);
  new[n] = 0;
  return new;
}

void replace(char **old, char *new) {
  free(*old);
  *old = new;
}
