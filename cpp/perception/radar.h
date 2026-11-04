#pragma once
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
namespace cs2n {
struct RadarHints {double timestamp=-1000;std::vector<double> bearings;};
class RadarObserver {
 public:
  RadarObserver();~RadarObserver();
  RadarHints latest()const;
 private:
  std::atomic<bool> stop_{false};mutable std::mutex mutex_;RadarHints hints_;std::thread thread_;
};
}
