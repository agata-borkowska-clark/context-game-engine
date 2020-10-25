#pragma once

#include <chrono>
#include <functional>

namespace util {

class executor {
 public:
  using clock = std::chrono::steady_clock;
  using time_point = clock::time_point;
  using duration = clock::duration;
  using task = std::function<void()>;

  constexpr executor() noexcept = default;
  virtual ~executor() noexcept = default;

  // Schedule a task to run at a certain point in time.
  virtual void schedule_at(time_point time, task) noexcept = 0;

  // Schedule a task to run now.
  void schedule(task f) noexcept;

  // Schedule a task to run a certain amount of time from now.
  void schedule_in(duration, task) noexcept;
};

// An executor which runs all work in a single thread.
class serial_executor final : public executor {
 public:
  void schedule_at(time_point, task) noexcept override;

  // Run work until there is no more work scheduled.
  void run();

 private:
  struct work_item {
    time_point time;
    task resume;
  };
  std::vector<work_item> work_;
};

}  // namespace util
