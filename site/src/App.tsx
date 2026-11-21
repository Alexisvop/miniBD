import { useEffect, useMemo, useRef, useState } from 'react';
import type { FormEvent, ReactNode } from 'react';
import { emptyTree, exampleTree, insertTree, searchTree, treeLevels, treeSize } from './demoTree';
import type { Tree, TreeNode } from './demoTree';
import { encodeRecord, hex } from './record';
import type { Cell, Field } from './record';

const REPO = 'https://github.com/Alexisvop/miniBD';
const BLOB = REPO + '/blob/main/';
const BENCH_SOURCE = BLOB + 'benchmarks/summary-phase5-2026-10-15.csv';
const BENCHMARKS = [
  { n: 1000, btree: 0.322252, linear: 1.498043 },
  { n: 10000, btree: 0.454206, linear: 13.297411 },
  { n: 50000, btree: 0.744984, linear: 140.732848 },
  { n: 100000, btree: 0.917055, linear: 287.190818 },
];
const MAGIC = [0x4d, 0x49, 0x4e, 0x49, 0x44, 0x42, 0x0d, 0x0a];
const MAGIC_ASCII = ['M', 'I', 'N', 'I', 'D', 'B', '\\r', '\\n'];

function External({ href, children, className }: { href: string; children: ReactNode; className?: string }) {
  return <a className={className} href={href} target="_blank" rel="noopener noreferrer">{children}<span className="sr-only"> (opens in a new tab)</span></a>;
}

/** Each section is drawn as one 4 KB page of the file; the gutter shows its real byte offset. */
function Page({ id, index, title, intro, children }: { id: string; index: number; title: string; intro?: ReactNode; children: ReactNode }) {
  return <section className="page" id={id} aria-labelledby={id + '-title'}>
    <div className="page-gutter" aria-hidden="true">
      <span className="page-offset">0x{hex(index * 4096, 4)}</span>
      <span className="page-id">page {index}</span>
    </div>
    <div className="page-body">
      <header className="page-head">
        <h2 id={id + '-title'}>{title}</h2>
        {intro && <p>{intro}</p>}
      </header>
      {children}
    </div>
  </section>;
}

/* ---------- Record encoder (hero) ---------- */

const FIELD_INFO: Record<Field, { label: string; size: string }> = {
  id: { label: 'ID', size: '4 bytes, little-endian' },
  age: { label: 'Age', size: '1 byte' },
  len: { label: 'Name length', size: '1 byte' },
  name: { label: 'Name', size: 'UTF-8 bytes' },
  pad: { label: 'Padding', size: 'zeros up to 70' },
};

function describe(cell: Cell) {
  const f = FIELD_INFO[cell.field];
  const printable = cell.field === 'name' && cell.value >= 0x20 && cell.value < 0x7f ? ' ‘' + String.fromCharCode(cell.value) + '’' : '';
  return 'Offset 0x' + hex(cell.offset) + ': ' + f.label.toLowerCase() + ', value 0x' + hex(cell.value) + ' (' + cell.value + ')' + printable;
}

