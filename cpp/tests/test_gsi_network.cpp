#include <winsock2.h>
#include <ws2tcpip.h>
#include <chrono>
#include <memory>
#include <thread>
#include "state/gsi_provider.h"
#include "tests/framework.h"
using namespace cs2n;
namespace {
struct Socket {
  SOCKET value=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
  ~Socket(){if(value!=INVALID_SOCKET)closesocket(value);}
  bool connectTo(unsigned short port){sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(port);a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);return connect(value,reinterpret_cast<sockaddr*>(&a),sizeof(a))==0;}
};
GsiConfig config(){GsiConfig c;c.port=0;c.token="test-only";return c;}
}
CS2N_TEST(GsiNetwork_StopDuringSlowHeaderIsBounded) {
  auto provider=std::make_unique<GsiProvider>(config());
  CHECK_TRUE(provider->listening());
  Socket client; CHECK_TRUE(client.connectTo(provider->port()));
  std::jthread sender([&](std::stop_token stop){while(!stop.stop_requested()){if(send(client.value,"x",1,0)!=1)break;std::this_thread::sleep_for(std::chrono::milliseconds(40));}});
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  const auto begin=std::chrono::steady_clock::now();
  provider.reset();
  const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
  sender.request_stop(); sender.join();
  CHECK_TRUE(seconds<1.5);
}
CS2N_TEST(GsiNetwork_RejectsTruncatedBody) {
  GsiProvider provider(config()); CHECK_TRUE(provider.listening());
  Socket client; CHECK_TRUE(client.connectTo(provider.port()));
  const std::string body=R"({"auth":{"token":"test-only"},"provider":{"appid":730,"steamid":"test"},"map":{"name":"de_dust2","phase":"live"},"player":{"steamid":"test","activity":"playing","state":{"health":100}}})";
  const auto request="POST / HTTP/1.1\r\nContent-Length: "+std::to_string(body.size()+1)+"\r\n\r\n"+body;
  send(client.value,request.data(),static_cast<int>(request.size()),0);shutdown(client.value,SD_SEND);
  DWORD timeout=2000;setsockopt(client.value,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<char*>(&timeout),sizeof(timeout));
  char response[512]{}; const int received=recv(client.value,response,sizeof(response),0);
  CHECK_TRUE(received>0);
  CHECK_TRUE(std::string(response,received>0?received:0).find("400")!=std::string::npos);
  CHECK_FALSE(provider.poll().has_value());
}
CS2N_TEST(GsiNetwork_RepeatedLifetimeReleasesSockets) {
  { GsiProvider warmup(config()); }
  DWORD before=0,after=0;
  CHECK_TRUE(GetProcessHandleCount(GetCurrentProcess(),&before)!=FALSE);
  for(int i=0;i<10;++i){GsiProvider provider(config());CHECK_TRUE(provider.listening());CHECK_TRUE(provider.port()!=0);}
  CHECK_TRUE(GetProcessHandleCount(GetCurrentProcess(),&after)!=FALSE);
  CHECK_TRUE(after<=before+2);
}

