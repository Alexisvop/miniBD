#include "btree.h"
#include "encoding.h"

#include <stdbool.h>
#include <string.h>

static uint32_t node_key(const uint8_t *page, uint32_t index, bool leaf)
{
    return decode_u32(page + (leaf ? 16U + 12U * index : 20U + 8U * index));
}

static uint32_t child_at(const uint8_t *page, uint32_t index)
{
    return decode_u32(page + 16U + 8U * index);
}

static RecordLocation location_at(const uint8_t *page, uint32_t index)
{
    const uint8_t *entry = page + 16U + 12U * index;
    return (RecordLocation){decode_u32(entry + 4), decode_u32(entry + 8)};
}

static DbStatus read_node(BTree *tree, uint32_t id, const uint8_t **page)
{
    if (id == 0 || id >= pager_page_count(tree->pager)) {
        return DB_INVALID_FORMAT;
    }
    DbStatus status = pager_get(tree->pager, id, page);
    if (status != DB_OK) {
        return status;
    }
    uint32_t type = decode_u32(*page);
    uint32_t count = decode_u32(*page + 4);
    if ((type != BTREE_LEAF_TYPE && type != BTREE_INTERNAL_TYPE) ||
        count > (type == BTREE_LEAF_TYPE ? BTREE_LEAF_CAPACITY : BTREE_INTERNAL_CAPACITY) ||
        (type == BTREE_INTERNAL_TYPE && count == 0)) {
        return DB_INVALID_FORMAT;
    }
    return DB_OK;
}

/* Leaves use lower_bound; internal nodes use upper_bound: equality goes right. */
static uint32_t find_position(const uint8_t *page, uint32_t key, bool leaf)
{
    uint32_t lo = 0, hi = decode_u32(page + 4);
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2U;
        uint32_t value = node_key(page, mid, leaf);
        if (value < key || (!leaf && value == key)) {
            lo = mid + 1U;
        } else {
            hi = mid;
        }
    }
    return lo;
}

DbStatus btree_create(BTree *tree, Pager *pager)
{
    uint8_t *page;
    uint32_t root;
    DbStatus status = pager_allocate(pager, &root, &page);
    if (status != DB_OK) {
        return status;
    }
    encode_u32(page, BTREE_LEAF_TYPE);
    *tree = (BTree){.pager = pager, .root = root};
    return DB_OK;
}

DbStatus btree_search(BTree *tree, uint32_t key, RecordLocation *output)
{
    uint32_t id = tree->root;
    for (uint32_t depth = 0; depth < BTREE_MAX_DEPTH; ++depth) {
        const uint8_t *page;
        DbStatus status = read_node(tree, id, &page);
        if (status != DB_OK) {
            return status;
        }
        bool leaf = decode_u32(page) == BTREE_LEAF_TYPE;
        uint32_t position = find_position(page, key, leaf);
        if (leaf) {
            if (position == decode_u32(page + 4) || node_key(page, position, true) != key) {
                return DB_NOT_FOUND;
            }
            *output = location_at(page, position);
            return DB_OK;
        }
        id = child_at(page, position);
    }
    return DB_INVALID_FORMAT;
}

typedef struct { bool split; uint32_t separator; uint32_t right; } Split;

static void write_leaf(uint8_t *page, const uint8_t *entries, uint32_t count, uint32_t next)
{
    memset(page, 0, MINIDB_PAGE_SIZE);
    encode_u32(page, BTREE_LEAF_TYPE);
    encode_u32(page + 4, count);
    encode_u32(page + 8, next);
    memcpy(page + 16, entries, (size_t)count * 12U);
}

static void write_internal(uint8_t *page, const uint32_t *keys, const uint32_t *children, uint32_t count)
{
    memset(page, 0, MINIDB_PAGE_SIZE);
    encode_u32(page, BTREE_INTERNAL_TYPE);
    encode_u32(page + 4, count);
    encode_u32(page + 16, children[0]);
    for (uint32_t i = 0; i < count; ++i) {
        encode_u32(page + 20U + 8U * i, keys[i]);
        encode_u32(page + 24U + 8U * i, children[i + 1U]);
    }
}

static DbStatus insert_leaf(BTree *tree, uint32_t id, const uint8_t *page,
                            uint32_t key, RecordLocation location, Split *split)
{
    uint32_t count = decode_u32(page + 4);
    uint32_t position = find_position(page, key, true);
    if (position < count && node_key(page, position, true) == key) {
        return DB_DUPLICATE_ID;
    }
    uint8_t entries[(BTREE_LEAF_CAPACITY + 1U) * 12U];
    memcpy(entries, page + 16, (size_t)position * 12U);
    encode_u32(entries + position * 12U, key);
    encode_u32(entries + position * 12U + 4U, location.page);
    encode_u32(entries + position * 12U + 8U, location.slot);
    memcpy(entries + (position + 1U) * 12U, page + 16U + position * 12U,
           (size_t)(count - position) * 12U);
    uint32_t next = decode_u32(page + 8);
    uint8_t *left;
    DbStatus status = pager_write(tree->pager, id, &left);
    if (status != DB_OK) {
        return status;
    }
    ++count;
    if (count <= BTREE_LEAF_CAPACITY) {
        write_leaf(left, entries, count, next);
        return DB_OK;
    }
    uint8_t *right;
    status = pager_allocate(tree->pager, &split->right, &right);
    if (status != DB_OK) {
        return status;
    }
    uint32_t middle = count / 2U;
    write_leaf(right, entries + middle * 12U, count - middle, next);
    write_leaf(left, entries, middle, split->right);
    split->split = true;
    split->separator = decode_u32(entries + middle * 12U);
    ++tree->leaf_splits;
    return DB_OK;
}

