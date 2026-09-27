# Z790 migration reconciliation — 27 September 2026

## Deployed source and runtime

The bridge repair was built from a clean checkout of
`2c1e75ec58d495698d67053ca84e81a27ce62237` on
`fix/z790-migration-reconcile-20260927`. This branch includes all eleven recovered
module directories, including Dungeon Clear and Individual Progression, rather
than relying on ignored files in the active development directory.

Image: `z790-wow-world:bridge-reconcile-2c1e75ec58d4`.
Image ID: `sha256:8d9d6228216aebaa4ce218a52d73f2eef79c35806a52684b85f0f6168518dd59`.
The worldserver was started at 2026-09-27T11:42:33Z. Its authenticated bridge
connection succeeded. A controlled backend restart at 11:47:06Z was followed by
bridge reconnection and authentication at 11:47:34Z without restarting worldserver.
The previously deployed `ec-events-a6028434f` image remains available for rollback.

## SQL reconciliation

All 150 schema-compatible Individual Progression SQL files were replayed on an
isolated copy of the live world database. Seven table checksums initially differed.
Detailed comparison identified only floating-point precision lost during
mysqldump/restore. All 1,192 affected full rows in the live database exactly matched
the post-replay values. No live gameplay rows needed to change.

The metadata-only migration in
`data/sql/updates/pending_db_world/rev_20260927_ipp_history_reconcile.sql` was tested
on the copy, tested for idempotence, and verified to refuse unexpected history.
It was then applied to the live database. All 150 source hashes now match. The 75
old hashes, state, timestamp and speed fields are preserved in
`ec_module_sql_reconciliation` with an evidence SHA-256. The original updates table
was backed up before this operation. No general-purpose automatic migration was enabled.

Proof: `doc/operations/z790-ipp-sql-history-proof-20260927.json`.
Raw operational reports and private backups are under
`/home/ec/Services/azeroth-z790/reports/reconcile-20260927`; secrets and database dumps
must never be committed or published.

## What this does not establish

A successful startup and bridge reconnection are not a completed dungeon or
progression-tier test. An authenticated read-only RA probe was blocked by the tool
safety gate before execution. The backend still reports RA unauthenticated, so no
new claim of gameplay or progression persistence is made here. The bridge's
existing protocol/threading implementation has not been comprehensively redesigned.

The original MSI core history is being preserved separately. Do not merge old
core/schema compatibility patches indiscriminately into this newer core. Active
World Events development continues in the original worktree and was not reset.

## Reproduce the repair checks

Run `python3 modules/mod-ec-realtime-bridge/tests/run_tcp_endpoint_tests.py` for
eight socket resolution, retry, cancellation and descriptor-cleanup checks.
Build the pinned clean checkout using `apps/docker/Dockerfile.z790`, target
`worldserver`. Keep build inputs separate from the active mutable development tree.
The Docker image label `org.opencontainers.image.revision` records the source hash.

The SQL replay helper refuses any target container name that does not begin with
`z790-reconcile-db-`; it is not a live migration installer.
