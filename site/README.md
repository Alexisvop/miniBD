# MiniDB Recruiter Showcase

An interactive, static portfolio website for the [MiniDB](../README.md) C17 educational database engine.

## What the website shows

- **Architecture explorer:** read about REPL, Parser, Executor, Storage, B+ Tree and Pager.
- **Educational B+ Tree:** a browser-side **order-4** visualization with insertion, splits and search paths. It is **not the actual C implementation** (which uses 340-entry leaves and up to 509 internal separators).
- **Command playground:** a client-side simulation of selected MiniDB commands. It does **not execute C code, connect to a live database or persist user input**.
- **Benchmarks:** a visual comparison of measured B+ Tree and sequential lookups. Values are transcribed from [Phase 5 measurements](../benchmarks/summary-phase5-2026-10-15.csv), using median timings for 2,000 operations, 5 trials, warm caches. Not an Oracle benchmark.

The site is strictly static; it does not require secrets, Cloudflare credentials, a database connection or an API server. No analytics or third-party images/fonts are loaded.

## Local development

Requires Node.js 22+ and npm (or a Node.js version compatible with the pinned Vite major release).

```sh
cd site
npm install
npm run test
npm run build
npm run dev
```

Open the local URL printed by Vite. Run `npm run preview` to inspect the production build.

The dependency lockfile should be committed when preparing a final deployment. Until then, `npm install` resolves declared semver ranges and is less reproducible than `npm ci`; use GitHub CI and review dependency updates.

## Cloudflare Pages (GitHub integration)

Connect repository `Alexisvop/miniBD` under Cloudflare **Workers & Pages → Create application → Pages → Import an existing Git repository**.

| Cloudflare Pages field | Value |
| --- | --- |
| Production branch | `main` |
| Root directory | `site` |
| Framework preset | React (Vite) or None |
| Build command | `npm run build` |
| Build output directory | `dist` |
| Node.js | 22 (set `NODE_VERSION=22` if necessary) |

The output is `site/dist/`. A static SPA does not require Pages Functions. The `site/public/_headers` file sets security-related response headers (review production responses after deployment).

**Do not** publish API tokens in tracked source files or browser variables. Use the Cloudflare GitHub integration so Pages deploys on merges to `main`; it can create preview URLs for pull requests.

For updates only under `site/**`, Cloudflare Pages can optionally configure build watch paths to avoid rebuilding when the C engine changes.

## Architecture and limitations

- React + TypeScript + Vite.
- CSS/SVG visualizations; no third-party image, icon or font service.
- No production SQL parser, no real disk persistence in the browser.
- Mobile responsive, keyboard-accessible controls, no `eval` or `dangerouslySetInnerHTML`.
- Static CSP and security-related headers under `public/_headers`.
- No real database records; default names and keys are synthetic.

For the actual C implementation and limits, see [MiniDB architecture](../docs/architecture.md), [B+ Tree design](../docs/btree-design.md), and [security policy](../SECURITY.md).

## Review checklist

1. `npm run test` passes for the demo tree.
2. `npm run build` produces `dist/index.html` and hashed assets.
3. Test keyboard navigation, resizing and mobile menu.
4. Verify both educational simulation disclaimers are visible.
5. Verify no credentials or sensitive data are present in `dist`.
6. Confirm links to the GitHub repository and benchmark CSV.
7. Verify the Cloudflare preview before merging to `main`.
