// Ed Callaghan
// Internal view of individual tracker ROC
// March 2026

#include "otsdaq-mu2e-tracker/Ui/ROC.hh"

#include <array>
#include <exception>
#include <format>
#include <stdexcept>
#include <string>

namespace trkdaq{
    ROC::ROC(link_t link, DTCLib::DTC* dtc):
            _link(link),
            _dtc(SharedDtcInterface::Get(dtc)),
            _nullstream(std::make_unique<trkdaq::NullStream>()),
            _null(_nullstream.get()){
        /**/
    }

    uint32_t ROC::ReadRegister(address_t address){
        auto rv = _dtc->ReadROCRegister(_link, address);
        return rv;
    }

    void ROC::WriteRegister(address_t address, uint16_t data){
        _dtc->WriteROCRegister(_link, address, data);
    }

    int ROC::Reset(){
        auto rv = _dtc->ResetLink(_link);
        return rv;
    }

    void ROC::ResetDigis(){
        // underlying call is deprecated
        //auto rv = _dtc->ResetDigis(_link);
        //return rv;

        // prefer a stacked reset of device and fifo
        trkdaq::registers::address_t address;

        // digis lo
        address = trkdaq::registers::roc::internal_digi_reset;
        this->WriteRegister(address, 0);
        std::this_thread::sleep_for(std::chrono::microseconds(10));

        // fifos lo
        address = trkdaq::registers::roc::from_digi_fifo_reset;
        this->WriteRegister(address, 0);
        std::this_thread::sleep_for(std::chrono::microseconds(10));

        // fifos hi
        address = trkdaq::registers::roc::from_digi_fifo_reset;
        this->WriteRegister(address, 1);
        std::this_thread::sleep_for(std::chrono::microseconds(10));

        // digis hi
        address = trkdaq::registers::roc::internal_digi_reset;
        this->WriteRegister(address, 1);
        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }

    int ROC::RebootMCU(){
        auto rv = _dtc->RebootMcu(_link);
        return rv;
    }

    int ROC::InitReadout(uint32_t                       rocReadoutMode,
                         uint16_t                       digitizationStart5ns,
                         uint16_t                       digitizationStop5ns,
                         uint8_t                        dtcId,
                         uint16_t                       eventWindowDelay5ns,
                         const ControlRoc_Read_Input_t0& readSettings,
                         std::ostream&                  output){
        auto rv = _dtc->InitReadoutROC(
                _link,
                rocReadoutMode,
                dtcId,
                digitizationStart5ns,
                digitizationStop5ns,
                eventWindowDelay5ns,
                readSettings,
                output);
        return rv;
    }

    int ROC::InitReadoutMode(std::ostream& stream){
        auto rv = _dtc->InitRocReadoutMode(stream);
        return rv;
    }

    void ROC::SetLaneMask(int mask){
        _dtc->SetRocLaneMask(mask);
    }

    void ROC::SetNHitsPerLane(int nhits){
        _dtc->SetRocNHitsPerLane(nhits);
    }

    int ROC::SetEventWindowDelay(uint16_t delay_5ns, std::ostream& stream){
        auto rv = _dtc->SetRocDelay(_link, delay_5ns, stream);
        return rv;
    }

    int ROC::SetDigitizationWindow(uint16_t t_start,
                                   uint16_t t_stop,
                                   int print_level,
                                   std::ostream& stream){
        auto rv = _dtc->SetRocDigitizationWindow(_link, t_start, t_stop, print_level, stream);
        return rv;
    }

    int ROC::ResetAndConfigure(uint16_t tdc_mode,
                               uint16_t lookback,
                               uint16_t sample_packets,
                               uint32_t mask_lo,
                               uint32_t mask_md,
                               uint32_t mask_hi,
                               uint16_t digitization_window_open,
                               uint16_t digitization_window_close){
      // upper bits: forward clock and EWMs from roc -> digis
      // lower bits: enable all 4 serdes lanes
      this->WriteRegister(trkdaq::registers::roc::readout_configuration,
                          0x030f);
      // reset counters
      auto rv = this->Reset();
      // clear digi -> roc fifos and reset digis
      this->ResetDigis();
      // set digitization window
      this->SetDigitizationWindow(digitization_window_open,
                                  digitization_window_close);
      // wrapped read command to set channel mask and begin triggering
      this->ConfigureDigis(tdc_mode, lookback, sample_packets,
                           mask_lo, mask_md, mask_hi);
      return rv;
    }

