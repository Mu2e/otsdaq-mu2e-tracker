#ifndef TRKDAQ_THRESHOLD_SEARCH_HH
#define TRKDAQ_THRESHOLD_SEARCH_HH

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

namespace trkdaq {
struct ThresholdSearchResult {
  int dac = -1;
  float measured_mv = std::numeric_limits<float>::quiet_NaN();
  float error_mv = std::numeric_limits<float>::quiet_NaN();
  float endpoint0_mv = std::numeric_limits<float>::quiet_NaN();
  float endpoint1023_mv = std::numeric_limits<float>::quiet_NaN();
  std::array<float, 5> verification_mv{};
  unsigned measurements = 0;
  std::string status = "not_run";
  bool success() const { return status == "ok"; }
};

// Measurements are signed ROC voltages; user-facing results use the requested
// threshold convention, i.e. +12 mV means a raw measurement of -12 mV.
// Callers must serialize access to the DTC. No threads or locks are used here.
template<class Program, class Measure>
ThresholdSearchResult SearchThreshold(float threshold, float tolerance,
                                      Program program, Measure measure) {
  if (!std::isfinite(threshold) || !std::isfinite(tolerance) || tolerance <= 0)
    throw std::invalid_argument("Threshold must be finite and tolerance finite and positive.");
  ThresholdSearchResult result;
  const float target = -threshold;
  auto read = [&]() {
    const float value = measure();
    ++result.measurements;
    if (!std::isfinite(value))
      throw std::runtime_error("Non-finite threshold measurement; search stopped.");
    return value;
  };
  std::map<int, float> medians;
  float bestError = std::numeric_limits<float>::infinity();
  auto sample = [&](int dac) {
    const auto found = medians.find(dac);
    if (found != medians.end()) return found->second;
    program(dac);
    std::array<float, 3> values{read(), read(), read()};
    std::sort(values.begin(), values.end());
    const float median = values[1];
    medians.emplace(dac, median);
    const float error = std::fabs(median - target);
    if (error < bestError) {
      bestError = error;
      result.dac = dac;
    }
    return median;
  };

  const float first = sample(0), last = sample(1023);
  result.endpoint0_mv = -first;
  result.endpoint1023_mv = -last;
  // Endpoint evidence applies to a monotonic DAC response. Do not claim a
  // hardware defect: an out-of-range response is reported as unreachable.
  const bool unreachable = target <= std::min(first, last) - tolerance ||
                           target >= std::max(first, last) + tolerance;
  if (!unreachable && first != last) {
    int low = 0, high = 1023;
    const bool increasing = last > first;
    while (high - low > 1) {
      const int middle = low + (high - low) / 2;
      const float value = sample(middle);
      if ((value < target) == increasing) low = middle;
      else high = middle;
    }
    // Noise can move the apparent crossing by a DAC code. Compare nearby
    // candidates using repeated readings rather than accepting the first hit.
    const int center = result.dac;
    for (int dac = std::max(0, center - 2); dac <= std::min(1023, center + 2); ++dac)
      sample(dac);
  }

  // Always leave the selected DAC programmed, including unsuccessful searches.
  // All five fresh readings must satisfy the user's original strict tolerance.
  program(result.dac);
  bool stable = true;
  for (auto& value : result.verification_mv) {
    value = -read();
    stable = stable && std::fabs(value - threshold) < tolerance;
  }
  auto sorted = result.verification_mv;
  std::sort(sorted.begin(), sorted.end());
  result.measured_mv = sorted[2];
  result.error_mv = result.measured_mv - threshold;
  result.status = stable ? "ok" : unreachable ? "unreachable" :
      (bestError < tolerance ? "unstable" : "out_of_tolerance");
  return result;
}

inline std::string FormatThresholdSearchResult(int channel, int preamp,
                                               const ThresholdSearchResult& r) {
  std::ostringstream out;
  out.setf(std::ios::fixed);
  out.precision(3);
  out << "channel=" << channel << " preamp=" << (preamp == 0 ? "CAL" : "HV")
      << " status=" << r.status << " DAC=" << r.dac
      << " measured_mV=" << r.measured_mv << " error_mV=" << r.error_mv
      << " endpoint0_mV=" << r.endpoint0_mv
      << " endpoint1023_mV=" << r.endpoint1023_mv << " verification_mV=[";
  for (unsigned i = 0; i < r.verification_mv.size(); ++i)
    out << (i ? ", " : "") << r.verification_mv[i];
  out << "] measurements=" << r.measurements << '\n';
  return out.str();
}
} // namespace trkdaq
#endif
