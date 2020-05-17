#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <resolv.h>
#include <arpa/inet.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include "message.h"
#include "util.h"

#define PORT 9998
#define MAX_CHANNELS 16



enum {
  CLIENT_STATUS_DISCONNECTED,
  CLIENT_STATUS_WAIT_NICK,
  CLIENT_STATUS_WAIT_USER,
  CLIENT_STATUS_OK
};

typedef struct {
  int sock;
  int status;

  char *nick;
  char *user;
  char *host;
  char *channels[MAX_CHANNELS];
} Client;



typedef void(*ClientCommand)(Client *c, Message *m);

Client *client_new();
void client_free(int idx);
int client_service(int idx);
bool client_in_channel(Client *c, char *channel);

#define COMMANDS \
X(nick, 1) \
X(user, 4) \
X(join, 1) \
X(part, 1) \
X(privmsg, 2) \
X(quit, 1)

#define X(c,...) void client_##c(Client *c, Message *m);
COMMANDS
#undef X

struct {
  const char *command;
  ClientCommand func;
  int min_args;
} client_commands[] = {
#define X(c,args,...) { .command=#c, .func=client_##c, .min_args=args },
COMMANDS
#undef X
};

int num_clients = 0;
Client *clients = NULL;



int read_line(int fd, char *buffer, size_t n);
void inspect(char *s);

void say(int fd, char *fmt, ...);
void say_str(int fd, char *msg, size_t len);
void say_message(int fd, Message *m);

void broadcast(Client *except, char *channel, char *fmt, ...);
void broadcast_str(Client *except, char *channel, char *msg, size_t len);
void broadcast_message(Client *except, char *channel, Message *m);




Client *client_new() {
  Client *c = NULL;

  for(int i = 0; i < num_clients; i++) {
    if(clients[i].status != CLIENT_STATUS_DISCONNECTED) continue;
    c = &clients[i];
    break;
  }

  if(!c) {
    num_clients++;
    clients = realloc(clients, sizeof(Client)*num_clients);
    c = &clients[num_clients-1];
  }

  *c = (Client){};
  return c;
}



void client_free(int idx) {
  if(idx >= num_clients) return;

  Client *c = &clients[idx];
  free(c->nick);
  free(c->user);
  free(c->host);
  for(int i = 0; i < MAX_CHANNELS; i++) free(c->channels[i]);
  *c = (Client){};
}



bool client_in_channel(Client *c, char *channel) {
  for(int i = 0; i < MAX_CHANNELS; i++) {
    if(!c->channels[i]) continue;
    if(!strcasecmp(c->channels[i], channel)) return true;
  }
  return false;
}



int client_service(int idx) {
  if(idx >= num_clients || clients[idx].status == CLIENT_STATUS_DISCONNECTED) return -1;
  Client *c = &clients[idx];

  static char buffer[MESSAGE_MAX_LEN+1];
  if(read_line(c->sock, buffer, MESSAGE_MAX_LEN) < 1) return -1;
  inspect(buffer);

  Message m = message_new(buffer);
  if(!m.valid) goto done;

  for(int i = 0; i < sizeof(client_commands) / sizeof(client_commands[0]); i++) {
    if(!strcasecmp(m.command, client_commands[i].command) &&
        m.num_args >= client_commands[i].min_args) {
      client_commands[i].func(c, &m);
    }
  }

done:
  message_free(&m);
  return 0;
}



