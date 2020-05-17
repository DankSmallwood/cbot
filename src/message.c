#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include "message.h"
#include "util.h"

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
X(argument)

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

#define ADDRESS_PATTERN "((\\d{1,3}\\.){3}(\\d{1,3}))"
#define HNAME_PATTERN "([0-9a-zA-Z]([0-9a-zA-Z-]*[0-9a-zA-Z])?+)"
#define HOSTNAME_PATTERN "("HNAME_PATTERN"(\\."HNAME_PATTERN")++)"
#define HOST_PATTERN "("ADDRESS_PATTERN"|"HOSTNAME_PATTERN")"

#define NICK_PATTERN "([a-zA-Z][a-zA-Z0-9\\-\\[\\]\\\\\\`\\^\\{\\}]*+)"
#define CHANNEL_PATTERN "([#&][^, \a]{1,200}+)"

static PCRE2_SPTR message_pattern = (PCRE2_SPTR)
  "^"

  // Tags
  "("
    "@"
    "("
      "(?<=[;@])"
      "(" // Vendor
        "\\+"
        "(?<tag_vendor>"
          HOST_PATTERN
        ")"
        "/"
      ")?"
      "(?<tag_key>"
        "[0-9a-zA-Z-]++"
      ")"
      "="
      "(?<tag_value>"
        "[^\\0\\r\\n; ]*+"
      ")"
      "(?C"TO_STR(CALLOUT_TAG)")"
      ";?"
    ")++"
    " "
  ")?"

  "("
    "("
      ":"
      "(?<prefix_hostonly>"
        HOST_PATTERN
      ")"
      " "
    ")"
    "(?C"TO_STR(CALLOUT_PREFIX_HOSTONLY)")"
    "|"
    "("
      ":"
      "(?<prefix_nick>"
        //"[^@!\\. ]++"
        NICK_PATTERN
      ")"
      "("
        "!"
        "(?<prefix_user>"
          "[^@ ]++"
        ")"
      ")?"
      "("
        "@"
        "(?<prefix_host>"
          HOST_PATTERN
        ")"
      ")?"
      " "
      "(?C"TO_STR(CALLOUT_PREFIX)")"
    ")"
  ")?"

  // Command
  "("
    "(?<command>"
      "("
        "[a-zA-Z]++"
      ")"
      "|"
      "("
        "[0-9]++"
      ")"
    ")"
    "(?C"TO_STR(CALLOUT_COMMAND)")"
    "(\\s++|$)"
  ")"

  // Arguments
  "("
    ":?"
    "(?<argument>"
      "("
        "(?<=:)[^\\r\\n]++"
      ")"
      "|"
      "("
        "\\S++"
      ")"
    ")"
    "(?C"TO_STR(CALLOUT_ARGUMENT)")"
    " ?+"
  "){0,15}+"

  "(\\r\\n)?"
  "$"
;


static pcre2_code *compile(PCRE2_SPTR pattern) {
  int errornumber;
  PCRE2_SIZE erroroffset;

  pcre2_code *regex = pcre2_compile(
      pattern,
      PCRE2_ZERO_TERMINATED,
      0,
      &errornumber,
      &erroroffset,
      NULL);
  if(!regex) {
    PCRE2_UCHAR buffer[256];
    pcre2_get_error_message(errornumber, buffer, sizeof(buffer));
    printf("Compile error at offset %d: %s\n", (int)erroroffset, buffer);
  }

  return regex;
}



void get_group(int g, pcre2_callout_block *block, char **start, int *len) {
  g = capture_groups[g].num;
  *start = (char *)block->subject+block->offset_vector[g*2];
  *len = block->offset_vector[g*2+1] - block->offset_vector[g*2];
}



