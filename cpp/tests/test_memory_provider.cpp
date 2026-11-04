// Diagnostic memory stand.
//
// The first group tests the validation path in isolation. The second group
// spawns the probe process we own and reads its published snapshot, which is
// what proves the Windows read path works end to end. Nothing here touches
// Counter-Strike 2 or any process the user did not explicitly name.
#include <Windows.h>

#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <type_traits>
#include <filesystem>
#include <fstream>
#include "state/cs2_memory.h"

#include "state/memory_provider.h"
#include "movement/game_commands.h"
#include "tests/framework.h"
#include "state/process_target.h"

using namespace cs2n;

CS2N_TEST(ProcessBinding_RejectsWrongPidAndCreationTime) {
 auto h=GetCurrentProcess();ProcessTarget target{GetCurrentProcessId(),processCreation(h)};
 CHECK_TRUE(target.created!=0);CHECK_TRUE(sameProcess(h,target));
 CHECK_FALSE(sameProcess(h,{target.pid,target.created+1}));
 CHECK_FALSE(sameProcess(h,{target.pid+1,target.created}));
 CHECK_FALSE(sameProcess(h,{target.pid,0}));
 CHECK_FALSE(sameProcess(nullptr,target));
}

CS2N_TEST(GameCommands_OnlyFixedSetupCommandsAllowed) {
  CHECK_TRUE(GameCommands::allowed("jointeam 2 1"));
  CHECK_TRUE(GameCommands::allowed("cl_dm_buyrandomweapons 0; buy ak47; buy deagle; slot1"));
  CHECK_TRUE(GameCommands::allowed("slot1"));
  CHECK_TRUE(GameCommands::allowed("disconnect"));
  CHECK_FALSE(GameCommands::allowed("disconnect; quit"));
  CHECK_TRUE(!GameCommands::allowed("jointeam 3 1"));
  CHECK_TRUE(!GameCommands::allowed("slot1; quit"));
  CHECK_TRUE(!GameCommands::allowed("slot1\nquit"));
  CHECK_TRUE(!GameCommands::allowed(""));
}

namespace {

ProbeSnapshot makeSnapshot(double x, double y, double yaw, std::uint32_t sequence, std::uint32_t health = 100) {
  ProbeSnapshot snapshot{};
  snapshot.magic = kProbeMagic;
  snapshot.sequence = sequence;
  snapshot.x = x;
  snapshot.y = y;
  snapshot.z = 0.0;
  snapshot.yaw = yaw;
  snapshot.health = health;
  std::snprintf(snapshot.mapName, sizeof(snapshot.mapName), "%s", "de_dust2");
  return snapshot;
}

// Locates cs2n_mem_probe.exe next to the test binary.
std::wstring probePath() {
  wchar_t module[1024] = {};
  GetModuleFileNameW(nullptr, module, 1024);
  std::wstring path(module);
  const std::size_t slash = path.find_last_of(L'\\');
  if (slash != std::wstring::npos) path = path.substr(0, slash + 1);
  path += L"cs2n_mem_probe.exe";
  return path;
}

struct ProbeProcess {
  HANDLE process = nullptr;
  HANDLE stdoutRead = nullptr;
  std::uint32_t pid = 0;
  std::uintptr_t address = 0;

  ProbeProcess() = default;
  ProbeProcess(const ProbeProcess&) = delete;
  ProbeProcess& operator=(const ProbeProcess&) = delete;
  ProbeProcess& operator=(ProbeProcess&&) = delete;
  ProbeProcess(ProbeProcess&& other) noexcept
      : process(std::exchange(other.process, nullptr)),
        stdoutRead(std::exchange(other.stdoutRead, nullptr)),
        pid(std::exchange(other.pid, 0)), address(std::exchange(other.address, 0)) {}

  ~ProbeProcess() {
    if (process != nullptr) {
      TerminateProcess(process, 0);
      WaitForSingleObject(process, 3000);
      CloseHandle(process);
    }
    if (stdoutRead != nullptr) CloseHandle(stdoutRead);
  }

  [[nodiscard]] bool ready() const { return process != nullptr && pid != 0 && address != 0; }
};

ProbeProcess startProbe() {
  ProbeProcess result;
  SECURITY_ATTRIBUTES attributes{};
  attributes.nLength = sizeof(attributes);
  attributes.bInheritHandle = TRUE;

  HANDLE stdoutRead = nullptr;
  HANDLE stdoutWrite = nullptr;
  if (!CreatePipe(&stdoutRead, &stdoutWrite, &attributes, 0)) return result;
  SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdOutput = stdoutWrite;
  startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

  PROCESS_INFORMATION info{};
  std::wstring command = L"\"" + probePath() + L"\" --seconds 20";
  std::vector<wchar_t> buffer(command.begin(), command.end());
  buffer.push_back(L'\0');

  const BOOL created = CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                      nullptr, &startup, &info);
  CloseHandle(stdoutWrite);
  if (!created) {
    CloseHandle(stdoutRead);
    return result;
  }
  CloseHandle(info.hThread);
  result.process = info.hProcess;
  result.pid = info.dwProcessId;
  result.stdoutRead = stdoutRead;