function RecordEncoder() {
  const [id, setId] = useState('258');
  const [name, setName] = useState('Ana');
  const [age, setAge] = useState('30');
  const [focus, setFocus] = useState<number | null>(null);
  const [cursor, setCursor] = useState(0);
  const dumpRef = useRef<HTMLDivElement>(null);
  function move(e: React.KeyboardEvent, offset: number) {
    const step = { ArrowRight: 1, ArrowLeft: -1, ArrowDown: 16, ArrowUp: -16, Home: -offset, End: 69 - offset }[e.key];
    if (step === undefined) return;
    e.preventDefault();
    const next = Math.max(0, Math.min(69, offset + step));
    setCursor(next);
    dumpRef.current?.querySelector<HTMLButtonElement>('[data-offset="' + next + '"]')?.focus();
  }
  const encoded = useMemo(() => encodeRecord(id, name, age), [id, name, age]);
  const [last, setLast] = useState(encoded.ok ? encoded : encodeRecord('258', 'Ana', '30'));
  useEffect(() => { if (encoded.ok) setLast(encoded); }, [encoded]);
  const shown = encoded.ok ? encoded : last;
  if (!shown.ok) return null;
  const rows = [0, 16, 32, 48, 64].map(start => shown.cells.slice(start, start + 16));
  const active = focus === null ? null : shown.cells[focus];

  return <div className="encoder">
    <form className="encoder-fields" onSubmit={e => e.preventDefault()} aria-describedby="encoder-help">
      <label><span>ID</span><input inputMode="numeric" value={id} maxLength={10} onChange={e => setId(e.target.value)} /></label>
      <label className="grow"><span>Name</span><input value={name} maxLength={80} spellCheck={false} onChange={e => setName(e.target.value)} /></label>
      <label><span>Age</span><input inputMode="numeric" value={age} maxLength={3} onChange={e => setAge(e.target.value)} /></label>
    </form>
    <p id="encoder-help" className={'encoder-status' + (encoded.ok ? '' : ' error')} role="status">
      {encoded.ok
        ? <>Name uses {encoded.nameBytes} of 64 bytes. 58 records like this fit in one data page after its 16-byte header.</>
        : <>{encoded.error} Showing the last valid record.</>}
    </p>
    <div className="dump" ref={dumpRef} role="group" aria-label="The record as stored on disk, 70 bytes in hexadecimal. Use the arrow keys to move between bytes.">
      {rows.map((row, r) => <div className="dump-row" key={r}>
        <span className="dump-off" aria-hidden="true">{hex(r * 16)}</span>
        <span className="dump-bytes">
          {row.map(cell => <button type="button" key={cell.offset} className={'byte f-' + cell.field + (focus === cell.offset ? ' on' : '')}
            style={{ animationDelay: cell.offset * 9 + 'ms' }} data-offset={cell.offset} tabIndex={cell.offset === cursor ? 0 : -1}
            onKeyDown={e => move(e, cell.offset)}
            aria-label={describe(cell)} onMouseEnter={() => setFocus(cell.offset)} onFocus={() => { setFocus(cell.offset); setCursor(cell.offset); }}
            onMouseLeave={() => setFocus(null)} onBlur={() => setFocus(null)}>{hex(cell.value)}</button>)}
        </span>
        <span className="dump-ascii" aria-hidden="true">{row.map(c => <i key={c.offset} className={'f-' + c.field + (focus === c.offset ? ' on' : '')}>{c.value >= 0x20 && c.value < 0x7f ? String.fromCharCode(c.value) : '.'}</i>)}</span>
      </div>)}
    </div>
    <p className="dump-caption" aria-hidden="true">{active ? describe(active) : 'Point at a byte to see what it stores.'}</p>
    <ul className="legend">
      {(Object.keys(FIELD_INFO) as Field[]).map(f => <li key={f}><i className={'swatch f-' + f} /><b>{FIELD_INFO[f].label}</b> {FIELD_INFO[f].size}</li>)}
    </ul>
  </div>;
}

/* ---------- Layers ---------- */

const LAYERS = [
  { name: 'REPL and parser', file: 'src/parser.c', text: 'Reads the line and checks it: the ID fits in 32 bits, the name is 1 to 64 UTF-8 bytes, the age is 0 to 150.' },
  { name: 'Executor', file: 'src/executor.c', text: 'Turns the parsed command into a storage call and returns results through a callback. Storage code never prints.' },
  { name: 'Storage engine', file: 'src/storage.c', text: 'Writes the 70-byte record into a free slot of a data page, up to 58 slots per page, and clears its tombstone bit.' },
  { name: 'B+ tree', file: 'src/btree.c', text: 'Maps ID 7 to its (page, slot). Leaf pages hold up to 340 entries and link to the next leaf for sorted scans; internal pages hold up to 509 separators.' },
  { name: 'Pager', file: 'src/pager.c', text: 'Caches 4,096-byte pages, marks the page dirty and writes it at page_id × 4096, with fsync on a normal sync. It does not guarantee crash recovery.' },
];

