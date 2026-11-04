#include "state/gsi_provider.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <vector>

#include "core/json.h"
#include "core/log.h"
#include "core/types.h"

namespace cs2n {
namespace {

double monotonicSeconds() {
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}

bool constantTimeEqual(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  unsigned char diff = 0;
  for (std::size_t i = 0; i < a.size(); ++i) diff |= static_cast<unsigned char>(a[i] ^ b[i]);
  return diff == 0;
}

bool validSource(const JsonValue& payload, const GsiConfig& config, double now) {
  const auto& provider = payload["provider"];
  const auto owner = provider["steamid"].asString();
  return std::isfinite(now) && now >= 0 && provider["appid"].asNumber(-1) == 730 &&
      !owner.empty() && (config.expectedSteamId.empty() || owner == config.expectedSteamId);
}

constexpr std::size_t kMaxBody = 1 << 17;  // 128 KiB, same budget as the Python version

}  // namespace

GsiProvider::GsiProvider(GsiConfig config) : config_(std::move(config)),
    clientState_(config_.staleAfterSeconds, config_.expectedSteamId) {
  WSADATA data{};
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
    Logger::instance().error("GSI: WSAStartup failed");
    return;
  }
  winsockStarted_ = true;
  listenSocket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listenSocket_ == INVALID_SOCKET) {
    Logger::instance().error("GSI: socket() failed");
    return;
  }
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(config_.port);
  inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
  if (bind(listenSocket_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    Logger::instance().error(format("GSI: bind() to 127.0.0.1:%u failed (%d)", config_.port, WSAGetLastError()));
    closesocket(listenSocket_);
    listenSocket_ = INVALID_SOCKET;
    return;
  }
  if (listen(listenSocket_, 8) != 0) {
    Logger::instance().error("GSI: listen() failed");
    closesocket(listenSocket_);
    listenSocket_ = INVALID_SOCKET;
    return;
  }
  u_long nonBlocking = 1;
  if (ioctlsocket(listenSocket_, FIONBIO, &nonBlocking) != 0) {
    closesocket(listenSocket_);
    listenSocket_ = INVALID_SOCKET;
    return;
  }
  running_ = true;
  listening_ = true;
  int addressSize = sizeof(address);
  if (getsockname(listenSocket_, reinterpret_cast<sockaddr*>(&address), &addressSize) == 0)
    config_.port = ntohs(address.sin_port);
  try { thread_ = std::thread([this] { serveLoop(); }); }
  catch (...) {
    running_ = false; listening_ = false;
    closesocket(listenSocket_); listenSocket_ = INVALID_SOCKET;
    WSACleanup(); winsockStarted_ = false;
    throw;
  }
  Logger::instance().info(format("GSI: listening on 127.0.0.1:%u", config_.port));
}

GsiProvider::~GsiProvider() {
  running_ = false;
  if (thread_.joinable()) thread_.join();
  if (listenSocket_ != INVALID_SOCKET) {
    closesocket(listenSocket_);
    listenSocket_ = INVALID_SOCKET;
  }
  if (winsockStarted_) WSACleanup();
}

std::string GsiProvider::lastRejection() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return lastRejection_;
}

ClientStateSnapshot GsiProvider::clientState() {
  return clientState(detectCs2Presence());
}

ClientStateSnapshot GsiProvider::clientState(ProcessPresence process) {
  std::lock_guard<std::mutex> lock(mutex_);
  return clientState_.snapshot(monotonicSeconds(), process, listening_);
}

std::optional<PlayerState> GsiProvider::poll() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!state_.valid) return std::nullopt;
  if (monotonicSeconds() - state_.timestamp > config_.staleAfterSeconds) {
    return std::nullopt;
  }
  return state_;
}

void GsiProvider::serveLoop() {
  std::vector<char> buffer(8192);
  while (running_) {
    sockaddr_in peer{};
    int peerLength = sizeof(peer);
    const SOCKET client = accept(listenSocket_, reinterpret_cast<sockaddr*>(&peer), &peerLength);
    if (client == INVALID_SOCKET) {
      if (!running_) break;
      Sleep(20);
      continue;
    }
    u_long blocking = 0;
    if (ioctlsocket(client, FIONBIO, &blocking) != 0) { closesocket(client); continue; }
    DWORD timeout=1000;
    if (setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) != 0 ||
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) != 0) {
      closesocket(client); continue;
    }
    const double deadline = monotonicSeconds() + 2.0;

    std::string request;
    const std::size_t headerLimit = 8192;
    std::size_t bodyStart = std::string::npos;
    std::size_t contentLength = 0;
    while (running_ && monotonicSeconds() < deadline && request.size() < headerLimit) {
      const int received = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
      if (received <= 0) break;
      request.append(buffer.data(), static_cast<std::size_t>(received));
      bodyStart = request.find("\r\n\r\n");
      if (bodyStart != std::string::npos) {
        const std::string head = request.substr(0, bodyStart);
        const std::string lower = [&] {
          std::string copy = head;
          for (char& c : copy) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
          return copy;
        }();
        const std::size_t lengthPos = lower.find("content-length:");
        if (lengthPos != std::string::npos) {
          contentLength = static_cast<std::size_t>(std::strtoull(head.substr(lengthPos + 15).c_str(), nullptr, 10));
        }
        break;
      }
    }
    int status = 400;
    std::string reason = "bad request";
    std::string body;
    if (running_ && bodyStart != std::string::npos && bodyStart <= headerLimit && contentLength <= kMaxBody) {
      body = request.substr(bodyStart + 4);
      while (running_ && monotonicSeconds() < deadline && body.size() < contentLength) {
        const int received = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (received <= 0) break;
        body.append(buffer.data(), static_cast<std::size_t>(received));
      }
      std::string method = request.substr(0, request.find(' '));
      std::string path = "/";
      const std::size_t space = request.find(' ');
      if (space != std::string::npos) {
        const std::size_t second = request.find(' ', space + 1);
        if (second != std::string::npos) path = request.substr(space + 1, second - space - 1);
      }
      if (running_ && body.size() == contentLength)
        handleRequest(method, path, body, status, reason);
    }

    const std::string response =
        "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n\r\n";
    if (running_) send(client, response.data(), static_cast<int>(response.size()), 0);
    closesocket(client);
  }
  listening_ = false;
}

