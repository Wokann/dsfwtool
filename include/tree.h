
#ifndef TREE_H
#define TREE_H

#include "nds_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _NODE {
    struct _NODE *left;
    struct _NODE *right;
    u32 value;
    u32 weight;
} NODE, *PNODE;

PNODE node_create(PNODE left, PNODE right, u32 value, u32 weight);
void free_tree(PNODE node);

#ifdef __cplusplus
}
#endif

#endif
