#include "executor.h"

#include <thread>

namespace util {

// Order time points in *descending* order so that they are put in *ascending*
// order in a heap.
static constexpr auto by_time = [](auto& l, auto& r) {
  return l.time > r.time;
};

void executor::schedule(std::function<void()> f) noexcept {
  schedule_at(clock::now(), std::move(f));
}

void executor::schedule_in(duration d, std::function<void()> f) noexcept {
  schedule_at(clock::now() + d, std::move(f));
}

void serial_executor::schedule_at(time_point t,
                                  std::function<void()> f) noexcept {
  work_.push_back({t, std::move(f)});
  std::push_heap(work_.begin(), work_.end(), by_time);
}

void serial_executor::run() {
  while (!work_.empty()) {
    std::pop_heap(work_.begin(), work_.end(), by_time);
    work_item work = std::move(work_.back());
    work_.pop_back();
    std::this_thread::sleep_until(work.time);
    work.resume();
  }
}

}  // namespace util
