#include "../common/protocol.h"

#include <arpa/inet.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <iostream>
#include <netdb.h>
#include <string>
#include <netinet/in.h>
#include <random>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

static uint32_t auto_patient_id() {
    char hostname[256]{};
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        std::size_t hash = std::hash<std::string>{}(hostname);
        return static_cast<uint32_t>((hash % 900000) + 1);
    }
    return static_cast<uint32_t>((getpid() % 900000) + 1);
}

static void run_device(uint32_t patient_id, const std::string& server_ip, int server_port) {
    std::mt19937 rng(std::random_device{}() + patient_id);
    std::normal_distribution<float> hr_jitter(0, 2.0);
    std::normal_distribution<float> bp_jitter(0, 3.0);
    std::normal_distribution<float> spo2_jitter(0, 0.5);
    std::uniform_real_distribution<float> anomaly_chance(0, 1);

    // Per-patient baseline so different patients look distinct on the dashboard.
    float base_hr = 65 + (patient_id % 5) * 5;      // 65-85
    float base_sys = 110 + (patient_id % 4) * 8;     // 110-134
    float base_dia = 70 + (patient_id % 3) * 5;      // 70-80
    float base_spo2 = 97;

    while (true) {
        int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(server_port);

        in_addr ipv4_addr{};
        if (inet_pton(AF_INET, server_ip.c_str(), &ipv4_addr) == 1) {
            server_addr.sin_addr = ipv4_addr;
        } else {
            addrinfo hints{};
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;

            addrinfo* result = nullptr;
            if (getaddrinfo(server_ip.c_str(), nullptr, &hints, &result) != 0 || !result) {
                std::cerr << "[patient " << patient_id << "] cannot resolve " << server_ip << ", retrying in 2s...\n";
                close(sock_fd);
                std::this_thread::sleep_for(std::chrono::seconds(2));
                continue;
            }

            server_addr.sin_addr = reinterpret_cast<sockaddr_in*>(result->ai_addr)->sin_addr;
            freeaddrinfo(result);
        }

        if (connect(sock_fd, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            std::cerr << "[patient " << patient_id << "] connect failed, retrying in 2s...\n";
            close(sock_fd);
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }
        std::cout << "[patient " << patient_id << "] connected to " << server_ip << ":" << server_port << "\n";

        bool connection_alive = true;
        while (connection_alive) {
            ReadingMessage msg{};
            msg.patient_id = patient_id;
            msg.timestamp = static_cast<uint32_t>(std::time(nullptr));

            // ~2% chance per tick of injecting an anomaly so the alert engine
            // has something real to catch during a demo.
            bool anomaly = anomaly_chance(rng) < 0.02;

            if (anomaly) {
                // Pick one vital to spike/drop badly.
                int which = rng() % 3;
                msg.heart_rate = which == 0 ? (rng() % 2 ? 145 : 40) : base_hr + hr_jitter(rng);
                msg.spo2 = which == 1 ? 85 : base_spo2 + spo2_jitter(rng);
                msg.systolic_bp = which == 2 ? 165 : base_sys + bp_jitter(rng);
                msg.diastolic_bp = base_dia + bp_jitter(rng);
                std::cout << "[patient " << patient_id << "] *** injecting anomaly ***\n";
            } else {
                msg.heart_rate = base_hr + hr_jitter(rng);
                msg.spo2 = base_spo2 + spo2_jitter(rng);
                msg.systolic_bp = base_sys + bp_jitter(rng);
                msg.diastolic_bp = base_dia + bp_jitter(rng);
            }

            if (!send_reading(sock_fd, msg)) {
                std::cerr << "[patient " << patient_id << "] send failed, reconnecting...\n";
                connection_alive = false;
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        }
        close(sock_fd);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <patient_id|auto> [server_ip] [server_port]\n"
                  << "   or: " << argv[0] << " --count N [server_ip] [server_port] [base_patient_id]\n";
        return 1;
    }

    std::string mode = argv[1];
    if (mode == "--count") {
        if (argc < 3) {
            std::cerr << "Missing count value\n";
            return 1;
        }

        int count = std::stoi(argv[2]);
        std::string server_ip = argc > 3 ? argv[3] : "127.0.0.1";
        int server_port = argc > 4 ? std::stoi(argv[4]) : 8080;
        uint32_t base_patient_id = argc > 5 ? static_cast<uint32_t>(std::stoul(argv[5])) : 1;

        std::vector<std::thread> devices;
        devices.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; i++) {
            devices.emplace_back(run_device, base_patient_id + static_cast<uint32_t>(i), server_ip, server_port);
        }
        for (auto& device : devices) {
            if (device.joinable()) device.join();
        }
        return 0;
    }

    uint32_t patient_id = mode == "auto" ? auto_patient_id() : static_cast<uint32_t>(std::stoul(mode));
    std::string server_ip = argc > 2 ? argv[2] : "127.0.0.1";
    int server_port = argc > 3 ? std::stoi(argv[3]) : 8080;

    run_device(patient_id, server_ip, server_port);
    return 0;
}