function Layers() {
  return <Page id="layers" index={1} title="One command, five layers" intro={<>Follow <code>INSERT 7 "Ana" 30</code> from the prompt down to the file. Each layer has one job and its own tests.</>}>
    <ol className="layers">
      {LAYERS.map((l, i) => <li key={l.name}>
        <span className="layer-n" aria-hidden="true">{i + 1}</span>
        <h3>{l.name}</h3>
        <p>{l.text}</p>
        <External className="layer-file" href={BLOB + l.file}>{l.file}</External>
      </li>)}
    </ol>
  </Page>;
}

/* ---------- B+ tree explorer ---------- */

type Positioned = { id: string; node: TreeNode; x: number; y: number };
const NODE_W = 96, NODE_H = 44, GAP_X = 116, GAP_Y = 96;

function TreeGraphic({ tree, path }: { tree: Tree; path: string[] }) {
  const layout = useMemo(() => {
    const levels = treeLevels(tree);
    const centers: Record<string, number> = {};
    const leaves = levels[levels.length - 1];
    leaves.forEach((id, i) => { centers[id] = NODE_W / 2 + 12 + i * GAP_X; });
    for (let level = levels.length - 2; level >= 0; level--) {
      for (const id of levels[level]) {
        const node = tree.nodes[id];
        if (node.kind === 'internal') centers[id] = (centers[node.children[0]] + centers[node.children[node.children.length - 1]]) / 2;
      }
    }
    const positions: Positioned[] = levels.flatMap((ids, depth) => ids.map(id => ({ id, node: tree.nodes[id], x: centers[id], y: 14 + depth * GAP_Y })));
    const width = Math.max(560, leaves.length * GAP_X + 8);
    return { positions, width, height: levels.length * GAP_Y - GAP_Y + NODE_H + 28 };
  }, [tree]);
  const byId = Object.fromEntries(layout.positions.map(n => [n.id, n]));
  const scroller = useRef<HTMLDivElement>(null);
  useEffect(() => {
    const el = scroller.current;
    if (el) el.scrollLeft = (el.scrollWidth - el.clientWidth) / 2;
  }, [layout.width]);
  const offsetX = (layout.width - (Math.max(...layout.positions.map(p => p.x)) + NODE_W / 2 + 12)) / 2;
  return <div className="tree-scroll" ref={scroller}>
    <svg width={layout.width} height={layout.height} viewBox={'0 0 ' + layout.width + ' ' + layout.height} role="img"
      aria-label={'B+ tree with ' + treeSize(tree) + ' keys in ' + treeLevels(tree).length + ' levels. Internal nodes route lookups; leaves store keys and link left to right.'}>
      <g transform={'translate(' + offsetX + ' 0)'}>
        <g className="edges">
          {layout.positions.map(item => item.node.kind === 'internal'
            ? item.node.children.map(child => <path key={item.id + child} className={path.includes(item.id) && path.includes(child) ? 'hot' : ''}
              d={'M' + item.x + ' ' + (item.y + NODE_H) + ' C' + item.x + ' ' + (item.y + NODE_H + 30) + ' ' + byId[child].x + ' ' + (byId[child].y - 30) + ' ' + byId[child].x + ' ' + byId[child].y} />)
            : null)}
        </g>
        {layout.positions.filter(item => item.node.kind === 'leaf' && item.node.next).map(item => {
          const other = byId[(item.node as Extract<TreeNode, { kind: 'leaf' }>).next!];
          return other ? <path key={item.id + '-next'} className="leaf-link" d={'M' + (item.x + NODE_W / 2) + ' ' + (item.y + NODE_H / 2) + ' H' + (other.x - NODE_W / 2)} /> : null;
        })}
        {layout.positions.map(item => {
          const leaf = item.node.kind === 'leaf';
          const hot = path.includes(item.id);
          return <g key={item.id} className={'node ' + (leaf ? 'leaf' : 'internal') + (hot ? ' hot' : '')}>
            <rect x={item.x - NODE_W / 2} y={item.y} width={NODE_W} height={NODE_H} rx="3" />
            <text className="node-kind" x={item.x - NODE_W / 2 + 7} y={item.y + 13}>{leaf ? 'leaf' : 'internal'}</text>
            <text className="node-keys" x={item.x} y={item.y + 32} textAnchor="middle">{item.node.keys.join(' ') || '—'}</text>
          </g>;
        })}
      </g>
    </svg>
  </div>;
}

