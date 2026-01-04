#pragma once
#include <arpa/inet.h>
#include <cstdint>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

// The fixed-size struct sent for every vitals reading.
// Packed layout, all fields fixed-width so size is identical on sender/receiver.
#pragma pack(push, 1)
struct ReadingMessage {
    uint32_t patient_id;
    uint32_t timestamp;     // unix epoch seconds
    float heart_rate;       // bpm
    float systolic_bp;      // mmHg
    float diastolic_bp;     // mmHg
    float spo2;              // % oxygen saturation
};
#pragma pack(pop)

constexpr size_t READING_SIZE = sizeof(ReadingMessage);

// Reads exactly `len` bytes from fd into buf, looping over recv() since a
// single recv() call is not guaranteed to return all requested bytes at once.
// Returns false if the connection closed or errored before len bytes arrived.
inline bool recv_exact(int fd, void* buf, size_t len) {
    size_t total = 0;
    char* ptr = static_cast<char*>(buf);
    while (total < len) {
        ssize_t n = recv(fd, ptr + total, len - total, 0);
        if (n <= 0) return false; // 0 = peer closed, <0 = error
        total += static_cast<size_t>(n);
    }
    return true;
}

// Same idea for send() -- loops until all bytes are written.
inline bool send_exact(int fd, const void* buf, size_t len) {
    size_t total = 0;
    const char* ptr = static_cast<const char*>(buf);
    while (total < len) {
        ssize_t n = send(fd, ptr + total, len - total, 0);
        if (n <= 0) return false;
        total += static_cast<size_t>(n);
    }
    return true;
}

// Sends one ReadingMessage as: [4-byte length prefix, network order][payload bytes]
inline bool send_reading(int fd, const ReadingMessage& msg) {
    uint32_t len = htonl(static_cast<uint32_t>(READING_SIZE));
    if (!send_exact(fd, &len, sizeof(len))) return false;
    return send_exact(fd, &msg, READING_SIZE);
}

// Reads one length-prefixed ReadingMessage. Returns false on disconnect/error
// or if the advertised length doesn't match what we expect (protocol mismatch).
inline bool recv_reading(int fd, ReadingMessage& out) {
    uint32_t len_net;
    if (!recv_exact(fd, &len_net, sizeof(len_net))) return false;
    uint32_t len = ntohl(len_net);
    if (len != READING_SIZE) return false; // guards against malformed/garbage data
    return recv_exact(fd, &out, READING_SIZE);
}
