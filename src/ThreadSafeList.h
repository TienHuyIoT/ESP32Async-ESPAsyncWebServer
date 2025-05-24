#ifndef THREAD_SAFE_LIST_H
#define THREAD_SAFE_LIST_H

#include <list>
#include <mutex>
#include <functional>
#include <utility>
#include <memory>

namespace ts {

template<typename T>
class ThreadSafeList {
private:
    std::list<T> data_;
    mutable std::mutex mutex_;

public:
    // Push
    void push_back(T value) {
        std::lock_guard<std::mutex> lock(mutex_);
        data_.push_back(std::move(value));
    }

    void push_front(T value) {
        std::lock_guard<std::mutex> lock(mutex_);
        data_.push_front(std::move(value));
    }

    // Emplace
    template<typename... Args>
    void emplace_back(Args&&... args) {
        std::lock_guard<std::mutex> lock(mutex_);
        data_.emplace_back(std::forward<Args>(args)...);
    }

    template<typename... Args>
    void emplace_front(Args&&... args) {
        std::lock_guard<std::mutex> lock(mutex_);
        data_.emplace_front(std::forward<Args>(args)...);
    }

    // Pop
    std::unique_ptr<T> pop_front() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (data_.empty()) return nullptr;
        std::unique_ptr<T> val(new T(std::move(data_.front())));
        data_.pop_front();
        return val;
    }

    std::unique_ptr<T> pop_back() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (data_.empty()) return nullptr;
        std::unique_ptr<T> val(new T(std::move(data_.back())));
        data_.pop_back();
        return val;
    }

    // Access
    std::unique_ptr<T> front() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (data_.empty()) return nullptr;
        return std::unique_ptr<T>(new T(data_.front()));
    }

    std::unique_ptr<T> back() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (data_.empty()) return nullptr;
        return std::unique_ptr<T>(new T(data_.back()));
    }

    std::unique_ptr<T> find_if(const std::function<bool(const T &)> &predicate) const {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = std::find_if(data_.begin(), data_.end(), predicate);
      if (it != data_.end()) {
        return std::unique_ptr<T>(new T(*it));
      }
      return nullptr;
    }

    // Remove
    void remove_if(const std::function<bool(const T&)>& predicate) {
        std::lock_guard<std::mutex> lock(mutex_);
        data_.remove_if(predicate);
    }

    // This uses std::list::remove, which removes all elements that compare equal to value
    // Note: These require that T supports operator==
    void erase(const T &value) {
      std::lock_guard<std::mutex> lock(mutex_);
      data_.remove(value);  // Removes all elements equal to value
    }

    // This version removes only the first element equal to value
    // Note: These require that T supports operator==
    void erase_first(const T &value) {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = std::find(data_.begin(), data_.end(), value);
      if (it != data_.end()) {
        data_.erase(it);
      }
    }

    void erase_if(const std::function<bool(const T&)>& predicate) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = data_.begin(); it != data_.end(); ) {
            if (predicate(*it)) {
                it = data_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        data_.clear();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return data_.size();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return data_.empty();
    }

    // for_each: read-only iteration
    void for_each(const std::function<void(const T&)>& fn) const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& item : data_) {
            fn(item);
        }
    }

    // for_each_mutable: allows mutation
    void for_each_mutable(const std::function<void(T&)>& fn) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& item : data_) {
            fn(item);
        }
    }
};

} // namespace ts

#endif // THREAD_SAFE_LIST_H
