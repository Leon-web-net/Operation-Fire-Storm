# Operation FIRE STORM

A C++ relay for CoreTech's FIRE STORM challenge. It accepts a TCP stream on port **5733** and
forwards all normal traffic to port **5744** unchanged and in order. CTFS packets are taken out
of the stream, and their payloads are put back in sequence order and saved as one PDF per file
ID (`1.pdf`, `2.pdf`, ...) in the current directory.

Uses only the C++17 standard library and POSIX APIs.

| Stage | Status |
|---|---|
| Stage 1 | Complete. `test.py` prints `OK`, and `1.pdf` matches the original byte for byte (SHA-256 checked). |
| Stage 2 | Reassembly works: `0.pdf`–`4.pdf` are rebuilt correctly. Routing to 5745/5746 is not implemented, so the Stage 2 test fails. |

## Build (fresh Ubuntu 24.04)

```bash
sudo apt update
sudo apt install -y build-essential cmake git python3
git clone https://github.com/Leon-web-net/Operation-Fire-Storm.git firestorm
cd firestorm

cmake -S . -B build/release
cmake --build build/release
```

## Test

**End-to-end with `e2e_check.sh` (Stage 1 only):**

```bash
./scripts/e2e_check.sh
```

This runs firestorm against CoreTech's `test.py` in a temporary folder. It checks that the test prints `OK`, that firestorm exits cleanly, and that `1.pdf` is identical to the original via SHA-256 fingerprint. Nothing is left in the repo.

## Run by hand

Terminal 1, from the project folder:

```bash
./build/release/firestorm
```

Terminal 2:

```bash
cd tests/coretech/stage_1
python3 test.py
```

Terminal 2 prints `OK`, and `1.pdf` appears in the folder firestorm was started from. Open it with `xdg-open 1.pdf`, or `explorer.exe 1.pdf` on WSL.

For Stage 2, use `tests/coretech/stage_2` in terminal 2. The test fails at the routing check, but `0.pdf`–`4.pdf` are written and open correctly.

## Output files and crash recovery

| File | Meaning |
|---|---|
| `<id>.pdf` | A clean run with no missing sequence numbers |
| `<id>.partial.pdf` | Recovered after a crash, or written from a run with gaps. The packets it holds are correct and in order, but some may be missing, so it may not open. |
| `spool/<id>.spool` | Payloads as they arrived. Deleted once its PDF is safely written; kept after a crash or a failed run. |
| `spool/<id>.spool.old` | A spool from an earlier run, moved aside rather than overwritten |

If firestorm is killed or a run fails, rebuild what was received:

```bash
./build/release/firestorm --recover
```

## Unit tests

```bash
ctest --test-dir build/release --output-on-failure
```

Expected: `100% tests passed, 0 tests failed out of 4`.

| Suite | What it checks |
|---|---|
| `parser` | Framing on its own, with no sockets: a split at every byte offset, one byte per read, partial markers and headers, several packets in one read, packets cut off by the end of the stream |
| `reassembler` | Duplicates, several file IDs, seq order, write errors, atomic replace, crash recovery |
| `golden` | The real Stage 1 capture fed through the parser and reassembler at 11 different read sizes; the PDF must match byte for byte |
| `recover` | Rebuilds a real crash spool (`unit_tests/data/42.spool`) into `42.partial.pdf` |

`test_recover` writes `<id>.partial.pdf` for each fixture into the project folder, and it refuses to run if `spool/` already holds spools.


## How it works

```
recv() ──▶ Parser (framing) ──forward(bytes)────────────────▶ :5744
                 │
                 └──on_packet(header, payload)──▶ Reassembler ──▶ spool/<id>.spool ──▶ <id>.pdf
```

- **`parser.cpp`** finds CTFS packets in the byte stream with two states. In `Searching` it looks for the `CTFS` marker; in `InMessage` it waits for the full header and payload. It has no socket or file code: everything else goes to two callbacks.
- **`reassembler.cpp`** appends each payload to a spool file on disk and keeps an index in memory (`std::map<seq, {offset, len}>`). At the end it walks the index in seq order and copies one payload at a time into the PDF.
- **`main.cpp`** owns the sockets and connects the parts with two lambdas: `forward` (connects to 5744 lazily, then sends) and `on_packet` (passes the packet to the reassembler, then logs it if it was stored; duplicates and write errors get a warning instead).
- **`ctfs.cpp`** decodes the 16-byte header. **`net.cpp`** wraps the socket calls.


See [`docs/how-it-works.html`](docs/how-it-works.html) for a visual walkthrough: step through one run and see which part of the program does the work.

How the header layout was worked out from the capture is in [`docs/protocol_analysis.md`](docs/protocol_analysis.md).

## Design decisions

<!-- Rewrite in my own words before submitting. -->

- **Lazy connect to 5744.** `test.py` only starts listening on 5744 after the sender connects, so connecting at startup would race it.
- **Hold back only a possible marker prefix.** A read ending in `C`, `CT` or `CTF` might be the start of a split marker, so only those bytes wait for the next read. Always holding back 3 bytes would delay all traffic and could stall request/response protocols.
- **The parser only does framing.** Storing, logging and networking live elsewhere, so the parser can be tested with plain strings, and changes to reassembly don't touch the state machine.
- **Fast path without copying.** When nothing is left over from the last read, the parser works straight from the `recv` buffer and copies only the unfinished tail.
- **Spool to disk, index in memory.** Peak memory is the index plus one payload, however large the file is or however out of order the packets arrive. Because `std::map` is ordered, nothing is ever sorted. The cost is that each byte is written to disk twice.
- **Crash safety.** Each record is flushed to the kernel as it arrives, and records store their own seq and length. So a killed run can be rebuilt with `--recover`, and a torn last record is ignored.
- **Never replace a good file with a bad one.** PDFs are written to `.tmp`, then renamed. A failed run writes no PDF and keeps its spools.
- **Duplicates keep the first copy.** Replacing data already received without saying so is the riskier choice.

Considered and not done: a ring buffer (packets wrapping around the end would break the contiguous memory that searching and header parsing need), threads (no measured bottleneck), and a reorder buffer (one missing seq would hold everything after it in memory).

## Error handling

| Situation | Behaviour |
|---|---|
| Duplicate seq | First copy kept, warning logged |
| Stream ends partway through a packet | Those bytes are forwarded as normal traffic, with a warning |
| Missing seqs (a gap before the last one received) | Warning; the file is written as `<id>.partial.pdf` |
| A spool write fails | Traffic keeps flowing to 5744; the run fails, so no PDF is written for any file; spools are kept; exit code 1 |
| `recv`, connect or send fails | Run stops and exits with 1; no PDF is written; spools are kept for `--recover` |

## Known limitations

- **Missing packets at the end of a file can't be detected.** The protocol has no total count or end marker, so a file missing its last packets looks complete and is written as `<id>.pdf`.
- **Crash safety covers a kill, not a power cut.** There's no `fsync`.
- **A `CTFS` that happens to appear inside normal traffic** would be taken as a packet. The padding and checksum are not yet checked.
- **There are no socket timeouts and no SIGINT handler.**
- **Stage 2 routing** (5745/5746, MAC and protocol rules) isn't implemented.

## Project layout

```
src/            firestorm source (main, net, parser, ctfs, reassembler, config)
unit_tests/     parser, reassembler, golden and recover tests; data/ holds the fixtures
scripts/        e2e_check.sh
tests/coretech/ CoreTech's test harness (stage_1, stage_2), unchanged
docs/           protocol analysis, visual guide (how-it-works.html), challenge brief
```