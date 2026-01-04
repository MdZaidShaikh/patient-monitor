# Real-Time Patient Monitoring System

A multithreaded C++ server that ingests simulated patient vitals over TCP,
stores them in SQLite, and fires alerts on threshold breaches.

## Directory structure

```
patient-monitor/
├── common/
│   └── protocol.h        # Shared wire protocol (ReadingMessage struct, send/recv helpers)
│                          # Used by BOTH server and device_sim -- keeps the two in sync.
│
├── server/
│   ├── main.cpp           # Acceptor loop + client handler threads + worker pool
│   ├── safe_queue.h        # Thread-safe bounded queue (producer-consumer)
│   ├── shared_state.h      # In-memory snapshot per patient + alert history (mutex-protected)
│   ├── db.h                 # SQLite wrapper, serialized writes, WAL mode
│   └── alert_engine.h      # Threshold checks with debounce logic
│
├── device_sim/
│   └── main.cpp            # standalone simulator with single-device and multi-device modes
│
├── docker/                  # Dockerfile + docker-compose.yml
│
├── Makefile
├── .gitignore
└── README.md
```

**Why this layout:**
- `common/` exists because the wire protocol must be *identical* on both ends —
  putting it in a shared header means the server and every device simulator
  compile against the same struct definition, so there's no drift.
- `server/` components are split one-per-concern (queue, state, db, alerts)
  rather than crammed into `main.cpp`, so each piece is independently testable
  and the threading boundaries are obvious from the file layout alone.
- `device_sim/` is a **separate executable**, not a thread inside the server.
  Real devices are separate machines/processes — running simulators as
  separate OS processes (and later, separate Docker containers) mimics that
  more honestly than spawning simulator threads inside the server binary.

The ncurses dashboard now runs as a thread inside the server process and reads
`SharedState` directly, which matches the architecture diagram more closely.

## Build & run

```bash
make                  # builds both server and device_sim

./server/patient_server                      # terminal 1
./device_sim/device_sim 1                     # terminal 2 -- patient ID 1
./device_sim/device_sim 2                     # terminal 3 -- patient ID 2
./device_sim/device_sim 3                     # terminal 4 -- patient ID 3
```

The server also accepts an optional database path, which is how the Docker setup
shares one SQLite file between the server and the simulator containers:

```bash
./server/patient_server /data/patient_monitor.db
```

Or via Makefile shortcuts (each blocks in the foreground, so use separate terminals):
```bash
make run-server
make run-sim1
make run-sim2
make run-sim3
```

Data lands in `server/patient_monitor.db`. Inspect it directly:
```bash
sqlite3 server/patient_monitor.db "SELECT * FROM readings ORDER BY id DESC LIMIT 10;"
sqlite3 server/patient_monitor.db "SELECT * FROM alerts;"
```

`make clean` removes build artifacts and the database.

## Status

- [x] TCP server, multithreaded (one thread per connected device)
- [x] Length-prefixed binary protocol
- [x] Producer-consumer queue + worker pool
- [x] SQLite persistence (WAL mode, serialized writes)
- [x] Threshold-based alerts with debounce
- [x] Device simulator with per-patient baselines + anomaly injection
- [x] ncurses dashboard thread inside the server process
- [x] Docker + docker-compose
- [x] stress helper for 50 devices via `--count`
- [x] server TSan target for concurrency checking
- [ ] Stress test (50+ simulated devices)
