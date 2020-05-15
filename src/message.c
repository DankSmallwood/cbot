#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include "message.h"

#define ADDRESS "((\\d{1,3}\\.){3}(\\d{1,3}))"
#define HNAME "([0-9a-zA-Z]([0-9a-zA-Z-]*[0-9a-zA-Z])*)"
#define HOSTNAME "("HNAME"(\\."HNAME ")*)"
#define HOST "("ADDRESS"|"HOSTNAME")"

#define NICK "([a-zA-Z][a-zA-Z0-9-\\[\\]\\\\\\`\\^\\{\\}]*)"

static PCRE2_SPTR regex_pattern = (PCRE2_SPTR)
  "^"

  // Tags
  "(@(?<tags>"
    "(?<tag>(?<=[;@])"
      "(?<key>"
        "(\\+(?<key_vendor>"HOST"/))?"
        "(?<key_name>[0-9a-zA-Z-]+)"
      ")="
      "(?<key_value>[^; ]*)"
    "[; ])+"
  "))?"

  // Prefix
  "(:(?<prefix>"
    "("
      "(?<prefix_nick>"NICK")"
      "(!(?<prefix_user>[^@ ]+))?"
      "(@(?<prefix_host>"HOST"))?"
    ")"
    "|(?<prefix_servername>"HOST")"
  ") )?"
  
  // Command, arguments and trailing
  "((?<command>([a-zA-Z]+)|([0-9]{3}))(\\s+|$))"
  "((?<argument>[^:]\\S+)\\s+|$)*"
  "(:(?<trailing>.*))?"
  
  "$"
;

pcre2_code *regex = NULL;

// X(group_name, offset)
#define GROUPS \
X(tags, _nocapture) \
X(tag, _nocapture) \
X(key, _nocapture) \
X(key_name, tags[0].key) \
X(key_vendor, _nocapture) \
X(key_value, tags[0].value) \
\
X(prefix, _nocapture) \
X(prefix_servername, prefix.servername) \
X(prefix_nick, prefix.nick) \
X(prefix_user, prefix.user) \
X(prefix_host, prefix.host) \
\
X(command, command) \
X(argument, args) \
X(trailing, trailing)

struct {
  int num;
  char *name;
  size_t offset;
} groups[] = {
#define X(g,o) { .name=#g, .offset=offsetof(Message,o) },
GROUPS
#undef X
};

enum {
#define X(g,...) GROUP_##g,
GROUPS
#undef X
NUM_GROUPS
};

char *dup(const char *s, int len) {
  char *n = malloc(len+1);
  strncpy(n,s,len);
  n[len]=0;
  return n;
}

void replace(char **old, char *new) {
  free(*old);
  *old = new;
}

static void compile() {
  if(regex) return;

  printf("%s\n", (char *)regex_pattern);

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
    //printf("%s=%d\n", name, num);
    for(int j = 0; j < sizeof(groups) / sizeof(groups[0]); j++) {
      if(strcmp(groups[j].name, name)) continue;
      groups[j].num = num;
      break;
    }
  }
}

typedef struct {
  uint32_t capture_last;
  int capture_start;
  Message *message;
  int current_tag;
  int current_arg;
} NewMessageState;

int callout(pcre2_callout_block *block, void *data) {
  int group = block->capture_last;
  NewMessageState *state = data;
  int start = (int)block->offset_vector[group*2];
  int len =
    (int)block->offset_vector[group*2+1] -
    (int)block->offset_vector[group*2];

  if(state->capture_last == group && state->capture_start == start) return 0;
  state->capture_last = group;
  state->capture_start = start;

  for(int i = 0; i < NUM_GROUPS; i++) {
    if(group != groups[i].num) continue;

    //printf("*** %d %s\n", group, groups[i].name);
    size_t offset = groups[i].offset;

    // Adjust for offset into tags array
    if(group == groups[GROUP_key_name].num || group == groups[GROUP_key_value].num) {
      if(
          group == groups[GROUP_key_name].num &&
          state->current_tag < MAX_TAGS &&
          state->message->tags[state->current_tag].key) {

        state->current_tag++;
      }
      if(state->current_tag >= MAX_TAGS) break;
      offset += sizeof(state->message->tags[0]) * state->current_tag;
    }

    // Adjust for offset into args array
    if(group == groups[GROUP_argument].num) {
      if(state->current_arg < MAX_ARGS && state->message->args[state->current_arg]) {
        state->current_arg++;
      }
      if(state->current_arg >= MAX_ARGS) break;
      offset += sizeof(state->message->args[0]) * state->current_arg;
    }

    replace(
      (char **)((char *)state->message + offset),
      dup((const char *)block->subject+start, len));
    break;
  }



  // // Tag key
  // if((group == groups[GROUP_key_name].num) &&
  //   (state->current_tag < MAX_TAGS)) {

  //   if(state->message->tags[state->current_tag].key) state->current_tag++;
  //   replace(
  //       &state->message->tags[state->current_tag].key,
  //       dup((const char *)block->subject+start, len));
  // }

  // // Tag value
  // else if((group == groups[GROUP_value].num) &&
  //   (state->current_tag < MAX_TAGS) &&
  //   (len > 0)) {

  //   replace(
  //       &state->message->tags[state->current_tag].value,
  //       dup((const char *)block->subject+start, len));
  // }

  // // Prefix servername
  // else if(group == groups[GROUP_prefix_servername].num) {
  //   replace(
  //       &state->message->prefix.servername,
  //       dup((const char *)block->subject+start, len));
  // }

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

  m.valid = true;
  pcre2_match_data_free(match_data);
  pcre2_match_context_free(match_context);
  return m;
}

void message_free(Message *m) {
  *m = (Message){};
}

