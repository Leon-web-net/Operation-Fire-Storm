# Operation FIRE STORM

A C++ TCP relay that sits between a sender on port **5733** and a receiver on port **5744**.
Normal traffic is forwarde, CTFS packets are removed from the
stream, and their payloads are reassembled by sequence number into a PDF.

```
 sender ──▶ :5733 [ firestorm ] ──▶ :5744 receiver
                       │
                       ▼
                  output.pdf
```

Uses only the C++17 standard library and POSIX socket APIs. No third-party libraries.

## Requirements

All commands in this README are run in a terminal on **Ubuntu 24.04 LTS**.

Install the build tools:

```bash
sudo apt update
sudo apt install -y build-essential cmake git python3
```

## Build

From any directory (this clones into a new `firestorm` folder):

```bash
git clone https://github.com/Leon-web-net/Operation-Fire-Storm.git firestorm
cd firestorm
cmake -S . -B build
cmake --build build
```

This produces `build/firestorm`.

## Run and verify

The test harness from CoreTech is included in `tests/coretech/`.

Open two terminals, both in the `firestorm` folder.

**Terminal 1** (start the relay first):

```bash
./build/firestorm
```

**Terminal 2** (moves into the test folder):

```bash
cd tests/coretech
python3 test.py
```

Expected results:

- Terminal 2 ends with `OK`.
- Terminal 1 prints one line per CTFS packet (`CTFS seq=0 len=512` … `seq=19 len=0`),
  then `Wrote 20 CTFS chunks to output.pdf`, and exits.
- `output.pdf` (9,694 bytes) is written to the directory firestorm was started from
  (here, the `firestorm` folder).

Open the PDF from Terminal 1:

```bash
xdg-open output.pdf        # Ubuntu desktop
explorer.exe output.pdf    # Ubuntu on WSL
```

Quick check without a viewer: `head -c 8 output.pdf` prints `%PDF-1.3`, and
`tail -c 6 output.pdf` ends with `%%EOF`.

firestorm handles one incoming connection and then exits, so start it again before each test run.

## Project layout

```
src/
  main.cpp      accept → recv loop → feed parser → write PDF
  net.*         socket helpers: make_listener, connect_to, send_all
  parser.*      CTFS stream parser (no socket code; testable on its own)
  pdf.*         writes payloads to file in sequence order
  config.hpp    ports and output filename
docs/
  protocol-analysis.md   how the CTFS header layout was worked out
tests/coretech/          CoreTech's test harness
```

## How it works

TCP is a byte stream: a CTFS header can be split across `recv()` calls, and one `recv()` can
contain several packets. The parser therefore keeps a `pending` buffer that persists between
reads and runs a two-state machine over it:

| State | Behaviour |
|---|---|
| **Searching** | Find `CTFS` anywhere in the buffer. Forward everything before it. If no marker is found, forward everything except a trailing `C`, `CT` or `CTF`, which is held back in case the next read completes the marker. |
| **InMessage** | Wait until the 16-byte header and the full payload are buffered. Store the payload under its sequence number, discard the packet (it is never forwarded), then return to Searching. |

When the sender closes the connection, any held-back bytes are forwarded (they could not have
been a marker after all), and the payloads are written to `output.pdf` in sequence order.

CTFS header (16 bytes, see `docs/protocol-analysis.md`):

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `CTFS` marker |
| 4 | 2 | Sequence number (network order) |
| 6 | 2 | Payload length (network order) |
| 8 | 1 | File ID |
| 9 | 4 | Checksum |
| 13 | 3 | Padding |
| 16 | len | Payload |

## Known limitations

- **False positives:** the bytes `CTFS` occurring inside normal traffic would be treated as a
  packet. Verifying the checksum and zero padding would guard against this.
- **Checksum and File ID are not checked.** All payloads are written to a single `output.pdf`.
- **Single connection:** one incoming connection per run, handled on a single thread.
- **Whole file in memory:** payloads are buffered until the stream ends. A reorder buffer that
  writes in-order chunks immediately would bound memory by how far out of order packets arrive,
  rather than by file size.
- **Missing chunks are not detected:** a gap in sequence numbers produces a truncated file
  without a warning.