#include "session/deathmatch.h"
#include "session/practice_vision.h"
#include "session/launch_confirmation.h"
#include "tests/framework.h"
#include <limits>
using namespace cs2n;
using namespace cs2n::session;
CS2N_TEST(Launch_RequiresNewMatchPacket){
 ClientStateSnapshot s{ClientState::IN_MATCH,GsiHealth::Fresh,"",10};
 CHECK_TRUE(confirmsLaunchedMatch(s,9));
 CHECK_FALSE(confirmsLaunchedMatch(s,10));
 CHECK_FALSE(confirmsLaunchedMatch(s,11));
 s.receivedAt=-1;CHECK_FALSE(confirmsLaunchedMatch(s,-2));
}
CS2N_TEST(Launch_RejectsStaleAndAbsentData){
 for(auto health:{GsiHealth::NoData,GsiHealth::Stale,GsiHealth::Unavailable})
 CHECK_FALSE(confirmsLaunchedMatch({ClientState::IN_MATCH,health,"",10},9));
}
CS2N_TEST(Launch_UnknownSearchAndLoadingDoNotStartEngine){
 for(auto state:{ClientState::UNKNOWN,ClientState::MAIN_MENU,ClientState::MATCH_SEARCH,
   ClientState::CONNECTING,ClientState::LOADING,ClientState::MATCH_ENDED,ClientState::DISCONNECTED,ClientState::Error})
 CHECK_FALSE(confirmsLaunchedMatch({state,GsiHealth::Fresh,"",10},9));
}
namespace {
const Binding target{42,100,55};
Inputs input(Scene scene,double at=0){Inputs i;i.process=target;i.alive=i.windowOwned=true;i.ui={scene,Source::Simulator,target,at,1};return i;}
}
CS2N_TEST(Session_NoClientTimeout){Controller c;c.start(target,0);CHECK_FALSE(c.tick({},1,true));CHECK_TRUE(c.phase()==Phase::WaitClient);c.tick({},30,true);CHECK_TRUE(c.phase()==Phase::Error);}
CS2N_TEST(Session_ExplicitRestartResetsDeadline){Controller c;c.start(target,0);c.start(target,20);c.tick({},31,true);CHECK_TRUE(c.phase()==Phase::WaitClient);c.tick({},50,true);CHECK_TRUE(c.phase()==Phase::Error);}
CS2N_TEST(Session_FullSimulatedFlow){Controller c;DryRun sink;c.start(target,0);auto p=c.tick(input(Scene::MainMenu),0,true);CHECK_TRUE(p.has_value());sink.record(*p);CHECK_TRUE(p->action==Action::SelectDeathmatch);p=c.tick(input(Scene::DeathmatchMenu,1),1,true);CHECK_TRUE(p.has_value());sink.record(*p);CHECK_TRUE(p->action==Action::StartSearch);CHECK_TRUE(c.phase()==Phase::WaitSearch);c.tick(input(Scene::Search,2),2,true);CHECK_TRUE(c.phase()==Phase::WaitConnection);c.tick(input(Scene::Connecting,3),3,true);CHECK_TRUE(c.phase()==Phase::WaitMatch);c.tick(input(Scene::Loading,4),4,true);CHECK_TRUE(c.phase()==Phase::WaitMatch);c.tick(input(Scene::InMatch,5),5,true);CHECK_TRUE(c.phase()==Phase::Ready);c.tick(input(Scene::MatchEnded,6),6,true);CHECK_TRUE(c.phase()==Phase::Stopped);CHECK_TRUE(sink.plans().size()==2);CHECK_FALSE(DryRun::realCommandsSupported);}
CS2N_TEST(Session_UnknownNeverActs){Controller c;c.start(target,0);CHECK_FALSE(c.tick(input(Scene::Unknown),0,true));CHECK_TRUE(c.phase()==Phase::WaitMenu);c.tick(input(Scene::Unknown,30),30,true);CHECK_TRUE(c.phase()==Phase::Error);}
CS2N_TEST(Session_SimulatorEvidenceRejectedInLiveMode){CHECK_TRUE(observe(target,input(Scene::MainMenu),0).scene==Scene::Unknown);}
CS2N_TEST(Session_StaleFutureLowConfidenceRejected){auto i=input(Scene::MainMenu);CHECK_TRUE(observe(target,i,2,true).scene==Scene::Unknown);i.ui.at=3;CHECK_TRUE(observe(target,i,2,true).scene==Scene::Unknown);i.ui.at=2;i.ui.confidence=.9;CHECK_TRUE(observe(target,i,2,true).scene==Scene::Unknown);i.ui.confidence=std::numeric_limits<double>::quiet_NaN();CHECK_TRUE(observe(target,i,2,true).scene==Scene::Unknown);}
CS2N_TEST(Session_IdentityCannotRebind){for(int field=0;field<4;++field){Controller c;c.start(target,0);auto i=input(Scene::MainMenu);if(field==0)++i.process.pid;if(field==1)++i.process.created;if(field==2)++i.process.window;if(field==3)i.windowOwned=false;CHECK_FALSE(c.tick(i,0,true));CHECK_TRUE(c.phase()==Phase::Error);}}
CS2N_TEST(Session_UiEvidenceMustMatchTarget){auto i=input(Scene::MainMenu);++i.ui.binding.pid;CHECK_TRUE(observe(target,i,0,true).scene==Scene::Unknown);}
CS2N_TEST(Session_GsiMustBeBound){auto i=input(Scene::Unknown);i.gsi={ClientState::IN_MATCH,GsiHealth::Fresh,"",0};CHECK_TRUE(observe(target,i,0).scene==Scene::Unknown);i.gsiBound=true;CHECK_TRUE(observe(target,i,0).scene==Scene::InMatch);}
CS2N_TEST(Session_GsiLossRecovery){Controller c;c.start(target,0);auto i=input(Scene::Unknown);i.gsiBound=true;i.gsi={ClientState::IN_MATCH,GsiHealth::Fresh,"",0};c.tick(i,0);CHECK_TRUE(c.phase()==Phase::Ready);i.gsi.gsi=GsiHealth::Unavailable;c.tick(i,1);CHECK_TRUE(c.observation().scene==Scene::Unknown);CHECK_TRUE(c.phase()==Phase::WaitMatch);i.gsi={ClientState::IN_MATCH,GsiHealth::Fresh,"",2};c.tick(i,2);CHECK_TRUE(c.phase()==Phase::Ready);}
CS2N_TEST(Session_GsiCannotInventMenuOrSearch){auto i=input(Scene::Unknown);i.gsiBound=true;for(auto state:{ClientState::MAIN_MENU,ClientState::MATCH_SEARCH,ClientState::CONNECTING,ClientState::LOADING,ClientState::DISCONNECTED,ClientState::UNKNOWN}){i.gsi={state,GsiHealth::Fresh,"",0};CHECK_TRUE(observe(target,i,0).scene==Scene::Unknown);}}
CS2N_TEST(Session_ConflictingSourcesUnknown){auto i=input(Scene::MainMenu);i.gsiBound=true;i.gsi={ClientState::IN_MATCH,GsiHealth::Fresh,"",0};CHECK_TRUE(observe(target,i,0,true).scene==Scene::Unknown);}
CS2N_TEST(Session_RetriesNeedFreshPrecondition){Controller c({30,5,2});c.start(target,0);CHECK_TRUE(c.tick(input(Scene::MainMenu),0,true).has_value());CHECK_FALSE(c.tick(input(Scene::MainMenu),5,true));auto p=c.tick(input(Scene::MainMenu,6),6,true);CHECK_TRUE(p&&p->attempt==2);CHECK_FALSE(c.tick(input(Scene::Unknown,12),12,true));CHECK_FALSE(c.tick(input(Scene::MainMenu,13),13,true));CHECK_TRUE(c.phase()==Phase::Error);}
CS2N_TEST(Session_StopPreventsFurtherPlans){Controller c;c.start(target,0);c.tick(input(Scene::MainMenu),0,true);c.stop();CHECK_FALSE(c.tick(input(Scene::DeathmatchMenu,1),1,true));CHECK_TRUE(c.phase()==Phase::Stopped);CHECK_TRUE(c.observation().scene==Scene::Unknown);}
CS2N_TEST(Session_ExitAndClockRegressionFailClosed){Controller c;c.start(target,2);c.tick(input(Scene::Unknown,2),2,true);c.tick({},3,true);CHECK_TRUE(c.phase()==Phase::Error);c.start(target,4);c.tick(input(Scene::MainMenu,3),3,true);CHECK_TRUE(c.phase()==Phase::Error);}
CS2N_TEST(Session_ConfirmedError){Controller c;c.start(target,0);c.tick(input(Scene::Error),0,true);CHECK_TRUE(c.phase()==Phase::Error);}
CS2N_TEST(Session_SameSearchCannotExtendTimeout){Controller c;c.start(target,0);c.tick(input(Scene::Search),0,true);c.tick(input(Scene::Search,29),29,true);CHECK_TRUE(c.phase()==Phase::WaitConnection);c.tick(input(Scene::Search,30),30,true);CHECK_TRUE(c.phase()==Phase::Error);}
CS2N_TEST(Session_InvalidLimitsRejected){bool caught=false;try{Controller c({0,5,2});}catch(const std::invalid_argument&){caught=true;}CHECK_TRUE(caught);}
CS2N_TEST(Session_DryRunStorageBounded){DryRun d;for(unsigned n=0;n<500;++n)d.record({n,target,Action::StartSearch,1,0});CHECK_TRUE(d.plans().size()==128);CHECK_TRUE(d.plans().front().id==372);}
CS2N_TEST(Session_ForegroundIndependent){ // No foreground/cursor/global-input dependency exists in the observer or FSM.
Controller a,b;a.start(target,0);b.start(target,0);auto x=a.tick(input(Scene::MainMenu),0,true);auto y=b.tick(input(Scene::MainMenu),0,true);CHECK_TRUE(x&&y&&x->action==y->action&&x->target==y->target);}

