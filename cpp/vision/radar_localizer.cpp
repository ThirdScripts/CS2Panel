#include "vision/radar_localizer.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>

namespace cs2n {
namespace {

struct RawBlob {
  double sumX = 0.0;
  double sumY = 0.0;
  int area = 0;
  int minX = 0;
  int maxX = 0;
  int minY = 0;
  int maxY = 0;
};

// Two-pass-free flood fill; the crops here are a few hundred pixels square, so
// clarity beats asymptotics.
void labelBlobs(const std::vector<std::uint8_t>& mask, int width, int height, std::vector<RawBlob>& out) {
  (void)height;  // derived from mask.size() / width; kept for clarity at call sites
  std::vector<int> visited(mask.size(), 0);
  out.clear();
  std::deque<int> queue;
  for (int start = 0; start < static_cast<int>(mask.size()); ++start) {
    if (mask[static_cast<std::size_t>(start)] == 0 || visited[static_cast<std::size_t>(start)] != 0) continue;
    RawBlob blob;
    blob.minX = blob.maxX = start % width;
    blob.minY = blob.maxY = start / width;
    visited[static_cast<std::size_t>(start)] = 1;
    queue.push_back(start);
    while (!queue.empty()) {
      const int index = queue.front();
      queue.pop_front();
      const int x = index % width;
      const int y = index / width;
      blob.sumX += x;
      blob.sumY += y;
      ++blob.area;
      blob.minX = std::min(blob.minX, x);
      blob.maxX = std::max(blob.maxX, x);
      blob.minY = std::min(blob.minY, y);
      blob.maxY = std::max(blob.maxY, y);
      const int neighbours[4] = {index - 1, index + 1, index - width, index + width};
      for (int i = 0; i < 4; ++i) {
        const int next = neighbours[i];
        if (next < 0 || next >= static_cast<int>(mask.size())) continue;
        if (i == 0 && x == 0) continue;
        if (i == 1 && x == width - 1) continue;
        if (visited[static_cast<std::size_t>(next)] != 0) continue;
        if (mask[static_cast<std::size_t>(next)] == 0) continue;
        visited[static_cast<std::size_t>(next)] = 1;
        queue.push_back(next);
      }
    }
    out.push_back(blob);
  }
}

bool within(const Hsv& value, const Hsv& low, const Hsv& high) {
  return value.hue >= low.hue && value.hue <= high.hue && value.saturation >= low.saturation &&
         value.saturation <= high.saturation && value.value >= low.value && value.value <= high.value;
}

}  // namespace

Vec2 RadarLocalizer::toWorld(const Vec2& pixel) const {
  return {pixel.x * tuning_.unitsPerPixel + tuning_.offsetX, pixel.y * tuning_.unitsPerPixel + tuning_.offsetY};
}
std::vector<double> RadarLocalizer::enemyBearings(const Image& radar) const {
  const auto own=locate(radar,false);if(!own.ok)return {};
  const Vec2 origin=toPixel(own.worldPosition);
  auto hsv=radar.hsv();std::vector<std::uint8_t> mask(hsv.size());
  for(std::size_t i=0;i<hsv.size();++i)mask[i]=((hsv[i].hue<8||hsv[i].hue>172)&&hsv[i].saturation>155&&hsv[i].value>125)?1:0;
  std::vector<RawBlob> blobs;labelBlobs(mask,radar.width(),radar.height(),blobs);
  std::vector<double> result;
  for(const auto& b:blobs) {
    int w=b.maxX-b.minX+1,h=b.maxY-b.minY+1;
    if(b.area<8||b.area>180||w>24||h>24||w<3||h<3)continue;
    if(double(b.area)/(w*h)<.45)continue; // Reject thin death crosses/text.
    int middle=0,rows=0;
    for(int y=(b.minY+b.maxY)/2-1;y<=(b.minY+b.maxY)/2+1;++y)if(y>=b.minY&&y<=b.maxY) {
      ++rows;for(int x=b.minX;x<=b.maxX;++x)middle+=mask[static_cast<std::size_t>(y*radar.width()+x)]!=0;
    }
    if(double(middle)/(rows*w)<.55)continue;
    double x=b.sumX/b.area-origin.x,y=b.sumY/b.area-origin.y;
    if(std::hypot(x,y)<10)continue;
    result.push_back(std::atan2(-y,x));
  }
  return result;
}

Vec2 RadarLocalizer::toPixel(const Vec2& world) const {
  const double scale = tuning_.unitsPerPixel > 1e-9 ? tuning_.unitsPerPixel : 1.0;
  return {(world.x - tuning_.offsetX) / scale, (world.y - tuning_.offsetY) / scale};
}

DetectionResult RadarLocalizer::locate(const Image& radar,bool requireHeading) const {
  DetectionResult result;
  if (radar.empty()) {
    result.reason = "empty radar frame";
    return result;
  }
  const int width = radar.width();
  const int height = radar.height();

  std::vector<Hsv> hsv = radar.hsv();
  std::vector<std::uint8_t> bodyMask(hsv.size(), 0);
  std::vector<std::uint8_t> wedgeMask(hsv.size(), 0);
  for (std::size_t i = 0; i < hsv.size(); ++i) {
    const bool isBody = within(hsv[i], tuning_.bodyHueLow, tuning_.bodyHueHigh) ||
                        within(hsv[i], tuning_.bodyAltLow, tuning_.bodyAltHigh);
    bodyMask[i] = isBody ? 1 : 0;
    wedgeMask[i] = within(hsv[i], tuning_.wedgeLow, tuning_.wedgeHigh) ? 1 : 0;
  }

  std::vector<RawBlob> bodyBlobs;
  std::vector<RawBlob> wedgeBlobs;
  labelBlobs(bodyMask, width, height, bodyBlobs);
  labelBlobs(wedgeMask, width, height, wedgeBlobs);

  struct Candidate {
    Vec2 position;
    double angle = 0.0;
    double solidity = 0.0;
  };
  std::vector<Candidate> candidates;

  for (const RawBlob& body : bodyBlobs) {
    const int bodyWidth = body.maxX - body.minX + 1;
    const int bodyHeight = body.maxY - body.minY + 1;
    if (body.area < tuning_.minDiscArea || body.area > tuning_.maxDiscArea) continue;
    if (bodyWidth < tuning_.minDiscSide || bodyWidth > tuning_.maxDiscSide) continue;
    if (bodyHeight < tuning_.minDiscSide || bodyHeight > tuning_.maxDiscSide) continue;
    if (bodyHeight == 0) continue;
    const double aspect = static_cast<double>(bodyWidth) / static_cast<double>(bodyHeight);
    if (aspect < 0.65 || aspect > 1.55) continue;
    const double fill = static_cast<double>(body.area) / static_cast<double>(bodyWidth * bodyHeight);
    if (fill < tuning_.minDiscFill) continue;

    const double cx = body.sumX / body.area;
    const double cy = body.sumY / body.area;
    const double radius = (bodyWidth + bodyHeight) / 4.0;
    if(!requireHeading){candidates.push_back({{cx,cy},0,std::min(1.0,fill)});continue;}

    for (const RawBlob& wedge : wedgeBlobs) {
      if (wedge.area < tuning_.minWedgeArea || wedge.area > tuning_.maxWedgeArea) continue;
      const int wedgeWidth = wedge.maxX - wedge.minX + 1;
      const int wedgeHeight = wedge.maxY - wedge.minY + 1;
      if (wedgeWidth > tuning_.maxWedgeSide || wedgeHeight > tuning_.maxWedgeSide) continue;
      const double wx = wedge.sumX / wedge.area;
      const double wy = wedge.sumY / wedge.area;
      const double distanceFromCentre = std::hypot(wx - cx, wy - cy);
      if (distanceFromCentre < radius * tuning_.wedgeDistanceMin ||
          distanceFromCentre > radius * tuning_.wedgeDistanceMax) {
        continue;
      }
      if (std::fabs(wx - cx) > bodyWidth + 8 || std::fabs(wy - cy) > bodyHeight + 8) continue;

      Candidate candidate;
      candidate.position = {cx, cy};
      candidate.angle = std::atan2(wy - cy, wx - cx);
      candidate.solidity = std::min(1.0, fill);
      candidates.push_back(candidate);
    }
  }

  result.candidates = static_cast<int>(candidates.size());
  if (candidates.empty()) {
    result.reason = "marker not found";
    return result;
  }
  if (candidates.size() > 1) {
    // Several discs: refuse to guess. Standing still beats walking into a wall.
    result.reason = "several candidate markers, refusing to choose";
    return result;
  }

  const Candidate& best = candidates[0];
  result.ok = true;
  result.worldPosition = toWorld(best.position);
  result.yaw = best.angle;
  result.confidence = best.solidity;
  result.reason = "marker located";
  return result;
}

}  // namespace cs2n
