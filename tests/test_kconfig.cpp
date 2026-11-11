/**
 * @file test_kconfig.cpp
 * @brief KconfigDefaults(): the defaults without Kconfig, and menuconfig values when they are set.
 *
 * Built twice: without CONFIG_ macros (KCONFIG_SET undefined) and with the values an sdkconfig.h
 * would define (KCONFIG_SET=1, see tests/CMakeLists.txt).
 */
#include "as5047u_kconfig.hpp"
#include "test_harness.hpp"

#ifdef KCONFIG_SET
TEST_CASE("Kconfig values reach the Config") {
  constexpr as5047u::Config kC = as5047u::KconfigDefaults();
  CHECK_EQ(kC.frame, as5047u::FrameFormat::Bits32);
  CHECK_EQ(kC.direction, as5047u::Direction::CounterClockwise);
  CHECK(!kC.dynamic_compensation);
  CHECK(kC.write_settings); // not a Kconfig option
  CHECK_EQ(as5047u::Validate(kC), as5047u::Error::Ok);
}
#else
TEST_CASE("Without Kconfig, KconfigDefaults() equals Config{}") {
  constexpr as5047u::Config kC = as5047u::KconfigDefaults();
  constexpr as5047u::Config kD{};
  CHECK_EQ(kC.frame, kD.frame);
  CHECK_EQ(kC.direction, kD.direction);
  CHECK_EQ(kC.dynamic_compensation, kD.dynamic_compensation);
}
#endif
