import test from 'node:test';
import assert from 'node:assert/strict';
import { emptyTree, exampleTree, insertTree, searchTree, treeLevels, treeSize } from '../src/demoTree.ts';

function validateTree(tree, expected) {
  const all = new Set();
  const depth = new Set();
  const leaves = [];
  function visit(id, level, lo, hi) {
    const node = tree.nodes[id];
    assert.ok(node, 'child reference must exist');
    assert.ok(node.keys.length <= 3, 'node occupancy cap');
    assert.deepEqual(node.keys, [...node.keys].sort((a,b)=>a-b), 'sorted keys');
    for(const value of node.keys) assert.ok(value >= lo && value < hi, 'range check');
    if(node.kind==='leaf') {
      depth.add(level);
      leaves.push(node);
      for(const value of node.keys) { assert.ok(!all.has(value), 'unique'); all.add(value); }
      return node.keys[0];
    }
    assert.equal(node.children.length, node.keys.length + 1);
    let min;
    for(let i=0;i<node.children.length;i++) {
      const next = visit(node.children[i],level+1,i===0?lo:node.keys[i-1],i===node.keys.length?hi:node.keys[i]);
      if(i===0) min=next;
      else assert.equal(next,node.keys[i-1], 'right subtree minimum equals separator');
    }
    return min;
  }
  visit(tree.root,0,-Infinity,Infinity);
  assert.equal(depth.size,1,'all leaves same depth');
  assert.equal(all.size,expected.length);
  assert.deepEqual([...all].sort((a,b)=>a-b),[...expected].sort((a,b)=>a-b));
  for(let i=0;i<leaves.length;i++) assert.equal(leaves[i].next,leaves[i+1]?.id??null);
  assert.equal(treeSize(tree),expected.length);
}

test('empty and example trees are consistent', () => {
  const empty = emptyTree();
  assert.equal(searchTree(empty,5).found,false);
  validateTree(empty,[]);
  const sample = exampleTree();
  validateTree(sample,[24,8,40,16,32,48,4,12,20,28,36,44,52]);
  assert.ok(treeLevels(sample).length>1);
});

test('insertion, split and lookup across orders',()=>{
  const orders = [
    Array.from({length:80},(_,i)=>i),
    Array.from({length:80},(_,i)=>79-i),
    Array.from({length:80},(_,i)=>(i*37)%80),
  ];
  for(const keys of orders) {
    let tree=emptyTree();
    const seen=[];
    for(const key of keys) {
      const result=insertTree(tree,key);
      assert.ok(result.inserted);
      tree=result.tree;
      seen.push(key);
      validateTree(tree,seen);
      assert.ok(searchTree(tree,key).found);
    }
    assert.ok(!insertTree(tree,keys[0]).inserted);
    assert.ok(!searchTree(tree,999).found);
  }
});

test('invalid keys do not change the tree',()=>{
  const tree=exampleTree();
  for(const key of [-1,1.5,NaN,Infinity,1000]) {
    const result=insertTree(tree,key);
    assert.equal(result.inserted,false);
    assert.equal(result.tree,tree);
  }
});