function TreePlayground() {
  const [tree, setTree] = useState<Tree>(exampleTree);
  const [value, setValue] = useState('55');
  const [path, setPath] = useState<string[]>([]);
  const [event, setEvent] = useState('This tree holds 13 keys. Insert one, or trace where a lookup goes.');
  const [totalSplits, setTotalSplits] = useState(0);
  const parsed = Number(value);
  const valid = /^\d{1,3}$/.test(value) && parsed <= 999;

  function addKey() {
    if (!valid) { setEvent('Enter a whole number from 0 to 999.'); return; }
    if (treeSize(tree) >= 25) { setEvent('The demo stops at 25 keys so the tree stays readable. Clear it to keep going.'); return; }
    const result = insertTree(tree, parsed);
    setTree(result.tree);
    setTotalSplits(n => n + result.splits);
    setPath(searchTree(result.tree, parsed).path);
    setEvent(!result.inserted ? 'Key ' + parsed + ' is already in the tree.'
      : result.splits ? 'Inserted ' + parsed + '. A full node split ' + (result.splits === 1 ? 'once' : result.splits + ' times') + ' to make room.'
      : 'Inserted ' + parsed + '. The leaf had room, so nothing split.');
  }

  function lookup() {
    if (!valid) { setEvent('Enter a whole number from 0 to 999.'); return; }
    const result = searchTree(tree, parsed);
    setPath(result.path);
    setEvent((result.found ? 'Found ' : 'Key ' + parsed + ' is not here. Checked ') + (result.found ? parsed + ' after reading ' : '') + result.path.length + ' node' + (result.path.length === 1 ? '' : 's') + ', one per level.');
  }

  return <Page id="explorer" index={2} title="Insert keys and watch the tree split"
    intro="Every lookup reads one node per level, so the cost grows with the tree's height, not its size. Highlighted nodes show the path of the last insert or lookup.">
    <div className="tool">
      <div className="tool-bar">
        <label className="sr-only" htmlFor="key-input">Key, a whole number from 0 to 999</label>
        <input id="key-input" inputMode="numeric" maxLength={3} value={value} onChange={e => setValue(e.target.value)} onKeyDown={e => { if (e.key === 'Enter') addKey(); }} placeholder="0–999" />
        <button type="button" className="btn primary" onClick={addKey}>Insert key</button>
        <button type="button" className="btn" onClick={lookup}>Trace lookup</button>
        <span className="tool-spacer" />
        <button type="button" className="btn quiet" onClick={() => { setTree(exampleTree()); setTotalSplits(0); setPath([]); setEvent('Back to the 13-key example.'); }}>Restore example</button>
        <button type="button" className="btn quiet" onClick={() => { setTree(emptyTree()); setTotalSplits(0); setPath([]); setEvent('The tree is empty. Insert a key to start.'); }}>Clear tree</button>
      </div>
      <TreeGraphic tree={tree} path={path} />
      <div className="tool-foot">
        <p role="status" aria-live="polite">{event}</p>
        <dl className="counts"><div><dt>keys</dt><dd>{treeSize(tree)}</dd></div><div><dt>levels</dt><dd>{treeLevels(tree).length}</dd></div><div><dt>splits</dt><dd>{totalSplits}</dd></div></dl>
      </div>
    </div>
    <p className="note">This demo runs in your browser and holds at most 3 keys per node so splits happen often. The real C17 tree fits 340 entries in a leaf page and 509 separators in an internal page.</p>
  </Page>;
}

/* ---------- Command playground ---------- */

