#pragma once

#include <array>

#include "metrics.h"

namespace tbm {

inline constexpr int kRows = 2;
inline constexpr int kColumns = 3;

using Cell = std::array<wchar_t, 16>;

// Row-major grid of values; the overlay draws an icon before each to tell them apart:
//   CPU usage    CPU clock      CPU temperature
//   CPU power    battery flow   memory load
using Cells = std::array<Cell, kRows * kColumns>;

// Values with units only, no labels. Fixed-size, no allocation. Missing values read "--".
Cells FormatMetrics(const Metrics& metrics);

// The widest text each column can show, per row. Columns are sized from these so changing
// values never shift the layout.
extern const wchar_t* const kWidestText[kColumns][kRows];

}  // namespace tbm
