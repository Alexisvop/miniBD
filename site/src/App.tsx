import { useMemo, useRef, useState } from 'react';
import type { FormEvent } from 'react';
import { emptyTree, exampleTree, insertTree, searchTree, treeLevels, treeSize } from './demoTree';
import type { Tree, TreeNode } from './demoTree';

const REPO = 'https://github.com/Alexisvop/miniBD';
const BENCH_SOURCE = REPO + '/blob/main/benchmarks/summary-phase5-2026-10-15.csv';
const DOCS = REPO + '/blob/main/docs/visual-guide.md';
const BENCHMARKS = [
  { n: 1000, btree: 0.322252, linear: 1.498043 },
  { n: 10000, btree: 0.454206, linear: 13.297411 },
  { n: 50000, btree: 0.744984, linear: 140.732848 },
  { n: 100000, btree: 0.917055, linear: 287.190818 },
];

function Arrow({ diagonal = false }: { diagonal?: boolean }) {
  return <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.9" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true"><path d={diagonal ? 'M7 17 17 7M8 7h9v9' : 'M4 12h16m-7-7 7 7-7 7'} /></svg>;
}

function DbGlyph() {
  return <svg viewBox="0 0 38 38" fill="none" stroke="currentColor" strokeWidth="1.8" aria-hidden="true">
    <ellipse cx="19" cy="10" rx="12.5" ry="5" /><path d="M6.5 10v17c0 6.5 25 6.5 25 0V10M6.5 19c0 6.5 25 6.5 25 0" /><circle cx="26.5" cy="26.5" r="1.4" fill="currentColor" stroke="none" />
  </svg>;
}

function Label({ children, accent = false }: { children: React.ReactNode; accent?: boolean }) {
  return <span className={'eyebrow' + (accent ? ' accent' : '')}>{children}</span>;
}

const ARCH = [
  { name: 'REPL + Parser', short: 'Command interface', detail: 'The REPL accepts terminal input, while the parser validates command syntax, IDs, UTF-8 names and numeric bounds.', tag: 'INPUT' },
  { name: 'Executor', short: 'Query coordination', detail: 'Dispatches validated commands, applies age filters and returns structured results through callbacks without printing from the storage layer.', tag: 'EXECUTION' },
  { name: 'Storage Engine', short: 'Record visibility', detail: 'Coordinates 70-byte records, indexed locations, updates and tombstone visibility in paged storage.', tag: 'STORAGE' },
  { name: 'B+ Tree', short: 'Persistent indexing', detail: 'Maps an ID to (page ID, slot). Linked leaf pages support sorted scans. Real leaves hold up to 340 entries; internal nodes up to 509 separators.', tag: 'INDEX' },
  { name: 'Pager', short: 'Memory ↔ disk', detail: 'Uses fixed 4096-byte pages, an in-memory cache, dirty tracking, savepoints and fsync on normal synchronization. It does not guarantee crash recovery.', tag: 'I/O' },
];

function Architecture() {
  const [active, setActive] = useState(3);
  return <section className="section container" id="architecture">
    <div className="section-top">
      <div><Label>01 / UNDER THE HOOD</Label><h2>Five layers.<br/><span>One database engine.</span></h2></div>
      <p>From a command in the terminal to bytes on disk. Each component has a clear responsibility and a testable interface.</p>
    </div>
    <div className="architecture-layout">
      <div className="arch-stack" role="group" aria-label="Explore MiniDB architecture layers">
        {ARCH.map((a, i) => <button key={a.name} className={'arch-row' + (i === active ? ' chosen' : '')} type="button" aria-pressed={i === active} onClick={() => setActive(i)}>
          <span className="arch-index">{String(i + 1).padStart(2, '0')}</span>
          <span className="arch-row-main"><strong>{a.name}</strong><small>{a.short}</small></span>
          <span className="arch-dot" aria-hidden="true">↗</span>
        </button>)}
      </div>
      <div className="arch-detail" aria-live="polite">
        <div className="arch-detail-grid" aria-hidden="true" />
        <span className="arch-pill">{ARCH[active].tag} LAYER</span>
        <div className="arch-feature"><span className="arch-feature-num">0{active + 1}</span><span className="arch-feature-glyph"><DbGlyph /></span></div>
        <h3>{ARCH[active].name}</h3>
        <p>{ARCH[active].detail}</p>
        <a href={DOCS} target="_blank" rel="noopener noreferrer" className="text-link">Explore architecture docs <Arrow diagonal /></a>
      </div>
    </div>
  </section>;
}

