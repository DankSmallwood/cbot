#ifndef NODE_H
#define NODE_H

#include "nodes/__types.z"
#include "nodes/__symbols.z"
#include "nodes/__union.z"

#define MAX_NAME_LEN 64

typedef struct {
  char name[MAX_NAME_LEN];
  int type;
  NodeUnion node_union;
} Node;

#endif
