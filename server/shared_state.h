#pragma once
#include "../common/protocol.h"
#include <map>
#include <mutex>
#include <string>
#include <vector>

struct PatientSnapshot {
    uint32_t patient_id;
    uint32_t last_timestamp;
    float heart_rate;
    float systolic_bp;
    float diastolic_bp;
    float spo2;
    bool connected;
};

struct Alert {
    uint32_t patient_id;
    uint32_t timestamp;
    std::string vital;      // "HR", "SpO2", "BP"
    std::string message;
    float value;
};

// Single source of truth read by the dashboard, written by workers/alert engine.
// Kept deliberately simple: one mutex guarding everything. At this scale
// (dozens of patients, a few reads/writes per second) a single mutex is not
// a bottleneck -- premature fine-grained locking would just add bugs.
class SharedState {
public:
    void update_snapshot(const PatientSnapshot& snap) {
        std::lock_guard<std::mutex> lock(mutex_);
        patients_[snap.patient_id] = snap;
    }

    void mark_disconnected(uint32_t patient_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = patients_.find(patient_id);
        if (it != patients_.end()) it->second.connected = false;
    }

    std::map<uint32_t, PatientSnapshot> get_all_snapshots() {
        std::lock_guard<std::mutex> lock(mutex_);
        return patients_; // copy out, caller works on its own copy
    }

    void add_alert(const Alert& alert) {
        std::lock_guard<std::mutex> lock(mutex_);
        alerts_.push_back(alert);
        if (alerts_.size() > 200) alerts_.erase(alerts_.begin()); // cap memory use
    }

    std::vector<Alert> get_recent_alerts(size_t n = 20) {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t start = alerts_.size() > n ? alerts_.size() - n : 0;
        return std::vector<Alert>(alerts_.begin() + start, alerts_.end());
    }

private:
    std::mutex mutex_;
    std::map<uint32_t, PatientSnapshot> patients_;
    std::vector<Alert> alerts_;
};