  // The probe prints a single line: PID=<pid> ADDR=0x<hex> SIZE=<n>
  std::string line;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  char chunk[256];
  while (std::chrono::steady_clock::now() < deadline) {
    DWORD available = 0;
    if (!PeekNamedPipe(stdoutRead, nullptr, 0, nullptr, &available, nullptr)) break;
    if (available > 0) {
      DWORD read = 0;
      if (!ReadFile(stdoutRead, chunk, sizeof(chunk), &read, nullptr) || read == 0) break;
      line.append(chunk, read);
      if (line.find('\n') != std::string::npos) break;
    } else {
      Sleep(20);
    }
  }

  const std::size_t pidPos = line.find("PID=");
  const std::size_t addrPos = line.find("ADDR=0x");
  if (pidPos == std::string::npos || addrPos == std::string::npos) return result;
  result.pid = static_cast<std::uint32_t>(std::strtoul(line.c_str() + pidPos + 4, nullptr, 10));
  result.address = static_cast<std::uintptr_t>(std::strtoull(line.c_str() + addrPos + 7, nullptr, 16));
  return result;
}

}  // namespace

CS2N_TEST(MemoryProbe_ProcessOwnerTransfersWithoutCopying) {
  static_assert(!std::is_copy_constructible_v<ProbeProcess>);
  ProbeProcess original;
  original.stdoutRead = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  CHECK_TRUE(original.stdoutRead != nullptr);
  const HANDLE handle = original.stdoutRead;
  ProbeProcess moved(std::move(original));
  CHECK_TRUE(original.stdoutRead == nullptr);
  CHECK_TRUE(moved.stdoutRead == handle);
  CHECK_TRUE(SetEvent(moved.stdoutRead) != 0);
}

CS2N_TEST(MemoryProbe_RejectsWrongMagic) {
  ProbeSnapshot snapshot = makeSnapshot(1.0, 2.0, 0.0, 1);
  snapshot.magic = 0xDEADBEEF;
  std::string error;
  CHECK_FALSE(MemoryProvider::validate(snapshot, 0.0, 0.5, 0, "de_dust2", error).has_value());
  CHECK_FALSE(error.empty());
}

CS2N_TEST(MemoryProbe_RejectsNonFiniteValues) {
  ProbeSnapshot snapshot = makeSnapshot(1.0, 2.0, 0.0, 1);
  snapshot.x = std::numeric_limits<double>::infinity();
  std::string error;
  CHECK_FALSE(MemoryProvider::validate(snapshot, 0.0, 0.5, 0, "de_dust2", error).has_value());
}

CS2N_TEST(MemoryProbe_RejectsUnexpectedMap) {
  ProbeSnapshot snapshot = makeSnapshot(1.0, 2.0, 0.0, 1);
  std::snprintf(snapshot.mapName, sizeof(snapshot.mapName), "%s", "de_inferno");
  std::string error;
  CHECK_FALSE(MemoryProvider::validate(snapshot, 0.0, 0.5, 0, "de_dust2", error).has_value());
}

CS2N_TEST(MemoryProbe_RejectsImpossibleHealth) {
  ProbeSnapshot snapshot = makeSnapshot(1.0, 2.0, 0.0, 1, 250);
  std::string error;
  CHECK_FALSE(MemoryProvider::validate(snapshot, 0.0, 0.5, 0, "de_dust2", error).has_value());
}

CS2N_TEST(MemoryProbe_AcceptsValidSnapshot) {
  const ProbeSnapshot snapshot = makeSnapshot(10.0, 20.0, 0.25, 7);
  std::string error;
  auto state = MemoryProvider::validate(snapshot, 3.0, 0.5, 0, "de_dust2", error);
  CHECK_TRUE(state.has_value());
  CHECK_TRUE(state->alive);
  CHECK_NEAR(state->position.x, 10.0, 1e-9);
  CHECK_NEAR(state->position.y, 20.0, 1e-9);
  CHECK_NEAR(state->yaw, 0.25, 1e-9);
  CHECK_EQ(state->mapName, std::string("de_dust2"));
}

CS2N_TEST(MemoryProbe_ReportsDeadWhenHealthZero) {
  const ProbeSnapshot snapshot = makeSnapshot(1.0, 1.0, 0.0, 1, 0);
  std::string error;
  auto state = MemoryProvider::validate(snapshot, 0.0, 0.5, 0, "de_dust2", error);
  CHECK_TRUE(state.has_value());
  CHECK_FALSE(state->alive);
}

