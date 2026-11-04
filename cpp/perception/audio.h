#pragma once
#include <atomic>
#include <mutex>
#include <thread>
namespace cs2n {
struct SoundHint {double timestamp=-1000,side=0,strength=0;bool sharpImpulse=false;};
// Game output only. No microphone, files, or network transmission.
class GameAudio {
 public:
  GameAudio();
  ~GameAudio();
  SoundHint latest() const;
  bool active() const {return active_;}
 private:
  void run();
  std::atomic<bool> stop_{false},active_{false};
  mutable std::mutex mutex_;
  SoundHint hint_;
  std::thread thread_;
};
}