type DemoRow = { id: number; name: string; age: number; deleted: boolean };
type TerminalLine = { kind: 'input' | 'result' | 'error' | 'hint'; text: string };
const ORIGINAL: DemoRow[] = [
  { id: 3, name: 'Mariana', age: 25, deleted: false },
  { id: 1, name: 'Alex', age: 21, deleted: false },
  { id: 2, name: 'Carlos', age: 20, deleted: false },
];
const PRESETS = ['SELECT_ALL', 'SELECT 1', 'UPDATE 1 "Alexandra" 22', 'DELETE 2', 'SELECT_ALL WHERE age >= 21', '.stats'];

function TerminalPlayground() {
  const [rows, setRows] = useState<DemoRow[]>(ORIGINAL);
  const [command, setCommand] = useState('');
  const [lines, setLines] = useState<TerminalLine[]>([
    { kind: 'hint', text: 'Simulated in your browser with three sample rows. Type .help for the syntax.' },
    { kind: 'input', text: 'SELECT_ALL' },
    { kind: 'result', text: '1 | Alex | 21' },
    { kind: 'result', text: '2 | Carlos | 20' },
    { kind: 'result', text: '3 | Mariana | 25' },
  ]);
  const scrollRef = useRef<HTMLDivElement>(null);
  const MAX_HISTORY = 28;

  function run(input: string) {
    const cmd = input.trim();
    if (!cmd) return;
    const log: TerminalLine[] = [{ kind: 'input', text: cmd }];
    const active = rows.filter(row => !row.deleted).sort((a, b) => a.id - b.id);
    const print = (s: string) => log.push({ kind: 'result', text: s });
    const err = (s: string) => log.push({ kind: 'error', text: s });
    const idRegex = '(0|[1-9][0-9]{0,9})';
    const readId = (v: string) => { const n = Number(v); return n <= 4294967295 ? n : null; };
    const rowFormat = (r: DemoRow) => r.id + ' | ' + r.name + ' | ' + r.age;
    const insertOrUpdate = cmd.match(new RegExp('^(INSERT|UPDATE) ' + idRegex + ' "([^"]+)" (0|[1-9][0-9]{0,2})$'));
    const selectOrDelete = cmd.match(new RegExp('^(SELECT|DELETE) ' + idRegex + '$'));
    const filter = cmd.match(/^SELECT_ALL WHERE age (<=|>=|!=|=|<|>) (0|[1-9][0-9]{0,2})$/);
    const validName = (s: string) => new TextEncoder().encode(s).length <= 64 && !/[\u0000-\u001f]/.test(s);

    if (cmd === '.help') {
      print('INSERT id "name" age   SELECT id   SELECT_ALL [WHERE age op value]');
      print('UPDATE id "name" age   DELETE id   .stats   .help   .exit');
    } else if (cmd === '.exit') {
      print('Session ended. Use Reset to start over.');
    } else if (cmd === '.stats') {
      print('Rows: ' + rows.length + ' total, ' + active.length + ' active, ' + (rows.length - active.length) + ' tombstoned');
      print('Counts come from this browser demo, not the real pager or index.');
    } else if (cmd === '.btree') {
      print('Use the B+ tree explorer above to see node structure.');
    } else if (cmd === 'SELECT_ALL' || filter) {
      let matches = active;
      if (filter) {
        const limit = Number(filter[2]);
        if (limit > 150) { err('Age must be 0 to 150.'); matches = []; }
        else {
          const op = filter[1];
          matches = matches.filter(r => ({
            '=': r.age === limit, '!=': r.age !== limit,
            '<': r.age < limit, '<=': r.age <= limit,
            '>': r.age > limit, '>=': r.age >= limit,
          }[op] ?? false));
        }
      }
      if (!filter || Number(filter[2]) <= 150) {
        matches.forEach(r => print(rowFormat(r)));
        if (!matches.length) print('OK: 0 rows');
      }
    } else if (insertOrUpdate) {
      const action = insertOrUpdate[1];
      const id = readId(insertOrUpdate[2]);
      const name = insertOrUpdate[3];
      const age = Number(insertOrUpdate[4]);
      const existing = rows.find(r => r.id === id);
      if (id === null || age > 150 || !validName(name)) err('Check the record: ID up to 4294967295, name 1 to 64 UTF-8 bytes, age 0 to 150.');
      else if (action === 'INSERT') {
        if (existing && !existing.deleted) err('ID ' + id + ' already exists. Use UPDATE to change it.');
        else {
          setRows(prev => existing
            ? prev.map(r => r.id === id ? { id, name, age, deleted: false } : r)
            : [...prev, { id, name, age, deleted: false }]);
          print('OK: 1 row inserted');
        }
      } else {
        if (!existing || existing.deleted) err('No active row with ID ' + id + '.');
        else {
          setRows(prev => prev.map(r => r.id === id ? { ...r, name, age } : r));
          print('OK: 1 row updated');
        }
      }
    } else if (selectOrDelete) {
      const id = readId(selectOrDelete[2]);
      const existing = rows.find(r => r.id === id);
      if (id === null) err('ID must be 0 to 4294967295.');
      else if (!existing || existing.deleted) err(existing?.deleted ? 'Row ' + id + ' was deleted.' : 'No row with ID ' + id + '.');
      else if (selectOrDelete[1] === 'SELECT') print(rowFormat(existing));
      else { setRows(prev => prev.map(r => r.id === id ? { ...r, deleted: true } : r)); print('OK: 1 row deleted'); }
    } else err('Unknown command. Type .help to see the syntax.');
    setLines(prev => [...prev, ...log].slice(-MAX_HISTORY));
    setCommand('');
    requestAnimationFrame(() => { if (scrollRef.current) scrollRef.current.scrollTop = scrollRef.current.scrollHeight; });
  }

  function submit(e: FormEvent<HTMLFormElement>) { e.preventDefault(); run(command); }
  return <Page id="terminal" index={3} title="Try the command language"
    intro="Inserts, lookups, updates, logical deletes and age filters, following the C parser's validation rules. Deleted rows keep their slot and are hidden by a tombstone bit.">
    <div className="term-layout">
      <div className="term">
        <div className="term-log" ref={scrollRef} aria-live="polite" aria-label="Command output">
          {lines.map((line, i) => <div key={i} className={'term-line ' + line.kind}>{line.text}</div>)}
        </div>
        <form className="term-entry" onSubmit={submit}>
          <label htmlFor="terminal-command" className="term-prompt">minidb&gt;</label>
          <input id="terminal-command" value={command} onChange={e => setCommand(e.target.value)} maxLength={200} autoComplete="off" spellCheck={false} placeholder="SELECT_ALL" />
          <button type="submit" className="btn primary">Run</button>
        </form>
      </div>
      <div className="presets">
        <h3>Run an example</h3>
        <ul>{PRESETS.map(c => <li key={c}><button type="button" onClick={() => run(c)}><code>{c}</code></button></li>)}</ul>
        <button type="button" className="btn quiet" onClick={() => { setRows(ORIGINAL); setLines([{ kind: 'hint', text: 'Back to the three sample rows.' }]); setCommand(''); }}>Reset rows</button>
        <p className="note">Nothing leaves your browser. This models the commands; it doesn't run the C engine or write to disk.</p>
      </div>
    </div>
  </Page>;
}

