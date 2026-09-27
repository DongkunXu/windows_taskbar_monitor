// Unit tests for FormatMetrics. Plain asserts; exits non-zero on failure.

#include "format.h"

#include <cstdio>
#include <cwchar>
#include <limits>

namespace {

using tbm::BatteryFlow;
using tbm::Cells;
using tbm::FormatMetrics;
using tbm::Metrics;

int g_failures = 0;

void ExpectCells(const Cells& actual, const wchar_t* const (&expected)[6], int line) {
  for (int i = 0; i < 6; ++i) {
    if (wcscmp(actual[i].data(), expected[i]) != 0) {
      fwprintf(stderr, L"line %d, cell %d: got \"%ls\", want \"%ls\"\n", line, i, actual[i].data(),
               expected[i]);
      ++g_failures;
    }
  }
}

Metrics Typical() {
  Metrics m;
  m.cpu_usage_pct = 12.4;
  m.cpu_clock_mhz = 1849;
  m.cpu_temp_c = 54.05;
  m.cpu_power_w = 5.21;
  m.memory_load_pct = 61.2;
  m.battery = {BatteryFlow::State::kDischarging, 8.43};
  return m;
}

void AllMissing() {
  ExpectCells(FormatMetrics(Metrics{}), {L"--%", L"--GHz", L"--\u00B0C", L"--W", L"--", L"--%"},
              __LINE__);
}

void TypicalOnBattery() {
  ExpectCells(FormatMetrics(Typical()),
              {L"12%", L"1.8GHz", L"54\u00B0C", L"5.2W",
               L"\u2212"
               L"8.4W",
               L"61%"},
              __LINE__);
}

void BatteryStates() {
  Metrics m = Typical();
  m.battery = {BatteryFlow::State::kCharging, 25.34};
  ExpectCells(FormatMetrics(m), {L"12%", L"1.8GHz", L"54\u00B0C", L"5.2W", L"+25.3W", L"61%"},
              __LINE__);
  m.battery = {BatteryFlow::State::kIdleOnAc, 0};
  ExpectCells(FormatMetrics(m), {L"12%", L"1.8GHz", L"54\u00B0C", L"5.2W", L"AC", L"61%"},
              __LINE__);
}

void RoundingAndLimits() {
  Metrics m = Typical();
  m.cpu_usage_pct = 142.6;  // % Processor Utility can exceed 100 under turbo.
  m.cpu_temp_c = 94.5;
  m.cpu_power_w = 99.96;  // Rounds to 100.0: switch to whole watts.
  m.memory_load_pct = 99.5;
  ExpectCells(FormatMetrics(m),
              {L"143%", L"1.8GHz", L"95\u00B0C", L"100W",
               L"\u2212"
               L"8.4W",
               L"100%"},
              __LINE__);
}

void NonFiniteIsMissing() {
  Metrics m = Typical();
  m.cpu_temp_c = std::numeric_limits<double>::quiet_NaN();
  m.cpu_power_w = std::numeric_limits<double>::infinity();
  ExpectCells(FormatMetrics(m),
              {L"12%", L"1.8GHz", L"--\u00B0C", L"--W",
               L"\u2212"
               L"8.4W",
               L"61%"},
              __LINE__);
}

}  // namespace

int main() {
  AllMissing();
  TypicalOnBattery();
  BatteryStates();
  RoundingAndLimits();
  NonFiniteIsMissing();
  if (g_failures == 0) std::puts("format_test: all passed");
  return g_failures == 0 ? 0 : 1;
}
