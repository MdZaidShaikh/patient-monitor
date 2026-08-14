#include "../common/protocol.h"
#include "alert_engine.h"
#include "db.h"
#include "safe_queue.h"
#include "shared_state.h"

#include <arpa/inet.h>
#include <atomic>
#include <csignal>
#include <cstring>
#include <cstdlib>
#include <ncurses.h>
#include <iostream>
#include <netinet/in.h>
#include <set>
#include <sstream>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <mutex>
#include <condition_variable>

constexpr int PORT = 8080;
constexpr int NUM_WORKERS = 4;
constexpr int DASH_REFRESH_MS = 500;
constexpr size_t DB_BATCH_SIZE = 32;

SafeQueue<ReadingMessage> g_queue(1000);
SharedState g_state;
std::atomic<bool> g_running{true};
std::atomic<int> g_connected_clients{0};
std::mutex g_clients_mutex;
std::set<int> g_active_clients;
std::condition_variable g_clients_cv;

void handle_signal(int) {
    g_running = false;
    g_queue.shutdown();
    
    std::lock_guard<std::mutex> lock(g_clients_mutex);
    for (int fd : g_active_clients) {
        shutdown(fd, SHUT_RDWR);
        close(fd);
    }
    g_active_clients.clear();
}

void draw_patient_row(int row, const PatientSnapshot& snap, bool active_alert) {
    if (active_alert) attron(COLOR_PAIR(2));
    else if (snap.connected) attron(COLOR_PAIR(1));

    mvprintw(row, 2, "%7u  %s  %6.1f  %6.1f/%-6.1f  %6.1f  %s",
             snap.patient_id,
             std::to_string(snap.last_timestamp).c_str(),
             snap.heart_rate,
             snap.systolic_bp,
             snap.diastolic_bp,
             snap.spo2,
             snap.connected ? "online" : "offline");

    if (active_alert) attroff(COLOR_PAIR(2));
    else if (snap.connected) attroff(COLOR_PAIR(1));
}

void dashboard_loop() {
    if (!isatty(STDIN_FILENO)) return;

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    curs_set(0);
    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_GREEN, -1);
        init_pair(2, COLOR_RED, -1);
        init_pair(3, COLOR_CYAN, -1);
    }

    while (g_running) {
        int ch = getch();
        if (ch == 'q' || ch == 'Q') {
            g_running = false;
            g_queue.shutdown();
            break;
        }

        auto snapshots = g_state.get_all_snapshots();
        auto alerts = g_state.get_recent_alerts(12);
        std::set<uint32_t> active_alert_patients;
        for (const auto& alert : alerts) active_alert_patients.insert(alert.patient_id);

        erase();
        attron(COLOR_PAIR(3));
        mvprintw(0, 2, "Real-Time Patient Monitor");
        attroff(COLOR_PAIR(3));

        mvprintw(1, 2, "Connected clients: %d", g_connected_clients.load());
        mvprintw(2, 2, "Queue depth: %zu", g_queue.size());
        mvprintw(3, 2, "Press q to quit");
        mvhline(4, 0, '-', COLS);

        mvprintw(6, 2, "Patient  Timestamp    HR      BP          SpO2   State");
        mvhline(7, 2, '-', COLS - 4);

        int row = 8;
        if (snapshots.empty()) {
            mvprintw(row++, 2, "Waiting for device data...");
        } else {
            for (const auto& [patient_id, snap] : snapshots) {
                (void)patient_id;
                draw_patient_row(row++, snap, active_alert_patients.count(snap.patient_id) != 0);
            }
        }

        row += 1;
        mvprintw(row++, 2, "Recent Alerts");
        mvhline(row++, 2, '-', COLS - 4);
        if (alerts.empty()) {
            mvprintw(row++, 2, "No alerts yet.");
        } else {
            for (const auto& alert : alerts) {
                attron(COLOR_PAIR(2));
                mvprintw(row++, 2, "%7u  %-12s  %-12s  %s",
                         alert.patient_id,
                         alert.vital.c_str(),
                         std::to_string(alert.timestamp).c_str(),
                         alert.message.c_str());
                attroff(COLOR_PAIR(2));
            }
        }

        mvhline(LINES - 2, 0, '-', COLS);
        mvprintw(LINES - 1, 2, "Dashboard thread inside server process");
        refresh();
        napms(DASH_REFRESH_MS);
    }

    endwin();
}

