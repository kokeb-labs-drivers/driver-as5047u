/**
 * @file as5047u_kconfig.hpp
 * @brief Optional ESP-IDF layer: a `Config` built from the `CONFIG_AS5047U_*` Kconfig options.
 * @copyright Copyright (c) 2026 The driver-as5047u authors. SPDX-License-Identifier: MIT
 *
 * @details
 * Include this header only where you want menuconfig to choose the defaults; the driver never
 * includes it and no driver signature depends on it. Under ESP-IDF the values come from
 * `sdkconfig.h`. Without Kconfig every option falls back to the default, so `KconfigDefaults()`
 * equals `Config{}`.
 *
 * Kconfig writes a bool option set to "n" as an absent macro; the "y by default" options are
 * therefore spelled as their negation (`CONFIG_AS5047U_DAEC_DISABLE`).
 *
 * One configuration per program (KB-CPP20-018): `KconfigDefaults()` is an inline function whose
 * body depends on `sdkconfig.h` and the `CONFIG_` macros, so every translation unit of a program
 * must see the same ones. ESP-IDF guarantees that (one sdkconfig per build); the host tests build
 * each Kconfig variant as its own executable. This header is a leaf: no driver header includes
 * it.
 */
#ifndef AS5047U_KCONFIG_HPP
#define AS5047U_KCONFIG_HPP

#ifdef __has_include
#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif
#endif

#include "as5047u_types.hpp"

namespace as5047u {

/**
 * @brief The configuration menuconfig selected, on top of the defaults.
 * @return `Config{}` with the frame format, direction and DAEC taken from Kconfig.
 */
constexpr Config KconfigDefaults() noexcept {
  Config c{};
#ifdef CONFIG_AS5047U_FRAME_16
  c.frame = FrameFormat::Bits16;
#elif defined(CONFIG_AS5047U_FRAME_32)
  c.frame = FrameFormat::Bits32;
#else
  c.frame = FrameFormat::Bits24;
#endif
#if defined(CONFIG_AS5047U_DIRECTION_CCW) && CONFIG_AS5047U_DIRECTION_CCW
  c.direction = Direction::CounterClockwise;
#endif
#if defined(CONFIG_AS5047U_DAEC_DISABLE) && CONFIG_AS5047U_DAEC_DISABLE
  c.dynamic_compensation = false;
#endif
  return c;
}

} // namespace as5047u

#endif // AS5047U_KCONFIG_HPP
