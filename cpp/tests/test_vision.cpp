// Radar marker detection, tested on synthetic frames first and on real captured
// Dust II frames when they are present.
#include <cmath>
#include <filesystem>
#include <string>

#include "tests/framework.h"
#include "vision/image.h"
#include "vision/radar_localizer.h"

using namespace cs2n;
CS2N_TEST(Vision_WindowsDibPreservesRedEnemyAndYellowOwnMarker) {
  const auto red=rgbFromDib(0x00ff0000),yellow=rgbFromDib(0x00ffff00),blue=rgbFromDib(0x000000ff);
  CHECK_EQ(red.r,255);CHECK_EQ(red.b,0);
  CHECK_EQ(yellow.r,255);CHECK_EQ(yellow.g,255);CHECK_EQ(yellow.b,0);
  CHECK_EQ(blue.r,0);CHECK_EQ(blue.b,255);
}

namespace {

// Draws a disc plus a white wedge pointing at `angle`, mirroring the real HUD.
Image markerFrame(int cx, int cy, double angle, int radius = 7) {
  Image image(120, 120, Rgb{20, 30, 25});  // dark radar floor
  const int wedgeDistance = radius + 4;
  const int wx = cx + static_cast<int>(std::round(std::cos(angle) * wedgeDistance));
  const int wy = cy + static_cast<int>(std::round(std::sin(angle) * wedgeDistance));

  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      if (std::hypot(x - cx, y - cy) <= radius) {
        image.at(x, y) = Rgb{0, 220, 230};  // saturated cyan/yellow disc
      }
      if (std::abs(x - wx) <= 2 && std::abs(y - wy) <= 2) {
        image.at(x, y) = Rgb{255, 255, 255};  // direction wedge
      }
    }
  }
  return image;
}

}  // namespace

CS2N_TEST(Vision_HsvRoundTrip) {
  const Hsv white = Image::toHsv(Rgb{255, 255, 255});
  CHECK_NEAR(white.saturation, 0.0, 1e-6);
  CHECK_NEAR(white.value, 255.0, 1e-6);

  const Hsv black = Image::toHsv(Rgb{0, 0, 0});
  CHECK_NEAR(black.value, 0.0, 1e-6);

  // Pure red has hue 0 in the 0..179 convention.
  const Hsv red = Image::toHsv(Rgb{255, 0, 0});
  CHECK_NEAR(red.hue, 0.0, 1e-6);
  CHECK_NEAR(red.saturation, 255.0, 1e-6);
}

CS2N_TEST(Vision_CropReturnsSubImage) {
  Image image(10, 10, Rgb{1, 2, 3});
  image.at(5, 5) = Rgb{9, 9, 9};
  const Image crop = image.crop(4, 4, 3, 3);
  CHECK_EQ(crop.width(), 3);
  CHECK_EQ(crop.height(), 3);
  CHECK_EQ(static_cast<int>(crop.at(1, 1).r), 9);
  // Out of bounds crop is empty rather than throwing.
  CHECK_TRUE(image.crop(8, 8, 5, 5).empty());
}

CS2N_TEST(Vision_FindsMarkerAndHeading) {
  RadarLocalizer localizer;
  for (const double angle : {0.0, 1.0, -1.5, 2.8}) {
    const Image frame = markerFrame(60, 60, angle);
    const DetectionResult result = localizer.locate(frame);
    CHECK_TRUE(result.ok);
    CHECK_NEAR(result.worldPosition.x, 60.0, 1.5);
    CHECK_NEAR(result.worldPosition.y, 60.0, 1.5);
    CHECK_NEAR(std::fabs(wrapAngle(result.yaw - angle)), 0.0, 0.2);
  }
}

