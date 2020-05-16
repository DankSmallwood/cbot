#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "message.h"

void parse_message(char *raw) {
  Message m = message_new(raw);

  printf("Raw: %s\n", raw);
  printf("Tags:\n");
  for(int i = 0; i < MAX_TAGS && m.tags[i].key; i++) {
    printf("  %s=%s\n", m.tags[i].key, m.tags[i].value);
  }
  printf("Prefix:\n");
  printf("  nick: %s\n", m.prefix.nick);
  printf("  user: %s\n", m.prefix.user);
  printf("  host: %s\n", m.prefix.host);
  printf("Command: %s\n", m.command);
  printf("Arguments:\n");
  for(int i = 0; i < MAX_ARGS && m.args[i]; i++) {
    printf("  %s\n", m.args[i]);
  }
  printf("Trailing: %s\n", m.trailing);

  message_free(&m);
}

char *test_messages[] = {
  "@badge-info=subscriber/1;badges=subscriber/0;color=;display-name=DinkSmaIIwood;emote-sets=0,97129,111925;mod=0;subscriber=1;user-type= :tmitest-test.twitch.tv USERSTATE #misterscoot",
  "@badge-info=founder/34;badges=moderator/1,founder/0,premium/1;color=#8A2BE2;display-name=FatalPierce;emote-only=1;emotes=300462338:0-9;flags=;id=a6ec9527-63bb-47e1-9668-8ddbdf2607ea;mod=1;room-id=100327976;subscriber=0;tmi-sent-ts=1589506867207;turbo=0;user-id=83894115;user-type=mod :fatalpierce!fatalpierce@fatalpierce.tmi.twitch.tv PRIVMSG #misterscoot :gloopdRock",
  ":nick COMMAND",
  ":nick!user COMMAND",
  ":nick@host.name COMMAND",
  ":nick!user@host.name COMMAND",
};


int main(int argc, char *argv[]) {
  if(argc == 2) {
    int n;
    sscanf(argv[1], "%d", &n);
    parse_message(test_messages[n]);
  } else {
    for(int i = 0; i < sizeof(test_messages) / sizeof(test_messages[0]); i++) {
      printf("Message #%d\n", i);
      parse_message(test_messages[i]);
      printf("\n");
    }
  }

  return 0;
}
