#pragma once

#include <windows.h>

#include <pdh.h>

#include <vector>

#include "metrics.h"
#include "win_handle.h"

namespace tbm {

// Reads values through standard Windows interfaces: PDH counters (CPU usage, clock, RAPL package
// power, ACPI thermal zones), memory status and battery status. Needs no driver and no
// elevation. Only the thermal zone read reaches hardware: it evaluates the zone's ACPI _TMP
// method, which asks the embedded controller (about 4 ms wall time, 0.35 ms CPU per read).
// Not thread-safe: owned by the sampler thread.
class Sensors {
 public:
  // Builds the PDH query. Counters missing on this machine are logged once and read as empty.
  void Open();
  Metrics Read();

 private:
  bool Add(const wchar_t* path, PDH_HCOUNTER* counter);
  std::optional<double> Value(PDH_HCOUNTER counter) const;
  // Calls `visit(name, value)` for each valid instance of a wildcard counter.
  template <typename Visit>
  void ForEachInstance(PDH_HCOUNTER counter, Visit&& visit);

  std::optional<double> ReadClock() const;
  std::optional<double> ReadTemperature();
  std::optional<double> ReadPackagePower();

  Unique<PDH_HQUERY, &::PdhCloseQuery> query_;
  PDH_HCOUNTER usage_ = nullptr;
  PDH_HCOUNTER actual_clock_ = nullptr;
  PDH_HCOUNTER nominal_clock_ = nullptr;  // Fallback pair when "Actual Frequency" is missing.
  PDH_HCOUNTER performance_pct_ = nullptr;
  PDH_HCOUNTER thermal_ = nullptr;
  double thermal_units_per_kelvin_ = 10.0;  // "High Precision Temperature" is in 0.1 K.
  PDH_HCOUNTER energy_ = nullptr;
  std::vector<BYTE> instances_;  // Reused buffer for wildcard counters; grows once.
};

}  // namespace tbm
