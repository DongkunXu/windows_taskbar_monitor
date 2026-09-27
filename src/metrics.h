#pragma once

#include <optional>

namespace tbm {

// Battery power flow. When discharging, this is the whole system's power draw.
struct BatteryFlow {
  enum class State { kUnknown, kDischarging, kCharging, kIdleOnAc };
  State state = State::kUnknown;
  double watts = 0;  // Magnitude; the sign is carried by `state`.
};

// One sample. An empty optional means the value is unavailable on this machine or right now.
struct Metrics {
  std::optional<double> cpu_usage_pct;
  std::optional<double> cpu_clock_mhz;
  std::optional<double> cpu_temp_c;
  std::optional<double> cpu_power_w;
  std::optional<double> memory_load_pct;
  BatteryFlow battery;
};

}  // namespace tbm
