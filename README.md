# Operation FIRE STORM

My solution to the Operation FIRE STORM challenge.

## Layout

```
.
├── CMakeLists.txt
├── src/                 # C++ solution
│   └── main.cpp
├── tests/coretech/      # CoreTech's test harness (unmodified)
├── scripts/             # my own exploration/helper scripts
└── docs/challenge.md    # original brief
```

## Build

```bash
cmake -S . -B build
cmake --build build
```

Produces `./build/firestorm`.

## Run

```bash
./build/firestorm            # TODO: document arguments
```

## Verify

Run CoreTech's harness (do not modify the files under `tests/coretech/`):

```bash
python3 tests/coretech/test.py
```

## Design notes

TODO: describe the approach — relay, parsing, reassembly.
