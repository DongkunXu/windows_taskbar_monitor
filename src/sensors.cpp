#include "sensors.h"

#include <pdhmsg.h>
#include <powrprof.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cwchar>

#include "log.h"

namespace tbm {
namespace {

constexpr double kZeroCelsiusInKelvin = 273.15;

// English counter paths, since counter names are localized on non-English systems.
constexpr wchar_t kUsagePath[] = L"\\Processor Information(_Total)\\% Processor Utility";
constexpr wchar_t kActualClockPath[] = L"\\Processor Information(_Total)\\Actual Frequency";
constexpr wchar_t kNominalClockPath[] = L"\\Processor Information(_Total)\\Processor Frequency";
constexpr wchar_t kPerformancePath[] = L"\\Processor Information(_Total)\\% Processor Performance";
constexpr wchar_t kThermalPrecisePath[] =
    L"\\Thermal Zone Information(*)\\High Precision Temperature";                  // 0.1 K
constexpr wchar_t kThermalPath[] = L"\\Thermal Zone Information(*)\\Temperature";  // 1 K
constexpr wchar_t kEnergyPath[] = L"\\Energy Meter(*)\\Power";

// Right after logon some counter providers may not be loaded yet. Missing counters are retried
// once a minute for the first ten minutes, then treated as absent on this machine.
constexpr int kRetryEverySamples = 30;
constexpr int kMaxRetries = 10;

bool IsValid(DWORD status) {
  return status == PDH_CSTATUS_VALID_DATA || status == PDH_CSTATUS_NEW_DATA;
}

bool EndsWithIgnoreCase(const wchar_t* text, const wchar_t* suffix) {
  const size_t text_length = wcslen(text);
  const size_t suffix_length = wcslen(suffix);
  return text_length >= suffix_length &&
         CompareStringOrdinal(text + text_length - suffix_length, static_cast<int>(suffix_length),
                              suffix, static_cast<int>(suffix_length), TRUE) == CSTR_EQUAL;
}

std::optional<double> ReadMemoryLoad() {
  MEMORYSTATUSEX status{};
  status.dwLength = sizeof(status);
  if (!GlobalMemoryStatusEx(&status) || status.ullTotalPhys == 0) return std::nullopt;
  const double used = static_cast<double>(status.ullTotalPhys - status.ullAvailPhys);
  return 100.0 * used / static_cast<double>(status.ullTotalPhys);
}

// Only the sign of Rate is trusted: some firmware reports "discharging" while idle on AC.
BatteryFlow ReadBattery() {
  SYSTEM_BATTERY_STATE state{};
  if (CallNtPowerInformation(SystemBatteryState, nullptr, 0, &state, sizeof(state)) != 0 ||
      !state.BatteryPresent) {
    return {};
  }
  const LONG rate_mw = static_cast<LONG>(state.Rate);  // LONG_MIN: rate unknown.
  if (rate_mw > 0) return {BatteryFlow::State::kCharging, rate_mw / 1000.0};
  if (rate_mw < 0 && rate_mw != LONG_MIN) {
    return {BatteryFlow::State::kDischarging, -static_cast<double>(rate_mw) / 1000.0};
  }
  if (state.AcOnLine) return {BatteryFlow::State::kIdleOnAc, 0};
  return {};
}

}  // namespace

void Sensors::Open() {
  retries_left_ = kMaxRetries;
  first_attempt_ = true;
  AddMissingCounters();
  first_attempt_ = false;
  // Rate counters need a baseline sample before the first real read.
  if (query_) PdhCollectQueryData(query_.get());
}

void Sensors::AddMissingCounters() {
  if (!query_) {
    PDH_HQUERY query = nullptr;
    const PDH_STATUS status = PdhOpenQueryW(nullptr, 0, &query);
    if (status != ERROR_SUCCESS) {
      if (first_attempt_) {
        log::Error(L"PdhOpenQuery failed: 0x%08lX", static_cast<unsigned long>(status));
      }
      return;
    }
    query_.reset(query);
  }

  AddIfMissing(kUsagePath, &usage_);
  if (!HasClock() && !AddIfMissing(kActualClockPath, &actual_clock_)) {
    AddIfMissing(kNominalClockPath, &nominal_clock_);
    AddIfMissing(kPerformancePath, &performance_pct_);
  }
  if (!thermal_) {
    if (AddIfMissing(kThermalPrecisePath, &thermal_)) {
      thermal_units_per_kelvin_ = 10.0;
    } else if (AddIfMissing(kThermalPath, &thermal_)) {
      thermal_units_per_kelvin_ = 1.0;
    }
  }
  AddIfMissing(kEnergyPath, &energy_);
}

// Logs a missing counter once, at startup, and again only if a later retry finds it.
bool Sensors::AddIfMissing(const wchar_t* path, PDH_HCOUNTER* counter) {
  if (*counter) return true;
  const PDH_STATUS status = PdhAddEnglishCounterW(query_.get(), path, 0, counter);
  if (status == ERROR_SUCCESS) {
    if (!first_attempt_) log::Info(L"counter now available: %ls", path);
    return true;
  }
  *counter = nullptr;
  if (first_attempt_) {
    log::Info(L"counter unavailable: %ls (0x%08lX)", path, static_cast<unsigned long>(status));
  }
  return false;
}

bool Sensors::HasClock() const { return actual_clock_ || (nominal_clock_ && performance_pct_); }

bool Sensors::Complete() const { return query_ && usage_ && HasClock() && thermal_ && energy_; }

Metrics Sensors::Read() {
  if (!Complete() && retries_left_ > 0 && ++samples_since_retry_ >= kRetryEverySamples) {
    samples_since_retry_ = 0;
    --retries_left_;
    AddMissingCounters();
  }

  Metrics m;
  m.memory_load_pct = ReadMemoryLoad();
  m.battery = ReadBattery();
  if (!query_ || PdhCollectQueryData(query_.get()) != ERROR_SUCCESS) return m;

  m.cpu_usage_pct = Value(usage_);
  m.cpu_clock_mhz = ReadClock();
  m.cpu_temp_c = ReadTemperature();
  m.cpu_power_w = ReadPackagePower();
  return m;
}

std::optional<double> Sensors::Value(PDH_HCOUNTER counter) const {
  if (!counter) return std::nullopt;
  PDH_FMT_COUNTERVALUE value{};
  if (PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, nullptr, &value) !=
          ERROR_SUCCESS ||
      !IsValid(value.CStatus)) {
    return std::nullopt;
  }
  return value.doubleValue;
}

