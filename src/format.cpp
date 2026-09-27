#include "format.h"

#include <cmath>
#include <cstdarg>
#include <cwchar>

// U+2212 (minus) matches the width of '+'; U+00B0 is the degree sign.
#define TBM_MINUS L"\u2212"
#define TBM_DEGREE L"\u00B0"

namespace tbm {
namespace {

void Put(Cell& cell, const wchar_t* format, ...) {
  va_list args;
  va_start(args, format);
  _vsnwprintf_s(cell.data(), cell.size(), _TRUNCATE, format, args);
  va_end(args);
}

bool Usable(const std::optional<double>& value) { return value && std::isfinite(*value); }

int Rounded(double value) { return static_cast<int>(std::lround(value)); }

void PutWatts(Cell& cell, const wchar_t* sign, double watts) {
  if (watts < 99.95) {
    Put(cell, L"%ls%.1fW", sign, watts);
  } else {
    Put(cell, L"%ls%dW", sign, Rounded(watts));
  }
}

}  // namespace

const wchar_t* const kWidestText[kColumns][kRows] = {
    {L"100%", L"88.8W"},
    {L"8.8GHz", L"+88.8W"},
    {L"100" TBM_DEGREE L"C", L"100%"},
};

Cells FormatMetrics(const Metrics& m) {
  Cells cells{};
  Cell& usage = cells[0];
  Cell& clock = cells[1];
  Cell& temp = cells[2];
  Cell& cpu_power = cells[3];
  Cell& battery = cells[4];
  Cell& memory = cells[5];

  if (Usable(m.cpu_usage_pct)) {
    Put(usage, L"%d%%", Rounded(*m.cpu_usage_pct));
  } else {
    Put(usage, L"--%%");
  }

  if (Usable(m.cpu_clock_mhz)) {
    Put(clock, L"%.1fGHz", *m.cpu_clock_mhz / 1000.0);
  } else {
    Put(clock, L"--GHz");
  }

  if (Usable(m.cpu_temp_c)) {
    Put(temp, L"%d" TBM_DEGREE L"C", Rounded(*m.cpu_temp_c));
  } else {
    Put(temp, L"--" TBM_DEGREE L"C");
  }

  if (Usable(m.cpu_power_w)) {
    PutWatts(cpu_power, L"", *m.cpu_power_w);
  } else {
    Put(cpu_power, L"--W");
  }

  switch (m.battery.state) {
    case BatteryFlow::State::kDischarging:
      PutWatts(battery, TBM_MINUS, m.battery.watts);
      break;
    case BatteryFlow::State::kCharging:
      PutWatts(battery, L"+", m.battery.watts);
      break;
    case BatteryFlow::State::kIdleOnAc:
      Put(battery, L"AC");
      break;
    case BatteryFlow::State::kUnknown:
      Put(battery, L"--");
      break;
  }

  if (Usable(m.memory_load_pct)) {
    Put(memory, L"%d%%", Rounded(*m.memory_load_pct));
  } else {
    Put(memory, L"--%%");
  }
  return cells;
}

}  // namespace tbm