// One thread per connected device. Reads length-prefixed ReadingMessages in a
// loop and pushes each onto the shared queue. Exits when the client disconnects.
void client_handler(int client_fd, sockaddr_in client_addr) {
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, ip, INET_ADDRSTRLEN);
    g_connected_clients++;

    ReadingMessage msg;
    uint32_t last_patient_id = 0;
    while (g_running && recv_reading(client_fd, msg)) {
        last_patient_id = msg.patient_id;
        g_queue.push(msg);
    }

    if (last_patient_id != 0) g_state.mark_disconnected(last_patient_id);
    
    {
        std::lock_guard<std::mutex> lock(g_clients_mutex);
        if (g_active_clients.count(client_fd)) {
            close(client_fd);
            g_active_clients.erase(client_fd);
        }
    }
    
    g_connected_clients--;
    g_clients_cv.notify_all();
}

// Worker threads: pop readings off the queue, persist to SQLite, update the
// in-memory snapshot, run threshold checks. This is the consumer side of the
// producer-consumer pattern; client_handler threads are the producers.
void worker_loop(int worker_id, Database& db, AlertEngine& alerts) {
    (void)worker_id;
    std::vector<ReadingMessage> batch;
    batch.reserve(DB_BATCH_SIZE);
    ReadingMessage r;
    while (g_queue.pop(r)) {
        batch.push_back(r);

        ReadingMessage extra;
        while (batch.size() < DB_BATCH_SIZE && g_queue.try_pop(extra)) {
            batch.push_back(extra);
        }

        db.insert_readings(batch);

        for (const auto& reading : batch) {
            PatientSnapshot snap{reading.patient_id, reading.timestamp, reading.heart_rate,
                                  reading.systolic_bp, reading.diastolic_bp, reading.spo2, true};
            g_state.update_snapshot(snap);
            alerts.check(reading);
        }
        batch.clear();
    }
}

int main(int argc, char* argv[]) {
    std::cout << std::unitbuf; // flush after every << so redirected logs aren't buffered away
    signal(SIGINT, handle_signal);
    signal(SIGPIPE, SIG_IGN); // don't die if a client vanishes mid-send

    std::string db_path = argc > 1 ? argv[1] : "patient_monitor.db";
    Database db(db_path);
    AlertEngine alert_engine(db, g_state);

    // Start the worker pool.
    std::vector<std::thread> workers;
    for (int i = 0; i < NUM_WORKERS; i++) {
        workers.emplace_back(worker_loop, i, std::ref(db), std::ref(alert_engine));
    }

    std::thread dashboard_thread(dashboard_loop);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind failed");
        return 1;
    }
    if (listen(server_fd, 16) < 0) {
        perror("listen failed");
        return 1;
    }

    std::cout << "Patient monitor server listening on port " << PORT << " with "
              << NUM_WORKERS << " workers...\n";

    while (g_running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        timeval tv{0, 500000}; // 500ms timeout
        
        int ret = select(server_fd + 1, &readfds, nullptr, nullptr, &tv);
        if (ret > 0 && FD_ISSET(server_fd, &readfds)) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(server_fd, (sockaddr*)&client_addr, &client_len);
            if (client_fd < 0) {
                if (!g_running) break; // accept() interrupted by shutdown
                continue;
            }
            {
                std::lock_guard<std::mutex> lock(g_clients_mutex);
                g_active_clients.insert(client_fd);
            }
            std::thread(client_handler, client_fd, client_addr).detach();
        }
    }

    std::cout << "\nShutting down...\n";
    close(server_fd);
    g_queue.shutdown();
    
    {
        std::unique_lock<std::mutex> lock(g_clients_mutex);
        g_clients_cv.wait(lock, [] { return g_active_clients.empty(); });
    }

    for (auto& t : workers) if (t.joinable()) t.join();
    if (dashboard_thread.joinable()) dashboard_thread.join();
    return 0;
}