    int ROC::SetChannelMask(uint32_t mask_lo,
                            uint32_t mask_md,
                            uint32_t mask_hi){
        int rv = 0;
        int rc;

        // 96 = 32x3 bits must be repartitioned into 2x48 = 2x3x16 bit words
        uint16_t digi_mask_lo;
        uint16_t digi_mask_md;
        uint16_t digi_mask_hi;

        // lower bits go to cal-side
        digi_mask_lo = (mask_lo & 0x0000FFFF) >>  0;
        digi_mask_md = (mask_lo & 0xFFFF0000) >> 16;
        digi_mask_hi = (mask_md & 0x0000FFFF) >>  0;
        rc = this->SetDigiChannelMask(trkdaq::fpga::digi::cal, digi_mask_lo,
                                                               digi_mask_md,
                                                               digi_mask_hi);
        rv += rc;

        // upper bits go to hv-side
        digi_mask_lo = (mask_md & 0xFFFF0000) >> 16;
        digi_mask_md = (mask_hi & 0x0000FFFF) >>  0;
        digi_mask_hi = (mask_hi & 0xFFFF0000) >> 16;
        rc = this->SetDigiChannelMask(trkdaq::fpga::digi::hv,  digi_mask_lo,
                                                               digi_mask_md,
                                                               digi_mask_hi);
        rv += rc;

        return rv;
    }

    int ROC::SetDigiChannelMask(trkdaq::fpga_t fpga, uint16_t mask_lo,
                                                     uint16_t mask_md,
                                                     uint16_t mask_hi){
        int rv = 0;
        int print_level = 0;
        rv += this->DigiWrite(registers::digi::channel_mask_lo,
                              fpga, mask_lo, print_level, _null);
        rv += this->DigiWrite(registers::digi::channel_mask_md,
                              fpga, mask_md, print_level, _null);
        rv += this->DigiWrite(registers::digi::channel_mask_hi,
                              fpga, mask_hi, print_level, _null);
        return rv;
    }

	int ROC::DigiRW(uint16_t rw,
	                uint16_t hv_cal,
	                uint16_t address,
	                uint32_t data,
	                std::ostream& stream){
		trkdaq::ControlRoc_DigiRW_Input_t  in;
		trkdaq::ControlRoc_DigiRW_Output_t out;
		in.rw      = rw;
		in.hvcal   = hv_cal;
		in.address = address;
		// 32-bit word splits into two 16-bit words, low word first
		in.data[0] = (data      ) & 0xffff;
		in.data[1] = (data >> 16) & 0xffff;

		// PrintLevel bit 1 => emit the parsed output to the stream
		int print_level = 0x2;
		auto rv = _dtc->DigiRW(&in, &out, _link, print_level, stream);
		return rv;
	}

	int ROC::DigiRead(int addr,
	                  int hv_cal,
	                  uint32_t& res,
	                  int print_level,
	                  std::ostream& stream){
        auto rv = _dtc->DigiRead(addr, hv_cal, res, _link, print_level, stream);
        return rv;
    }

    int ROC::DigiWrite(int addr,
                       int hv_cal,
                       uint16_t dat,
                       int print_level,
                       std::ostream& stream){
        auto rv = _dtc->DigiWrite(addr, hv_cal, dat, _link, print_level, stream);
        return rv;
    }

	std::vector<uint16_t> ROC::InitializeDigis(){
		std::vector<uint16_t> res;
		_dtc->RocBlockRead(_link, 0x116, res);
		return res;
	}

	int ROC::ReadSpi(std::vector<uint16_t>& spi_raw_data,
	                 int print_level,
	                 std::ostream& stream){
		auto rv = _dtc->ReadSpi(spi_raw_data, _link, print_level, stream);
		return rv;
	}

	int ROC::PrintStatus(uint32_t format, std::ostream& stream){
		auto rv = _dtc->PrintRocStatus(format, _link, stream);
		return rv;
	}

	int ROC::ReadPanelID(int print_level){
        auto rv = _dtc->ReadPanelID(_link, print_level);
        return rv;
    }

