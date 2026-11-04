// Core math primitives shared by every module.
#pragma once

#include <cstdarg>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace cs2n {

inline constexpr double kPi = 3.14159265358979323846;

struct Vec2 {
  double x = 0.0;
  double y = 0.0;

  constexpr Vec2() = default;
  constexpr Vec2(double x_, double y_) : x(x_), y(y_) {}

  constexpr Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
  constexpr Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
  constexpr Vec2 operator*(double s) const { return {x * s, y * s}; }
  constexpr Vec2 operator/(double s) const { return {x / s, y / s}; }
  Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
  Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
  bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }

  [[nodiscard]] double dot(const Vec2& o) const { return x * o.x + y * o.y; }
  [[nodiscard]] double cross(const Vec2& o) const { return x * o.y - y * o.x; }
  [[nodiscard]] double lengthSq() const { return x * x + y * y; }
  [[nodiscard]] double length() const { return std::sqrt(lengthSq()); }
  [[nodiscard]] Vec2 normalized() const {
    const double len = length();
    return len > 1e-12 ? Vec2{x / len, y / len} : Vec2{0.0, 0.0};
  }
};

inline double distance(const Vec2& a, const Vec2& b) { return (a - b).length(); }
inline double distanceSq(const Vec2& a, const Vec2& b) { return (a - b).lengthSq(); }

struct Vec3 {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;

  constexpr Vec3() = default;
  constexpr Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
  [[nodiscard]] Vec2 xy() const { return {x, y}; }
  [[nodiscard]] double length() const { return std::sqrt(x * x + y * y + z * z); }
};

// Normalises an angle to (-pi, pi].
inline double wrapAngle(double angle) {
  while (angle > kPi) angle -= 2.0 * kPi;
  while (angle <= -kPi) angle += 2.0 * kPi;
  return angle;
}

// Shortest signed rotation that turns `from` into `to`, in (-pi, pi].
inline double angleDifference(double to, double from) { return wrapAngle(to - from); }

inline double clamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline bool finiteNumber(double v) { return std::isfinite(v); }

// Direction unit vector for a yaw in radians (math convention: 0 = +X, CCW).
inline Vec2 directionFromYaw(double yaw) { return {std::cos(yaw), std::sin(yaw)}; }
inline double yawFromDirection(const Vec2& d) { return std::atan2(d.y, d.x); }

inline std::string format(const char* fmt, ...) {
  std::vector<char> buffer(512);
  for (;;) {
    va_list args;
    va_start(args, fmt);
    const int written = std::vsnprintf(buffer.data(), buffer.size(), fmt, args);
    va_end(args);
    if (written < 0) return {};
    if (static_cast<std::size_t>(written) < buffer.size()) return std::string(buffer.data(), written);
    buffer.resize(static_cast<std::size_t>(written) + 1);
  }
}

}  // namespace cs2n
