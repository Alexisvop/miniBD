/**
 * Small order-4 educational B+ tree for the browser visualization.
 * NOT the C17 MiniDB B+ tree. The real engine has 340-entry leaf pages and
 * 509 separators per internal page; this demo uses 3 keys per node for clarity.
 */
export type LeafNode = { id: string; kind: 'leaf'; keys: number[]; next: string | null };
export type InternalNode = { id: string; kind: 'internal'; keys: number[]; children: string[] };
export type TreeNode = LeafNode | InternalNode;
export type Tree = { root: string; nodes: Record<string, TreeNode>; nextId: number };
export type Insertion = { tree: Tree; inserted: boolean; splits: number };
export type Search = { found: boolean; path: string[] };

const MAX_KEYS = 3;

export function emptyTree(): Tree {
  return { root: 'n1', nodes: { n1: { id: 'n1', kind: 'leaf', keys: [], next: null } }, nextId: 2 };
}

function cloneTree(previous: Tree): Tree {
  const nodes: Record<string, TreeNode> = {};
  Object.entries(previous.nodes).forEach(([id, n]) => {
    nodes[id] = n.kind === 'leaf'
      ? { ...n, keys: [...n.keys] }
      : { ...n, keys: [...n.keys], children: [...n.children] };
  });
  return { root: previous.root, nodes, nextId: previous.nextId };
}

function newId(tree: Tree): string {
  const result = 'n' + tree.nextId;
  tree.nextId += 1;
  return result;
}

function childIndex(keys: number[], value: number): number {
  let i = 0;
  while (i < keys.length && value >= keys[i]) i += 1;
  return i;
}

export function searchTree(tree: Tree, key: number): Search {
  const path: string[] = [];
  let node = tree.nodes[tree.root];
  while (node) {
    path.push(node.id);
    if (node.kind === 'leaf') return { found: node.keys.includes(key), path };
    node = tree.nodes[node.children[childIndex(node.keys, key)]];
  }
  return { found: false, path };
}

type Split = { separator: number; rightId: string };

export function insertTree(previous: Tree, key: number): Insertion {
  if (!Number.isSafeInteger(key) || key < 0 || key > 999) {
    return { tree: previous, inserted: false, splits: 0 };
  }
  if (searchTree(previous, key).found) return { tree: previous, inserted: false, splits: 0 };
  const tree = cloneTree(previous);
  let splits = 0;

  function insertAt(id: string): Split | null {
    const node = tree.nodes[id];
    if (node.kind === 'leaf') {
      node.keys.push(key);
      node.keys.sort((a, b) => a - b);
      if (node.keys.length <= MAX_KEYS) return null;
      const midpoint = Math.ceil(node.keys.length / 2);
      const rightKeys = node.keys.splice(midpoint);
      const rightId = newId(tree);
      tree.nodes[rightId] = { id: rightId, kind: 'leaf', keys: rightKeys, next: node.next };
      node.next = rightId;
      splits += 1;
      return { separator: rightKeys[0], rightId };
    }

    const index = childIndex(node.keys, key);
    const split = insertAt(node.children[index]);
    if (!split) return null;
    node.keys.splice(index, 0, split.separator);
    node.children.splice(index + 1, 0, split.rightId);
    if (node.keys.length <= MAX_KEYS) return null;
    const middle = Math.floor(node.keys.length / 2);
    const separator = node.keys[middle];
    const rightKeys = node.keys.splice(middle + 1);
    node.keys.splice(middle, 1);
    const rightChildren = node.children.splice(middle + 1);
    const rightId = newId(tree);
    tree.nodes[rightId] = { id: rightId, kind: 'internal', keys: rightKeys, children: rightChildren };
    splits += 1;
    return { separator, rightId };
  }

  const split = insertAt(tree.root);
  if (split) {
    const oldRoot = tree.root;
    const rootId = newId(tree);
    tree.nodes[rootId] = { id: rootId, kind: 'internal', keys: [split.separator], children: [oldRoot, split.rightId] };
    tree.root = rootId;
  }
  return { tree, inserted: true, splits };
}

export function exampleTree(): Tree {
  const numbers = [24, 8, 40, 16, 32, 48, 4, 12, 20, 28, 36, 44, 52];
  return numbers.reduce((tree, key) => insertTree(tree, key).tree, emptyTree());
}

export function treeLevels(tree: Tree): string[][] {
  const levels: string[][] = [];
  let layer = [tree.root];
  while (layer.length) {
    levels.push(layer);
    const next: string[] = [];
    for (const id of layer) {
      const node = tree.nodes[id];
      if (node.kind === 'internal') next.push(...node.children);
    }
    layer = next;
  }
  return levels;
}

export function treeSize(tree: Tree): number {
  return Object.values(tree.nodes).reduce((count, n) => count + (n.kind === 'leaf' ? n.keys.length : 0), 0);
}
