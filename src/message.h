#ifndef MESSAGE_H
#define MESSAGE_H

#include <stdbool.h>

#define MAX_ARGS 15
#define MAX_TAGS 32

enum {
  MESSAGE_INVALID = 0,
  MESSAGE_VALID
};

typedef struct {
  int state;
  struct {
    char *key;
    char *value;
  } tags[MAX_TAGS];
  char *prefix;
  char *command;
  char *args[15];
  char *trailing;
} Message;

char *dup(const char *s, int len);
Message message_new(char *s);
void message_free(Message *m);
void message_set_prefix(Message *m, const char *prefix, int len);
void message_set_command(Message *m, const char *command, int len);
void message_set_arg(Message *m, int n, const char *arg, int len);
void message_set_tag_key(Message *m, int n, const char *key, int len);
void message_set_tag_value(Message *m, int n, const char *value, int len);
void message_set_trailing(Message *m, const char *trailing, int len);

#endif
