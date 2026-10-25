// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cmath>
namespace esphome { namespace micronet_compass {
// Right-handed sensor frame, X forward, Y starboard, Z down.
inline float heading(float mx, float my, float mz, float ax, float ay, float az, float offset) {
  if (!std::isfinite(mx + my + mz + ax + ay + az + offset)) return NAN;
  float norm = std::sqrt(ax*ax + ay*ay + az*az);
  if (norm < 1e-6f) return NAN;
  ax /= norm; ay /= norm; az /= norm;
  // Project vessel forward and magnetic north onto the horizontal plane.
  float nx = mx - (mx*ax + my*ay + mz*az)*ax;
  float ny = my - (mx*ax + my*ay + mz*az)*ay;
  float nz = mz - (mx*ax + my*ay + mz*az)*az;
  float fx = 1-ax*ax, fy = -ax*ay, fz = -ax*az;
  float north = nx*fx + ny*fy + nz*fz;
  float east = ax*(ny*fz - nz*fy) + ay*(nz*fx - nx*fz) + az*(nx*fy - ny*fx);
  if (std::hypot(north, east) < 1e-6f) return NAN;
  constexpr float TAU = 6.283185307179586f;
  float result = std::fmod(std::atan2(east, north) + offset, TAU);
  return result < 0 ? result + TAU : result;
}
}}
