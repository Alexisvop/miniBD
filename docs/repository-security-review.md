# Repository exposure and portfolio-readiness review

**Snapshot:** 2026-10-23, public repository `Alexisvop/miniBD`, default branch
`main`, initial review at commit `eda66882650e285faca1489dc18f72178ed2a97b`.

## Scope of this review

- Enumerated all tracked paths on the default branch and historical file trees
  reachable from the 19 commits returned by the repository history query.
- Pattern-reviewed the contents of tracked sources, headers, tests, documentation,
  sample inputs, benchmark data, workflows and scripts.
- Pattern-reviewed available commit patches for private-key headers, token-shaped
  strings, apparent credential assignments, email addresses and local home paths.
- Checked the current `.gitignore`, README, CI configuration and documented
  database-engine security limitations.

## Findings

| Priority | Observation | Action |
| --- | --- | --- |
| Informational | No matching credential/token/private-key patterns were detected in the reviewed tracked contents or returned commit patches. | Do **not** equate this with proof that no information was ever exposed. |
| Attention | Some early commit author/committer email metadata is not a GitHub `noreply` address. | Review public commit details and set a private `noreply` email for future commits; do not casually rewrite published history. |
| Preventive | Existing ignore rules did not explicitly cover environment files, private keys, local databases beyond `*.db`, and logs. | Extend `.gitignore` and add a non-disclosing CI hygiene check. |
| Documentation | MiniDB intentionally lacks production security, authentication, WAL, crash recovery and concurrency control. | Add `SECURITY.md` and make the README's scope explicit. |

## Important limitations

This is a **pattern-based repository review**, not a professional penetration
test, full historical secret scan, audit of private GitHub settings, or guarantee
against leakage. Commit patches can be truncated by GitHub, so large historical
versions or non-matching credential formats may require a dedicated local
historical scan. Local ignored/untracked files, GitHub Actions secrets, Cloudflare
account settings and other external services were not inspected.

The new CI checker scans **tracked working-tree files**, not every historical
commit. Commit author metadata remains publicly observable unless history is
rewritten, which this review intentionally does **not** do.

## Follow-up before the showcase

1. Review commit author metadata for privacy in GitHub's web interface; change
   future `git config user.email` to the private address from GitHub Settings.
2. Enable GitHub **Private vulnerability reporting** under the repository's
   security settings if available.
3. Check any future website build artifacts before publishing; do not put
   private .db data, access tokens or deployment keys in the frontend.
4. Keep the website's demos clearly labeled as real browser execution,
   recorded outputs, or simulations, as appropriate.
5. Run CI and review the PR before merging to `main`.

For engine-level guarantees, see [limitations](limitations.md). For the testing
matrix, see [testing](testing.md).