static DbStatus insert_node(BTree *tree, uint32_t id, uint32_t key,
                            RecordLocation location, Split *split, uint32_t depth)
{
    if (depth >= BTREE_MAX_DEPTH) {
        return DB_INVALID_FORMAT;
    }
    const uint8_t *page;
    DbStatus status = read_node(tree, id, &page);
    if (status != DB_OK) {
        return status;
    }
    if (decode_u32(page) == BTREE_LEAF_TYPE) {
        return insert_leaf(tree, id, page, key, location, split);
    }
    uint32_t position = find_position(page, key, false);
    Split below = {0};
    status = insert_node(tree, child_at(page, position), key, location, &below, depth + 1U);
    if (status != DB_OK || !below.split) {
        return status;
    }
    uint32_t count = decode_u32(page + 4);
    uint32_t keys[BTREE_INTERNAL_CAPACITY + 1U];
    uint32_t children[BTREE_INTERNAL_CAPACITY + 2U];
    for (uint32_t i = 0; i < count; ++i) {
        keys[i < position ? i : i + 1U] = node_key(page, i, false);
    }
    for (uint32_t i = 0; i <= count; ++i) {
        children[i <= position ? i : i + 1U] = child_at(page, i);
    }
    keys[position] = below.separator;
    children[position + 1U] = below.right;
    uint8_t *left;
    status = pager_write(tree->pager, id, &left);
    if (status != DB_OK) {
        return status;
    }
    ++count;
    if (count <= BTREE_INTERNAL_CAPACITY) {
        write_internal(left, keys, children, count);
        return DB_OK;
    }
    uint8_t *right;
    status = pager_allocate(tree->pager, &split->right, &right);
    if (status != DB_OK) {
        return status;
    }
    uint32_t middle = count / 2U;
    write_internal(left, keys, children, middle);
    write_internal(right, keys + middle + 1U, children + middle + 1U, count - middle - 1U);
    split->split = true;
    split->separator = keys[middle];
    ++tree->internal_splits;
    return DB_OK;
}

DbStatus btree_insert(BTree *tree, uint32_t key, RecordLocation location)
{
    Split split = {0};
    DbStatus status = insert_node(tree, tree->root, key, location, &split, 0);
    if (status != DB_OK || !split.split) {
        return status;
    }
    uint32_t id;
    uint8_t *root;
    status = pager_allocate(tree->pager, &id, &root);
    if (status != DB_OK) {
        return status;
    }
    uint32_t children[] = {tree->root, split.right};
    write_internal(root, &split.separator, children, 1);
    tree->root = id;
    ++tree->root_splits;
    return DB_OK;
}

DbStatus btree_scan(BTree *tree, IndexVisitor visitor, void *context)
{
    uint32_t id = tree->root;
    for (uint32_t depth = 0; depth < BTREE_MAX_DEPTH; ++depth) {
        const uint8_t *page;
        DbStatus status = read_node(tree, id, &page);
        if (status != DB_OK) {
            return status;
        }
        if (decode_u32(page) == BTREE_LEAF_TYPE) {
            break;
        }
        id = child_at(page, 0);
    }
    for (uint32_t steps = 0; id != 0 && steps < pager_page_count(tree->pager); ++steps) {
        const uint8_t *page;
        DbStatus status = read_node(tree, id, &page);
        if (status != DB_OK) {
            return status;
        }
        if (decode_u32(page) != BTREE_LEAF_TYPE) {
            return DB_INVALID_FORMAT;
        }
        for (uint32_t i = 0; i < decode_u32(page + 4); ++i) {
            status = visitor(node_key(page, i, true), location_at(page, i), context);
            if (status != DB_OK) {
                return status;
            }
        }
        id = decode_u32(page + 8);
    }
    return id == 0 ? DB_OK : DB_INVALID_FORMAT;
}

typedef struct { uint32_t minimum; uint32_t maximum; } KeyRange;
typedef struct {
    BTree *tree;
    uint8_t *seen;
    IndexVisitor visitor;
    void *context;
    BTreeStats stats;
    uint32_t expected_leaf;
    bool have_leaf;
} Validation;