namespace {
std::string post(GsiProvider& provider, const std::string& body) {
  Socket client;
  if(!client.connectTo(provider.port())) { CHECK_TRUE(false); return {}; }
  DWORD timeout=2000;
  setsockopt(client.value,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<char*>(&timeout),sizeof(timeout));
  setsockopt(client.value,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<char*>(&timeout),sizeof(timeout));
  const std::string request="POST / HTTP/1.1\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;
  std::size_t sent=0;
  while(sent<request.size()) {
    const int n=send(client.value,request.data()+sent,static_cast<int>(request.size()-sent),0);
    if(n<=0){CHECK_TRUE(false);return {};}
    sent+=static_cast<std::size_t>(n);
  }
  shutdown(client.value,SD_SEND);
  std::string response; char buffer[512]; int received;
  while((received=recv(client.value,buffer,sizeof(buffer),0))>0) response.append(buffer,received);
  return response;
}
std::string lifecyclePayload(const std::string& token="test-only",const std::string& owner="owner",const std::string& phase="live") {
  return "{\"auth\":{\"token\":\""+token+"\"},\"provider\":{\"appid\":730,\"steamid\":\""+owner+
    "\"},\"map\":{\"name\":\"de_inferno\",\"phase\":\""+phase+"\"}}";
}
}
CS2N_TEST(GsiNetwork_ClientStateUsesAuthenticatedSnapshotsAcrossMaps) {
  auto c=config();c.expectedSteamId="owner"; // Old player provider still requires Dust II.
  GsiProvider provider(c); CHECK_TRUE(provider.listening());
  CHECK_TRUE(post(provider,lifecyclePayload()).find("200")!=std::string::npos);
  CHECK_TRUE(provider.clientState(ProcessPresence::Running).state==ClientState::IN_MATCH);
  CHECK_FALSE(provider.poll().has_value()); // Lifecycle must not depend on player-ready gates.
  CHECK_TRUE(post(provider,lifecyclePayload("bad")).find("403")!=std::string::npos);
  CHECK_TRUE(post(provider,lifecyclePayload("test-only","other")).find("403")!=std::string::npos);
  CHECK_TRUE(post(provider,"{bad").find("400")!=std::string::npos);
  CHECK_TRUE(provider.clientState(ProcessPresence::Running).state==ClientState::IN_MATCH);
  CHECK_TRUE(post(provider,lifecyclePayload("test-only","owner","gameover")).find("200")!=std::string::npos);
  CHECK_TRUE(provider.clientState(ProcessPresence::Running).state==ClientState::MATCH_ENDED);
}
CS2N_TEST(GsiNetwork_ClientStateRecoversAfterTransportLoss) {
  auto c=config();c.staleAfterSeconds=.5;
  GsiProvider provider(c); CHECK_TRUE(provider.listening());
  CHECK_TRUE(post(provider,lifecyclePayload()).find("200")!=std::string::npos);
  std::this_thread::sleep_for(std::chrono::milliseconds(650));
  CHECK_TRUE(provider.clientState(ProcessPresence::Running).gsi==GsiHealth::Stale);
  CHECK_TRUE(provider.clientState(ProcessPresence::Running).state==ClientState::UNKNOWN);
  CHECK_TRUE(post(provider,lifecyclePayload()).find("200")!=std::string::npos);
  CHECK_TRUE(provider.clientState(ProcessPresence::Running).state==ClientState::IN_MATCH);
}

CS2N_TEST(GsiNetwork_BindFailureIsNotGameError) {
  GsiProvider first(config()); CHECK_TRUE(first.listening());
  auto c=config(); c.port=first.port();
  GsiProvider second(c);
  CHECK_FALSE(second.listening());
  const auto result=second.clientState(ProcessPresence::Running);
  CHECK_TRUE(result.state==ClientState::UNKNOWN);
  CHECK_TRUE(result.gsi==GsiHealth::Unavailable);
}

CS2N_TEST(GsiNetwork_HealthDeathRespawnMissingFieldsAndExpiry) {
  auto c=config(); c.expectedSteamId="owner"; c.staleAfterSeconds=.5;
  GsiProvider provider(c); CHECK_TRUE(provider.listening());
  const auto payload=[](const std::string& health) {
    return std::string(R"({"auth":{"token":"test-only"},"provider":{"appid":730,"steamid":"owner"},"map":{"name":"de_dust2","phase":"live"},"player":{"steamid":"owner","activity":"playing","state":{)") + health + "}}}";
  };
  for (int hp : {100,37,0,100}) {
    CHECK_TRUE(post(provider,payload("\"health\":"+std::to_string(hp))).find("200")!=std::string::npos);
    auto state=provider.poll(); CHECK_TRUE(state.has_value());
    if(state) { CHECK_EQ(state->health,hp); CHECK_EQ(state->alive,hp>0); }
  }
  CHECK_TRUE(post(provider,payload("")).find("200")!=std::string::npos);
  CHECK_FALSE(provider.poll().has_value());
  CHECK_TRUE(provider.clientState(ProcessPresence::Running).state==ClientState::IN_MATCH);
  CHECK_TRUE(post(provider,payload("\"health\":100")).find("200")!=std::string::npos);
  std::this_thread::sleep_for(std::chrono::milliseconds(650));
  CHECK_FALSE(provider.poll().has_value());
  CHECK_TRUE(provider.clientState(ProcessPresence::Running).gsi==GsiHealth::Stale);
  CHECK_TRUE(provider.clientState(ProcessPresence::Running).state==ClientState::UNKNOWN);
  CHECK_TRUE(post(provider,payload("\"health\":100")).find("200")!=std::string::npos);
  CHECK_TRUE(provider.poll().has_value());
  CHECK_TRUE(post(provider,R"({"auth":{"token":"test-only"},"provider":{"appid":730,"steamid":"owner"}})").find("200")!=std::string::npos);
  CHECK_FALSE(provider.poll().has_value());
  CHECK_TRUE(provider.clientState(ProcessPresence::Running).state==ClientState::UNKNOWN);
}
