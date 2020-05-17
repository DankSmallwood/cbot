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
#define SERVER_HOST "the.server"
#define MAX_CHANNELS 16
#define MAX_CLIENTS 128

#define PREFIX_FMT ":%s!%s@%s "
#define PREFIX_MEMB(c) c->nick, c->user, c->host



enum {
  CLIENT_STATUS_DISCONNECTED = 0,
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
void client_free(Client *c);
int client_service(Client *c);
bool client_in_channel(Client *c, char *channel);

#define COMMANDS \
X(nick, 1) \
X(user, 4) \
X(join, 1) \
X(part, 1) \
X(privmsg, 2) \
X(quit, 0)

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

Client clients[MAX_CLIENTS];



int read_line(int fd, char *buffer, size_t n);
void inspect(char *s);

void say(Client *c, char *fmt, ...);
void say_str(Client *c, char *msg, size_t len);
void say_message(Client *c, Message *m);

void broadcast(Client *except, char *channel, char *fmt, ...);
void broadcast_str(Client *except, char *channel, char *msg, size_t len);
void broadcast_message(Client *except, char *channel, Message *m);




Client *client_new() {
  Client *c = NULL;

  for(int i = 0; i < MAX_CLIENTS; i++) {
    if(clients[i].status != CLIENT_STATUS_DISCONNECTED) continue;
    c = &clients[i];
    break;
  }

  if(!c) {
    printf("Max clients reached!\n");
    return NULL;
  }

  *c = (Client){};
  return c;
}



void client_free(Client *c) {
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



int client_service(Client *c) {
  if(c->status == CLIENT_STATUS_DISCONNECTED) return -1;

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
  switch(c->status) {
  case CLIENT_STATUS_WAIT_NICK:
  case CLIENT_STATUS_OK:
    if(!message_is_nick_valid(m->args[0])) {
      say(c, "432 %s :Erroneous nickname", m->args[0]);
      break;
    }

    for(Client *o = clients; o < clients + MAX_CLIENTS; o++) {
      if(o->status >= CLIENT_STATUS_WAIT_USER && !strcasecmp(m->args[0], o->nick)) {
        say(c, "%s 433 :Nickname already in use", m->args[0]);
        return;
      }
    }

    if(c->status == CLIENT_STATUS_WAIT_NICK) {
      replace(&c->nick, strdup(m->args[0]));
      c->status = CLIENT_STATUS_WAIT_USER;
    } else {
      for(int i = 0; i < MAX_CHANNELS; i++) {
        if(!c->channels[i]) continue;
        broadcast(c, c->channels[i],
            ":%s NICK %s\r\n",
            c->nick, m->args[0]);
      }
      replace(&c->nick, strdup(m->args[0]));
    }
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

    say(c, ":"SERVER_HOST" 001 %s :You", c->nick);
    say(c, ":"SERVER_HOST" 002 %s :are", c->nick);
    say(c, ":"SERVER_HOST" 003 %s :now", c->nick);
    say(c, ":"SERVER_HOST" 004 %s :connected", c->nick);
    break;

  default:
    break;
  }
}



void client_join(Client *c, Message *m) {
  if(!message_is_channel_valid(m->args[0])) {
    say(c, ":"SERVER_HOST" 403 %s :Invalid channel name", c->nick);
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
      if(!c->channels[i] || strcasecmp(m->args[0], c->channels[i])) continue;
      broadcast(NULL, m->args[0],
          ":%s!%s@%s PART %s\r\n",
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
  for(int i = 0; i < MAX_CHANNELS; i++) {
    if(!c->channels[i]) continue;
    broadcast(c, c->channels[i],
        ":%s!%s@%s QUIT :%s\r\n",
        c->nick, c->user, c->host,
        m->num_args >= 1 ? m->args[0] : "Client disconnected");
  }

  say(c, ":%s!%s@%s QUIT :%s",
        c->nick, c->user, c->host,
        m->num_args >= 1 ? m->args[0] : "Client disconnected");
  c->status = CLIENT_STATUS_DISCONNECTED;
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



void say_str(Client *c, char *msg, size_t len) {
  send(c->sock, msg, len, 0);
}



void say(Client *c, char *fmt, ...) {
  static char buffer[MESSAGE_MAX_LEN+1];

  va_list args;
  va_start(args, fmt);
  int len = vsnprintf(buffer, MESSAGE_MAX_LEN+1, fmt, args);
  len += snprintf(buffer+len, MESSAGE_MAX_LEN+1-len, "\r\n");
  va_end(args);

  say_str(c, buffer, len);
}



void say_message(Client *c, Message *m) {
  static char buffer[MESSAGE_MAX_LEN+1];
  message_tostring(m, buffer, MESSAGE_MAX_LEN);

  say_str(c, buffer, strlen(buffer));
}



void broadcast_str(Client *except, char *channel, char *msg, size_t len) {
  for(Client *o = clients; o < clients + MAX_CLIENTS; o++) {
    if(o == except || o->status != CLIENT_STATUS_OK) continue;
    for(char **chan = o->channels; chan < o->channels + MAX_CHANNELS; chan++) {
      if(!chan) continue;
      if(!strcasecmp(channel, *chan)) {
        send(o->sock, msg, len, 0);
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
      for(int j = 0; j < MAX_CLIENTS; j++) {
        if(clients[j].status == CLIENT_STATUS_DISCONNECTED || clients[j].sock != i) continue;
        Client *c = &clients[j];
        if(client_service(c) < 0 || c->status == CLIENT_STATUS_DISCONNECTED) {
          FD_CLR(c->sock, &fdset);
          close(c->sock);
          client_free(c);
        }
        break;
      }
    }
  }

  return 0;
}

