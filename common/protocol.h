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

inline uint32_t pack_float(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    return htonl(bits);
}

inline float unpack_float(uint32_t bits) {
    bits = ntohl(bits);
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

// Sends one ReadingMessage as: [4-byte length prefix, network order][payload bytes]
inline bool send_reading(int fd, const ReadingMessage& msg) {
    uint32_t len = htonl(static_cast<uint32_t>(READING_SIZE));
    if (!send_exact(fd, &len, sizeof(len))) return false;

    ReadingMessage net_msg;
    net_msg.patient_id = htonl(msg.patient_id);
    net_msg.timestamp = htonl(msg.timestamp);

    uint32_t hr = pack_float(msg.heart_rate);
    std::memcpy(&net_msg.heart_rate, &hr, sizeof(float));

    uint32_t sys = pack_float(msg.systolic_bp);
    std::memcpy(&net_msg.systolic_bp, &sys, sizeof(float));

    uint32_t dia = pack_float(msg.diastolic_bp);
    std::memcpy(&net_msg.diastolic_bp, &dia, sizeof(float));

    uint32_t spo2 = pack_float(msg.spo2);
    std::memcpy(&net_msg.spo2, &spo2, sizeof(float));

    return send_exact(fd, &net_msg, READING_SIZE);
}

// Reads one length-prefixed ReadingMessage. Returns false on disconnect/error
// or if the advertised length doesn't match what we expect (protocol mismatch).
inline bool recv_reading(int fd, ReadingMessage& out) {
    uint32_t len_net;
    if (!recv_exact(fd, &len_net, sizeof(len_net))) return false;
    uint32_t len = ntohl(len_net);
    if (len != READING_SIZE) return false; // guards against malformed/garbage data

    ReadingMessage net_msg;
    if (!recv_exact(fd, &net_msg, READING_SIZE)) return false;

    out.patient_id = ntohl(net_msg.patient_id);
    out.timestamp = ntohl(net_msg.timestamp);

    uint32_t hr;
    std::memcpy(&hr, &net_msg.heart_rate, sizeof(uint32_t));
    out.heart_rate = unpack_float(hr);

    uint32_t sys;
    std::memcpy(&sys, &net_msg.systolic_bp, sizeof(uint32_t));
    out.systolic_bp = unpack_float(sys);

    uint32_t dia;
    std::memcpy(&dia, &net_msg.diastolic_bp, sizeof(uint32_t));
    out.diastolic_bp = unpack_float(dia);

    uint32_t spo2;
    std::memcpy(&spo2, &net_msg.spo2, sizeof(uint32_t));
    out.spo2 = unpack_float(spo2);

    return true;
}