bool GsiProvider::handleRequest(const std::string& method, const std::string& path, const std::string& body,
                                int& status, std::string& reason) {
  if (method != "POST") {
    status = 405;
    reason = "method not allowed";
    return false;
  }
  if (path != "/" && path != "/state") {
    status = 404;
    reason = "not found";
    return false;
  }
  JsonValue payload;
  std::string error;
  if (!JsonValue::parse(body, payload, error)) {
    std::lock_guard<std::mutex> lock(mutex_);
    lastRejection_ = "malformed JSON: " + error;
    status = 400;
    reason = "bad json";
    return false;
  }
  const std::string supplied = payload["auth"]["token"].asString();
  if (config_.token.empty() || !constantTimeEqual(supplied, config_.token)) {
    std::lock_guard<std::mutex> lock(mutex_);
    lastRejection_ = "token mismatch";
    status = 403;
    reason = "forbidden";
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!clientState_.accept(payload, monotonicSeconds())) {
      lastRejection_ = "invalid GSI source identity";
      status = 403; reason = "forbidden"; return false;
    }
  }
  applyPayload(body);
  status = 200;
  reason = "ok";
  return true;
}

GsiProvider::Evaluation GsiProvider::evaluatePayload(const std::string& body, const GsiConfig& config, double now) {
  Evaluation result;
  JsonValue payload;
  std::string error;
  if (!JsonValue::parse(body, payload, error)) {
    result.rejection = "malformed JSON: " + error;
    return result;
  }

  const JsonValue& provider = payload["provider"];
  if (!validSource(payload, config, now)) {
    result.rejection = "invalid GSI source identity or timestamp";
    return result;
  }
  const std::string mapName = payload["map"]["name"].asString();
  if (mapName.empty() || (!config.requiredMap.empty() && mapName != config.requiredMap)) {
    result.rejection = "map is " + (mapName.empty() ? std::string("unknown") : mapName) + ", need " + config.requiredMap;
    return result;
  }
  const std::string phase = payload["map"]["phase"].asString();
  if (phase != "live" && phase != "warmup") {
    result.rejection = "match phase is " + (phase.empty() ? std::string("unknown") : phase);
    return result;
  }
  if(payload["player"]["steamid"].asString()!=provider["steamid"].asString()) {result.rejection="spectating another player";return result;}
  const std::string activity = payload["player"]["activity"].asString();
  if (activity != "playing") {
    result.rejection = "player activity is " + (activity.empty() ? std::string("unknown") : activity);
    return result;
  }
  const double health = payload["player"]["state"]["health"].asNumber(-1.0);
  if (!std::isfinite(health) || health < 0.0 || health > 500 || std::floor(health) != health) {
    result.rejection = "invalid health";
    return result;
  }

  result.accepted = true;
  result.state.valid = true;
  result.state.alive = health>0;
  result.state.health = static_cast<int>(health);
  result.state.mapName = mapName;
  result.state.timestamp = now;
  return result;
}

MatchSetupState GsiProvider::evaluateMatchSetup(const std::string& body,const GsiConfig& config,double now) {
  JsonValue p;std::string error;
  if(!JsonValue::parse(body,p,error)||!validSource(p,config,now))return {};
  const auto owner=p["provider"]["steamid"].asString(),player=p["player"]["steamid"].asString();
  if(owner.empty()||(!player.empty()&&player!=owner))return {};
  const auto map=p["map"]["name"].asString(),phase=p["map"]["phase"].asString();
  if(map.empty()||(!config.requiredMap.empty()&&map!=config.requiredMap)||p["map"]["mode"].asString()!="deathmatch"||
     (phase!="live"&&phase!="warmup"))return {};
  const auto team=p["player"]["team"].asString();
  if(!team.empty()&&team!="T"&&team!="CT")return {};
  return {true,team=="T"?2:team=="CT"?3:0,now,map};
}

MatchSetupState GsiProvider::matchSetup() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return setup_.valid&&monotonicSeconds()-setup_.timestamp<=config_.staleAfterSeconds?setup_:MatchSetupState{};
}

void GsiProvider::applyPayload(const std::string& body) {
  const Evaluation evaluation = evaluatePayload(body, config_, monotonicSeconds());
  std::lock_guard<std::mutex> lock(mutex_);
  setup_=evaluateMatchSetup(body,config_,monotonicSeconds());
  if (!evaluation.accepted) {
    lastRejection_ = evaluation.rejection;
    state_.valid=false;
    return;
  }
  state_ = evaluation.state;
  lastRejection_ = "accepted";
  ++sequence_;
}

}  // namespace cs2n