    roc_serial_t ROC::ReadSerialNumber(){
        auto rv = _dtc->ReadSerialNumber(DTCLib::DTC_Link_ID(_link));
        return rv;
    }

    ControlRoc_DeviceID_t ROC::ReadDeviceInfo(){
        ControlRoc_DeviceID_t info;
        const int rc = _dtc->ReadDeviceID(_link, info, 0, _null);
        if (rc != 0)
            throw std::runtime_error(std::format(
                "READDEVICE failed for ROC link {} (return code {}).", _link, rc));
        return info;
    }

    std::string ROC::ReadFirmwareGitCommit(){
        std::string commit;
        const int rc = _dtc->ReadGitCommit(commit, _link, 0, _null);
        // READGITREV returns the 40 ASCII hex characters of LAST_GIT_REV.
        // Also reject READ_ERROR: older low-level code can return rc=0 on failure.
        if (rc != 0 || commit.size() != 40 ||
            commit.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
            throw std::runtime_error(std::format(
                "READGITREV failed or returned a malformed Git revision for ROC link {} "
                "(return code {}, {} characters).", _link, rc, commit.size()));
        return commit;
    }

    uint32_t ROC::ReadUserCode(){
        constexpr int readUserCode = 0x118; // ROC/utils.h: READUSERCODE = 280
        if (_link < 0 || _link >= 6)
            throw std::runtime_error(std::format("READUSERCODE: invalid ROC link {}.", _link));
        std::vector<uint16_t> data;
        const int rc = _dtc->RocBlockRead(_link, readUserCode, data, 4);
        if (rc != 0 || data.size() != 4)
            throw std::runtime_error(std::format(
                "READUSERCODE failed for ROC link {} (return code {}, {} payload words; "
                "expected 4).", _link, rc, data.size()));
        uint32_t userCode = 0;
        // SYS_get_user_code returns the least-significant byte first.
        // See ROC/iaputils.c::execute_usercode_service for the MSB-first display.
        for (size_t i = 0; i < data.size(); ++i) {
            if (data[i] > 0xff)
                throw std::runtime_error(std::format(
                    "READUSERCODE: non-byte payload word {} for ROC link {}.", i, _link));
            userCode |= static_cast<uint32_t>(data[i]) << (8 * i);
        }
        return userCode;
    }

    int ROC::FindAlignment(){
        // Alignment enables every channel. Save the raw hardware words first;
        // restoring them must not apply the logical-channel mapping again.
        const std::array<int, 3> maskAddresses = {
            registers::digi::channel_mask_lo,
            registers::digi::channel_mask_md,
            registers::digi::channel_mask_hi};
        const std::array<int, 2> digis = {fpga::digi::cal, fpga::digi::hv};
        std::array<std::array<uint16_t, 3>, 2> savedMasks{};
        for (size_t digi = 0; digi < digis.size(); ++digi) {
            for (size_t word = 0; word < maskAddresses.size(); ++word) {
                uint32_t value = 0;
                const int rc = this->DigiRead(maskAddresses[word], digis[digi],
                                             value, 0, _null);
                if (rc != 0 || value > 0xffffu)
                    throw std::runtime_error(std::format(
                        "FindAlignment: cannot save mask, link {} DIGI {} register 0x{:02x}, rc {}",
                        _link, digis[digi], maskAddresses[word], rc));
                savedMasks[digi][word] = static_cast<uint16_t>(value);
            }
        }

        // A failed alignment may already have changed masks. Defer its error
        // until all six restoration attempts and their readbacks are complete.
        std::exception_ptr alignmentError;
        int rv = 0;
        try {
            rv = _dtc->FindAlignment(DTCLib::DTC_Link_ID(_link), _alignment);
        }
        catch (...) {
            alignmentError = std::current_exception();
        }

        std::exception_ptr restoreError;
        for (size_t digi = 0; digi < digis.size(); ++digi) {
            for (size_t word = 0; word < maskAddresses.size(); ++word) {
                try {
                    const int writeRc = this->DigiWrite(maskAddresses[word], digis[digi],
                                                       savedMasks[digi][word], 0, _null);
                    uint32_t value = 0;
                    const int readRc = this->DigiRead(maskAddresses[word], digis[digi],
                                                    value, 0, _null);
                    if (writeRc != 0 || readRc != 0 || value != savedMasks[digi][word])
                        throw std::runtime_error(std::format(
                            "link {} DIGI {} register 0x{:02x}: expected 0x{:04x}, read 0x{:04x}, write rc {}, read rc {}",
                            _link, digis[digi], maskAddresses[word], savedMasks[digi][word],
                            value, writeRc, readRc));
                }
                catch (...) {
                    if (!restoreError) restoreError = std::current_exception();
                }
            }
        }
        if (restoreError) {
            const auto describe = [](std::exception_ptr error) -> std::string {
                try { std::rethrow_exception(error); }
                catch (const std::exception& ex) { return ex.what(); }
                catch (...) { return "unknown exception"; }
            };
            throw std::runtime_error(
                "FindAlignment: mask restoration failed: " + describe(restoreError) +
                (alignmentError ? "; alignment also failed: " + describe(alignmentError)
                                : std::format("; alignment rc {}", rv)));
        }
        if (alignmentError) std::rethrow_exception(alignmentError);
        return rv;
    }

    int ROC::ReadThresholds(std::vector<float>& out,
                            uint32_t mask_lo,
                            uint32_t mask_md,
                            uint32_t mask_hi,
                            int print_level,
                            std::ostream& stream){
        auto rv = _dtc->ReadThresholds(_link, out, mask_lo, mask_md, mask_hi, print_level, stream);
        return rv;
    }

    int ROC::SetThreshold(int channel, int preamp, int dac, int print_level){
        auto rv = _dtc->SetThreshold(_link, channel, preamp, dac, print_level);
        return rv;
    }

    bool ROC::FindThreshold(int channel,
                            int preamp,
                            float threshold_mv,
                            float tolerance_mv,
                            DTCLib::roc_data_t& out, ThresholdSearchResult* detail){
        auto rv = _dtc->FindThreshold(_link, channel, preamp, threshold_mv, tolerance_mv, out, detail);
        return rv;
    }

    int ROC::FindThresholds(int channel,
                            float threshold_mv,
                            float tolerance_mv,
                            DTCLib::roc_data_t& cal_out,
                            DTCLib::roc_data_t& hv_out){
        // preamp 0 = CAL, preamp 1 = HV
        int n_failed = 0;
        if (not this->FindThreshold(channel, 0, threshold_mv, tolerance_mv, cal_out)) n_failed++;
        if (not this->FindThreshold(channel, 1, threshold_mv, tolerance_mv, hv_out )) n_failed++;
        return n_failed;
    }

    int ROC::FindThresholds(float threshold_mv,
                            float tolerance_mv,
                            std::vector<DTCLib::roc_data_t>& out,
                            std::vector<ThresholdSearchResult>* details,
                            const std::function<void(size_t, size_t)>& progress){
        out.clear();
        if (details) details->clear();
        if (progress) progress(0, 192);
        int n_failed = 0;
        for (int channel = 0; channel < 96; ++channel)
            for (int preamp = 0; preamp < 2; ++preamp) {
                DTCLib::roc_data_t dac;
                ThresholdSearchResult detail;
                if (!FindThreshold(channel, preamp, threshold_mv, tolerance_mv, dac, &detail))
                    ++n_failed;
                out.push_back(dac);
                if (details) details->push_back(detail);
                if (progress) progress(out.size(), 192);
            }
        return n_failed;
    }

    int ROC::NotoriousRead(uint16_t adc_mode,
                         uint16_t tdc_mode,
                         uint16_t num_lookback,
                         uint16_t num_samples,
                         uint32_t num_triggers,
                         uint32_t mask_lo,
                         uint32_t mask_md,
                         uint32_t mask_hi,
                         uint16_t enable_pulser,
                         uint16_t marker_clock,
                         uint16_t mode,
                         uint16_t clock,
                         std::ostream& stream){
    trkdaq::ControlRoc_Read_Input_t0 par;
    par.adc_mode        = adc_mode;
    par.tdc_mode        = tdc_mode;
    par.num_lookback    = num_lookback;
    par.num_samples     = num_samples;
    // 32-bit count splits into two 16-bit words, low word first:
    // num_triggers[0] = count & 0xffff, num_triggers[1] = count >> 16
    par.num_triggers[0] = (num_triggers      ) & 0xffff;
    par.num_triggers[1] = (num_triggers >> 16) & 0xffff;
    // each 32-bit mask splits into two 16-bit words, low word first:
    // ch_mask[2*i] = mask & 0xffff, ch_mask[2*i+1] = mask >> 16
    par.ch_mask[0]      = (mask_lo      ) & 0xffff;
    par.ch_mask[1]      = (mask_lo >> 16) & 0xffff;
    par.ch_mask[2]      = (mask_md      ) & 0xffff;
    par.ch_mask[3]      = (mask_md >> 16) & 0xffff;
    par.ch_mask[4]      = (mask_hi      ) & 0xffff;
    par.ch_mask[5]      = (mask_hi >> 16) & 0xffff;
    par.enable_pulser   = enable_pulser;
    par.marker_clock    = marker_clock;
    par.mode            = mode;
    par.clock           = clock;

    // PrintLevel bit 1 => emit the parsed fields to the stream
    int print_level = 0x2;
    auto rv = _dtc->Read(&par, _link, print_level, stream);
    return rv;
  }

  int ROC::ConfigureDigis(uint16_t tdc_mode,
                          uint16_t num_lookback,
                          uint16_t num_samples,
                          uint32_t mask_lo,
                          uint32_t mask_md,
                          uint32_t mask_hi,
                          std::ostream& stream){
    // hardcoded defaults for the rarely-changed parameters
    uint16_t adc_mode      = 0;
    uint32_t num_triggers  = 0;
    uint16_t enable_pulser = 0;
    uint16_t marker_clock  = 3; // receive markers from over fiber
    uint16_t mode          = 0;
    uint16_t clock         = 99;

    auto rv = this->NotoriousRead(adc_mode,
                                  tdc_mode,
                                  num_lookback,
                                  num_samples,
                                  num_triggers,
                                  mask_lo,
                                  mask_md,
                                  mask_hi,
                                  enable_pulser,
                                  marker_clock,
                                  mode,
                                  clock,
                                  stream);
    return rv;
  }

  int ROC::ChannelRates(uint16_t tdc_mode, std::vector<rates_t>& rates,
                        std::array<uint16_t, 6>* masksBeforeRead){
    // hardcoded defaults for the rarely-changed parameters
    uint16_t adc_mode      = 0;
    uint16_t num_lookback  = 0;
    uint16_t num_samples   = 1;
    uint32_t num_triggers  = 0;
    uint32_t mask_lo       = 0xFFFFFFFF;
    uint32_t mask_md       = 0xFFFFFFFF;
    uint32_t mask_hi       = 0xFFFFFFFF;
    uint16_t enable_pulser = 0;
    uint16_t marker_clock  = 0; // internal markers
    uint16_t mode          = 0;
    uint16_t clock         = 99;

    // Save raw DIGI masks before NotoriousRead enables every channel. These
    // are already in hardware channel order; do not remap them on restoration.
    rates.clear();
    const std::array<int, 3> maskAddresses = {
        registers::digi::channel_mask_lo,
        registers::digi::channel_mask_md,
        registers::digi::channel_mask_hi};
    const std::array<int, 2> digis = {fpga::digi::cal, fpga::digi::hv};
    std::array<std::array<uint16_t, 3>, 2> savedMasks{};
    for (size_t digi = 0; digi < digis.size(); ++digi) {
      for (size_t word = 0; word < maskAddresses.size(); ++word) {
        uint32_t value = 0;
        const int rc = this->DigiRead(maskAddresses[word], digis[digi],
                                     value, 0, _null);
        if (rc != 0 || value > 0xffffu)
          throw std::runtime_error(std::format(
              "ChannelRates: cannot save mask, link {} DIGI {} register 0x{:02x}, rc {}",
              _link, digis[digi], maskAddresses[word], rc));
        savedMasks[digi][word] = static_cast<uint16_t>(value);
      }
    }

    // Keep the acquisition exception until all six masks have been restored.
    // A setup failure may occur after it has already changed some registers.
    std::exception_ptr acquisitionError;
    int rv = 0;
    std::vector<uint16_t> readings;
    try {
      rv = this->NotoriousRead(adc_mode, tdc_mode, num_lookback, num_samples,
                               num_triggers, mask_lo, mask_md, mask_hi,
                               enable_pulser, marker_clock, mode, clock, _null);
      if (rv == 0) {
        ControlRoc_Rates_t par;
        rv = _dtc->Rates(_link, &readings, 0, &par, _null);
      }
    }
    catch (...) {
      acquisitionError = std::current_exception();
    }

    // Attempt every restoration even if one write/readback fails.
    std::exception_ptr restoreError;
    for (size_t digi = 0; digi < digis.size(); ++digi) {
      for (size_t word = 0; word < maskAddresses.size(); ++word) {
        try {
          const int writeRc = this->DigiWrite(maskAddresses[word], digis[digi],
                                              savedMasks[digi][word], 0, _null);
          uint32_t value = 0;
          const int readRc = this->DigiRead(maskAddresses[word], digis[digi],
                                           value, 0, _null);
          if (writeRc != 0 || readRc != 0 || value != savedMasks[digi][word])
            throw std::runtime_error(std::format(
                "link {} DIGI {} register 0x{:02x}: expected 0x{:04x}, read 0x{:04x}, write rc {}, read rc {}",
                _link, digis[digi], maskAddresses[word], savedMasks[digi][word],
                value, writeRc, readRc));
        }
        catch (...) {
          if (!restoreError) restoreError = std::current_exception();
        }
      }
    }
    if (restoreError) {
      const auto describe = [](std::exception_ptr error) -> std::string {
        try { std::rethrow_exception(error); }
        catch (const std::exception& ex) { return ex.what(); }
        catch (...) { return "unknown exception"; }
      };
      throw std::runtime_error(
          "ChannelRates: mask restoration failed: " + describe(restoreError) +
          (acquisitionError ? "; acquisition also failed: " + describe(acquisitionError)
                            : std::format("; acquisition rc {}", rv)));
    }
    if (acquisitionError) std::rethrow_exception(acquisitionError);
    if (rv != 0) return rv;
    if (readings.size() != 580)
      throw std::runtime_error(std::format(
          "ChannelRates: expected 580 rate words, received {}", readings.size()));

    // Decode only after hardware masks have been restored successfully.
    float period = 5e-9; // 200 MHz clock <-> 5 ns tick spacing
    uint32_t ticks_lo = readings[578] + (readings[579] << 16); // lower 48 channels
    uint32_t ticks_hi = readings[576] + (readings[577] << 16); // upper 48 channels
    uint32_t ticks_mean = 0.5*(ticks_lo + ticks_hi); // does this really make sense?
    rates.clear();
    rates.resize(96);
    for (size_t i = 0 ; i < rates.size() ; i++){
      size_t idx = 6*i;
      uint32_t counts_hiv = readings[idx + 0] + (readings[idx + 1] << 16);
      uint32_t counts_cal = readings[idx + 2] + (readings[idx + 3] << 16);
      uint32_t counts_coi = readings[idx + 4] + (readings[idx + 5] << 16);

      auto ticks = ticks_lo;
      if (47 < i){
        ticks = ticks_hi;
      }
      float rate_hiv = 1e-3 * static_cast<float>(counts_hiv) / ticks / period;
      float rate_cal = 1e-3 * static_cast<float>(counts_cal) / ticks / period;

      ticks = ticks_mean;
      float rate_coi = 1e-3 * static_cast<float>(counts_coi) / ticks / period;

      // populate output
      rates[i] = std::make_tuple(rate_hiv, rate_cal, rate_coi);
    }

    if (masksBeforeRead) {
      // CAL then HV, each in register order 0x0B, 0x0E, 0x0D.
      for (size_t digi = 0; digi < savedMasks.size(); ++digi)
        for (size_t word = 0; word < savedMasks[digi].size(); ++word)
          (*masksBeforeRead)[3 * digi + word] = savedMasks[digi][word];
    }
    return rv;
  }

  int ROC::EnableChargeInjection(int first_channel_mask,
                                   int duty_cycle,
                                   int delay,
                                   int print_level,
                                   std::ostream& stream){
        auto rv = _dtc->PulserOn(_link, first_channel_mask, duty_cycle, delay, print_level, stream);
        return rv;
    }

    int ROC::DisableChargeInjection(int print_level, std::ostream& stream){
        auto rv = _dtc->PulserOff(_link, print_level, stream);
        return rv;
    }

    const Alignment& ROC::LatestAlignment(){
        return _alignment;
    }
} // namespace trkdaq
