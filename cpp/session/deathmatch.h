#pragma once
#include <cmath>
#include <cstdint>
#include <deque>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include "state/client_state.h"
namespace cs2n::session {
struct Binding {
  std::uint32_t pid=0; std::uint64_t created=0; std::uintptr_t window=0;
  bool valid()const{return pid && created && window;}
  bool operator==(const Binding&)const=default;
};
enum class Scene { Unknown, MainMenu, DeathmatchMenu, Search, Connecting, Loading, InMatch, MatchEnded, Error, PracticeMenu, PracticeDeathmatchMenu };
inline const char* name(Scene s){switch(s){
case Scene::MainMenu:return "MAIN_MENU";case Scene::DeathmatchMenu:return "DEATHMATCH_MENU";
case Scene::PracticeMenu:return "PRACTICE_MENU";case Scene::PracticeDeathmatchMenu:return "PRACTICE_DEATHMATCH_MENU";
case Scene::Search:return "MATCH_SEARCH";case Scene::Connecting:return "CONNECTING";case Scene::Loading:return "LOADING";
case Scene::InMatch:return "IN_MATCH";case Scene::MatchEnded:return "MATCH_ENDED";case Scene::Error:return "ERROR";default:return "UNKNOWN";}}
enum class Source { None, Visual, Accessibility, Gsi, Simulator };
struct Evidence { Scene scene=Scene::Unknown; Source source=Source::None; Binding binding; double at=-1,confidence=0; };
struct Inputs {
  Binding process; bool alive=false,windowOwned=false;
  Evidence ui; ClientStateSnapshot gsi; bool gsiBound=false;
};
struct Observation {Scene scene=Scene::Unknown;Source source=Source::None;double at=-1,confidence=0;std::string reason="No verified evidence";};
inline bool fresh(double at,double now,double ttl){return std::isfinite(at)&&std::isfinite(now)&&at>=0&&now>=at&&now-at<ttl;}
inline Observation observe(const Binding& target,const Inputs& in,double now,bool simulation=false,double ttl=2) {
  if(!target.valid()||!in.alive||in.process!=target||!in.windowOwned)return {Scene::Unknown,Source::None,-1,0,"Process/window identity not verified"};
  Observation ui;
  if(in.ui.binding==target&&fresh(in.ui.at,now,ttl)&&std::isfinite(in.ui.confidence)&&in.ui.confidence>=.95&&in.ui.confidence<=1&&
     (in.ui.source==Source::Visual||in.ui.source==Source::Accessibility||(simulation&&in.ui.source==Source::Simulator)))
    ui={in.ui.scene,in.ui.source,in.ui.at,in.ui.confidence,"Verified UI evidence"};
  Observation gsi;
  if(in.gsiBound&&in.gsi.gsi==GsiHealth::Fresh&&fresh(in.gsi.receivedAt,now,ttl)) {
    if(in.gsi.state==ClientState::IN_MATCH)gsi={Scene::InMatch,Source::Gsi,in.gsi.receivedAt,1,"Bound GSI match context"};
    if(in.gsi.state==ClientState::MATCH_ENDED)gsi={Scene::MatchEnded,Source::Gsi,in.gsi.receivedAt,1,"Bound GSI gameover"};
  }
  if(ui.scene!=Scene::Unknown&&gsi.scene!=Scene::Unknown&&ui.scene!=gsi.scene)return {Scene::Unknown,Source::None,-1,0,"Conflicting sources"};
  return ui.scene!=Scene::Unknown?ui:gsi;
}
enum class Phase { WaitClient,WaitMenu,SelectDm,StartSearch,WaitSearch,WaitConnection,WaitMatch,Ready,Error,Stopped };
inline const char* name(Phase p){switch(p){case Phase::WaitClient:return "WAIT_FOR_CLIENT";case Phase::WaitMenu:return "WAIT_FOR_MENU";
case Phase::SelectDm:return "SELECT_DEATHMATCH";case Phase::StartSearch:return "START_SEARCH";case Phase::WaitSearch:return "WAIT_FOR_SEARCH";
case Phase::WaitConnection:return "WAIT_FOR_CONNECTION";case Phase::WaitMatch:return "WAIT_FOR_MATCH";case Phase::Ready:return "READY";
case Phase::Error:return "ERROR";default:return "STOPPED";}}
enum class Action { SelectDeathmatch,StartSearch,OpenPractice,SelectPracticeDeathmatch,StartPractice };
struct Plan {std::uint64_t id;Binding target;Action action;unsigned attempt;double evidenceAt;};
enum class MatchKind { Network, Practice };
struct Config {double timeout=30,retryAfter=5;unsigned maxAttempts=2;MatchKind kind=MatchKind::Network;};
// No execution callback: this sink records intentions and cannot send input.
class DryRun {
 public:
  void record(const Plan& p){if(plans_.size()==128)plans_.pop_front();plans_.push_back(p);}
  const std::deque<Plan>& plans()const{return plans_;}
  static constexpr bool realCommandsSupported=false;
 private:std::deque<Plan> plans_;
};
class Controller {
 public:
  explicit Controller(Config c={}):config_(c){if(!std::isfinite(c.timeout)||!std::isfinite(c.retryAfter)||c.timeout<=0||c.retryAfter<=0||!c.maxAttempts)throw std::invalid_argument("Invalid controller limits");}
  void start(Binding target,double now){if(!std::isfinite(now)||now<0)throw std::invalid_argument("Invalid clock");target_=target;pending_.reset();observation_={};phase_=Phase::Stopped;lastNow_=now;change(Phase::WaitClient,now,"DRY_RUN started");}
  void stop(){pending_.reset();observation_={};phase_=Phase::Stopped;log("STOPPED: user stop; no commands dispatched");}
  std::optional<Plan> tick(const Inputs& in,double now,bool simulation=false){
    if(phase_==Phase::Stopped||phase_==Phase::Error)return {};
    if(!std::isfinite(now)||now<lastNow_){fail("Non-monotonic clock",lastNow_);return {}; } lastNow_=now;
    if(!in.alive){observation_={};if(phase_!=Phase::WaitClient){fail("Bound client exited",now);return {};}if(now>=deadline_)fail("Client wait timeout",now);return {};}
    if(!target_.valid()||in.process!=target_||!in.windowOwned){observation_={};fail("Identity changed; refusing rebind",now);return {};}
    observation_=observe(target_,in,now,simulation);
    const auto scene=observation_.scene;
    if(scene==Scene::Error){fail("Verified UI error",now);return {};}
    if(config_.kind==MatchKind::Practice&&(scene==Scene::DeathmatchMenu||scene==Scene::Search)){
      fail("Network matchmaking UI is not allowed in practice mode",now);return {};
    }
    if(scene==Scene::InMatch){pending_.reset();change(Phase::Ready,now,"Match confirmed");return {};}
    if(scene==Scene::MatchEnded){pending_.reset();phase_=Phase::Stopped;log("STOPPED: match ended; no automatic requeue");return {};}
    if(phase_==Phase::Ready){change(Phase::WaitMatch,now,"Match evidence lost; observe only");}
    if(phase_==Phase::WaitClient)change(Phase::WaitMenu,now,"Client identity verified");
    if(now>=deadline_){fail("Transition timeout",now);return {};}
    if(scene==Scene::Unknown)return {}; // preserve deadline, never infer disconnect/menu
    if(scene==Scene::Search){pending_.reset();change(Phase::WaitConnection,now,"Search observed");return {};}
    if(scene==Scene::Connecting||scene==Scene::Loading){pending_.reset();change(Phase::WaitMatch,now,"Connection/loading observed");return {};}
    if(pending_){
      const bool confirmed=(pending_->action==Action::SelectDeathmatch&&scene==Scene::DeathmatchMenu)||
        (pending_->action==Action::OpenPractice&&scene==Scene::PracticeMenu)||
        (pending_->action==Action::SelectPracticeDeathmatch&&scene==Scene::PracticeDeathmatchMenu);
      if(confirmed){pending_.reset();change(Phase::StartSearch,now,"Deathmatch selection observed");}
      else {
        const bool same=scene==precondition(pending_->action);
        if(same&&now-plannedAt_>=config_.retryAfter&&observation_.at>pending_->evidenceAt){
          if(pending_->attempt>=config_.maxAttempts){fail("Retry limit",now);return {};}
          ++pending_->attempt;pending_->id=++sequence_;pending_->evidenceAt=observation_.at;plannedAt_=now;
          log("DRY_RUN retry planned; zero input");return pending_;
        }
        return {};
      }
    }
    if(config_.kind==MatchKind::Practice){
      if(phase_==Phase::WaitMenu||phase_==Phase::StartSearch){
        if(scene==Scene::MainMenu)return plan(Action::OpenPractice,now);
        if(scene==Scene::PracticeMenu)return plan(Action::SelectPracticeDeathmatch,now);
        if(scene==Scene::PracticeDeathmatchMenu)return plan(Action::StartPractice,now);
      }
      return {};
    }
    if(scene==Scene::MainMenu&&phase_==Phase::WaitMenu)return plan(Action::SelectDeathmatch,now);
    if(scene==Scene::DeathmatchMenu&&(phase_==Phase::WaitMenu||phase_==Phase::StartSearch))return plan(Action::StartSearch,now);
    return {};
  }
  Phase phase()const{return phase_;} const Observation& observation()const{return observation_;}
  const std::deque<std::string>& logs()const{return logs_;}
 private:
  Config config_;Binding target_;Phase phase_=Phase::Stopped;Observation observation_;
  static Scene precondition(Action a){switch(a){
    case Action::SelectDeathmatch:case Action::OpenPractice:return Scene::MainMenu;
    case Action::StartSearch:return Scene::DeathmatchMenu;
    case Action::SelectPracticeDeathmatch:return Scene::PracticeMenu;
    case Action::StartPractice:return Scene::PracticeDeathmatchMenu;
    }return Scene::Unknown;
  }
  double deadline_=0,lastNow_=0,plannedAt_=0;std::uint64_t sequence_=0;std::optional<Plan> pending_;std::deque<std::string> logs_;
  void log(std::string s){if(logs_.size()==128)logs_.pop_front();logs_.push_back(std::move(s));}
  void change(Phase p,double now,const char* reason){if(phase_!=p){phase_=p;deadline_=now+config_.timeout;log(std::string(name(p))+": "+reason);}}
  void fail(const char* reason,double now){pending_.reset();change(Phase::Error,now,reason);}
  std::optional<Plan> plan(Action a,double now){
    change(a==Action::SelectDeathmatch?Phase::SelectDm:Phase::StartSearch,now,"DRY_RUN intention only");
    pending_=Plan{++sequence_,target_,a,1,observation_.at};plannedAt_=now;
    switch(a){
      case Action::SelectDeathmatch:log("DRY_RUN select Deathmatch; zero input");break;
      case Action::StartSearch:log("DRY_RUN start search; zero input");break;
      case Action::OpenPractice:log("DRY_RUN open practice menu; zero input");break;
      case Action::SelectPracticeDeathmatch:log("DRY_RUN select practice Deathmatch; zero input");break;
      case Action::StartPractice:log("DRY_RUN start local practice; zero input");break;
    }
    if(a==Action::StartSearch)change(Phase::WaitSearch,now,"Await external confirmation, not a fake success");
    if(a==Action::StartPractice)change(Phase::WaitMatch,now,"Await practice loading, not a fake success");
    return pending_;
  }
};
}