void client_nick(Client *c, Message *m) {
  bool nick_in_use = false;

  switch(c->status) {
  case CLIENT_STATUS_WAIT_NICK:
  case CLIENT_STATUS_OK:
    if(!message_is_nick_valid(m->args[0])) {
      say(c->sock, "432 %s :Erroneous nickname\r\n", m->args[0]);
      break;
    }

    for(int i = 0; i < num_clients; i++) {
      if(clients[i].status != CLIENT_STATUS_OK &&
          clients[i].status != CLIENT_STATUS_WAIT_USER) {
        continue;
      }
      
      if(!strcasecmp(m->args[0], clients[i].nick)) {
        nick_in_use = true;
        break;
      }
    }

    if(nick_in_use) {
      say(c->sock, "%s 433 :Nickname already in use\r\n", m->args[0]);
      break;
    }

    if(c->status == CLIENT_STATUS_WAIT_NICK) {
      replace(&c->nick, strdup(m->args[0]));
      c->status = CLIENT_STATUS_WAIT_USER;
      break;
    }

    for(int i = 0; i < MAX_CHANNELS; i++) {
      if(!c->channels[i]) continue;
      broadcast(c, c->channels[i],
          ":%s NICK %s\r\n",
          c->nick, m->args[0]);
    }
    replace(&c->nick, strdup(m->args[0]));
    break;

  default:
    break;
  }
}



void client_user(Client *c, Message *m) {
  switch(c->status) {
  case CLIENT_STATUS_WAIT_USER:
    replace(&c->user, strdup(m->args[0]));
    replace(&c->host, strdup(m->args[1]));
    c->status = CLIENT_STATUS_OK;

    say(c->sock, ":the.server 001 %s :You\r\n", c->nick);
    say(c->sock, ":the.server 002 %s :are\r\n", c->nick);
    say(c->sock, ":the.server 003 %s :now\r\n", c->nick);
    say(c->sock, ":the.server 004 %s :connected\r\n", c->nick);
    break;

  default:
    break;
  }
}



void client_join(Client *c, Message *m) {
  if(!message_is_channel_valid(m->args[0])) {
    say(c->sock, ":the.server 403 %s :Invalid channel name\r\n", c->nick);
    return;
  }

  int empty_slot = -1;
  switch(c->status) {
  case CLIENT_STATUS_OK:
    for(int i = 0; i < MAX_CHANNELS; i++) {
      if(!c->channels[i]) {
        if(empty_slot == -1) empty_slot = i;
      } else {
        if(!strcasecmp(m->args[0], c->channels[i])) return;
      }
    }

    // No free slots
    if(empty_slot == -1) return;

    c->channels[empty_slot] = strdup(m->args[0]);
    broadcast(NULL, m->args[0],
        ":%s!%s@%s JOIN %s\r\n",
        c->nick, c->user, c->host,
        m->args[0]);
    break;

  default:
    break;
  }
}



void client_part(Client *c, Message *m) {
  if(m->args[0][0] != '#') return;

  switch(c->status) {
  case CLIENT_STATUS_OK:
    for(int i = 0; i < MAX_CHANNELS; i++) {
      if(strcasecmp(m->args[0], c->channels[i])) continue;
      broadcast(NULL, m->args[0],
          ":%s!%s@%s PART %s",
          c->nick, c->user, c->host,
          m->args[0]);
      replace(&c->channels[i], NULL);
      break;
    }
    break;

  default:
    break;
  }
}



void client_privmsg(Client *c, Message *m) {
  static char raw_msg[MESSAGE_MAX_LEN+1];

  {
    Message tmp = *m;
    tmp.prefix.nick = c->nick;
    tmp.prefix.user = c->user;
    tmp.prefix.host = c->host;
    message_tostring(&tmp, raw_msg, MESSAGE_MAX_LEN);
  }

  switch(c->status) {
  case CLIENT_STATUS_OK:
    broadcast(c, m->args[0],
        ":%s!%s@%s PRIVMSG %s :%s\r\n",
        c->nick, c->user, c->host,
        m->args[0], m->args[1]);
    break;

  default:
    break;
  }
}



void client_quit(Client *c, Message *m) {
}


int read_line(int fd, char *buffer, size_t n) {
  if(n <= 0 || buffer == NULL) return -1;

  char *cursor = buffer;
  while(1) {
    switch(read(fd, cursor, 1)) {
    case -1:
      if(errno == EINTR) continue;
      return -1;
    case 0:
      *cursor = '\0';
      return cursor - buffer;
    default:
      switch(*cursor) {
      case '\n':
        cursor++;
        if(cursor - buffer < n) {
          *cursor = '\0';
          return cursor - buffer - 1;
        } else {
          return -2;
        }
      default:
        cursor++;
        if(cursor - buffer >= n) return -2;
      }
    }
  }

  return -3; // Unreachable
}



