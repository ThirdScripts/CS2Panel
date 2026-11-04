#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>
namespace cs2n {
class JsonValue;
// Version-pinned command queue bridge. No keyboard, focus changes or hooks.
class GameCommands {
 public:
  explicit GameCommands(DWORD pid, std::uint64_t created = 0);
  ~GameCommands();
  bool available() const;
  bool send(const std::string& command);
  const std::string& error() const {return error_;}
  static bool validateProfile(const JsonValue& profile, std::string& error);
  static bool allowed(const std::string& command);
 private:
  bool read(std::uintptr_t address,void* out,std::size_t size) const;
  HANDLE process_=nullptr;
  std::uintptr_t base_=0,object_=0,function_=0;
  bool failed_=false;
  std::string error_;
};
}
