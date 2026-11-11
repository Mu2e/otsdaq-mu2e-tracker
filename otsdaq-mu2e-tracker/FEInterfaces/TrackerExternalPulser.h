#ifndef OTS_TRACKER_EXTERNAL_PULSER_H
#define OTS_TRACKER_EXTERNAL_PULSER_H

#include <cmath>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>

namespace tracker_external_pulser {
struct Settings {
    int channel, mask, period, highTicks;
    double frequencyHz, dutyFactor, clockHz;
};

inline Settings settings(int channel, double frequencyHz = 50000.0,
                         double dutyFactor = 0.01, double clockHz = 50000000.0) {
    if(channel < 0 || channel > 7 || !std::isfinite(frequencyHz) || frequencyHz <= 0 ||
       !std::isfinite(dutyFactor) || dutyFactor <= 0 || dutyFactor >= 1 ||
       !std::isfinite(clockHz) || clockHz <= 0)
        throw std::invalid_argument("External pulser: channel must be 0..7, frequency/clock positive, duty factor strictly between 0 and 1");
    // CorePWM: period = granularity * (PERIOD+1). The checked-in FPGA fixes
    // prescale at zero, so granularity is 1/clockHz, regardless of PWM_init(1).
    const double ticks = std::round(clockHz / frequencyHz);
    if(!std::isfinite(ticks) || ticks < 2 || ticks > double(std::numeric_limits<int32_t>::max()) + 1)
        throw std::invalid_argument("External pulser frequency is outside the period register/API range");
    const double high = std::round(ticks * dutyFactor);
    if(high < 1 || high > 65535 || high >= ticks)
        throw std::invalid_argument("External pulser duty factor cannot be represented by the 16-bit high-time field");
    return {channel, 1 << channel, int(ticks - 1), int(high),
            clockHz / ticks, high / ticks, clockHz};
}

inline int64_t rawCount(const std::string& text) {
    size_t end = 0;
    const int64_t value = std::stoll(text, &end, 10);
    if(end != text.size())
        throw std::invalid_argument("External pulser counts must be decimal integers");
    return value;
}

// Script-compatible raw values: -d is PERIOD, -y is the high-time count.
// The tick frequency is used for diagnostics only; neither value is converted.
inline Settings rawSettings(int channel, int64_t delay, int64_t width) {
    if(channel < 0 || channel > 7 || delay < 1 ||
       delay > std::numeric_limits<int32_t>::max() || width < 1 ||
       width > 65535 || width > delay)
        throw std::invalid_argument("External pulser: channel must be 0..7, Delay (-d) an integer 1..2147483647, Pulse width (-y) an integer 1..65535 and no greater than delay");
    constexpr double clockHz = 50000000.0; // nominal; not used for register values
    const double ticks = double(delay) + 1.0;
    return {channel, 1 << channel, int(delay), int(width),
            clockHz / ticks, double(width) / ticks, clockHz};
}

// Read only the configured counts: no built-in fallback hides a missing row.
template<class Read> Settings configuredSettings(int channel, Read read) {
    const auto delay = rawCount(read("ExternalPulserDelayCounts"));
    const auto width = rawCount(read("ExternalPulserWidthCounts"));
    return rawSettings(channel, delay, width);
}

// Only scan-owned state is changed. Serializes macro preparation and FSM callbacks.
class Run {
    mutable std::mutex mutex_;
    Settings settings_{};
    bool prepared_ = false, active_ = false, uncertain_ = false;
public:
    void prepare(const Settings& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(active_) throw std::runtime_error("External pulser is active or cleanup is unconfirmed; Stop/Halt before preparing another step");
        settings_ = value;
        prepared_ = true;
    }
    template<class Enable, class Disable> void start(Enable enable, Disable disable) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(uncertain_) throw std::runtime_error("External pulser state is unconfirmed; Stop/Halt before retrying Start");
        if(!prepared_ || active_) return;
        active_ = true; // A failed transport may still have enabled hardware.
        uncertain_ = true;
        try { enable(settings_); uncertain_ = false; }
        catch(...) {
            auto error = std::current_exception();
            try { disable(); active_ = false; uncertain_ = false; prepared_ = false; }
            catch(const std::exception& cleanup) {
                throw std::runtime_error(std::string("External pulser Start failed and cleanup is unconfirmed: ") + cleanup.what());
            }
            std::rethrow_exception(error);
        }
    }
    template<class Disable> void stop(Disable disable, bool clearPrepared = true) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(active_) { uncertain_ = true; disable(); active_ = false; uncertain_ = false; }
        if(clearPrepared) prepared_ = false;
    }
    bool active() const { std::lock_guard<std::mutex> lock(mutex_); return active_; }
    std::string summary() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream out;
        out << std::setprecision(12) << "channel_mod8=" << settings_.channel
            << " mask=" << settings_.mask << " frequency_hz=" << settings_.frequencyHz
            << " duty_factor=" << settings_.dutyFactor << " pwm_clock_hz=" << settings_.clockHz
            << " period=" << settings_.period << " high_ticks=" << settings_.highTicks
            << " prepared=" << prepared_ << " active_or_unconfirmed=" << active_;
        return out.str();
    }
};
}
#endif
