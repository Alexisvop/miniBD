/**
 * Browser mirror of MiniDB's 70-byte record serialization (docs/storage-format.md):
 *   offset 0  · 4 bytes  · uint32 ID, little-endian
 *   offset 4  · 1 byte   · age 0..150
 *   offset 5  · 1 byte   · UTF-8 name length 1..64
 *   offset 6  · 64 bytes · name bytes, then zero padding
 */
export const RECORD_SIZE = 70;
export const NAME_MAX_BYTES = 64;
export const AGE_MAX = 150;
export const ID_MAX = 4294967295;

export type Field = 'id' | 'age' | 'len' | 'name' | 'pad';
export type Cell = { offset: number; value: number; field: Field };
export type Encoded = { ok: true; cells: Cell[]; nameBytes: number } | { ok: false; error: string };

export function encodeRecord(idText: string, name: string, ageText: string): Encoded {
  if (!/^(0|[1-9][0-9]{0,9})$/.test(idText) || Number(idText) > ID_MAX) {
    return { ok: false, error: 'ID must be a whole number from 0 to 4,294,967,295.' };
  }
  if (!/^(0|[1-9][0-9]{0,2})$/.test(ageText) || Number(ageText) > AGE_MAX) {
    return { ok: false, error: 'Age must be a whole number from 0 to 150.' };
  }
  const bytes = new TextEncoder().encode(name);
  if (bytes.length < 1) return { ok: false, error: 'Name needs at least 1 byte.' };
  if (bytes.length > NAME_MAX_BYTES) {
    return { ok: false, error: 'Name is ' + bytes.length + ' bytes in UTF-8; the limit is 64 bytes.' };
  }
  if (/[\u0000-\u001f\u007f]/.test(name)) return { ok: false, error: 'Name cannot contain control characters.' };

  const id = Number(idText);
  const cells: Cell[] = [];
  for (let i = 0; i < 4; i++) cells.push({ offset: i, value: Math.floor(id / 2 ** (8 * i)) % 256, field: 'id' });
  cells.push({ offset: 4, value: Number(ageText), field: 'age' });
  cells.push({ offset: 5, value: bytes.length, field: 'len' });
  for (let i = 0; i < NAME_MAX_BYTES; i++) {
    cells.push({ offset: 6 + i, value: i < bytes.length ? bytes[i] : 0, field: i < bytes.length ? 'name' : 'pad' });
  }
  return { ok: true, cells, nameBytes: bytes.length };
}

export const hex = (n: number, width = 2) => n.toString(16).padStart(width, '0');
