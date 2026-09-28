#ifndef _ots_ROCTrackerInterface_h_
#define _ots_ROCTrackerInterface_h_

#include <array>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "otsdaq-mu2e/FEInterfaces/ROCPolarFireCoreInterface.h"
#include "otsdaq/DataManager/DataProducer.h"
#include "otsdaq/FECore/FEProducerVInterface.h"

#include "artdaq-core-mu2e/Overlays/DTC_Types/DTC_Link_ID.h"
#include "artdaq-core-mu2e/Overlays/Decoders/TrackerDataDecoder.hh"
#include "otsdaq-mu2e-tracker/ParseAlignment/Alignment.hh"
#include "otsdaq-mu2e-tracker/ParseAlignment/PrintLegacyTable.hh"
#include "otsdaq-mu2e-tracker/Ui/ControlRocTypes.hh"

#include "otsdaq-mu2e-tracker/Ui/BisectionSearch.hh"
#include "otsdaq-mu2e-tracker/Ui/ControlRocTypes.hh"
#include "otsdaq-mu2e-tracker/Ui/ProgramRoc.hh"

#include "otsdaq-mu2e-tracker/Ui/ROC.hh"

#include <nlohmann/json.hpp>

namespace ots
{
class ROCTrackerInterface : public ROCPolarFireCoreInterface
{
	// clang-format off

	/// Note:  Tracker UI source code is imported by running the following e.g. from srcs/otsdaq-mu2e-tracker:
	/// 		otsdaq_import_tracker_test_stand.sh otsdaq-mu2e-tracker/Ui/ otsdaq-mu2e-tracker/FEInterfaces/ > t.txt
	/// Tracker UI Cold-start Steps:
	///	1. FindAlignments  (ADC serial data alignment, ADCs are internal FPGA)
	///	2. Program thresholds
	///		loop through all ControlRoc_SetThreshold
	///	3. Check threshold programming
	///		ControlRoc_MeasureThresholds reads in voltage units
	///  (do not need to set Gains, at the moment)
	///	4. Ready to setup for a run
	///		InitReadout()/InitRocReadoutMode()
	///			Lane = fifo = 48 channels (ends of a straw) non-sequential
	///				1 ROC = 4 x 48 channels = 1 panel = 96 straws (Cal side and HV side of a straw)
	///
	///
	/// Cal pulsing -- internal mode is fixed-amplitude signal on all 96 ADCs, external mode is variable-amplitude analog signal into preamp of chosen channels

public:
	ROCTrackerInterface(const std::string &rocUID,
						const ConfigurationTree &theXDAQContextConfigTree,
						const std::string &interfaceConfigurationPath);

	~ROCTrackerInterface(void);

	virtual void onDTCReady();

	using address_t = trkdaq::ROC::address_t;
	std::string getFirmwareVersion(void) override;
	void InitReadout(__ARGS__);
	void ReadRegister(__ARGS__);
	void ResetCounters(__ARGS__);
	void GetDTCCounterValues(__ARGS__);
	void FindAlignment(__ARGS__);
	void SetThreshold(__ARGS__);
	void MeasureThreshold(__ARGS__);

	void EnableChargeInjection(__ARGS__);
	void DisableChargeInjection(__ARGS__);

	void ResetDigis(__ARGS__);
	void ResetAndConfigure(__ARGS__);
	void RebootMCU(__ARGS__);
	void SetEventWindowDelay(__ARGS__);
	void SetDigitizationWindow(__ARGS__);
	void DigiRead(__ARGS__);
	void PrintDigis(__ARGS__);
	void Preflight(__ARGS__);
	void DigiWrite(__ARGS__);
	void ReadPanelID(__ARGS__);
	void ReadSerialNumber(__ARGS__);
	void MeasureThresholds(__ARGS__);
	void FindThreshold(__ARGS__);
	void FindThresholds(__ARGS__);
	void SetChannelMask(__ARGS__);
	std::map<std::string, std::string> getFEMacroInputDefaults(
	    const std::string&                        feMacroName,
	    const std::map<std::string, std::string>& currentInputValues) const override;
	void NotoriousRead(__ARGS__);
	void ConfigureDigis(__ARGS__);
	void InitializeDigis(__ARGS__);
	void DigiRW(__ARGS__);
	void ReadSPI(__ARGS__);
	void PrintStatus(__ARGS__);
  void MeasureChannelRates(__ARGS__);

