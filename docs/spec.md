# FIRE STORM: Post-submission Spec

Working spec for finishing the project. The brief is `first_mission.pdf` (Stage 2 + optional extras) and `docs/challenge.md` (Stage 1).

**Out of scope:** the brief's "Program Obfuscation" extra. Don't implement it, research it or plan for it.

## Ground rules

- `main` always passes `ctest` and `scripts/e2e_check.sh` (Stage 1).
- `v1.0-submission` = commit `c3b0dbc` (the submitted version). Tag it before any change.
- One branch per work item, merged by PR. Each commit leaves the tests green.
- C++17, standard library only, builds on Ubuntu 24.04.
- Docs and comments: short and plain.

## Workflow (Opus → Sonnet)

1. Opus writes a task card: files, interface, tests, done-when, out of scope.
2. Sonnet implements it in a worktree on the feature branch.
3. Opus reviews the diff and runs `ctest` + e2e. Fixes go back to Sonnet.
4. Leon reads the diff and explains it back.
5. Then commit or merge.

The parser framing change (C3) and anything ambiguous stay with Opus.

## Facts from the Stage 2 test data

Checked against `tests/coretech/stage_2` (decoded with `o.ps`):

| Fact | Value |
|---|---|
| Stream | 2813 units on one TCP connection to 5733: 2556 CTFS packets, 257 Ethernet frames |
| CTFS unit | Starts with `CTFS`, length = `16 + len`. Not wrapped in Ethernet. All padding is `00 00 00` |
| Frame unit | Ethernet II (`0x0800`), IPv4, IHL 5, length = `14 + IP total length` (bytes 16–17). No frame contains `CTFS` |
| IP protocol | Byte 23 of the frame. TCP 6, UDP 17, ICMP 1 (dropped) |
| Files | IDs 0–4, no seq gaps. Each file ends with a **zero-length** packet |
| Forwarding | `test.py` checks `assertIn(frame, received[port])`: forward the **whole frame**. Extra traffic on a port is never penalised |

## Routing rules (Stage 2)

`route(frame)` returns a bitmask of `TCP_OUT` (5744), `MONITOR` (5745) and `UDP_OUT` (5746):

1. If the **source** OUI is VMware (`00:0C:29`, `00:50:56`, `00:1C:14`): no TCP/UDP output.
2. Otherwise, IP protocol 6 → `TCP_OUT`, 17 → `UDP_OUT`, anything else → none.
3. If the **destination** OUI is `00:1A:A0`, `00:1E:C9` or `00:15:5D`: add `MONITOR`, **even if rule 1 or 2 dropped it**.

OUI lists live in `config.hpp`.

**Brief vs `test.py`:** the test's expected lists leave 5745 out for VMware → monitored (14 frames) and for any frame to `00:1E:C9` (3 frames). We follow the brief. `test.py` still passes, because it only checks inclusion. Our own golden test asserts the brief's behaviour. Note this in the README.

## Checksum (optional extra)

How it's computed (confirmed):
- Set bytes 9–12 to `CTFS`.
- Sum big-endian `u32` words mod 2³², rotating left 1 bit after each addition.
- The checksum field is read big-endian.

Test vector: `4354465302e2000001f761b64c000000` → `0xf761b64c`.

**Open issue:** all 2552 packets whose length is a multiple of 4 pass. The 4 that aren't (file 0 seq 558, file 2 seq 689, file 3 seq 225, file 4 seq 337) fail under every tail-padding variant tried. They're the real PDF tails (`%%EOF`), so dropping them would corrupt 4 of the 5 PDFs.
- **Until this is resolved:** on a mismatch, log a warning and keep the packet.
- **Never forward a CTFS packet with a bad checksum**: that would send document data to the wider network.

## Work items

Order: **A → B1 → C → B2 → D**.

### A. `fix/spool-rename`
Code: `reassembler.cpp`, around line 35.
- A1. Check the result of `rename(<id>.spool → .spool.old)`. On failure, report it and fail that file.
- A2. If `.spool.old` already exists, don't overwrite it. Fail with a message instead.
- A3. After a clean `write_files`, remove the `spool/` folder if it's empty.

**Done when:** each case has a test in `test_reassembler`.

### B. `feat/header-validation`
- **B1. Padding:** a header whose padding isn't zero is not a CTFS packet. Treat the bytes as normal traffic (Stage 1 search mode). `CtfsHeader::padding_ok` already exists.
- **B2. Checksum:** implement `ctfs_checksum(p, n)` with the vector above as a test, and apply the open-issue policy.
- Remove the "byte order unconfirmed" comment in `ctfs.hpp`.

### C. `feat/stage2-routing`
Stage 1 stays the default. `--stage2` turns on the new mode. One commit per step:

| Step | Owner | Content | Done when |
|---|---|---|---|
| C1 | Sonnet | `bytes.hpp`: shared `read_u16`/`read_u32` used by `ctfs.cpp`, `frame`, spool | Existing tests pass, no behaviour change |
| C2 | Sonnet | `frame.hpp/.cpp`: `frame_len(p,n)` (0 = need more, `SIZE_MAX` = invalid), `parse_frame`, pure `route()` | `test_router`: one case per rule + combined cases + every row of the table above |
| C3 | Opus | Parser length-framing mode: at each unit boundary, `CTFS` → `on_packet`, else `frame_len` → `on_frame`. Invalid length fails the run | `test_parser` cases: split at every byte, back-to-back units, invalid length |
| C4 | Sonnet | `main`: `--stage2` flag, three lazily connected outputs, `on_frame` sends to every port in `route()` | Builds; `--stage2` usage line |
| C5 | Sonnet | Golden test from `test.py --pcap` (5 PDFs, per-port frame lists); `e2e_check.sh <stage>` | Both e2e modes pass |

C1 and C2 can run in parallel. Merge to `main` only when both e2e modes pass, then tag `v2.0-stage2`.

**If C stalls:** merge C1 + C2 alone. They're pure code and fully tested.

### D. Optional
SIGINT handler (finish cleanly, keep spools), socket timeouts.

## Decisions

| # | Decision | Choice |
|---|---|---|
| 1 | Stage 2 delivery | One binary, `--stage2` flag |
| 2 | Framing | Length framing in Stage 2 mode, search framing in Stage 1 |
| 3 | What to forward | The whole Ethernet frame |
| 4 | Brief vs test | Follow the brief (see Routing) |
| 5 | One output port fails | Fail the run (consistent with Stage 1). Revisit if needed |
| 6 | Bad padding / checksum | Padding → not CTFS, normal traffic. Checksum → see open issue |
| 7 | `route()` | Pure, no I/O |