CS2N_TEST(Vision_RejectsFrameWithoutMarker) {
  RadarLocalizer localizer;
  Image blank(120, 120, Rgb{20, 30, 25});
  const DetectionResult result = localizer.locate(blank);
  CHECK_FALSE(result.ok);
  CHECK_EQ(result.candidates, 0);
}
CS2N_TEST(Vision_RadarRedContactProducesNorthUpBearing) {
  RadarLocalizer detector;auto frame=markerFrame(60,60,0);
  for(int y=20;y<26;++y)for(int x=57;x<63;++x)frame.set(x,y,{255,0,0});
  const auto bearings=detector.enemyBearings(frame);
  CHECK_EQ(bearings.size(),std::size_t(1));
  if(!bearings.empty())CHECK_NEAR(bearings[0],kPi/2,.03);
}
CS2N_TEST(Vision_RadarDoesNotRequireOwnHeadingWedgeAndRejectsDeathCross) {
  RadarLocalizer detector;Image frame(120,120,{20,30,25});
  for(int y=0;y<120;++y)for(int x=0;x<120;++x)
    if(std::hypot(x-60,y-60)<=7)frame.set(x,y,{255,255,0});
  for(int y=20;y<26;++y)for(int x=57;x<63;++x)frame.set(x,y,rgbFromDib(0x00ff0000));
  CHECK_EQ(detector.enemyBearings(frame).size(),std::size_t(1));
  for(int y=80;y<100;++y)for(int x=80;x<100;++x)
    if(std::abs(x-y)<=2||std::abs(x+y-179)<=2)frame.set(x,y,{255,0,0});
  CHECK_EQ(detector.enemyBearings(frame).size(),std::size_t(1));
}

CS2N_TEST(Vision_RejectsTwoMarkersRatherThanGuessing) {
  RadarLocalizer localizer;
  Image frame = markerFrame(40, 40, 0.0);
  const Image second = markerFrame(90, 90, 1.0);
  for (int y = 0; y < second.height(); ++y) {
    for (int x = 0; x < second.width(); ++x) {
      const Rgb& pixel = second.at(x, y);
      if (pixel.r > 30 || pixel.g > 60 || pixel.b > 60) frame.set(x + 0, y + 0, pixel);
    }
  }
  const DetectionResult result = localizer.locate(frame);
  CHECK_FALSE(result.ok);
  CHECK_GT(result.candidates, 1);
}

CS2N_TEST(Vision_RejectsDiscWithoutDirection) {
  RadarLocalizer localizer;
  Image frame(120, 120, Rgb{20, 30, 25});
  // A disc with no wedge must not produce a heading.
  for (int y = 52; y <= 68; ++y) {
    for (int x = 52; x <= 68; ++x) {
      if (std::hypot(x - 60, y - 60) <= 7) frame.at(x, y) = Rgb{0, 220, 230};
    }
  }
  const DetectionResult result = localizer.locate(frame);
  CHECK_FALSE(result.ok);
}

CS2N_TEST(Vision_CalibrationMapsPixelsToWorld) {
  MarkerTuning tuning;
  tuning.unitsPerPixel = 2.5;
  tuning.offsetX = -100.0;
  tuning.offsetY = -50.0;
  RadarLocalizer localizer(tuning);

  const Vec2 world = localizer.toWorld({10.0, 20.0});
  CHECK_NEAR(world.x, -75.0, 1e-9);
  CHECK_NEAR(world.y, 0.0, 1e-9);

  const Vec2 pixel = localizer.toPixel(world);
  CHECK_NEAR(pixel.x, 10.0, 1e-9);
  CHECK_NEAR(pixel.y, 20.0, 1e-9);
}

CS2N_TEST(Vision_ReplaysRealCapturedFrames) {
  // Real radar crops exported from the Python tooling, when available.
  const std::string directory = "assets/frames";
  if (!std::filesystem::exists(directory)) {
    return;  // no captured frames in this checkout
  }
  RadarLocalizer localizer;
  int checked = 0;
  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    if (entry.path().extension() != ".ppm") continue;
    Image frame;
    std::string error;
    if (!Image::loadPnm(entry.path().string(), frame, error)) continue;
    const DetectionResult result = localizer.locate(frame);
    // Real frames may legitimately contain no marker (death cam, menu), but the
    // detector must never report a pose it is unsure about.
    if (result.ok) {
      CHECK_TRUE(result.confidence > 0.5);
      CHECK_TRUE(std::isfinite(result.yaw));
      ++checked;
    }
  }
  (void)checked;
}