/* ---------- Benchmark ---------- */

const fmtMs = (v: number) => v < 10 ? v.toFixed(3) : v.toFixed(1);

function BenchmarkChart() {
  const [selected, setSelected] = useState(3);
  const row = BENCHMARKS[selected];
  const W = 640, H = 300, L = 56, R = 120, T = 18, B = 40;
  const xs = BENCHMARKS.map((_, i) => L + 20 + i * ((W - L - R - 40) / 3));
  const lo = Math.log10(0.2), hi = Math.log10(400);
  const y = (v: number) => T + (hi - Math.log10(v)) / (hi - lo) * (H - T - B);
  const pts = (k: 'btree' | 'linear') => BENCHMARKS.map((r, i) => xs[i] + ',' + y(r[k])).join(' ');
  const tipX = selected >= 2 ? xs[selected] - 152 : xs[selected] + 14;
  return <Page id="performance" index={4} title="Indexed lookups stay under a millisecond"
    intro="Median time for 2,000 lookups, five trials each, with caches already warm. A sequential scan grows with the row count; the B+ tree barely moves.">
    <div className="bench">
      <figure className="chart">
        <figcaption className="chart-legend">
          <span><i className="swatch s-tree" />B+ tree lookup</span>
          <span><i className="swatch s-scan" />Sequential scan</span>
          <span className="muted">Milliseconds, log scale</span>
        </figcaption>
        <svg viewBox={'0 0 ' + W + ' ' + H} role="img" aria-label="Line chart of lookup time against row count. Values are listed in the data table below.">
          {[1, 10, 100].map(v => <g key={v} className="grid"><line x1={L} x2={W - R + 20} y1={y(v)} y2={y(v)} /><text x={L - 10} y={y(v) + 4} textAnchor="end">{v}</text></g>)}
          <line className="crosshair" x1={xs[selected]} x2={xs[selected]} y1={T} y2={H - B} />
          <polyline className="line s-scan" points={pts('linear')} />
          <polyline className="line s-tree" points={pts('btree')} />
          {BENCHMARKS.map((r, i) => <g key={r.n}>
            <circle className="dot s-scan" cx={xs[i]} cy={y(r.linear)} r={i === selected ? 6 : 4.5} />
            <circle className="dot s-tree" cx={xs[i]} cy={y(r.btree)} r={i === selected ? 6 : 4.5} />
            <text className="axis" x={xs[i]} y={H - B + 22} textAnchor="middle">{r.n / 1000}k rows</text>
            <rect className="hit" x={xs[i] - 50} y={T} width="100" height={H - T - B} onMouseEnter={() => setSelected(i)} />
          </g>)}
          <text className="direct" x={xs[3] + 14} y={y(BENCHMARKS[3].linear) + 4}>scan</text>
          <text className="direct" x={xs[3] + 14} y={y(BENCHMARKS[3].btree) + 4}>B+ tree</text>
          <g className="tip" transform={'translate(' + tipX + ' ' + Math.min(y(row.linear) + 14, H - B - 70) + ')'} aria-hidden="true">
            <rect width="138" height="62" rx="3" />
            <text x="10" y="18" className="tip-head">{row.n.toLocaleString('en-US')} rows</text>
            <text x="10" y="36"><tspan className="tip-k">scan</tspan> {fmtMs(row.linear)} ms</text>
            <text x="10" y="52"><tspan className="tip-k">B+ tree</tspan> {fmtMs(row.btree)} ms</text>
          </g>
        </svg>
        <div className="chart-select" role="group" aria-label="Choose a dataset size">
          {BENCHMARKS.map((r, i) => <button key={r.n} type="button" aria-pressed={selected === i} onClick={() => setSelected(i)}>{r.n / 1000}k</button>)}
        </div>
      </figure>
      <div className="bench-read">
        <p className="bench-sentence">At {row.n.toLocaleString('en-US')} rows the scan took <b>{fmtMs(row.linear)} ms</b> and the B+ tree <b>{fmtMs(row.btree)} ms</b>, about <b>{Math.round(row.linear / row.btree)} times</b> faster.</p>
        <p className="note">Measured with warm caches. These numbers say nothing about disk latency, and they are not a comparison with other databases.</p>
        <details className="table-view">
          <summary>Show the data table</summary>
          <table>
            <thead><tr><th scope="col">Rows</th><th scope="col">B+ tree (ms)</th><th scope="col">Scan (ms)</th></tr></thead>
            <tbody>{BENCHMARKS.map(r => <tr key={r.n}><th scope="row">{r.n.toLocaleString('en-US')}</th><td>{r.btree.toFixed(3)}</td><td>{r.linear.toFixed(3)}</td></tr>)}</tbody>
          </table>
        </details>
        <External className="link" href={BENCH_SOURCE}>Raw benchmark CSV on GitHub</External>
      </div>
    </div>
  </Page>;
}