#define R(cap,dst) \
  get_group(CAPTURE_GROUP_##cap, block, &start, &len); \
  new = len > 0 ? strndup(start,len) : NULL; \
  replace(&m->dst, new);
static int new_message_callout(pcre2_callout_block *block, void *data) {
  Message *m = data;
  char *start;
  int len;
  char *new;

  switch(block->callout_number) {
  case CALLOUT_TAG:
    if(m->num_tags < MESSAGE_MAX_TAGS) {
      R(tag_key, tags[m->num_tags].key);
      R(tag_value, tags[m->num_tags].value);
      m->num_tags++;
    }
    break;

  case CALLOUT_PREFIX_HOSTONLY:
    R(prefix_hostonly, prefix.host);
    break;

  case CALLOUT_PREFIX:
    R(prefix_nick, prefix.nick);
    R(prefix_user, prefix.user);
    R(prefix_host, prefix.host);
    break;

  case CALLOUT_COMMAND:
    R(command, command);
    break;

  case CALLOUT_ARGUMENT:
    if(m->num_args < MESSAGE_MAX_ARGS) {
      R(argument, args[m->num_args]);
      m->num_args++;
    }
    break;
  }

  return 0;
}



Message message_new(char *s) {
  static pcre2_code *regex = NULL;

  if(!regex) {
    regex = compile(message_pattern);
    if(!regex) exit(EXIT_FAILURE);

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

  Message m = {};

  pcre2_match_context *match_context = pcre2_match_context_create(NULL);
  pcre2_set_callout(match_context, new_message_callout, &m);
  pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(regex, NULL);

  int ret = pcre2_match(
      regex,
      (PCRE2_SPTR)s,
      strlen(s),
      0,
      0,
      match_data,
      match_context);

  m.valid = ret > 1;
  pcre2_match_data_free(match_data);
  pcre2_match_context_free(match_context);
  return m;
}



bool message_tostring(Message *m, char *dst, size_t n) {
  size_t cursor = 0;

  if(m->num_tags > 0) {
    cursor += snprintf(dst+cursor, n-cursor, "@");
    for(int i = 0; i < m->num_tags; i++) {
      cursor += snprintf(dst+cursor, n-cursor, "%s%s=%s",
          i > 0 ? ";" : "",
          m->tags[i].key,
          m->tags[i].value ? m->tags[i].value : "");
    }
    cursor += snprintf(dst+cursor, n-cursor, " ");
  }

  if(m->prefix.host && !m->prefix.nick && !m->prefix.user) {
    cursor += snprintf(dst+cursor, n-cursor, ":%s ", m->prefix.host);
  } else {
    cursor += snprintf(dst+cursor, n-cursor, ":%s%s%s%s%s ",
        m->prefix.nick,
        m->prefix.user ? "!" : "",
        m->prefix.user ? m->prefix.user : "",
        m->prefix.host ? "@" : "",
        m->prefix.host ? m->prefix.host : "");
  }

  cursor += snprintf(dst+cursor, n-cursor, "%s ", m->command);
  for(int i = 0; i < m->num_args; i++) {
    cursor += snprintf(
        dst+cursor, n-cursor,
        "%s%s ",
        strchr(m->args[i], ' ') ? ":" : "",
        m->args[i]);
  }

  return true;
}



static bool validate(pcre2_code **regex, PCRE2_SPTR pattern, char *subject) {
  if(!*regex) {
    *regex = compile(pattern);
    if(!*regex) exit(EXIT_FAILURE);
  }

  pcre2_match_data *match = pcre2_match_data_create_from_pattern(*regex, NULL);
  int ret = pcre2_match(*regex, (PCRE2_SPTR)subject, strlen(subject), 0, 0, match, NULL);

  pcre2_match_data_free(match);
  return ret >= 1;
}



bool message_is_nick_valid(char *nick) {
  static pcre2_code *regex = NULL;
  return validate(&regex, (PCRE2_SPTR)"^"NICK_PATTERN"$", nick);
}



bool message_is_channel_valid(char *chan) {
  static pcre2_code *regex = NULL;
  return validate(&regex, (PCRE2_SPTR)"^"CHANNEL_PATTERN"$", chan);
}



void message_free(Message *m) {
  for(int i = 0; i < MESSAGE_MAX_TAGS; i++) {
    free(m->tags[i].key);
    free(m->tags[i].value);
  }

  free(m->prefix.nick);
  free(m->prefix.user);
  free(m->prefix.host);

  free(m->command);
  for(int i = 0; i < MESSAGE_MAX_ARGS; i++) {
    free(m->args[i]);
  }

  *m = (Message){};
}