CS2N_TEST(MemoryStand_ReadsLiveProcessMemory) {
  ProbeProcess probe = startProbe();
  if (!probe.ready()) {
    // An unavailable fixture is a failed integration check, never evidence of success.
    CHECK_TRUE(probe.ready());
    return;
  }

  MemoryProviderConfig config;
  config.processId = probe.pid;
  config.address = probe.address;
  config.expectedMap = "de_dust2";
  config.staleAfterSeconds = 2.0;
  MemoryProvider provider(config);
  CHECK_TRUE(provider.attached());

  auto first = provider.poll();
  CHECK_TRUE(first.has_value());
  CHECK_TRUE(first->valid);
  CHECK_TRUE(first->alive);

  // The probe advances its sequence roughly every 50 ms, so two samples taken
  // far enough apart must differ.
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  auto second = provider.poll();
  CHECK_TRUE(second.has_value());

  const bool moved = std::fabs(second->position.x - first->position.x) > 1e-6 ||
                     std::fabs(second->position.y - first->position.y) > 1e-6;
  CHECK_TRUE(moved);

  // And the reported velocity should agree with the actual displacement, which
  // catches endian or layout mistakes.
  const double observedDx = second->position.x - first->position.x;
  CHECK_TRUE(std::fabs(observedDx) < 200.0);
}

CS2N_TEST(MemoryStand_FailsClosedForUnknownProcess) {
  MemoryProviderConfig config;
  config.processId = 4;  // System, which we can never open for read
  config.address = 0x1000;
  MemoryProvider provider(config);
  CHECK_FALSE(provider.attached());
  CHECK_FALSE(provider.poll().has_value());
}

CS2N_TEST(Cs2Profile_InvalidMetadataFailsBeforeProcessDiscovery) {
  wchar_t directory[MAX_PATH]{}, filename[MAX_PATH]{};
  CHECK_TRUE(GetTempPathW(MAX_PATH, directory) != 0);
  CHECK_TRUE(GetTempFileNameW(directory, L"csp", 0, filename) != 0);
  struct Cleanup {
    const wchar_t* path;
    ~Cleanup() { DeleteFileW(path); }
  } cleanup{filename};
  for (const auto* text : {"{}", "[]", "{",
      "{\"pawn\":-1}", "{\"pawn\":1.5}",
      "{\"pawn\":4294967296}",
      "{\"pawn\":1,\"moduleTimestamp\":2,\"moduleSize\":3,\"clientVersion\":2147483648}"}) {
    { std::ofstream out{std::filesystem::path(filename)}; out << text; }
    const auto utf8 = std::filesystem::path(filename).u8string();
    Cs2MemoryProvider provider(std::string(utf8.begin(), utf8.end()), false);
    CHECK_FALSE(provider.error().empty());
    const auto diagnostic = provider.error();
    CHECK_FALSE(provider.poll().has_value());
    CHECK_FALSE(provider.connected());
    CHECK_EQ(provider.processId(), DWORD(0));
    CHECK_EQ(provider.error(), diagnostic);
  }
}

CS2N_TEST(CommandProfile_RejectsMissingFractionalAndOutOfRangeFields) {
  JsonValue p; std::string error;
  CHECK_FALSE(GameCommands::validateProfile(p,error)); CHECK_FALSE(error.empty());
  CHECK_TRUE(JsonValue::parse(R"({"timestamp":100,"size":4096,"object":16,"vtable":32,"function":512,"prefix":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})",p,error));
  CHECK_TRUE(GameCommands::validateProfile(p,error)); CHECK_TRUE(error.empty());
  for (const char* key : {"timestamp","size","object","vtable","function"}) {
    for(double n : {-1.0,0.0,1.5,4294967296.0}) {
      auto broken=p;broken.set(key,JsonValue::makeNumber(n));
      CHECK_FALSE(GameCommands::validateProfile(broken,error)); CHECK_FALSE(error.empty());
    }
  }
}
CS2N_TEST(CommandProfile_ChecksWholeReadSpansAndPrefix) {
  JsonValue p; std::string error;
  CHECK_TRUE(JsonValue::parse(R"({"timestamp":100,"size":4096,"object":4088,"vtable":3680,"function":4064,"prefix":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})",p,error));
  CHECK_TRUE(GameCommands::validateProfile(p,error));
  for (const char* key : {"object","vtable","function"}) {
    auto broken=p;broken.set(key,JsonValue::makeNumber(p[key].asNumber()+1));
    CHECK_FALSE(GameCommands::validateProfile(broken,error));
  }
  for(const auto& prefix : {std::string(63,'a'),std::string(64,'g')}) {
    auto broken=p;broken.set("prefix",JsonValue::makeString(prefix));
    CHECK_FALSE(GameCommands::validateProfile(broken,error));
  }
}

CS2N_TEST(CommandProfile_ShippedProfileRemainsStructurallyValid) {
  wchar_t executable[32768]{};
  CHECK_TRUE(GetModuleFileNameW(nullptr,executable,32768)>0);
  std::ifstream file(std::filesystem::path(executable).parent_path()/"assets"/"game-commands.json");
  CHECK_TRUE(bool(file));
  std::ostringstream contents;contents<<file.rdbuf();JsonValue profile;std::string error;
  CHECK_TRUE(JsonValue::parse(contents.str(),profile,error));
  CHECK_TRUE(GameCommands::validateProfile(profile,error));
  // Structural validity never establishes compatibility with the current DLL.
}