type Positioned = { id: string; node: TreeNode; x: number; y: number; depth: number };

function TreeGraphic({ tree, path }: { tree: Tree; path: string[] }) {
  const layout = useMemo(() => {
    const levels = treeLevels(tree);
    const centers: Record<string, number> = {};
    const leaves = levels[levels.length - 1];
    leaves.forEach((id, i) => { centers[id] = 70 + i * 114; });
    for (let level = levels.length - 2; level >= 0; level--) {
      for (const id of levels[level]) {
        const node = tree.nodes[id];
        if (node.kind === 'internal') {
          centers[id] = (centers[node.children[0]] + centers[node.children[node.children.length - 1]]) / 2;
        }
      }
    }
    const positions: Positioned[] = levels.flatMap((ids, depth) => ids.map(id => ({
      id, node: tree.nodes[id], x: centers[id], y: 20 + depth * 106, depth,
    })));
    return { positions, width: Math.max(580, leaves.length * 114 + 30), height: levels.length * 106 + 25 };
  }, [tree]);
  const byId = Object.fromEntries(layout.positions.map(node => [node.id, node]));
  return <div className="tree-scroll">
    <svg width={layout.width} height={layout.height} viewBox={'0 0 ' + layout.width + ' ' + layout.height} aria-label="Educational B plus tree diagram. Blue boxes represent internal nodes, green boxes represent leaf nodes." role="img">
      <g stroke="#345068" strokeWidth="1.5" fill="none">
        {layout.positions.map(item => item.node.kind === 'internal'
          ? item.node.children.map(child => <path key={item.id + child} d={'M' + item.x + ' ' + (item.y + 49) + ' L' + byId[child].x + ' ' + byId[child].y} />)
          : null)}
      </g>
      {layout.positions.map(item => {
        const leaf = item.node.kind === 'leaf';
        const highlighted = path.includes(item.id);
        return <g key={item.id}>
          <rect x={item.x - 46} y={item.y} width="92" height="49" rx="8" fill={leaf ? '#102d32' : '#152a46'} stroke={highlighted ? '#f6c26d' : leaf ? '#4db29d' : '#6688bc'} strokeWidth={highlighted ? 2.6 : 1.25} />
          <text x={item.x} y={item.y + 15} fontSize="8.5" letterSpacing="1.5" fill={leaf ? '#81dcca' : '#9eb9ff'} textAnchor="middle">{leaf ? 'LEAF' : 'INTERNAL'}</text>
          <text x={item.x} y={item.y + 35} fontSize={item.node.keys.length > 2 ? '12' : '14'} fontWeight="650" fill="#f4f7ff" textAnchor="middle">{item.node.keys.join('  |  ') || '∅'}</text>
        </g>;
      })}
      {layout.positions.filter(item => item.node.kind === 'leaf' && item.node.next).map(item => {
        const other = byId[(item.node as Extract<TreeNode, { kind: 'leaf' }>).next!];
        if (!other) return null;
        return <path key={item.id + '-next'} d={'M' + (item.x + 47) + ' ' + (item.y + 26) + ' L' + (other.x - 47) + ' ' + (other.y + 26)} stroke="#59b9a6" strokeWidth="1.6" strokeDasharray="3 3" />;
      })}
    </svg>
  </div>;
}

