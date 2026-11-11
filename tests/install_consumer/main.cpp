/**
 * @file main.cpp
 * @brief Builds the driver from the installed package only: headers, version header and .ipp.
 */
#include <cstddef>
#include <cstdint>

#include "as5047u.hpp"

namespace {

// A port that answers every read with 0: enough to instantiate the whole driver template.
class NullPort : public as5047u::SpiInterface<NullPort> {
public:
  bool EnsureInitialized() noexcept {
    return true;
  }
  bool Transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept {
    (void)tx;
    for (std::size_t i = 0; rx != nullptr && i < len; ++i) {
      rx[i] = 0;
    }
    return true;
  }
  void DelayUs(uint32_t us) noexcept {
    (void)us;
  }
};

} // namespace

int main() {
  NullPort port;
  as5047u::AS5047U<NullPort> sensor(port);
  // MISO reads 0, a stuck line, so the start-up must fail with NoDevice: proof the template
  // compiled and ran.
  const auto r = sensor.EnsureInitialized();
  return (r.error == as5047u::Error::NoDevice && as5047u::GetDriverVersion()[0] != '\0') ? 0 : 1;
}
