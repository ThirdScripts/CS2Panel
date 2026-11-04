#include "perception/radar.h"
#include "vision/capture.h"
#include "vision/radar_localizer.h"
#include <chrono>
#include "core/log.h"
namespace cs2n {
RadarObserver::RadarObserver():thread_([this]{
  RadarCapture capture;RadarLocalizer detector;
  int previousContacts=-1;std::string previousReason;
  while(!stop_) {
    int w=0,h=0;std::string error;
    if(RadarCapture::cs2ClientSize(w,h)) {
      // This user's verified 1.3 HUD scale, north-up radar. Reject other
      // resolutions until a calibration profile is supplied.
      if(w==1920&&h==1080)if(auto frame=capture.grab({8,8,373,373},error)) {
        auto bearings=detector.enemyBearings(*frame);
        const auto own=detector.locate(*frame,false);
        if(previousContacts!=static_cast<int>(bearings.size())||previousReason!=own.reason) {
          Logger::instance().info("Radar: "+own.reason+" contacts="+std::to_string(bearings.size()));
          previousContacts=static_cast<int>(bearings.size());previousReason=own.reason;
        }
        double now=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        std::lock_guard lock(mutex_);hints_={now,std::move(bearings)};
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}){}
RadarObserver::~RadarObserver(){stop_=true;if(thread_.joinable())thread_.join();}
RadarHints RadarObserver::latest()const{std::lock_guard lock(mutex_);return hints_;}
}