function TreePlayground() {
  const [tree, setTree] = useState<Tree>(exampleTree);
  const [value, setValue] = useState('60');
  const [path, setPath] = useState<string[]>([]);
  const [event, setEvent] = useState('Example tree loaded. Insert a key or trace a lookup.');
  const [totalSplits, setTotalSplits] = useState(0);
  const parsed = Number(value);
  const valid = /^\d{1,3}$/.test(value) && parsed <= 999;

  function addKey() {
    if (!valid) { setEvent('Enter an integer from 0 to 999.'); return; }
    if (treeSize(tree) >= 25) { setEvent('Visualization limit reached (25 keys). Reset to continue.'); return; }
    const result = insertTree(tree, parsed);
    setTree(result.tree);
    setTotalSplits(n => n + result.splits);
    setPath(searchTree(result.tree, parsed).path);
    setEvent(result.inserted ? 'Inserted key ' + parsed + (result.splits ? ' · ' + result.splits + ' node split(s).' : ' · no split needed.') : 'Key ' + parsed + ' already exists.');
  }

  function lookup() {
    if (!valid) { setEvent('Enter an integer from 0 to 999.'); return; }
    const result = searchTree(tree, parsed);
    setPath(result.path);
    setEvent(result.found ? 'Found key ' + parsed + ' in ' + result.path.length + ' level(s).' : 'Key ' + parsed + ' not found · visited ' + result.path.length + ' level(s).');
  }

  return <section className="section container" id="explorer">
    <div className="section-top">
      <div><Label>02 / INTERACTIVE EXPLORER</Label><h2>Watch an index<br/><span>organize itself.</span></h2></div>
      <p>Insert keys, trigger node splits and trace lookups. A hands-on visualization of the B+ Tree concepts behind MiniDB.</p>
    </div>
    <div className="playground">
      <div className="panel-bar"><div className="panel-title"><span className="panel-icon">◇</span> B+ TREE EXPLORER</div><span className="pill-outline"><i /> EDUCATIONAL SIMULATION</span></div>
      <div className="tree-tools">
        <label className="sr-only" htmlFor="key-input">Key from 0 to 999</label>
        <input id="key-input" inputMode="numeric" pattern="[0-9]*" maxLength={3} value={value} onChange={e => setValue(e.target.value)} onKeyDown={e => { if (e.key === 'Enter') addKey(); }} placeholder="Key (0–999)" />
        <button type="button" className="btn btn-primary small" onClick={addKey}>+ Insert key</button>
        <button type="button" className="btn btn-outline small" onClick={lookup}>Trace search</button>
        <button type="button" className="btn btn-quiet small" onClick={() => { setTree(exampleTree()); setTotalSplits(0); setPath([]); setEvent('Example tree restored.'); }}>Example</button>
        <button type="button" className="btn btn-quiet small" onClick={() => { setTree(emptyTree()); setTotalSplits(0); setPath([]); setEvent('Tree cleared. Insert a key to begin.'); }}>Reset</button>
      </div>
      <div className="tree-stage"><TreeGraphic tree={tree} path={path} /></div>
      <div className="tree-footer">
        <div className="tree-status" role="status" aria-live="polite"><span className="pulse-square" />{event}</div>
        <div className="tree-stats"><span><b>{treeSize(tree)}</b> keys</span><span><b>{treeLevels(tree).length}</b> levels</span><span><b>{totalSplits}</b> splits</span></div>
      </div>
    </div>
    <p className="disclaimer"><strong>How this demo differs:</strong> this browser-only visualization uses an order-4 tree (3 keys per node) for readability. The real C17 MiniDB B+ Tree stores up to 340 entries per leaf page and 509 separators per internal page. These actions do not modify a real database.</p>
  </section>;
}

type DemoRow = { id: number; name: string; age: number; deleted: boolean };
type TerminalLine = { kind: 'input' | 'result' | 'error' | 'hint'; text: string };
const ORIGINAL: DemoRow[] = [
  { id: 3, name: 'Mariana', age: 25, deleted: false },
  { id: 1, name: 'Alex', age: 21, deleted: false },
  { id: 2, name: 'Carlos', age: 20, deleted: false },
];