static DbStatus validate_node(Validation *v, uint32_t id, uint32_t depth, KeyRange *range)
{
    if (depth > BTREE_MAX_DEPTH || id == 0 || id >= pager_page_count(v->tree->pager) || v->seen[id] != 0) {
        return DB_INVALID_FORMAT;
    }
    const uint8_t *page;
    DbStatus status = read_node(v->tree, id, &page);
    if (status != DB_OK) {
        return status;
    }
    v->seen[id] = 2;
    bool leaf = decode_u32(page) == BTREE_LEAF_TYPE;
    uint32_t count = decode_u32(page + 4);
    if ((depth > 1 && count < (leaf ? BTREE_LEAF_CAPACITY / 2U : BTREE_INTERNAL_CAPACITY / 2U)) ||
        decode_u32(page + 12) != 0 || (!leaf && decode_u32(page + 8) != 0)) {
        return DB_INVALID_FORMAT;
    }
    size_t used = leaf ? 16U + (size_t)count * 12U : 20U + (size_t)count * 8U;
    for (size_t i = used; i < MINIDB_PAGE_SIZE; ++i) {
        if (page[i] != 0) {
            return DB_INVALID_FORMAT;
        }
    }
    for (uint32_t i = 1; i < count; ++i) {
        if (node_key(page, i - 1U, leaf) >= node_key(page, i, leaf)) {
            return DB_INVALID_FORMAT;
        }
    }
    if (leaf) {
        if ((v->have_leaf && v->expected_leaf != id) || (v->stats.height != 0 && v->stats.height != depth)) {
            return DB_INVALID_FORMAT;
        }
        v->have_leaf = true;
        v->expected_leaf = decode_u32(page + 8);
        v->stats.height = depth;
        ++v->stats.leaves;
        v->stats.entries += count;
        for (uint32_t i = 0; i < count; ++i) {
            status = v->visitor(node_key(page, i, true), location_at(page, i), v->context);
            if (status != DB_OK) {
                return status;
            }
        }
        range->minimum = count == 0 ? 0 : node_key(page, 0, true);
        range->maximum = count == 0 ? 0 : node_key(page, count - 1U, true);
        return DB_OK;
    }
    ++v->stats.internal_nodes;
    KeyRange previous = {0};
    for (uint32_t i = 0; i <= count; ++i) {
        KeyRange child;
        status = validate_node(v, child_at(page, i), depth + 1U, &child);
        if (status != DB_OK) {
            return status;
        }
        if (i == 0) {
            range->minimum = child.minimum;
        } else if (previous.maximum >= child.minimum || node_key(page, i - 1U, false) != child.minimum) {
            return DB_INVALID_FORMAT;
        }
        previous = child;
    }
    range->maximum = previous.maximum;
    return DB_OK;
}

DbStatus btree_validate(BTree *tree, uint8_t *seen, IndexVisitor visitor,
                        void *context, BTreeStats *stats)
{
    Validation validation = {.tree = tree, .seen = seen, .visitor = visitor, .context = context};
    KeyRange range;
    DbStatus status = validate_node(&validation, tree->root, 1, &range);
    if (status == DB_OK && validation.expected_leaf != 0) {
        return DB_INVALID_FORMAT;
    }
    if (status == DB_OK) {
        *stats = validation.stats;
    }
    return status;
}

static DbStatus inspect_node(BTree *tree, uint32_t id, BTreeNodeVisitor visitor,
                              void *context, uint8_t *seen, unsigned int depth)
{
    if (depth >= BTREE_MAX_DEPTH || id == 0 || id >= pager_page_count(tree->pager) || seen[id])
        return DB_INVALID_FORMAT;
    seen[id] = 1;
    const uint8_t *page;
    DbStatus status = read_node(tree, id, &page);
    if (status != DB_OK) return status;
    BTreeNodeView node = {
        .id = id, .depth = depth, .count = decode_u32(page + 4),
        .leaf = decode_u32(page) == BTREE_LEAF_TYPE, .root = id == tree->root
    };
    node.next = node.leaf ? decode_u32(page + 8) : 0;
    for (uint32_t i = 0; i < node.count; ++i) {
        node.keys[i] = node_key(page, i, node.leaf);
        if (node.leaf) node.locations[i] = location_at(page, i);
    }
    if (!node.leaf) {
        for (uint32_t i = 0; i <= node.count; ++i) node.children[i] = child_at(page, i);
    }
    status = visitor(&node, context);
    if (status != DB_OK) return status;
    if (!node.leaf) {
        for (uint32_t i = 0; i <= node.count; ++i) {
            status = inspect_node(tree, node.children[i], visitor, context, seen, depth + 1U);
            if (status != DB_OK) return status;
        }
    }
    return DB_OK;
}

DbStatus btree_inspect(BTree *tree, BTreeNodeVisitor visitor, void *context)
{
    uint8_t seen[MINIDB_MAX_PAGES] = {0};
    return inspect_node(tree, tree->root, visitor, context, seen, 0);
}
