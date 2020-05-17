#ifndef MESSAGE_H
#define MESSAGE_H

#include <stdbool.h>

#define MESSAGE_MAX_LEN  2048
#define MESSAGE_MAX_ARGS 16
#define MESSAGE_MAX_TAGS 64

typedef struct {
  bool valid;

  struct {
    char *key;
    char *value;
  } tags[MESSAGE_MAX_TAGS];
  size_t num_tags;

  struct {
    char *nick;
    char *user;
    char *host;
  } prefix;

  char *command;
  char *args[MESSAGE_MAX_ARGS];
  size_t num_args;
} Message;

void replace(char **old, char *new);

Message message_new(char *s);
bool message_tostring(Message *m, char *dst, size_t n);
void message_free(Message *m);

bool message_is_nick_valid(char *nick);
bool message_is_channel_valid(char *chan);

#endif
