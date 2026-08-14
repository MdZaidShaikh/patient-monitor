# Real-Time Patient Monitoring System

A high-performance, multithreaded C++ backend infrastructure for a hospital floor. This system continuously ingests simulated patient vitals (Heart Rate, Blood Pressure, SpO2) over TCP, stores them in a SQLite database for historical analysis, and runs an "Alert Engine" to immediately fire alerts on dangerous threshold breaches. The system features a real-time terminal dashboard to monitor live patient streams.

## Architecture

The system follows a classic **Producer-Consumer** architecture over TCP, ensuring high throughput without blocking the main network thread.

*   **Producers (Client Handlers):** Each connected device spawns a dedicated TCP handler thread that reads length-prefixed binary payloads and pushes them to a thread-safe bounded queue (`SafeQueue`).
*   **Consumers (Worker Pool):** A pool of worker threads pulls batches of readings from the queue.
*   **Persistence (SQLite WAL):** Workers bulk-insert readings and alerts into an SQLite database configured in Write-Ahead Logging (WAL) mode for extreme performance.
*   **Business Logic (Alert Engine):** Evaluates incoming readings against physiological thresholds with a debounce mechanism to prevent alert spam.
*   **State Management (SharedState):** Maintains a live, mutex-protected snapshot of the hospital floor.
*   **Presentation (Dashboard):** A dedicated thread polls the `SharedState` and draws a live `ncurses` UI in the terminal.

## Getting Started

You can run this project effortlessly using **Docker** (recommended) or natively on **Linux / WSL**.

### Option 1: Docker (Recommended)

Running via Docker Compose is the easiest way to start both the server and the device simulators without installing dependencies.

1. **Start the System**:
   ```bash
   docker compose -f docker/docker-compose.yml up -d
   ```
2. **View the Live Dashboard**: 
   Attach your terminal to the server to see the real-time ncurses UI:
   ```bash
   docker attach docker-server-1
   ```
   *(To exit the dashboard, press `q` on your keyboard).*

3. **Scale the Number of Patients**:
   You can easily simulate an entire hospital wing by scaling the number of devices:
   ```bash
   docker compose -f docker/docker-compose.yml up -d --scale device=5
   ```

### Option 2: Linux / WSL (Native)

If you prefer building from source, ensure you have `g++`, `make`, `libsqlite3-dev`, and `libncurses-dev` installed.

1. **Build the project**:
   ```bash
   make
   ```
2. **Start the Server** (Terminal 1):
   ```bash
   make run-server
   ```
3. **Start a Device Simulator** (Terminal 2):
   ```bash
   make run-sim1
   ```

## Advanced Commands & Options

Here are some helpful commands for interacting with the database, running stress tests, and managing the project.

| Goal | Command | Environment |
| :--- | :--- | :--- |
| **Simulate 50 Patients** | `make run-stress50` | Linux / WSL |
| **Simulate Specific Patient** | `./device_sim/device_sim <patient_id> 127.0.0.1 8080` | Linux / WSL |
| **View Latest DB Readings** | `sqlite3 server/patient_monitor.db "SELECT * FROM readings ORDER BY timestamp DESC LIMIT 10;"` | Linux / WSL |
| **View Latest DB Alerts** | `sqlite3 server/patient_monitor.db "SELECT * FROM alerts ORDER BY timestamp DESC LIMIT 10;"` | Linux / WSL |
| **View DB via Docker** | `docker run --rm -it -v docker_patient-data:/data keinos/sqlite3 /data/patient_monitor.db "SELECT * FROM readings ORDER BY id DESC LIMIT 15;"` | Docker |
| **Clean Build & DB** | `make clean` | Linux / WSL |
| **Stop Docker Containers** | `docker compose -f docker/docker-compose.yml down` | Docker |

## Directory Structure

- `common/`: Shared wire protocol definition (`ReadingMessage` struct). Shared to prevent struct drift between server and devices.
- `server/`: One-per-concern splitting of backend components (queue, state, db, alerts).
- `device_sim/`: Standalone simulator representing a physical bedside monitor.
- `docker/`: Contains the multi-stage `Dockerfile` and `docker-compose.yml`.
