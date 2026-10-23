# Security policy

## Status and supported scope

MiniDB v0.5 is an **educational, single-process database engine**. It is not a
production service and is not intended to store real personal, financial or
confidential information. The current maintained demonstration version is v0.5
(on the default branch). Older disk formats v1/v2 are intentionally unsupported.

The project **does not provide** authentication, encryption at rest, multiple
writer coordination, transactional crash recovery, WAL, hardened untrusted file
handling or ACID guarantees. A successful command is not a durable transaction.
See [limitations](docs/limitations.md) for failure windows and precise guarantees.

## Reporting security issues

Please **do not post exploit details, credentials, personal records or private
database files in a public GitHub issue**.

If the repository has GitHub **Private vulnerability reporting** enabled, use
the repository's Security / Advisories / **Report a vulnerability** interface.
If it is not enabled, open a public issue asking for a private communication
channel **without disclosing the sensitive details**.

Do not attach sensitive sample databases. Use synthetic, minimal reproduction
steps wherever possible.

## Repository and demo hygiene

- Do not commit .env files, database dumps, private keys, credentials or logs.
- Run `python3 scripts/check_repo_hygiene.py` before pushing changes.
- CI runs this lightweight hygiene check; pattern matching can miss secrets.
- For local demonstrations, use `sh scripts/demo.sh ./build/minidb`: this uses
  a throwaway database, not your personal data.
- Never put Cloudflare account API tokens or other deployment credentials in
  client-side JavaScript or tracked configuration files. Use Git integration
  for the [published static showcase](https://minibd.pages.dev/), or
  platform-managed secrets when necessary. The browser demo uses synthetic
  data and must never collect real credentials or sensitive records.
- If a real credential was pushed, **revoke or rotate it immediately**.
  Deleting the file in a later commit does not remove it from Git history.

## Disclosure boundaries

The source, benchmarks and reproducible tests are public under the MIT license.
Published performance measurements are environment-specific and do not attest
production security, crash safety or compatibility with Oracle Database.