function TerminalPlayground() {
  const [rows, setRows] = useState<DemoRow[]>(ORIGINAL);
  const [command, setCommand] = useState('');
  const [lines, setLines] = useState<TerminalLine[]>([
    { kind: 'hint', text: 'MiniDB command demo · browser simulation (not the C engine)' },
    { kind: 'hint', text: 'Try SELECT_ALL, UPDATE 1 "Alex" 22, or .help' },
  ]);
  const scrollRef = useRef<HTMLDivElement>(null);
  const MAX_HISTORY = 28;

  function run(input: string) {
    const cmd = input.trim();
    if (!cmd) return;
    const log: TerminalLine[] = [{ kind: 'input', text: 'minidb> ' + cmd }];
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
      print('INSERT id "name" age · SELECT id · SELECT_ALL [WHERE age op value]');
      print('UPDATE id "name" age · DELETE id · .stats · .help · .exit');
    } else if (cmd === '.exit') {
      print('Demo session complete. This page remains open; use Reset to start over.');
    } else if (cmd === '.stats') {
      print('Demo records: ' + rows.length + ' total | ' + active.length + ' active | ' + (rows.length - active.length) + ' tombstones');
      print('These are in-browser demo counts, not real pager or index statistics.');
    } else if (cmd === '.btree') {
      print('Open the B+ Tree Explorer above to visualize node structure (educational simulation).');
    } else if (cmd === 'SELECT_ALL' || filter) {
      let matches = active;
      if (filter) {
        const limit = Number(filter[2]);
        if (limit > 150) { err('Invalid age: expected 0..150.'); matches = []; }
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
      if (id === null || age > 150 || !validName(name)) err('Invalid record: check ID, UTF-8 name (1–64 bytes), and age (0–150).');
      else if (action === 'INSERT') {
        if (existing && !existing.deleted) err('Duplicate ID.');
        else {
          setRows(prev => existing
            ? prev.map(r => r.id === id ? { id, name, age, deleted: false } : r)
            : [...prev, { id, name, age, deleted: false }]);
          print('OK: 1 row inserted');
        }
      } else {
        if (!existing || existing.deleted) err('Record not found or deleted.');
        else {
          setRows(prev => prev.map(r => r.id === id ? { ...r, name, age } : r));
          print('OK: 1 row updated');
        }
      }
    } else if (selectOrDelete) {
      const id = readId(selectOrDelete[2]);
      const existing = rows.find(r => r.id === id);
      if (id === null) err('Invalid ID.');
      else if (!existing || existing.deleted) err(existing?.deleted ? 'Record was deleted.' : 'Record not found.');
      else if (selectOrDelete[1] === 'SELECT') print(rowFormat(existing));
      else { setRows(prev => prev.map(r => r.id === id ? { ...r, deleted: true } : r)); print('OK: 1 row deleted'); }
    } else err('Unknown or invalid command. Type .help for supported syntax.');
    setLines(prev => [...prev, ...log].slice(-MAX_HISTORY));
    setCommand('');
    requestAnimationFrame(() => { if (scrollRef.current) scrollRef.current.scrollTop = scrollRef.current.scrollHeight; });
  }

  function submit(e: FormEvent<HTMLFormElement>) { e.preventDefault(); run(command); }
  return <section className="section container" id="terminal">
    <div className="section-top">
      <div><Label>03 / COMMAND PLAYGROUND</Label><h2>Think in queries.<br/><span>See the results.</span></h2></div>
      <p>Explore MiniDB's simplified command language: inserts, lookups, updates, logical deletes and age filters.</p>
    </div>
    <div className="terminal-layout">
      <div className="terminal-shell">
        <div className="terminal-top"><div className="traffic"><i/><i/><i/></div><span>miniDB · simulated session</span><button type="button" onClick={() => { setRows(ORIGINAL); setLines([{ kind: 'hint', text: 'New demo session. State resets on refresh.' }]); setCommand(''); }}>Reset session ↻</button></div>
        <div className="terminal-log" ref={scrollRef} aria-live="polite" aria-label="Simulated database output">
          {lines.map((line, i) => <div key={i} className={'terminal-line ' + line.kind}>{line.text}</div>)}
        </div>
        <form className="terminal-entry" onSubmit={submit}>
          <span aria-hidden="true">↳</span><label className="sr-only" htmlFor="terminal-command">Demo command</label>
          <input id="terminal-command" value={command} onChange={e => setCommand(e.target.value)} maxLength={200} autoComplete="off" spellCheck={false} placeholder='Enter a MiniDB command…' />
          <button type="submit" aria-label="Execute demo command">RUN ↗</button>
        </form>
      </div>
      <div className="terminal-sidebar">
        <div className="mini-label">TRY A COMMAND</div>
        {['SELECT_ALL','SELECT 1','UPDATE 1 "Alexandra" 22','DELETE 2','SELECT_ALL WHERE age >= 21','.stats'].map(c => <button key={c} type="button" className="preset-command" onClick={() => run(c)}><code>{c}</code><Arrow diagonal /></button>)}
        <div className="demo-callout"><span className="sim-icon">i</span><div><strong>Not a live database connection</strong><p>This is a client-side model of MiniDB commands. It does not execute the C engine, save data to disk or send inputs to a server.</p></div></div>
      </div>
    </div>
  </section>;
}

