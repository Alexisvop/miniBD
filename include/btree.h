#ifndef MINIDB_BTREE_H
#define MINIDB_BTREE_H

#include "pager.h"
#include <stdbool.h>

#define BTREE_LEAF_TYPE 3U
#define BTREE_INTERNAL_TYPE 4U
#define BTREE_HEADER_SIZE 16U
#define BTREE_LEAF_ENTRY_SIZE 12U
#define BTREE_LEAF_CAPACITY ((MINIDB_PAGE_SIZE - BTREE_HEADER_SIZE) / BTREE_LEAF_ENTRY_SIZE)
#define BTREE_INTERNAL_CAPACITY ((MINIDB_PAGE_SIZE - BTREE_HEADER_SIZE - 4U) / 8U)
/* Defensive recursion bound; valid trees within the page limit are much shorter. */
#define BTREE_MAX_DEPTH 32U

typedef struct { uint32_t page; uint32_t slot; } RecordLocation;
typedef struct {
    Pager *pager; /* Borrowed. */
    uint32_t root;
    uint64_t leaf_splits;
    uint64_t internal_splits;
    uint64_t root_splits;
} BTree;
typedef struct {
    uint32_t leaves;
    uint32_t internal_nodes;
    uint32_t height; /* Root leaf has height 1, including the empty tree. */
    uint32_t entries;
} BTreeStats;
typedef DbStatus (*IndexVisitor)(uint32_t key, RecordLocation location, void *context);

DbStatus btree_create(BTree *tree, Pager *pager);
DbStatus btree_search(BTree *tree, uint32_t key, RecordLocation *output);
/* Requires an active pager savepoint. Caller restores BTree value on failure. */
DbStatus btree_insert(BTree *tree, uint32_t key, RecordLocation location);
DbStatus btree_scan(BTree *tree, IndexVisitor visitor, void *context);
/* seen has MINIDB_MAX_PAGES bytes: 0=unvisited, 1=data/metadata, 2=tree.
 * Validator marks tree nodes and checks each record through visitor. */
DbStatus btree_validate(BTree *tree, uint8_t *seen, IndexVisitor visitor,
                        void *context, BTreeStats *stats);
typedef struct {
    uint32_t id, depth, count, next;
    bool leaf, root;
    uint32_t keys[BTREE_INTERNAL_CAPACITY];
    uint32_t children[BTREE_INTERNAL_CAPACITY + 1U];
    RecordLocation locations[BTREE_LEAF_CAPACITY];
} BTreeNodeView;
/* View/arrays are borrowed for the callback only; the visitor must not mutate DB. */
typedef DbStatus (*BTreeNodeVisitor)(const BTreeNodeView *node, void *context);
DbStatus btree_inspect(BTree *tree, BTreeNodeVisitor visitor, void *context);

#endif
