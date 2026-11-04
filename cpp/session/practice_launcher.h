#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include "state/process_target.h"
#include "state/gsi_provider.h"
namespace cs2n::session {
enum class LaunchStatus {Idle,Running,Ready,Failed,Cancelled};
class DeathmatchLauncher {
 public:
  DeathmatchLauncher(ProcessTarget target,std::shared_ptr<GsiProvider> gsi):target_(target),gsi_(std::move(gsi)){}
  ~DeathmatchLauncher(){cancel();if(thread_.joinable())thread_.join();}
  void start();
  void cancel();
  LaunchStatus status()const{return state_.load();}
  std::string message()const{std::lock_guard lock(mutex_);return message_;}
 private:
  void run();
  void report(LaunchStatus state,std::string message);
  ProcessTarget target_;
  std::shared_ptr<GsiProvider> gsi_;
  std::atomic<bool> cancelled_{false};
  std::atomic<LaunchStatus> state_{LaunchStatus::Idle};
  std::thread thread_;
  mutable std::mutex mutex_;
  std::mutex dispatch_;
  std::string message_;
};
}
