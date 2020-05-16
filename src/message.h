#ifndef MESSAGE_H
#define MESSAGE_H

#include <stdbool.h>

#define MAX_ARGS 15
#define MAX_TAGS 32

typedef struct {
  bool valid;

  struct {
    char *key;
    char *value;
  } tags[MAX_TAGS];

  struct {
    char *nick;
    char *user;
    char *host;
  } prefix;

  char *command;
  char *args[MAX_ARGS];
  char *trailing;
} Message;

char *dup(const char *s, int len);
void replace(char **old, char *new);
Message message_new(char *s);
void message_free(Message *m);

#endif