/* ---------- Limits ---------- */

const LIMITS = [
  ['16 MiB per file', '4,096 pages of 4,096 bytes. Index pages count against the same budget.'],
  ['No crash recovery', 'Pages are synced with fsync, but there is no write-ahead log or redo.'],
  ['Deletes are logical', 'A tombstone bit hides the row. Its payload, slot and index key stay in the file.'],
  ['No checksums', 'Open validates structure, so a well-formed but altered page can go unnoticed.'],
  ['One fixed schema', 'Every row is ID, name and age. No compaction, rebalancing or migrations.'],
];

function Limits() {
  return <Page id="limits" index={5} title="What MiniDB doesn't do" intro="It's a learning project, and the documentation says so plainly. These are the main boundaries.">
    <dl className="limits">{LIMITS.map(([t, d]) => <div key={t}><dt>{t}</dt><dd>{d}</dd></div>)}</dl>
    <div className="actions">
      <External className="btn primary" href={REPO}>Read the source on GitHub</External>
      <External className="btn" href={BLOB + 'docs/limitations.md'}>Full list of limitations</External>
    </div>
  </Page>;
}

/* ---------- App ---------- */

const NAV: [string, string][] = [['Layers', '#layers'], ['B+ tree', '#explorer'], ['Commands', '#terminal'], ['Benchmarks', '#performance']];