function BenchmarkChart() {
  const [selected, setSelected] = useState(3);
  const row = BENCHMARKS[selected];
  const ratio = (row.linear / row.btree).toFixed(1);
  const xs = [94, 240, 386, 532];
  const y = (num: number) => 235 - (Math.log10(num) - Math.log10(0.2)) / (Math.log10(400) - Math.log10(0.2)) * 185;
  const points = (key: 'btree' | 'linear') => BENCHMARKS.map((r, i) => xs[i] + ',' + y(r[key])).join(' ');
  return <section className="section container" id="performance">
    <div className="section-top"><div><Label>04 / MEASURED PERFORMANCE</Label><h2>Don't just say faster.<br/><span>Show the evidence.</span></h2></div><p>Actual median timings from MiniDB's published Phase 5 benchmark. Indexed lookup versus a sequential lookup baseline.</p></div>
    <div className="performance-layout">
      <div className="chart-card">
        <div className="chart-head"><div><span className="mini-label">LOOKUP LATENCY</span><h3>2,000 lookups · 5 trials</h3></div><span className="pill-outline">WARM CACHE</span></div>
        <div className="chart-legend"><span><i className="mint" /> B+ Tree indexed</span><span><i className="blue" /> Sequential scan</span><small>Logarithmic Y-axis · milliseconds</small></div>
        <svg className="perf-svg" viewBox="0 0 620 275" role="img" aria-label="Log-scale plot comparing B+ Tree indexed lookup with sequential lookup as row counts increase">
          {[0.2,1,10,100,400].map(v => <g key={v}><line x1="72" x2="557" y1={y(v)} y2={y(v)} stroke="#273c4e" strokeDasharray="3 5" /><text x="60" y={y(v)+4} textAnchor="end" fill="#879cae" fontSize="12">{v}</text></g>)}
          <polyline points={points('linear')} fill="none" stroke="#8ea8ff" strokeWidth="2.8" strokeLinejoin="round" />
          <polyline points={points('btree')} fill="none" stroke="#7ce8cb" strokeWidth="3.1" strokeLinejoin="round" />
          {BENCHMARKS.map((r, i) => <g key={r.n}>
            <circle cx={xs[i]} cy={y(r.linear)} r={i===selected?7:5} fill="#8ea8ff" stroke="#0b1723" strokeWidth="2" />
            <circle cx={xs[i]} cy={y(r.btree)} r={i===selected?7:5} fill="#7ce8cb" stroke="#0b1723" strokeWidth="2" />
            <text x={xs[i]} y="260" fontSize="12" textAnchor="middle" fill="#9badbd">{r.n >=1000?r.n/1000+'k':r.n}</text>
          </g>)}
          <text x="578" y="260" fontSize="10" fill="#66798b">ROWS</text>
        </svg>
        <div className="chart-select" aria-label="Select dataset size">
          {BENCHMARKS.map((r, i) => <button key={r.n} type="button" aria-pressed={selected===i} className={selected===i?'active':''} onClick={() => setSelected(i)}>{(r.n / 1000).toString()}k rows</button>)}
        </div>
      </div>
      <div className="perf-summary">
        <span className="mini-label">SELECTED DATASET · {row.n.toLocaleString('en-US')} ROWS</span>
        <div className="speedup"><strong>{ratio}×</strong><span>faster in this measured lookup workload</span></div>
        <div className="perf-line"><span><i className="mint"/>Indexed B+ Tree</span><strong>{row.btree.toFixed(3)} ms</strong></div>
        <div className="perf-line"><span><i className="blue"/>Sequential scan</span><strong>{row.linear.toFixed(3)} ms</strong></div>
        <div className="perf-note">Measurements use preloaded caches and report the median of 5 repetitions for 2,000 lookups. They are <strong>not</strong> disk-latency or Oracle-versus-MiniDB benchmarks.</div>
        <a href={BENCH_SOURCE} target="_blank" rel="noopener noreferrer" className="text-link">View raw benchmark data <Arrow diagonal /></a>
      </div>
    </div>
  </section>;
}