template <typename Visit>
void Sensors::ForEachInstance(PDH_HCOUNTER counter, Visit&& visit) {
  if (!counter) return;
  DWORD size = static_cast<DWORD>(instances_.size());
  DWORD count = 0;
  auto items = [this] { return reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(instances_.data()); };
  PDH_STATUS status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100,
                                                   &size, &count, items());
  if (status == static_cast<PDH_STATUS>(PDH_MORE_DATA)) {
    instances_.resize(size);
    status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &size, &count,
                                          items());
  }
  if (status != ERROR_SUCCESS) return;
  for (DWORD i = 0; i < count; ++i) {
    const PDH_FMT_COUNTERVALUE_ITEM_W& item = items()[i];
    if (IsValid(item.FmtValue.CStatus) && std::isfinite(item.FmtValue.doubleValue)) {
      visit(item.szName, item.FmtValue.doubleValue);
    }
  }
}

std::optional<double> Sensors::ReadClock() const {
  if (actual_clock_) return Value(actual_clock_);
  const std::optional<double> nominal = Value(nominal_clock_);
  const std::optional<double> performance = Value(performance_pct_);
  if (!nominal || !performance) return std::nullopt;
  return *nominal * *performance / 100.0;
}

// The hottest ACPI thermal zone. Its meaning is firmware-defined; on typical laptops it follows
// CPU load closely, but it is smoother than the CPU package temperature.
std::optional<double> Sensors::ReadTemperature() {
  std::optional<double> hottest;
  ForEachInstance(thermal_, [&](const wchar_t*, double raw) {
    if (raw <= 0) return;
    const double celsius = raw / thermal_units_per_kelvin_ - kZeroCelsiusInKelvin;
    hottest = hottest ? std::max(*hottest, celsius) : celsius;
  });
  return hottest;
}

// RAPL package domain from the Windows Energy Meter interface; the counter is in milliwatts.
std::optional<double> Sensors::ReadPackagePower() {
  std::optional<double> watts;
  ForEachInstance(energy_, [&](const wchar_t* name, double milliwatts) {
    if (EndsWithIgnoreCase(name, L"_PKG")) watts = milliwatts / 1000.0;
  });
  return watts;
}

}  // namespace tbm