export default function App() {
  const [menu, setMenu] = useState(false);
  return <>
    <a className="skip-link" href="#main">Skip to content</a>
    <header className="site-head">
      <nav className="nav" aria-label="Main">
        <a className="wordmark" href="#top">miniDB</a>
        <button type="button" className="menu-btn" aria-expanded={menu} aria-controls="nav-links" onClick={() => setMenu(!menu)}>{menu ? 'Close' : 'Menu'}</button>
        <ul className={'nav-links' + (menu ? ' open' : '')} id="nav-links">
          {NAV.map(([label, href]) => <li key={href}><a href={href} onClick={() => setMenu(false)}>{label}</a></li>)}
          <li><External className="nav-gh" href={REPO}>GitHub</External></li>
        </ul>
      </nav>
    </header>
    <main id="main">
      <section className="page hero" id="top" aria-labelledby="hero-title">
        <div className="page-gutter" aria-hidden="true"><span className="page-offset">0x0000</span><span className="page-id">page 0</span></div>
        <div className="page-body hero-grid">
          <div className="hero-head">
            <p className="magic" aria-label="The first 8 bytes of every MiniDB file spell MINIDB followed by a carriage return and line feed.">
              {MAGIC.map((b, i) => <span key={i} aria-hidden="true" style={{ animationDelay: i * 70 + 'ms' }}><b>{hex(b)}</b><i>{MAGIC_ASCII[i]}</i></span>)}
            </p>
            <h1 id="hero-title">A database engine you can read byte&nbsp;by&nbsp;byte</h1>
          </div>
          <div className="hero-copy">
            <p className="lede">MiniDB is a small storage engine written from scratch in C17: fixed 4&nbsp;KB pages, a persistent B+&nbsp;tree and a command prompt. Every byte it writes has a documented place. Type a record and see exactly how it lands on disk.</p>
            <div className="actions">
              <a className="btn primary" href="#explorer">Explore the B+ tree</a>
              <External className="btn" href={REPO}>Source on GitHub</External>
            </div>
            <dl className="facts">
              <div><dt>Test suites passing in CI</dt><dd>19 of 19</dd></div>
              <div><dt>Records in the stress test</dt><dd>100,000</dd></div>
              <div><dt>Bytes per page</dt><dd>4,096</dd></div>
            </dl>
          </div>
          <RecordEncoder />
        </div>
      </section>
      <Layers />
      <TreePlayground />
      <TerminalPlayground />
      <BenchmarkChart />
      <Limits />
    </main>
    <footer className="site-foot">
      <span className="wordmark small">miniDB</span>
      <span>MIT licensed educational project in C17</span>
      <ul>
        <li><External href={BLOB + 'docs/architecture.md'}>Architecture</External></li>
        <li><External href={BLOB + 'docs/storage-format.md'}>Storage format</External></li>
        <li><External href={BLOB + 'SECURITY.md'}>Security</External></li>
      </ul>
    </footer>
  </>;
}