export default function App() {
  const [menu, setMenu] = useState(false);
  return <>
    <a className="skip-link" href="#main">Skip to content</a>
    <div className="site-shell">
      <header className="nav-wrap">
        <nav className="nav container" aria-label="Main navigation">
          <a className="logo" href="#top" aria-label="MiniDB home"><span className="logo-icon"><DbGlyph /></span><span>mini<span>DB</span><small>DATABASE ENGINE LAB</small></span></a>
          <button type="button" className="menu-trigger" aria-expanded={menu} aria-controls="site-nav-links" onClick={() => setMenu(!menu)}>{menu ? 'CLOSE ×' : 'MENU ☰'}</button>
          <div className={'nav-links' + (menu?' nav-open':'')} id="site-nav-links">
            {[['Architecture','#architecture'],['B+ Tree','#explorer'],['Playground','#terminal'],['Performance','#performance']].map(([label,href])=><a key={href} href={href} onClick={()=>setMenu(false)}>{label}</a>)}
          </div>
          <a className="nav-gh" href={REPO} target="_blank" rel="noopener noreferrer">GitHub <Arrow diagonal /></a>
        </nav>
      </header>
      <main id="main">
        <section className="hero container" id="top">
          <div className="hero-glow" aria-hidden="true"/>
          <div className="hero-copy">
            <div className="hero-kicker"><span className="status-orb"/> SYSTEMS ENGINEERING · C17 PROJECT</div>
            <h1>Databases,<br/>from the <em>inside out.</em></h1>
            <p className="hero-description">MiniDB is a database engine built from scratch in C. Explore the inner workings of paged storage, persistent B+ Tree indexing and query execution.</p>
            <div className="hero-actions"><a href="#explorer" className="btn btn-primary">Explore the engine <Arrow /></a><a className="btn btn-outline" href={REPO} target="_blank" rel="noopener noreferrer">View source code <Arrow diagonal /></a></div>
            <div className="hero-stats"><div><strong>19<span>/19</span></strong><small>CI test suites</small></div><div><strong>100<span>k</span></strong><small>Stress-tested records</small></div><div><strong>4<span>KB</span></strong><small>Fixed page size</small></div></div>
          </div>
          <div className="hero-visual">
            <div className="hero-visual-label"><span className="orbit-dot"/> STORAGE ENGINE / INTERNALS <span>FIG. 01</span></div>
            <svg viewBox="0 0 500 360" role="img" aria-label="Decorative technical illustration of B plus tree levels and storage pages">
              <defs><radialGradient id="bg-g"><stop stopColor="#1f6e78" stopOpacity=".25"/><stop offset="1" stopColor="#09202b" stopOpacity="0"/></radialGradient></defs>
              <circle cx="251" cy="182" r="169" fill="url(#bg-g)" stroke="#234354" strokeDasharray="2 8"/>
              <circle cx="251" cy="182" r="128" fill="none" stroke="#27495b" strokeDasharray="3 10"/>
              <g stroke="#456b83" strokeWidth="1.5" opacity=".8" fill="none"><path d="M250 106L132 190M250 106L249 190M250 106L367 190M132 227L132 273M249 227L249 273M367 227L367 273"/></g>
              <g><rect x="201" y="72" width="98" height="56" rx="8" fill="#132e40" stroke="#6ea6d5"/><text x="250" y="93" fill="#94c1e2" textAnchor="middle" fontSize="11" letterSpacing="2">ROOT</text><text x="250" y="114" fill="#e8f4ff" textAnchor="middle" fontSize="17" fontWeight="700">30 | 60</text></g>
              {[{x:89,t:'12 | 20'},{x:206,t:'35 | 46'},{x:324,t:'72 | 90'}].map(o=><g key={o.x}><rect x={o.x} y="177" width="86" height="52" rx="7" fill="#133535" stroke="#5accb8"/><text x={o.x+43} y="196" textAnchor="middle" fill="#8be8d2" fontSize="10" letterSpacing="1">LEAF</text><text x={o.x+43} y="216" textAnchor="middle" fill="#e0fff6" fontSize="15">{o.t}</text></g>)}
              {[{x:90,t:'PAGE 024'},{x:207,t:'PAGE 058'},{x:324,t:'PAGE 097'}].map(o=><g key={o.x}><rect x={o.x} y="268" width="84" height="35" rx="5" fill="#172634" stroke="#426278"/><text x={o.x+42} y="290" textAnchor="middle" fill="#9ab3c7" fontSize="11">{o.t}</text></g>)}
              <g fill="#78e0cb"><circle cx="250" cy="106" r="3"/><circle cx="132" cy="192" r="3"/><circle cx="249" cy="192" r="3"/><circle cx="367" cy="192" r="3"/></g>
            </svg>
            <div className="hero-visual-footer"><span><span className="visual-swatch mint"/> INDEX NODES</span><span><span className="visual-swatch blue"/> DATA PAGES</span><span>CONCEPTUAL VIEW</span></div>
          </div>
        </section>
        <div className="strip"><div className="container strip-content"><span>BUILT TO UNDERSTAND WHAT HAPPENS BELOW SQL</span><span className="strip-line"/><span>01 / STORAGE</span><span>02 / INDEXING</span><span>03 / EXECUTION</span><span>04 / VALIDATION</span></div></div>
        <Architecture />
        <TreePlayground />
        <TerminalPlayground />
        <BenchmarkChart />
        <section className="closing container" id="about">
          <div className="closing-mark"><DbGlyph /></div>
          <div><Label accent>ENGINEERING, NOT JUST FEATURES</Label><h2>Small engine.<br/><span>Deep systems thinking.</span></h2><p>MiniDB explores the trade-offs behind databases—from binary serialization to index splits and error handling. It is intentionally educational, thoroughly tested within its scope and honest about what it does not implement.</p><div className="closing-links"><a className="btn btn-primary" href={REPO} target="_blank" rel="noopener noreferrer">Explore on GitHub <Arrow diagonal /></a><a className="btn btn-outline" href={REPO+'/blob/main/docs/limitations.md'} target="_blank" rel="noopener noreferrer">Read limitations <Arrow diagonal /></a></div></div>
        </section>
      </main>
      <footer className="footer"><div className="container footer-content"><div className="footer-brand"><span className="logo-icon"><DbGlyph /></span><div><strong>miniDB</strong><small>ENGINEERED TO LEARN.</small></div></div><span>Open-source MIT · C17 · Educational project</span><div className="footer-links"><a href={REPO+'/blob/main/SECURITY.md'} target="_blank" rel="noopener noreferrer">Security</a><a href={DOCS} target="_blank" rel="noopener noreferrer">Documentation</a><a href={REPO} target="_blank" rel="noopener noreferrer">Source ↗</a></div></div></footer>
    </div>
  </>;
}
