#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>

namespace engine {

template <typename T>
class BlockingQueue {
 private:
  std::mutex d_mutex;
  std::condition_variable d_condition;
  std::deque<T> d_queue;

 public:
  template <typename U>
  void push(U&& value) {
    {
      std::scoped_lock lock{this->d_mutex};
      d_queue.push_back(std::forward<U>(value));
    }
    this->d_condition.notify_one();
  }
  void wait() {
    std::unique_lock lock{this->d_mutex};
    this->d_condition.wait(lock, [=] { return !this->d_queue.empty(); });
  }
  T pop() {
    std::unique_lock lock{this->d_mutex};
    this->d_condition.wait(lock, [=] { return !this->d_queue.empty(); });
    T rc(std::move(this->d_queue.front()));
    this->d_queue.pop_front();
    return rc;
  }
  void clear() {
    std::scoped_lock lock{this->d_mutex};
    this->d_queue.clear();
  }
  std::optional<T> popMaybe() {
    std::scoped_lock lock{this->d_mutex};
    if (this->d_queue.empty())
      return std::nullopt;
    T rc(std::move(this->d_queue.front()));
    this->d_queue.pop_front();
    return {std::move(rc)};
  }
};
} // namespace
