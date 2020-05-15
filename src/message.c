#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include "message.h"

#define ADDRESS "((\\d{1,3}\\.){3}(\\d{1,3}))"
#define HNAME "([0-9a-zA-Z]([0-9a-zA-Z-]*[0-9a-zA-Z])*)"
#define HOSTNAME "("HNAME"(\\."HNAME ")*)"
#define HOST "("ADDRESS"|"HOSTNAME")"

static PCRE2_SPTR regex_pattern = (PCRE2_SPTR)
  "(?<tags>@"
    "(?<tag>(?<=[;@])"
      "(?<key>"
        "(\\+(?<vendor>"HOST"/))?"
        "(?<key_name>[0-9a-zA-Z-]+)"
      ")="
      "(?<value>[^; ]*)"
    "[; ])+"
  ")"
;

pcre2_code *regex = NULL;

#define GROUPS \
    X(tags) \
    X(tag) \
    X(vendor) \
    X(key) \
    X(key_name) \
    X(value)


//#define X(g) int g##_group;
//GROUPS
//#undef X

struct {
  int num;
  char *name;
} groups[] = {
#define X(g) { .name=#g },
GROUPS
#undef X
};

enum {
#define X(g) GROUP_##g,
GROUPS
#undef X
};

char *dup(const char *s, int len) {
  char *n = malloc(len+1);
  strncpy(n,s,len);
  n[len]=0;
  return n;
}

static void compile() {
  if(regex) return;

  int errornumber;
  PCRE2_SIZE erroroffset;
  regex = pcre2_compile(
      regex_pattern,
      PCRE2_ZERO_TERMINATED,
      PCRE2_AUTO_CALLOUT | PCRE2_NO_AUTO_CAPTURE,
      &errornumber,
      &erroroffset,
      NULL);
  if(!regex) {
    PCRE2_UCHAR buffer[256];
    pcre2_get_error_message(errornumber, buffer, sizeof(buffer));
    printf("Compile error at offset %d: %s\n", (int)erroroffset, buffer);
    exit(EXIT_FAILURE);
  }

  uint32_t namecount;
  uint32_t nameentrysize;
  char *nametable;
  pcre2_pattern_info(regex, PCRE2_INFO_NAMECOUNT, &namecount);
  pcre2_pattern_info(regex, PCRE2_INFO_NAMEENTRYSIZE, &nameentrysize);
  pcre2_pattern_info(regex, PCRE2_INFO_NAMETABLE, &nametable);

  for(int i = 0; i < namecount; i++) {
    int num = *(int8_t*)(nametable + nameentrysize*i + 1);
    char *name = nametable + nameentrysize*i + 2;
    for(int j = 0; j < sizeof(groups) / sizeof(groups[0]); j++) {
      if(strcmp(groups[j].name, name)) continue;
      groups[j].num = num;
      break;
    }
  }

  //for(int i = 0; i < namecount; i++) {
  //  printf("%s -> %d\n", groups[i].name, groups[i].num);
  //}
}

typedef struct {
  uint32_t capture_last;
  int capture_start;
  Message *message;
  int current_tag;
  int current_arg;
} NewMessageState;

int callout(pcre2_callout_block *block, void *data) {
  NewMessageState *state = data;
  int start = (int)block->offset_vector[block->capture_last*2];
  int len =
    (int)block->offset_vector[block->capture_last*2+1] -
    (int)block->offset_vector[block->capture_last*2];


  if(
      state->capture_last == block->capture_last &&
      state->capture_start == start
    ) {
    return 0;
  }

  state->capture_last = block->capture_last;
  state->capture_start = start;

  if((block->capture_last == groups[GROUP_key_name].num) &&
    (state->current_tag < MAX_TAGS)) {

    if(state->message->tags[state->current_tag].key) state->current_tag++;
    message_set_tag_key(
        state->message,
        state->current_tag,
        (const char *)block->subject+start,
        len);
  } else if((block->capture_last == groups[GROUP_value].num) &&
    (state->current_tag < MAX_TAGS) &&
    (len > 0)) {

    message_set_tag_value(
        state->message,
        state->current_tag,
        (const char *)block->subject+start,
        len);
  }

  return 0;
}


Message message_new(char *s) {
  compile();
  Message m = {};
  NewMessageState state = { .message = &m };

  pcre2_match_context *match_context = pcre2_match_context_create(NULL);
  pcre2_set_callout(match_context, callout, &state);
  pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(regex, NULL);

  pcre2_match(
      regex,
      (PCRE2_SPTR)s,
      strlen(s),
      0,
      0,
      match_data,
      match_context);

  m.state = MESSAGE_VALID;
  pcre2_match_data_free(match_data);
  pcre2_match_context_free(match_context);
  return m;
}

void message_free(Message *m) {
  free(m->prefix);
  free(m->command);
  for(int i = 0; i < MAX_ARGS; i++) free(m->args[i]);
  free(m->trailing);
  *m = (Message){};
}

void message_set_prefix(Message *m, const char *prefix, int len) {
  free(m->prefix);
  if(len == 0) len = strlen(prefix);
  m->prefix = dup(prefix, len);
}

void message_set_command(Message *m, const char *command, int len) {
  free(m->command);
  if(len == 0) len = strlen(command);
  m->command = dup(command, len);
}

void message_set_arg(Message *m, int n, const char *arg, int len) {
  free(m->tags[n].key);
  if(len == 0) len = strlen(arg);
  m->args[n] = dup(arg, len);
}

void message_set_tag_key(Message *m, int n, const char *key, int len) {
  free(m->tags[n].value);
  if(len == 0) len = strlen(key);
  m->tags[n].key = dup(key, len);
}

void message_set_tag_value(Message *m, int n, const char *value, int len) {
  free(m->args[n]);
  if(len == 0) len = strlen(value);
  m->tags[n].value = dup(value, len);
}

void message_set_trailing(Message *m, const char *trailing, int len) {
  free(m->trailing);
  if(len == 0) len = strlen(trailing);
  m->trailing = dup(trailing, len);
}

