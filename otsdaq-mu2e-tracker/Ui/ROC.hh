// Ed Callaghan
// Internal view of individual tracker ROC
// March 2026

#ifndef __trkdaq_roc_hh__
#define __trkdaq_roc_hh__

// stl
#include <array>
#include <chrono>
#include <memory>
#include <functional>
#include <streambuf>
#include <thread>


// otsdaq-mu2e-tracker
#include "otsdaq-mu2e-tracker/Ui/SharedDtcInterface.hh"
#include "otsdaq-mu2e-tracker/Ui/TrackerRegisters.hh"

namespace trkdaq{
    class NullStream; // forward

    class ROC{
        public:
            using link_t = int;
            using address_t = DTCLib::roc_address_t;
            ROC(link_t, DTCLib::DTC*);

            uint32_t ReadRegister(address_t address);
            void WriteRegister(address_t address, uint16_t data);
            int Reset();
            void ResetDigis();
            int RebootMCU();
            int FindAlignment();

            // top-level reset before beginning nominal data-taking
            int ResetAndConfigure(uint16_t tdc_mode,
                                  uint16_t lookback,
                                  uint16_t sample_packets,
                                  uint32_t mask_lo,
                                  uint32_t mask_md,
                                  uint32_t mask_hi,
                                  uint16_t digitization_window_open,
                                  uint16_t digitization_window_close);

            // readout / lane configuration
            int InitReadout(uint32_t                       rocReadoutMode,
                            uint16_t                       digitizationStart5ns,
                            uint16_t                       digitizationStop5ns,
                            uint8_t                        dtcId,
                            uint16_t                       eventWindowDelay5ns,
                            const ControlRoc_Read_Input_t0& readSettings,
                            std::ostream&                  output);
            int InitReadoutMode(std::ostream& Stream = std::cout);
            void SetLaneMask(int Mask);
            void SetNHitsPerLane(int NHits);
            int SetEventWindowDelay(uint16_t Delay5ns,
                                    std::ostream& Stream = std::cout);
            int SetDigitizationWindow(uint16_t TStart,
                                      uint16_t TStop,
                                      int PrintLevel = 0,
                                      std::ostream& Stream = std::cout);
            int SetChannelMask(uint32_t mask_lo,
                               uint32_t mask_md,
                               uint32_t mask_hi);
            int SetDigiChannelMask(trkdaq::fpga_t fpga,
                                   uint16_t mask_lo,
                                   uint16_t mask_md,
                                   uint16_t mask_hi);

            // digi register access
            // low-level digi read/write; Data is the 32-bit word that the
            // firmware carries as two 16-bit words.
            int DigiRW(uint16_t Rw,
                       uint16_t HvCal,
                       uint16_t Address,
                       uint32_t Data,
                       std::ostream& Stream = std::cout);
            int DigiRead (int Addr,
                          int HvCal,
                          uint32_t& Res,
                          int PrintLevel = 0,
                          std::ostream& Stream = std::cout);
            int DigiWrite(int Addr,
                          int HvCal,
                          uint16_t Dat,
                          int PrintLevel = 0,
                          std::ostream& Stream = std::cout);

            // initialize the digi FPGAs via a block read of register 0x116,
            // returning the read-back words
            std::vector<uint16_t> InitializeDigis();

            // status
            int ReadSpi(std::vector<uint16_t>& SpiRawData,
                        int PrintLevel = 0,
                        std::ostream& Stream = std::cout);
            int PrintStatus(uint32_t Format = 1,
                            std::ostream& Stream = std::cout);

            // identity
            int ReadPanelID(int PrintLevel = 0);
            roc_serial_t ReadSerialNumber();
            ControlRoc_DeviceID_t ReadDeviceInfo();
            std::string ReadFirmwareGitCommit();
            uint32_t ReadUserCode();
            int ReadThresholds(std::vector<float>& Thr,
                               uint32_t MaskC = 0xFFFFFFFF,
                               uint32_t MaskD = 0xFFFFFFFF,
                               uint32_t MaskE = 0xFFFFFFFF,
                               int PrintLevel = 0x2,
                               std::ostream& Stream = std::cout );
            int SetThreshold(int ChannelID,
                             int PreampType,
                             int Threshold,
                             int PrintLevel = 0);
            bool FindThreshold(int ChannelID,
                               int PreampType,
                               float ThresholdMv,
                               float ToleranceMv,
                               DTCLib::roc_data_t& Out,
                               ThresholdSearchResult* Detail = nullptr);

            // find thresholds for both preamps (CAL then HV) of a single channel.
            // returns the number of searches which failed to converge.
            int FindThresholds(int ChannelID,
                               float ThresholdMv,
                               float ToleranceMv,
                               DTCLib::roc_data_t& CalOut,
                               DTCLib::roc_data_t& HvOut);

            // find thresholds for all 96 channels, both preamps.
            // Out is filled with 192 DAC values, sorted by channel then preamp
            // (CAL before HV): Out[2*ch+0] = CAL, Out[2*ch+1] = HV.
            // returns the number of searches which failed to converge.
            int FindThresholds(float ThresholdMv,
                               float ToleranceMv,
                               std::vector<DTCLib::roc_data_t>& Out,
                               std::vector<ThresholdSearchResult>* Details = nullptr,
                               const std::function<void(size_t, size_t)>& Progress = {});

            // the notorious "read" command: program an acquisition and read
            // back the resulting configuration. all 16 input words are exposed.
            int NotoriousRead(uint16_t AdcMode,
                              uint16_t TdcMode,
                              uint16_t NumLookback,
                              uint16_t NumSamples,
                              uint32_t NumTriggers,
                              uint32_t MaskLo,
                              uint32_t MaskMd,
                              uint32_t MaskHi,
                              uint16_t EnablePulser,
                              uint16_t MarkerClock,
                              uint16_t Mode,
                              uint16_t Clock,
                              std::ostream& Stream = std::cout);

            // configure the digi readout: a NotoriousRead with sensible
            // hardcoded defaults for the rarely-changed parameters.
            int ConfigureDigis(uint16_t TdcMode,
                               uint16_t NumLookback,
                               uint16_t NumSamples,
                               uint32_t MaskLo,
                               uint32_t MaskMd,
                               uint32_t MaskHi,
                               std::ostream& Stream = std::cout);

            // read per-channel trigger rates
            using rates_t = std::tuple<float,float,float>;
            int ChannelRates(uint16_t tdc_mode, std::vector<rates_t >& rates,
                             std::array<uint16_t, 6>* masksBeforeRead = nullptr);

            int EnableChargeInjection(int FirstChannelMask = 0x10,
                                      int DutyCycle        = 10  ,
                                      int PulserDelay      = 1000,
                                      int PrintLevel       = 0x2,
                                      std::ostream& Stream = std::cout);
            int DisableChargeInjection(int PrintLevel = 0,
                                       std::ostream& Stream = std::cout);

            const Alignment& LatestAlignment();

    protected:
            link_t _link;
            std::shared_ptr<SharedDtcInterface> _dtc;
            std::unique_ptr<trkdaq::NullStream> _nullstream;
            std::ostream _null;
            Alignment _alignment;

        private:
            /**/
    };

    class NullStream: public std::streambuf{
        public:
            int overflow(int c){
                return 0;
            }
    };
} // namespace trkdaq

#endif
