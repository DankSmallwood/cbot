#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include "message.h"

#define ADDRESS "((\\d{1,3}\\.){3}(\\d{1,3}))"
#define HNAME "([0-9a-zA-Z]([0-9a-zA-Z-]*[0-9a-zA-Z])?)"
//#define HNAME "([0-9a-zA-Z]+)"
//#define HOSTNAME "("HNAME"(\\."HNAME")*)"
#define HOSTNAME HNAME
//#define HOST "("ADDRESS"|"HOSTNAME")"
#define HOST HNAME

#define NICK "([a-zA-Z][a-zA-Z0-9-\\[\\]\\\\\\`\\^\\{\\}]*)"

#define CALLOUT_TAG 1
#define CALLOUT_PREFIX_HOSTONLY 2
#define CALLOUT_PREFIX 3
#define CALLOUT_COMMAND 4
#define CALLOUT_ARGUMENT 5
#define CALLOUT_TRAILING 6

#define TO_STR(x) TO_STR2(x)
#define TO_STR2(x) #x

#define CAPTURE_GROUPS \
X(tag_vendor) \
X(tag_key) \
X(tag_value) \
X(prefix_nick) \
X(prefix_user) \
X(prefix_host) \
X(prefix_hostonly) \
X(command) \
X(argument) \
X(trailing)

struct {
  char *name;
  int num;
} capture_groups[] = {
#define X(g,...) { .name=#g },
CAPTURE_GROUPS
#undef X
};

enum {
#define X(g,...) CAPTURE_GROUP_##g,
CAPTURE_GROUPS
#undef X
NUM_CAPTURE_GROUPS
};

static PCRE2_SPTR regex_pattern = (PCRE2_SPTR)
  "^"

  // Tags
  "(@"
    "((?<=[;@])"
      "(\\+(?<tag_vendor>"HOST")/)?"
      "(?<tag_key>[0-9a-zA-Z-]++)"
      "="
      "(?<tag_value>[^; ]*+)"
      "(?C"TO_STR(CALLOUT_TAG)")"
    "[; ])++"
  ")?"

  // Prefix
  "("
    "(:(?<prefix_hostonly>[^@! ]++) )(?C"TO_STR(CALLOUT_PREFIX_HOSTONLY)")|"
    "(:"
      "(?<prefix_nick>[^@!\\. ]++)"
      "(!(?<prefix_user>[^@ ]++))?"
      "(@(?<prefix_host>[^ ]++))?"
    " (?C"TO_STR(CALLOUT_PREFIX)"))"
  ")?"
  
  // Command, arguments and trailing
  "((?<command>([a-zA-Z]++)|([0-9]++))(?C"TO_STR(CALLOUT_COMMAND)")(\\s++|$))"
  "((?<argument>[^:]\\S++)(?C"TO_STR(CALLOUT_ARGUMENT)")\\s++|$)*+"
  "(:(?<trailing>.*+)(?C"TO_STR(CALLOUT_TRAILING)"))?"
  
  "$"
;

pcre2_code *regex = NULL;

char *dup(const char *s, int len) {
  if(!s || len <= 0) return NULL;

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
      PCRE2_DUPNAMES,
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
  pcre2_pattern_info(regex, PCRE2_INFO_NAMECOUNT, &namecount);

  uint32_t nameentrysize;
  pcre2_pattern_info(regex, PCRE2_INFO_NAMEENTRYSIZE, &nameentrysize);

  char *nametable;
  pcre2_pattern_info(regex, PCRE2_INFO_NAMETABLE, &nametable);

  for(int i = 0; i < namecount; i++) {
    // TODO: This is really 16-bit, but who has more than 255 named capture groups?
    int num = *(int8_t *)(nametable + nameentrysize*i + 1);
    char *name = nametable + nameentrysize*i + 2;

    for(int j = 0; j < NUM_CAPTURE_GROUPS; j++) {
      if(strcmp(capture_groups[j].name, name)) continue;
      capture_groups[j].num = num;
      break;
    }
  }
}

typedef struct {
  Message *message;
  int current_tag;
  int current_arg;
} NewMessageState;


void get_group(int g, pcre2_callout_block *block, char **start, int *len) {
  g = capture_groups[g].num;
  *start = (char *)block->subject+block->offset_vector[g*2];
  *len = block->offset_vector[g*2+1] - block->offset_vector[g*2];
}


int callout(pcre2_callout_block *block, void *data) {
  NewMessageState *state = data;
  char *start;
  int len;

  switch(block->callout_number) {
  case CALLOUT_TAG:
    if(state->current_tag > MAX_TAGS) break;

    get_group(CAPTURE_GROUP_tag_key, block, &start, &len);
    replace(&state->message->tags[state->current_tag].key, dup(start, len));

    get_group(CAPTURE_GROUP_tag_value, block, &start, &len);
    replace(&state->message->tags[state->current_tag].value, dup(start, len));

    state->current_tag++;
    break;

  case CALLOUT_PREFIX_HOSTONLY:
    get_group(CAPTURE_GROUP_prefix_hostonly, block, &start, &len);
    replace(&state->message->prefix.host, dup(start, len));

    break;

  case CALLOUT_PREFIX:
    get_group(CAPTURE_GROUP_prefix_nick, block, &start, &len);
    replace(&state->message->prefix.nick, dup(start, len));

    get_group(CAPTURE_GROUP_prefix_user, block, &start, &len);
    replace(&state->message->prefix.user, dup(start, len));

    get_group(CAPTURE_GROUP_prefix_host, block, &start, &len);
    replace(&state->message->prefix.host, dup(start, len));

    break;

  case CALLOUT_COMMAND:
    get_group(CAPTURE_GROUP_command, block, &start, &len);
    replace(&state->message->command, dup(start, len));

    break;

  case CALLOUT_ARGUMENT:
    if(state->current_arg >= MAX_ARGS) break;

    get_group(CAPTURE_GROUP_argument, block, &start, &len);
    replace(&state->message->args[state->current_arg], dup(start, len));
    state->current_arg++;

    break;

  case CALLOUT_TRAILING:
    get_group(CAPTURE_GROUP_trailing, block, &start, &len);
    replace(&state->message->trailing, dup(start, len));

    break;
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

  m.valid = true;
  pcre2_match_data_free(match_data);
  pcre2_match_context_free(match_context);
  return m;
}

void message_free(Message *m) {
  for(int i = 0; i < MAX_TAGS; i++) {
    free(m->tags[i].key);
    free(m->tags[i].value);
  }

  free(m->prefix.nick);
  free(m->prefix.user);
  free(m->prefix.host);

  free(m->command);
  for(int i = 0; i < MAX_ARGS; i++) {
    free(m->args[i]);
  }
  free(m->trailing);

  *m = (Message){};
}