	void FindAndSerializeThresholds(__ARGS__);
	void DeserializeAndSetThresholds(__ARGS__);
	void TestJSON(__ARGS__);

	void UpdateChannelThresholds(__ARGS__);

	// state machine
	//----------------
	void configure(void) override;
	void start(std::string runNumber) override;
	void stop(void) override;
	bool running(void) override;

	// write and read to registers
	virtual void writeEmulatorRegister(uint16_t address,
                                     uint16_t data_to_write) override;
	virtual uint16_t readEmulatorRegister(uint16_t address) override;
	virtual void readEmulatorBlock(std::vector<uint16_t>& data,
                                 uint16_t address,
                                 uint16_t wordCount,
                                 bool incrementAddress) override;

	// prepackaged ROC interface
	// must be an indirection to support deferred initialization
	std::shared_ptr<trkdaq::ROC> _roc;

	bool emulatorWorkLoop(void) override;

  // ejc: are these... part of the interface...? or related to macro exports?
//void ReadTrackerFIFO(__ARGS__);
//void ReadROCErrorCounter(__ARGS__);
//virtual void GetStatus(__ARGS__) override;
//void SetupForDigiDataTaking(__ARGS__);
//void FindAlignment(__ARGS__);
//void GetUIParameter(__ARGS__);
//void SetUIParameters(__ARGS__);

protected:
	enum TrackerRegisters
	{

		ROC_ADDRESS_DDRRESET                 = 14,
		ROC_ADDRESS_ANALOGRESET              = 13,
		ROC_ADDRESS_IS_PATTERN               = 8,

		ROC_ADDRESS_ERRCNT                   = 17,


		ROC_ADDRESS_WORKMODE                 = 122,

		//ROC_ADDRESS_EW_LENGHT                = 123,
		//ROC_ADDRESS_EW_BLIND                 = 124,

		ROC_ADDRESS_ON_EW_SIZE                = 123,
		ROC_ADDRESS_ON_EW_BLIND               = 124,

		ROC_ADDRESS_OFF_EW_SIZE                = 512,
		ROC_ADDRESS_OFF_EW_BLIND               = 513,

		ROC_ADDRESS_EW_DELAY                 = 125,

		ROC_ADDRESS_MASK_A                   = 120,
		ROC_ADDRESS_MASK_B                   = 121,
		ROC_ADDRESS_BASE_THRESHOLD           = 100,

		ROC_ADDRESS_IS_COUNTER               = 79,
		ROC_ADDRESS_COUNTER_IS_FALLING       = 80,
		ROC_ADDRESS_COUNTER_SIZE             = 81,

		ROC_ADDRESS_IS_LASER                 = 78,
		ROC_ADDRESS_LASER_DELAY              = 77,

		ROC_ADDRESS_LOOPBACK_GROSS_DELAY     = 4

	};

private:
  /**/

	// format a per-channel threshold table from the flat 3*96 vector
	// returned by trkdaq::ROC::ReadThresholds
	// (layout: [3*ch+0]=HV, [3*ch+1]=CAL, [3*ch+2]=sum)
	static std::string FormatThresholdTable(const std::vector<float>& Thresholds);

	// format a per-channel DAC table from the flat 2*96 vector returned by
	// trkdaq::ROC::FindThresholds (layout: [2*ch+0]=CAL, [2*ch+1]=HV)
	static std::string FormatDacTable(const std::vector<DTCLib::roc_data_t>& Dacs);

	// parse the parsed-field printout emitted by trkdaq::ROC::NotoriousRead
	// (one "label : value(s)" per line) into a label->value map, with each
	// label trimmed of surrounding whitespace
	static std::map<std::string, std::string> ParseNotoriousReadOutput(
	    const std::string& Text);

	std::array<uint32_t, 3> GetConfiguredChannelMasks() const;
	std::string ResolveThresholdFilePath(
	    const std::string& rootOverride = "", const std::string& setOverride = "");

  // format a per-channel rates table from a vector-of-tuples
  // returned by trkdaq::ROC::ChannelRates
  static std::string FormatRatesTable(const std::vector<trkdaq::ROC::rates_t>& rates);

	static bool SafeSerialize(std::string path,
														std::string key,
														nlohmann::json value);
	static nlohmann::json SafeDeserialize(std::string path,
													              std::string key);

	// clang-format on
};
}  // namespace ots

#endif