CS2N_TEST(Practice_FullMenuFlow){
 Controller c({30,5,2,MatchKind::Practice});c.start(target,0);
 auto p=c.tick(input(Scene::MainMenu),0,true);CHECK_TRUE(p&&p->action==Action::OpenPractice);
 p=c.tick(input(Scene::PracticeMenu,1),1,true);CHECK_TRUE(p&&p->action==Action::SelectPracticeDeathmatch);
 p=c.tick(input(Scene::PracticeDeathmatchMenu,2),2,true);CHECK_TRUE(p&&p->action==Action::StartPractice);
 CHECK_TRUE(c.phase()==Phase::WaitMatch);
 CHECK_FALSE(c.tick(input(Scene::Loading,3),3,true));
 CHECK_FALSE(c.tick(input(Scene::InMatch,4),4,true));CHECK_TRUE(c.phase()==Phase::Ready);
}
CS2N_TEST(Practice_RejectsOnlineMenuAndSearch){
 for(auto scene:{Scene::DeathmatchMenu,Scene::Search}){
  Controller c({30,5,2,MatchKind::Practice});c.start(target,0);
  CHECK_FALSE(c.tick(input(scene),0,true));CHECK_TRUE(c.phase()==Phase::Error);
 }
}
CS2N_TEST(Practice_RetryRequiresFreshSameMenu){
 Controller c({30,5,2,MatchKind::Practice});c.start(target,0);
 auto p=c.tick(input(Scene::PracticeDeathmatchMenu),0,true);CHECK_TRUE(p&&p->action==Action::StartPractice);
 CHECK_FALSE(c.tick(input(Scene::PracticeDeathmatchMenu),6,true));
 p=c.tick(input(Scene::PracticeDeathmatchMenu,7),7,true);CHECK_TRUE(p&&p->attempt==2&&p->action==Action::StartPractice);
 CHECK_FALSE(c.tick(input(Scene::PracticeDeathmatchMenu,13),13,true));CHECK_TRUE(c.phase()==Phase::Error);
}
CS2N_TEST(Practice_UnknownDoesNotStart){
 Controller c({30,5,2,MatchKind::Practice});c.start(target,0);
 CHECK_FALSE(c.tick(input(Scene::Unknown),0,true));
 CHECK_FALSE(c.tick(input(Scene::Unknown,30),30,true));CHECK_TRUE(c.phase()==Phase::Error);
}
CS2N_TEST(Practice_AlreadyInMatchDoesNotLaunchAgain){
 Controller c({30,5,2,MatchKind::Practice});c.start(target,0);
 CHECK_FALSE(c.tick(input(Scene::InMatch),0,true));CHECK_TRUE(c.phase()==Phase::Ready);
 CHECK_FALSE(c.tick(input(Scene::MatchEnded,1),1,true));CHECK_TRUE(c.phase()==Phase::Stopped);
}
CS2N_TEST(Practice_StopCancelsPendingLaunch){
 Controller c({30,5,2,MatchKind::Practice});c.start(target,0);
 c.tick(input(Scene::PracticeDeathmatchMenu),0,true);c.stop();
 CHECK_FALSE(c.tick(input(Scene::PracticeDeathmatchMenu,6),6,true));CHECK_TRUE(c.phase()==Phase::Stopped);
}
CS2N_TEST(Practice_PatchRejectsInvalidGeometry){
 Image frame(30,30,{120,130,140}),ref(10,10,{120,130,140});
 CHECK_TRUE(matchesPracticePatch(frame,ref,5,5));
 CHECK_FALSE(matchesPracticePatch(frame,Image{},5,5));
 CHECK_FALSE(matchesPracticePatch(frame,ref,-1,5));
 CHECK_FALSE(matchesPracticePatch(frame,ref,25,5));
}
CS2N_TEST(Practice_PatchRejectsChangedOrObscuredControl){
 Image frame(10,10,{100,150,200}),ref=frame;
 CHECK_TRUE(matchesPracticePatch(frame,ref,0,0));
 for(int x=0;x<10;++x)frame.at(x,0)={0,0,0};
 CHECK_FALSE(matchesPracticePatch(frame,ref,0,0));
 CHECK_FALSE(matchesPracticePatch(Image(10,10),ref,0,0));
}
