#pragma once
#include "../common/protocol.h"
#include "db.h"
#include "shared_state.h"
#include <map>
#include <mutex>

// Normal ranges. Anything outside these is a breach.
struct Thresholds {
    static constexpr float HR_LOW = 60, HR_HIGH = 100;
    static constexpr float SPO2_LOW = 92;
    static constexpr float SYS_BP_LOW = 90, SYS_BP_HIGH = 140;
    static constexpr float DIA_BP_LOW = 60, DIA_BP_HIGH = 90;
};

class AlertEngine {
public:
    AlertEngine(Database& db, SharedState& state) : db_(db), state_(state) {}

    // Called by worker threads after a reading is stored. Debounces: only
    // inserts/logs a new alert when a vital transitions from normal->breach.
    // While it stays in breach, we don't spam a new row every second; once it
    // returns to normal we clear the flag so the NEXT breach fires again.
    void check(const ReadingMessage& r) {
        check_vital(r.patient_id, "HR", r.heart_rate,
                     r.heart_rate < Thresholds::HR_LOW || r.heart_rate > Thresholds::HR_HIGH,
                     r.timestamp);
        check_vital(r.patient_id, "SpO2", r.spo2,
                     r.spo2 < Thresholds::SPO2_LOW,
                     r.timestamp);
        check_vital(r.patient_id, "Systolic BP", r.systolic_bp,
                     r.systolic_bp < Thresholds::SYS_BP_LOW || r.systolic_bp > Thresholds::SYS_BP_HIGH,
                     r.timestamp);
        check_vital(r.patient_id, "Diastolic BP", r.diastolic_bp,
                     r.diastolic_bp < Thresholds::DIA_BP_LOW || r.diastolic_bp > Thresholds::DIA_BP_HIGH,
                     r.timestamp);
    }

private:
    void check_vital(uint32_t patient_id, const std::string& vital, float value,
                      bool breached, uint32_t timestamp) {
        std::string key = std::to_string(patient_id) + ":" + vital;
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = active_breaches_.find(key);
        bool was_breached = (it != active_breaches_.end() && it->second);

        if (breached && !was_breached) {
            // Transition: normal -> breach. Fire the alert.
            std::string msg = vital + " out of range: " + std::to_string(value);
            db_.insert_alert(patient_id, timestamp, vital, msg, value);
            state_.add_alert(Alert{patient_id, timestamp, vital, msg, value});
            active_breaches_[key] = true;
        } else if (!breached && was_breached) {
            // Transition: breach -> normal. Clear the flag, stay quiet.
            active_breaches_.erase(it);
        }
        // breach->breach or normal->normal: no action, this is the debounce.
    }

    Database& db_;
    SharedState& state_;
    std::mutex mutex_;
    std::map<std::string, bool> active_breaches_;
};
