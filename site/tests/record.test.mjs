import test from 'node:test';
import assert from 'node:assert/strict';
import { encodeRecord, RECORD_SIZE } from '../src/record.ts';

test('encodes a record as 70 little-endian bytes', () => {
  const r = encodeRecord('258', 'Ana', '30');
  assert.ok(r.ok);
  assert.equal(r.cells.length, RECORD_SIZE);
  assert.deepEqual(r.cells.slice(0, 4).map(c => c.value), [0x02, 0x01, 0x00, 0x00]);
  assert.equal(r.cells[4].value, 30);
  assert.equal(r.cells[5].value, 3);
  assert.deepEqual(r.cells.slice(6, 9).map(c => c.value), [0x41, 0x6e, 0x61]);
  assert.ok(r.cells.slice(9).every(c => c.value === 0 && c.field === 'pad'));
});

test('counts UTF-8 bytes, not characters', () => {
  const r = encodeRecord('1', 'José', '20');
  assert.ok(r.ok);
  assert.equal(r.nameBytes, 5);
  assert.equal(r.cells[5].value, 5);
});

test('rejects values outside the on-disk format', () => {
  assert.equal(encodeRecord('4294967296', 'A', '1').ok, false);
  assert.equal(encodeRecord('1', 'A', '151').ok, false);
  assert.equal(encodeRecord('1', '', '1').ok, false);
  assert.equal(encodeRecord('1', 'é'.repeat(33), '1').ok, false);
  assert.equal(encodeRecord('1', 'a\nb', '1').ok, false);
  assert.ok(encodeRecord('4294967295', 'x'.repeat(64), '150').ok);
});