void inspect(char *s) {
  while(*s) {
    switch(*s) {
    case '\r': printf("\\r"); break;
    case '\n': printf("\\n"); break;
    default: printf("%c", *s);
    }

    s++;
  }
  printf("\n");
}



void say_str(int fd, char *msg, size_t len) {
  send(fd, msg, len, 0);
}



void say(int fd, char *fmt, ...) {
  static char buffer[MESSAGE_MAX_LEN+1];

  va_list args;
  va_start(args, fmt);
  int len = vsnprintf(buffer, MESSAGE_MAX_LEN+1, fmt, args);
  va_end(args);

  say_str(fd, buffer, len);
}



void say_message(int fd, Message *m) {
  static char buffer[MESSAGE_MAX_LEN+1];
  message_tostring(m, buffer, MESSAGE_MAX_LEN);

  say_str(fd, buffer, strlen(buffer));
}



void broadcast_str(Client *except, char *channel, char *msg, size_t len) {
  for(int i = 0; i < num_clients; i++) {
    if(&clients[i] == except || clients[i].status != CLIENT_STATUS_OK) continue;
    for(int j = 0; j < MAX_CHANNELS; j++) {
      if(!clients[i].channels[j]) continue;
      if(!strcasecmp(channel, clients[i].channels[j])) {
        send(clients[i].sock, msg, len, 0);
        break;
      }
    }
  }
}



void broadcast(Client *except, char *channel, char *fmt, ...) {
  static char buffer[MESSAGE_MAX_LEN+1];

  va_list args;
  va_start(args, fmt);
  int len = vsnprintf(buffer, MESSAGE_MAX_LEN+1, fmt, args);
  va_end(args);

  broadcast_str(except, channel, buffer, len);
}



void broadcast_message(Client *except, char *channel, Message *m) {
  static char buffer[MESSAGE_MAX_LEN+1];
  message_tostring(m, buffer, MESSAGE_MAX_LEN);

  broadcast_str(except, channel, buffer, strlen(buffer));
}



#define DIE_IF(cond,msg) if(cond) { perror(msg); exit(errno); }
int main(int argc, char *argv[]) {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  DIE_IF(sock < 0, "socket");

  DIE_IF(
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0,
    "setsockopt");

  DIE_IF(
    bind(
      sock,
      (struct sockaddr*)&(struct sockaddr_in){
        .sin_family = AF_INET,
        .sin_port = htons(PORT),
        .sin_addr = {.s_addr = INADDR_ANY}
      },
      sizeof(struct sockaddr_in)) != 0,
    "bind");

  DIE_IF(
    listen(sock, 20) != 0,
    "listen");

  fd_set fdset;
  FD_ZERO(&fdset);
  FD_SET(sock, &fdset);
  while(1) {
    fd_set read_fdset = fdset;
    DIE_IF(
      select(FD_SETSIZE, &read_fdset, NULL, NULL, NULL) < 0,
      "select");

    for(int i = 0; i < FD_SETSIZE; i++) {
      if(!FD_ISSET(i, &read_fdset)) continue;

      // Accept new connection
      if(i == sock) {
        struct sockaddr_in client_addr;
        
        int clientfd = accept(
            sock,
            (struct sockaddr*)&client_addr,
            &(unsigned){0});
        printf("Accepting new connection on socket %d\n", clientfd);
        Client *c = client_new();
        c->sock = clientfd;
        c->status = CLIENT_STATUS_WAIT_NICK;
        FD_SET(clientfd, &fdset);
        continue;
      }

      // Service existing connection
      for(int j = 0; j < num_clients; j++) {
        if(clients[j].status == CLIENT_STATUS_DISCONNECTED || clients[j].sock != i) continue;
        if(client_service(j) < 0) {
          client_free(j);
          FD_CLR(clients[j].sock, &fdset);
        }
        break;
      }
    }
  }

  return 0;
}

