#pragma once
#include <condition_variable>
#include <deque>
#include <mutex>

// Bounded, thread-safe queue. Multiple producer threads (client handlers) push,
// multiple consumer threads (workers) pop. Blocks producers when full and
// consumers when empty, rather than spinning.
template <typename T>
class SafeQueue {
public:
    explicit SafeQueue(size_t max_size = 1000) : max_size_(max_size) {}

    // Blocks if the queue is full (backpressure on producers).
    void push(T item) {
        std::unique_lock<std::mutex> lock(mutex_);
        not_full_.wait(lock, [this] { return queue_.size() < max_size_ || shutting_down_; });
        if (shutting_down_) return;
        queue_.push_back(std::move(item));
        lock.unlock();
        not_empty_.notify_one();
    }

    // Blocks if empty. Returns false only if shutting down and queue drained.
    bool pop(T& out) {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [this] { return !queue_.empty() || shutting_down_; });
        if (queue_.empty()) return false; // shutting_down_ and nothing left
        out = std::move(queue_.front());
        queue_.pop_front();
        lock.unlock();
        not_full_.notify_one();
        return true;
    }

    bool try_pop(T& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return false;
        out = std::move(queue_.front());
        queue_.pop_front();
        not_full_.notify_one();
        return true;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    // Wakes up all blocked producers/consumers so threads can exit cleanly.
    void shutdown() {
        std::lock_guard<std::mutex> lock(mutex_);
        shutting_down_ = true;
        not_empty_.notify_all();
        not_full_.notify_all();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    std::deque<T> queue_;
    size_t max_size_;
    bool shutting_down_ = false;
};
