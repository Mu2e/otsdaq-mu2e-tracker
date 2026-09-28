#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <unistd.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <stdexcept>

#include "otsdaq-mu2e-tracker/FEInterfaces/ROCTrackerInterface.h"
#include "otsdaq/ConfigurationInterface/ConfigurationManagerRW.h"
#include "otsdaq/Macros/InterfacePluginMacros.h"

using namespace ots;

#undef __MF_SUBJECT__
#define __MF_SUBJECT__ "FE-ROCTrackerInterface"


ROCTrackerInterface::ROCTrackerInterface(
    const std::string&       rocUID,
    const ConfigurationTree& theXDAQContextConfigTree,
    const std::string&       theConfigurationPath)
    : ROCPolarFireCoreInterface(rocUID, theXDAQContextConfigTree, theConfigurationPath)
{
	INIT_MF("." /*directory used is USER_DATA/LOG/.*/);

	__COUT_INFO__ << "ROCTrackerInterface instantiated with link: " << linkID_
	              << " and EventWindowDelayOffset = " << delay_ << __E__;

	__CFG_COUT__ << "Constructor..." << __E__;

	registerFEMacroFunction("Read Register",
	                        static_cast<FEVInterface::frontEndMacroFunction_t>(
	                            &ROCTrackerInterface::ReadRegister),
	                        std::vector<std::string>{"Address"},
	                        std::vector<std::string>{"Value"},
	                        1,
	                        "" /* tooltip info here */);

	registerFEMacroFunction("Reset Counters",
	                        static_cast<FEVInterface::frontEndMacroFunction_t>(
	                            &ROCTrackerInterface::ResetCounters),
	                        std::vector<std::string>{},
	                        std::vector<std::string>{"Return Code"},
	                        1,
	                        "" /* tooltip info here */);

	registerFEMacroFunction(
	    "Init Readout",
	    static_cast<FEVInterface::frontEndMacroFunction_t>(
	        &ROCTrackerInterface::InitReadout),
	    std::vector<std::string>{},
	    std::vector<std::string>{"Readout Mode", "DTC ID", "Return Code", "Summary"},
	    1,
	    "Initialize this tracker ROC from the active tracker tables: reset ROC "
	    "register 14, apply ControlRoc_Read with the configured channel masks and "
	    "zero trigger count, clear DIGIs in real-DIGI mode, write the parent DTC ID, "
	    "and program the digitization window. The DTC automatically exposes this as "
	    "'ROC FEMacro - Init Readout' for one or all enabled ROCs.");

	registerFEMacroFunction("Find Alignment",
	                        static_cast<FEVInterface::frontEndMacroFunction_t>(
	                            &ROCTrackerInterface::FindAlignment),
	                        std::vector<std::string>{},
	                        std::vector<std::string>{"Success", "Summary"},
	                        1,
	                        "" /* tooltip info here */);

	registerFEMacroFunction(
	    "Set Threshold",
	    static_cast<FEVInterface::frontEndMacroFunction_t>(
	        &ROCTrackerInterface::SetThreshold),
	    std::vector<std::string>{"Channel", "Preamp", "DAC value", "PrintLevel"},
	    std::vector<std::string>{"Success"},
	    1,
	    "" /* tooltip info here */);

	registerFEMacroFunction(
	    "Measure Threshold",
	    static_cast<FEVInterface::frontEndMacroFunction_t>(
	        &ROCTrackerInterface::MeasureThreshold),
	    std::vector<std::string>{"Channel"},
	    std::vector<std::string>{"Readback count", "Cal", "HV", "Total"},
	    1,
	    "" /* tooltip info here */);

	registerFEMacroFunction(
	    "Enable charge injection",
	    static_cast<FEVInterface::frontEndMacroFunction_t>(
	        &ROCTrackerInterface::EnableChargeInjection),
	    std::vector<std::string>{"First channel mask", "Duty cycle", "Delay"},
	    std::vector<std::string>{"Success"},
	    1,
	    "" /* tooltip info here */);

	registerFEMacroFunction("Disable charge injection",
	                        static_cast<FEVInterface::frontEndMacroFunction_t>(
	                            &ROCTrackerInterface::EnableChargeInjection),
	                        std::vector<std::string>{},
	                        std::vector<std::string>{"Success"},
	                        1,
	                        "" /* tooltip info here */);

	registerFEMacroFunction("Reset Digis",
	                        static_cast<FEVInterface::frontEndMacroFunction_t>(
	                            &ROCTrackerInterface::ResetDigis),
	                        std::vector<std::string>{},
	                        std::vector<std::string>{},
	                        1,
	                        "" /* tooltip info here */);

	registerFEMacroFunction("Reset and configure",
	                        static_cast<FEVInterface::frontEndMacroFunction_t>(
	                            &ROCTrackerInterface::ResetAndConfigure),
	                        std::vector<std::string>{},
	                        std::vector<std::string>{},
	                        1,
	                        "" /* tooltip info here */);

	registerFEMacroFunction("Reboot MCU",
	                        static_cast<FEVInterface::frontEndMacroFunction_t>(
	                            &ROCTrackerInterface::RebootMCU),
	                        std::vector<std::string>{},
	                        std::vector<std::string>{"Return code"},
	                        1,
	                        "" /* tooltip info here */);

	registerFEMacroFunction("Set Event Window Delay",
	                        static_cast<FEVInterface::frontEndMacroFunction_t>(
	                            &ROCTrackerInterface::SetEventWindowDelay),
	                        std::vector<std::string>{"Delay (5 ns units)"},
	                        std::vector<std::string>{"Return code"},
	                        1,
	                        "" /* tooltip info here */);

	registerFEMacroFunction(
	    "Set Digitization Window",
	    static_cast<FEVInterface::frontEndMacroFunction_t>(
	        &ROCTrackerInterface::SetDigitizationWindow),
	    std::vector<std::string>{"TStart (5 ns units)", "TStop (5 ns units)"},
	    std::vector<std::string>{"Return code"},
	    1,
	    "" /* tooltip info here */);

	registerFEMacroFunction("Read DIGI Register",
	                        static_cast<FEVInterface::frontEndMacroFunction_t>(
	                            &ROCTrackerInterface::DigiRead),
	                        std::vector<std::string>{"Address", "HvCal"},
	                        std::vector<std::string>{"Return code", "Value"},
	                        1,
	                        "Read one 16-bit register from a DIGI through this ROC. "
	                        "HvCal selects the target: 1=CAL DIGI, 2=HV DIGI, "
	                        "3=ROC internal register space. For a read-only "
	                        "communications check, read address 0xC0; its low 10 bits "
	                        "are expected to be 0x047 by the current ROC firmware.");

	registerFEMacroFunction(
	    "Print Digis",
	    static_cast<FEVInterface::frontEndMacroFunction_t>(
	        &ROCTrackerInterface::PrintDigis),
	    std::vector<std::string>{},
	    std::vector<std::string>{"Return code", "Output"},
	    1,
	    "Read DIGI registers 0xA4, 0xA5, 0xA6, 0xC0, 0xD0, 0xD1, and 0xD2 "
	    "from both CAL and HV. This is the otsdaq equivalent of MIDAS print_digis.");

	registerFEMacroFunction(
	    "Preflight",
	    static_cast<FEVInterface::frontEndMacroFunction_t>(
	        &ROCTrackerInterface::Preflight),
	    std::vector<std::string>{},
	    std::vector<std::string>{"Return code",
	                             "ROC FIFO status",
	                             "ROC enabled lanes",
	                             "ROC alignment 0xB6",
	                             "CAL DIGI 0xA4",
	                             "HV DIGI 0xA4",
	                             "CAL DIGI 0xA5",
	                             "HV DIGI 0xA5",
	                             "CAL DIGI 0xA6",
	                             "HV DIGI 0xA6",
	                             "CAL DIGI 0xC0",
	                             "HV DIGI 0xC0",
	                             "CAL DIGI 0xD0",
	                             "HV DIGI 0xD0",
	                             "CAL DIGI 0xD1",
	                             "HV DIGI 0xD1",
	                             "CAL DIGI 0xD2",
	                             "HV DIGI 0xD2",
	                             "Checks"},
	    1,
	    "Read-only preflight for this ROC. Requires ROC register 18 to be "
	    "0x0F00 and internal ROC register 0xB6 to report alignment for every "
	    "lane enabled in register 8; requires CAL and HV DIGI register 0xC0 "
	    "low 10 bits to be 0x047; and requires DIGI registers 0xA4, 0xA5, 0xA6, 0xD0, 0xD1, "
	    "and 0xD2 to be zero. Returns every CAL and HV DIGI value read.");

	registerFEMacroFunction("Write DIGI Register",
	                        static_cast<FEVInterface::frontEndMacroFunction_t>(
	                            &ROCTrackerInterface::DigiWrite),
	                        std::vector<std::string>{"Address", "HvCal", "Data"},
	                        std::vector<std::string>{"Return code"},
	                        1,
	                        "Write one 16-bit register through this ROC. HvCal selects "
	                        "the target: 1=CAL DIGI, 2=HV DIGI, 3=ROC internal register "
	                        "space. This changes DIGI or ROC firmware state and must not "
	                        "be used during active readout.");

	registerFEMacroFunction("Read Panel ID",
	                        static_cast<FEVInterface::frontEndMacroFunction_t>(
	                            &ROCTrackerInterface::ReadPanelID),
	                        std::vector<std::string>{},
	                        std::vector<std::string>{"Panel ID", "Success"},
	                        1,
	                        "" /* tooltip info here */);

	registerFEMacroFunction("Read Serial Number",
	                        static_cast<FEVInterface::frontEndMacroFunction_t>(
	                            &ROCTrackerInterface::ReadSerialNumber),
	                        std::vector<std::string>{},
	                        std::vector<std::string>{"Serial Number"},
	                        1,
	                        "" /* tooltip info here */);

	registerFEMacroFunction("Measure Thresholds",
	                        static_cast<FEVInterface::frontEndMacroFunction_t>(
	                            &ROCTrackerInterface::MeasureThresholds),
	                        std::vector<std::string>{},
	                        std::vector<std::string>{"Return code", "Thresholds"},
	                        1,
	                        "" /* tooltip info here */);

	registerFEMacroFunction(
	    "Find Threshold",
	    static_cast<FEVInterface::frontEndMacroFunction_t>(
	        &ROCTrackerInterface::FindThreshold),
	    std::vector<std::string>{"Channel", "Preamp", "Threshold (mV)", "Tolerance (mV)"},
	    std::vector<std::string>{"Success", "DAC value", "Threshold diagnostics"},
	    1,
	    "" /* tooltip info here */);

	registerFEMacroFunction("Find Thresholds",
	                        static_cast<FEVInterface::frontEndMacroFunction_t>(
	                            &ROCTrackerInterface::FindThresholds),
	                        std::vector<std::string>{"Threshold (mV)", "Tolerance (mV)"},
	                        std::vector<std::string>{"Summary", "Failed count", "DAC values", "Threshold diagnostics"},
	                        1,
	                        "" /* tooltip info here */);

	registerFEMacroFunction(
	    "Set Channel Mask",
	    static_cast<FEVInterface::frontEndMacroFunction_t>(
	        &ROCTrackerInterface::SetChannelMask),
	    std::vector<std::string>{"Mask channels 0-31", "Mask channels 32-63", "Mask channels 64-95"},
	    std::vector<std::string>{"Return code"},
	    1,
	    "For a single selected ROC, MacroMaker loads these editable mask words from "
	    "the active SubsystemTrackerChannelsTable group. Run writes the displayed "
	    "values. A field left as Default is resolved from the active configuration "
	    "when the macro executes.");

	registerFEMacroFunction(
	    "\"Notorious Read\"",
	    static_cast<FEVInterface::frontEndMacroFunction_t>(
	        &ROCTrackerInterface::NotoriousRead),
	    std::vector<std::string>{"ADC Mode",
	                             "TDC Mode",
	                             "Waveform delay (lookback)",
	                             "Additional Sample Packets",
	                             "Trigger count",
	                             "Channel mask lo",
	                             "Channel mask md",
	                             "Channel mask hi",
	                             "Enable pulser",
	                             "Fiber clock mask",
	                             "\"Mode\" (deprecated)",
	                             "\"Clock\" (deprecated)"},
	    std::vector<std::string>{"ADC Mode",
	                             "TDC Mode",
	                             "Waveform delay (lookback)",
	                             "Trigger count",
	                             "Channel masks",
	                             "Additional Sample Packets",
	                             "Enable pulser",
	                             "Fiber clock mask",
	                             "\"Mode\" (deprecated)",
	                             "\"Clock\" (deprecated)",
	                             "\"digi_read_0xb\"",
	                             "\"digi_read_0xe\"",
	                             "\"digi_read_0xd\"",
	                             "\"digi_read_0xc\""},
	    1,
	    "" /* tooltip info here */);

	registerFEMacroFunction(
	    "Configure Digis",
	    static_cast<FEVInterface::frontEndMacroFunction_t>(
	        &ROCTrackerInterface::ConfigureDigis),
	    std::vector<std::string>{"TDC Mode",
	                             "Waveform delay (lookback)",
	                             "Additional Sample Packets",
	                             "Channel mask lo",
	                             "Channel mask md",
	                             "Channel mask hi"},
	    std::vector<std::string>{},
	    1,
	    "" /* tooltip info here */);

	registerFEMacroFunction("Initialize Digis",
	                        static_cast<FEVInterface::frontEndMacroFunction_t>(
	                            &ROCTrackerInterface::InitializeDigis),
	                        std::vector<std::string>{},
	                        std::vector<std::string>{"Output"},
	                        1,
	                        "" /* tooltip info here */);

	registerFEMacroFunction(
	    "Digi Read/Write",
	    static_cast<FEVInterface::frontEndMacroFunction_t>(
	        &ROCTrackerInterface::DigiRW),
	    std::vector<std::string>{"Read/Write", "HvCal", "Address", "Data"},
	    std::vector<std::string>{"Output"},
	    1,
	    "" /* tooltip info here */);

	registerFEMacroFunction(
	    "Read SPI",
	    static_cast<FEVInterface::frontEndMacroFunction_t>(
	        &ROCTrackerInterface::ReadSPI),
	    std::vector<std::string>{},
	    std::vector<std::string>{"Return Code", "Output"},
	    1,
	    "Read and convert the 36 ROC/DIGI SPI monitoring values for this ROC, "
	    "including currents, voltages, and temperatures. This operation is read-only.");

	registerFEMacroFunction("Print Status",
	                        static_cast<FEVInterface::frontEndMacroFunction_t>(
	                            &ROCTrackerInterface::PrintStatus),
	                        std::vector<std::string>{},
	                        std::vector<std::string>{"Output"},
	                        1,
	                        "" /* tooltip info here */);

  registerFEMacroFunction("Measure Channel Rates",
                          static_cast<FEVInterface::frontEndMacroFunction_t>(
                              &ROCTrackerInterface::MeasureChannelRates),
                          std::vector<std::string>{},
                          std::vector<std::string>{"Return Code", "Rates", "Saved hardware masks"},
                          1,
	                        "" /* tooltip info here */);

  registerFEMacroFunction("Find and serialize thresholds",
	                        static_cast<FEVInterface::frontEndMacroFunction_t>(
	                            &ROCTrackerInterface::FindAndSerializeThresholds),
	                        std::vector<std::string>{"Threshold (mV)", "Tolerance (mV)", "Filesystem path"},
	                        std::vector<std::string>{"Summary", "Failed count", "Serialization successful", "Resolved path", "Diagnostic path", "DAC values", "Threshold diagnostics"},
	                        1,
	                        "Adjust all 96 CAL and HV thresholds and save all selected DACs, including failed searches, with calibration status metadata. "
                            "Threshold (mV) uses the existing signed search convention; tolerance must be positive. "
                            "Filesystem path may be blank for the configured ThresholdRoot/slot/ThresholdSet/MN path, "
                            "an absolute root directory (writes slot_XX/thresholds-YY-mV/MNxxx.json, YY is Threshold in mV), "
                            "or the matching MNxxx.json filename. "
                            "Existing files are backed up as .bak. Run only with data taking stopped.");

  registerFEMacroFunction("Deserialize and set thresholds",
	                        static_cast<FEVInterface::frontEndMacroFunction_t>(
	                            &ROCTrackerInterface::DeserializeAndSetThresholds),
	                        std::vector<std::string>{"Validate only"},
	                        std::vector<std::string>{"Failed count", "Resolved path", "Summary"},
	                        1,
	                        "Resolves the threshold JSON from the active tracker node-map and "
	                        "global-parameter tables. Leave Validate only blank or enter 1 to "
	                        "validate without writing; enter 0 to program all 96 Cal and 96 HV "
	                        "threshold DACs.");

  registerFEMacroFunction("Test JSON write",
	                        static_cast<FEVInterface::frontEndMacroFunction_t>(
	                            &ROCTrackerInterface::TestJSON),
	                        std::vector<std::string>{"Key", "Value", "Path"},
	                        std::vector<std::string>{"Serialization successful"},
	                        1,
	                        "" /* tooltip info here */);

	registerFEMacroFunction("Update Channel Thresholds",
	                        static_cast<FEVInterface::frontEndMacroFunction_t>(
	                            &ROCTrackerInterface::UpdateChannelThresholds),
	                        std::vector<std::string>{"ChannelUID", "ThresholdCal", "ThresholdHV"},
	                        std::vector<std::string>{"Result"},
	                        1,
	                        "Updates ThresholdCal and ThresholdHV for a given ChannelUID "
	                        "in SubsystemTrackerChannelsTable. Creates a new table version.");
}  // end constructor

ROCTrackerInterface::~ROCTrackerInterface(void)
{
	// NOTE:: be careful not to call __FE_COUT__ decoration because it uses the
	// tree and it may already be destructed partially
	__COUT__ << FEVInterface::interfaceUID_ << " Destructor" << __E__;
}  // end destructor

void ROCTrackerInterface::onDTCReady()
{
	auto dtc = getDTC();
	_roc     = std::make_shared<trkdaq::ROC>(linkID_, dtc);
}

std::string ROCTrackerInterface::getFirmwareVersion(void)
{
	if(!_roc)
		throw std::runtime_error("Get ROC Firmware Version: DTC is not ready.");
	const auto info = _roc->ReadDeviceInfo();
	const auto gitCommit = _roc->ReadFirmwareGitCommit();
	const auto userCode = _roc->ReadUserCode();
	// The inherited macro returns this complete MIDAS-style report in Result.
	// READDEVICE supplies FPGA metadata; READGITREV supplies controller source ID.
	return std::format(
	    "DeviceSerial:{}\nDesignVer   :{}\nDesignInfo  :{}\n"
	    "BackLevelVer:{}\nUserCode    :'{:08x}'\ngit_commit  :'{}'\n",
	    info.DeviceSerial, info.DesignVer, info.DesignInfo,
	    info.BackLevelVer, userCode, gitCommit);
}

void ROCTrackerInterface::InitReadout(__ARGS__)
{
	static const std::string trackerGlobalTable =
	    "/SubsystemTrackerGlobalParametersTable";
	static const std::array<std::string, 11> requiredParameters = {
	    "ROCReadoutMode",
	    "DigitizationStart5ns",
	    "DigitizationStop5ns",
	    "ADCMode",
	    "TDCMode",
	    "NumLookback",
	    "NumSamples",
	    "EnablePulser",
	    "MarkerClock",
	    "Mode",
	    "Clock"};

	std::map<std::string, std::string> parameterValues;
	const auto trackerGlobalRecords =
	    getConfigurationManager()->getNode(trackerGlobalTable).getChildren();
	for(const auto& parameter : trackerGlobalRecords)
	{
		if(std::find(requiredParameters.begin(),
		             requiredParameters.end(),
		             parameter.first) == requiredParameters.end())
			continue;
		if(!parameter.second.getNode("Status").getValue<bool>())
			throw std::runtime_error(std::format(
			    "Init Readout: {}/{} is disabled.",
			    trackerGlobalTable,
			    parameter.first));
		parameterValues[parameter.first] =
		    parameter.second.getNode("ParameterValue").getValue<std::string>();
	}

	auto parseUnsigned = [&](const std::string& name,
	                         uint64_t           maximum) -> uint64_t {
		const auto valueIt = parameterValues.find(name);
		if(valueIt == parameterValues.end())
			throw std::runtime_error(std::format(
			    "Init Readout: required enabled parameter {}/{} is missing.",
			    trackerGlobalTable,
			    name));

		size_t             parsedCharacters = 0;
		unsigned long long value            = 0;
		try
		{
			value = std::stoull(valueIt->second, &parsedCharacters, 0);
		}
		catch(const std::exception&)
		{
			throw std::runtime_error(std::format(
			    "Init Readout: {}/{} value '{}' is not an unsigned integer.",
			    trackerGlobalTable,
			    name,
			    valueIt->second));
		}
		if(parsedCharacters != valueIt->second.size() || value > maximum)
			throw std::runtime_error(std::format(
			    "Init Readout: {}/{} value '{}' is outside 0-{}.",
			    trackerGlobalTable,
			    name,
			    valueIt->second,
			    maximum));
		return value;
	};

	const uint32_t rocReadoutMode =
	    static_cast<uint32_t>(parseUnsigned("ROCReadoutMode", 0xffffffffu));
	const uint32_t readoutMode = rocReadoutMode & 0xf;
	if(readoutMode > 2)
		throw std::runtime_error(std::format(
		    "Init Readout: ROCReadoutMode={} has invalid low nibble {}; expected 0 "
		    "(variable pattern), 1 (real DIGIs), or 2 (checkerboard pattern).",
		    rocReadoutMode,
		    readoutMode));

	const uint16_t digitizationStart5ns =
	    static_cast<uint16_t>(parseUnsigned("DigitizationStart5ns", 0xffffu));
	const uint16_t digitizationStop5ns =
	    static_cast<uint16_t>(parseUnsigned("DigitizationStop5ns", 0xffffu));
	if(digitizationStop5ns <= digitizationStart5ns)
		throw std::runtime_error(std::format(
		    "Init Readout: DigitizationStop5ns={} must be greater than "
		    "DigitizationStart5ns={}.",
		    digitizationStop5ns,
		    digitizationStart5ns));

	const std::string parentMarker = "/LinkToROCGroupTable/";
	const size_t      parentPosition = theConfigurationPath_.rfind(parentMarker);
	if(parentPosition == std::string::npos)
		throw std::runtime_error(std::format(
		    "Init Readout: ROC configuration path '{}' does not identify its parent "
		    "DTC through LinkToROCGroupTable.",
		    theConfigurationPath_));
	const std::string parentDtcPath = theConfigurationPath_.substr(0, parentPosition);
	const uint32_t dtcId = theXDAQContextConfigTree_
	                           .getNode(parentDtcPath)
	                           .getNode("EventBuilderDTCID")
	                           .getValue<uint32_t>();
	if(dtcId > 0xffu)
		throw std::runtime_error(std::format(
		    "Init Readout: parent DTC EventBuilderDTCID={} is outside 0-255.", dtcId));

	// ROCCoreVInterface loads this ROC's EventWindowDelayOffset into delay_.
	// Tracker firmware uses register 4 (10 bits), in 5 ns units.
	if(delay_ > 0x3ffu)
		throw std::runtime_error(std::format(
		    "Init Readout: ROC '{}' EventWindowDelayOffset={} is outside 0-1023 (5 ns units).",
		    getInterfaceUID(), delay_));
	const uint16_t eventWindowDelay5ns = static_cast<uint16_t>(delay_);

	const auto channelMasks = GetConfiguredChannelMasks();
	trkdaq::ControlRoc_Read_Input_t0 readSettings;
	readSettings.adc_mode =
	    static_cast<uint16_t>(parseUnsigned("ADCMode", 0xffffu));
	readSettings.tdc_mode =
	    static_cast<uint16_t>(parseUnsigned("TDCMode", 0xffffu));
	readSettings.num_lookback =
	    static_cast<uint16_t>(parseUnsigned("NumLookback", 0xffffu));
	readSettings.num_samples =
	    static_cast<uint16_t>(parseUnsigned("NumSamples", 0xffffu));
	readSettings.num_triggers[0] = 0;
	readSettings.num_triggers[1] = 0;
	for(size_t maskIndex = 0; maskIndex < channelMasks.size(); ++maskIndex)
	{
		readSettings.ch_mask[2 * maskIndex] =
		    static_cast<uint16_t>(channelMasks[maskIndex] & 0xffffu);
		readSettings.ch_mask[2 * maskIndex + 1] =
		    static_cast<uint16_t>((channelMasks[maskIndex] >> 16) & 0xffffu);
	}
	readSettings.enable_pulser =
	    static_cast<uint16_t>(parseUnsigned("EnablePulser", 0xffffu));
	readSettings.marker_clock =
	    static_cast<uint16_t>(parseUnsigned("MarkerClock", 0xffffu));
	readSettings.mode = static_cast<uint16_t>(parseUnsigned("Mode", 0xffffu));
	readSettings.clock = static_cast<uint16_t>(parseUnsigned("Clock", 0xffffu));

	std::stringstream summary;
	summary << std::format(
	    "Sources\n"
	    "  Read settings: {}\n"
	    "  Channel masks: ROCTypeLinkTable/LinkToTrackerROCChannelsTable\n"
	    "  DTC ID: {}/EventBuilderDTCID\n"
	    "  Event-window delay: ROCInterfaceTable/EventWindowDelayOffset\n"
	    "Settings\n"
	    "  ROC link: {}\n"
	    "  ROC readout mode: {}\n"
	    "  Digitization window: {}-{} ticks (5 ns)\n"
	    "  Event-window delay: {} ticks (5 ns), ROC register 4\n"
	    "  ADC mode: {}\n"
	    "  TDC mode: {}\n"
	    "  Lookback: {}\n"
	    "  Samples: {}\n"
	    "  Trigger count words: 0, 0\n"
	    "  Channel masks: 0x{:08x}, 0x{:08x}, 0x{:08x}\n"
	    "  Enable pulser: {}\n"
	    "  Marker clock: {}\n"
	    "  Mode: {}\n"
	    "  Clock: {}\n"
	    "  DTC ID: {}\n",
	    trackerGlobalTable,
	    parentDtcPath,
	    static_cast<unsigned int>(linkID_),
	    rocReadoutMode,
	    digitizationStart5ns,
	    digitizationStop5ns,
	    eventWindowDelay5ns,
	    readSettings.adc_mode,
	    readSettings.tdc_mode,
	    readSettings.num_lookback,
	    readSettings.num_samples,
	    channelMasks[0],
	    channelMasks[1],
	    channelMasks[2],
	    readSettings.enable_pulser,
	    readSettings.marker_clock,
	    readSettings.mode,
	    readSettings.clock,
	    dtcId);

	const int rc = _roc->InitReadout(rocReadoutMode,
	                                 digitizationStart5ns,
	                                 digitizationStop5ns,
	                                 static_cast<uint8_t>(dtcId),
	                                 eventWindowDelay5ns,
	                                 readSettings,
	                                 summary);
	__SET_ARG_OUT__("Readout Mode", std::to_string(rocReadoutMode));
	__SET_ARG_OUT__("DTC ID", std::to_string(dtcId));
	__SET_ARG_OUT__("Return Code", std::to_string(rc));
	__SET_ARG_OUT__("Summary", summary.str());
	if(rc != 0)
		throw std::runtime_error(std::format(
		    "Init Readout failed for ROC '{}' link {} with return code {}.\n{}",
		    getInterfaceUID(),
		    static_cast<unsigned int>(linkID_),
		    rc,
		    summary.str()));
}

void ROCTrackerInterface::ReadRegister(__ARGS__)
{
	address_t address = __GET_ARG_IN__("Address", address_t, 0x0);
	uint32_t  rv      = _roc->ReadRegister(address);
	__SET_ARG_OUT__("Value", std::to_string(rv));
}

void ROCTrackerInterface::ResetCounters(__ARGS__)
{
	int rv = _roc->Reset();
	__SET_ARG_OUT__("Return Code", std::to_string(rv));
}

void ROCTrackerInterface::FindAlignment(__ARGS__)
{
	auto rv = _roc->FindAlignment();

	std::stringstream stream;
	const auto&       alignment = _roc->LatestAlignment();
	print_legacy_table(alignment, stream);

	__SET_ARG_OUT__("Success", std::to_string(rv));
	__SET_ARG_OUT__("Summary", stream.str());
}

void ROCTrackerInterface::SetThreshold(__ARGS__)
{
	int channel     = __GET_ARG_IN__("Channel", int, -1);
	int preamp      = __GET_ARG_IN__("Preamp", int, -1);
	int threshold   = __GET_ARG_IN__("DAC value", int, -1);
	int print_level = __GET_ARG_IN__("PrintLevel", int, 0x2);
	// TODO throw on -1s
	__FE_COUT__ << "ejc: ROCTrackerInterface::SetThreshold" << __E__;
	auto rv = _roc->SetThreshold(channel, preamp, threshold, print_level);
	__SET_ARG_OUT__("Success", std::to_string(rv));
}

void ROCTrackerInterface::MeasureThreshold(__ARGS__)
{
	int channel = __GET_ARG_IN__("Channel", int, -1);

	std::vector<float> rvs;
	uint32_t           mask_lo = 0xFFFFFFFF;
	uint32_t           mask_md = 0xFFFFFFFF;
	uint32_t           mask_hi = 0xFFFFFFFF;
	trkdaq::NullStream null;
	auto               stream = std::ostream(&null);
	_roc->ReadThresholds(rvs, mask_lo, mask_md, mask_hi, 0, stream);

	size_t idx;

	idx      = 3 * channel;
	auto rvh = rvs[idx + 0];
	auto rvc = rvs[idx + 1];
	auto rvt = rvs[idx + 2];

	std::string msg = "ejc: channel " + std::to_string(channel) +
	                  " thresholds = " + std::to_string(rvh) + ", " +
	                  std::to_string(rvc) + ", " + std::to_string(rvt);
	__FE_COUT__ << msg << __E__;
	__SET_ARG_OUT__("Readback count", std::to_string(rvs.size()));
	__SET_ARG_OUT__("HV", std::to_string(rvh));
	__SET_ARG_OUT__("Cal", std::to_string(rvc));
	__SET_ARG_OUT__("Total", std::to_string(rvt));
}

void ROCTrackerInterface::EnableChargeInjection(__ARGS__)
{
	int first_channel_mask = __GET_ARG_IN__("First channel mask", int, 0x10);
	int duty_cycle         = __GET_ARG_IN__("Duty cycle", int, 10);
	int delay              = __GET_ARG_IN__("Delay", int, 1000);
	int print_level        = 0;
	trkdaq::NullStream null;
	auto               stream = std::ostream(&null);

	__FE_COUT__ << "ejc: ROCTrackerInterface::EnableChargeInjection" << __E__;
	auto rv = _roc->EnableChargeInjection(
	    first_channel_mask, duty_cycle, delay, print_level, stream);
	__SET_ARG_OUT__("Success", std::to_string(rv));
}

void ROCTrackerInterface::DisableChargeInjection(__ARGS__)
{
	int                print_level = 0;
	trkdaq::NullStream null;
	auto               stream = std::ostream(&null);

	__FE_COUT__ << "ejc: ROCTrackerInterface::DisableChargeInjection" << __E__;
	auto rv = _roc->DisableChargeInjection(print_level, stream);
	__SET_ARG_OUT__("Success", std::to_string(rv));
}

void ROCTrackerInterface::ResetDigis(__ARGS__)
{
	__FE_COUT__ << "ROCTrackerInterface::ResetDigis" << __E__;
	_roc->ResetDigis();
}

void ROCTrackerInterface::ResetAndConfigure(__ARGS__)
{
	uint16_t tdc_mode     = __GET_ARG_IN__("TDC Mode", uint16_t, 0);
	uint16_t num_lookback = __GET_ARG_IN__("Waveform delay (lookback)", uint16_t, 8);
	uint16_t num_samples  = __GET_ARG_IN__("Additional Sample Packets", uint16_t, 1);
	uint32_t mask_lo      = __GET_ARG_IN__("Channel mask lo", uint32_t, 0xFFFFFFFF);
	uint32_t mask_md      = __GET_ARG_IN__("Channel mask md", uint32_t, 0xFFFFFFFF);
	uint32_t mask_hi      = __GET_ARG_IN__("Channel mask hi", uint32_t, 0xFFFFFFFF);
	int t_start           = __GET_ARG_IN__("TStart (5 ns units)", int, -1);
	int t_stop            = __GET_ARG_IN__("TStop (5 ns units)", int, -1);

	if(t_start < 0 || t_start > 0xFFFF)
	{
		__FE_SS__ << "TStart (5 ns units) out of range [0, 65535]: " << t_start << __E__;
		__FE_SS_THROW__;
	}
	if(t_stop < 0 || t_stop > 0xFFFF)
	{
		__FE_SS__ << "TStop (5 ns units) out of range [0, 65535]: " << t_stop << __E__;
		__FE_SS_THROW__;
	}
	if(t_stop <= t_start)
	{
		__FE_SS__ << "TStop must exceed TStart: TStart=" << t_start << " TStop=" << t_stop
		          << __E__;
		__FE_SS_THROW__;
	}
	__FE_COUT__ << "ROCTrackerInterface::ResetAndConfigure" << __E__;
	auto rv = _roc->ResetAndConfigure(tdc_mode, num_lookback, num_samples,
                                    mask_lo, mask_md, mask_hi,
                                    t_start, t_stop);
  __SET_ARG_OUT__("Return code", std::to_string(rv));
}

void ROCTrackerInterface::RebootMCU(__ARGS__)
{
	__FE_COUT__ << "ROCTrackerInterface::RebootMCU" << __E__;
	auto rv = _roc->RebootMCU();
	__SET_ARG_OUT__("Return code", std::to_string(rv));
}

void ROCTrackerInterface::SetEventWindowDelay(__ARGS__)
{
	int delay_5ns = __GET_ARG_IN__("Delay (5 ns units)", int, 0);
	if(delay_5ns < 0 || delay_5ns > 0xFFFF)
	{
		__FE_SS__ << "Delay (5 ns units) out of range [0, 65535]: " << delay_5ns << __E__;
		__FE_SS_THROW__;
	}

	trkdaq::NullStream null;
	auto               stream = std::ostream(&null);

	__FE_COUT__ << "ROCTrackerInterface::SetEventWindowDelay delay_5ns=" << delay_5ns
	            << __E__;
	auto rv = _roc->SetEventWindowDelay(static_cast<uint16_t>(delay_5ns), stream);
	__SET_ARG_OUT__("Return code", std::to_string(rv));
}

void ROCTrackerInterface::SetDigitizationWindow(__ARGS__)
{
	int t_start     = __GET_ARG_IN__("TStart (5 ns units)", int, -1);
	int t_stop      = __GET_ARG_IN__("TStop (5 ns units)", int, -1);
	int print_level = 0;

	if(t_start < 0 || t_start > 0xFFFF)
	{
		__FE_SS__ << "TStart (5 ns units) out of range [0, 65535]: " << t_start << __E__;
		__FE_SS_THROW__;
	}
	if(t_stop < 0 || t_stop > 0xFFFF)
	{
		__FE_SS__ << "TStop (5 ns units) out of range [0, 65535]: " << t_stop << __E__;
		__FE_SS_THROW__;
	}
	if(t_stop <= t_start)
	{
		__FE_SS__ << "TStop must exceed TStart: TStart=" << t_start << " TStop=" << t_stop
		          << __E__;
		__FE_SS_THROW__;
	}

	trkdaq::NullStream null;
	auto               stream = std::ostream(&null);

	__FE_COUT__ << "ROCTrackerInterface::SetDigitizationWindow TStart=" << t_start
	            << " TStop=" << t_stop << __E__;
	auto rv = _roc->SetDigitizationWindow(static_cast<uint16_t>(t_start),
	                                      static_cast<uint16_t>(t_stop),
	                                      print_level,
	                                      stream);
	__SET_ARG_OUT__("Return code", std::to_string(rv));
}

void ROCTrackerInterface::DigiRead(__ARGS__)
{
	int addr        = __GET_ARG_IN__("Address", int, -1);
	int hv_cal      = __GET_ARG_IN__("HvCal", int, -1);
	int print_level = 0;

	if(addr < 0 || addr > 0xFF)
	{
		__FE_SS__ << "DIGI address out of range [0, 255]: " << addr << __E__;
		__FE_SS_THROW__;
	}
	if(hv_cal != 1 && hv_cal != 2 && hv_cal != 3)
	{
		__FE_SS__ << "HvCal must be 1 (CAL), 2 (HV), or 3 (ROC internal): "
		          << hv_cal << __E__;
		__FE_SS_THROW__;
	}

	trkdaq::NullStream null;
	auto               stream = std::ostream(&null);

	uint32_t value = 0;
	__FE_COUT__ << "ROCTrackerInterface::DigiRead addr=" << addr << " hv_cal=" << hv_cal
	            << __E__;
	auto rv = _roc->DigiRead(addr, hv_cal, value, print_level, stream);
	__SET_ARG_OUT__("Return code", std::to_string(rv));
	__SET_ARG_OUT__("Value", std::to_string(value));
}

void ROCTrackerInterface::PrintDigis(__ARGS__)
{
	struct DigiRegister
	{
		uint16_t address;
	};
	constexpr std::array<DigiRegister, 7> registers = {
	    DigiRegister{0xA4}, DigiRegister{0xA5}, DigiRegister{0xA6},
	    DigiRegister{0xC0}, DigiRegister{0xD0}, DigiRegister{0xD1},
	    DigiRegister{0xD2}};

	trkdaq::NullStream null;
	auto               stream = std::ostream(&null);
	int                returnCode = 0;
	std::stringstream  output;
	output << std::format(" {:>10} {:>10} {:>10}\n", "Register", "CAL", "HV");
	output << "----------------------------------\n";

	__FE_COUT__ << "ROCTrackerInterface::PrintDigis" << __E__;
	for(const auto& digiRegister : registers)
	{
		uint32_t calValue = 0;
		uint32_t hvValue  = 0;
		// Match MIDAS PrintDigis: selector 1 is CAL and selector 2 is HV.
		const int calReturnCode =
		    _roc->DigiRead(digiRegister.address, 1, calValue, 0, stream);
		const int hvReturnCode =
		    _roc->DigiRead(digiRegister.address, 2, hvValue, 0, stream);
		if(returnCode == 0 && calReturnCode != 0)
			returnCode = calReturnCode;
		if(returnCode == 0 && hvReturnCode != 0)
			returnCode = hvReturnCode;

		const std::string calText = calReturnCode == 0 ?
		                                std::format("0x{:04X}", calValue & 0xFFFF) :
		                                "ERROR";
		const std::string hvText = hvReturnCode == 0 ?
		                               std::format("0x{:04X}", hvValue & 0xFFFF) :
		                               "ERROR";
		output << std::format(
		    " 0x{:04X} {:>10} {:>10}\n", digiRegister.address, calText, hvText);
	}

	__SET_ARG_OUT__("Return code", std::to_string(returnCode));
	__SET_ARG_OUT__("Output", output.str());
}

void ROCTrackerInterface::Preflight(__ARGS__)
{
	struct DigiCheck
	{
		uint16_t address;
		uint16_t expected;
		uint16_t mask;
	};

	constexpr std::array<DigiCheck, 7> digiChecks = {
	    DigiCheck{0xA4, 0x0000, 0xFFFF},
	    DigiCheck{0xA5, 0x0000, 0xFFFF},
	    DigiCheck{0xA6, 0x0000, 0xFFFF},
	    DigiCheck{0xC0, 0x0047, 0x03FF},
	    DigiCheck{0xD0, 0x0000, 0xFFFF},
	    DigiCheck{0xD1, 0x0000, 0xFFFF},
	    DigiCheck{0xD2, 0x0000, 0xFFFF}};

	std::vector<std::string>       failures;
	std::array<std::string, 7>     calValues{};
	std::array<std::string, 7>     hvValues{};
	const uint16_t rocFifoStatus =
	    static_cast<uint16_t>(_roc->ReadRegister(18) & 0xFFFF);
	if(rocFifoStatus != 0x0F00)
		failures.emplace_back(std::format(
		    "ROC R18 expected 0x0F00 read 0x{:04X}", rocFifoStatus));

	trkdaq::NullStream null;
	auto               stream = std::ostream(&null);
	// Empty receive FIFOs do not establish receiver alignment. Always check
	// internal B6, including when R18 passes. Register 8 selects the used lanes.
	const uint16_t enabledLanes =
	    static_cast<uint16_t>(_roc->ReadRegister(8) & 0x000F);
	uint32_t alignment = 0;
	const int alignmentReturnCode = _roc->DigiRead(0xB6, 3, alignment, 0, stream);
	std::string alignmentText = "ERROR";
	if(alignmentReturnCode != 0)
		failures.emplace_back(std::format(
		    "ROC internal 0xB6 read failed rc {}", alignmentReturnCode));
	else
	{
		alignmentText = std::format("0x{:04X}", alignment & 0xFFFF);
		const uint16_t missingLanes =
		    static_cast<uint16_t>(enabledLanes & ~alignment);
		if(missingLanes != 0)
		{
			constexpr std::array<const char*, 4> laneNames = {
			    "CAL0", "CAL1", "HV0", "HV1"};
			std::string missingNames;
			for(size_t lane = 0; lane < laneNames.size(); ++lane)
				if(missingLanes & (1u << lane))
					missingNames +=
					    (missingNames.empty() ? "" : ", ") + std::string(laneNames[lane]);
			failures.emplace_back(std::format(
			    "ROC internal 0xB6 read {}, enabled lanes 0x{:X}; unaligned: {}",
			    alignmentText, enabledLanes, missingNames));
		}
	}
	for(size_t checkIndex = 0; checkIndex < digiChecks.size(); ++checkIndex)
	{
		const auto& check = digiChecks[checkIndex];
		for(const int hvCal : {1, 2})
		{
			uint32_t value = 0;
			const int returnCode =
			    _roc->DigiRead(check.address, hvCal, value, 0, stream);
			const char* digiName = hvCal == 1 ? "CAL" : "HV";
			auto& valueText = hvCal == 1 ? calValues[checkIndex] : hvValues[checkIndex];
			if(returnCode != 0)
			{
				valueText = "ERROR";
				failures.emplace_back(std::format(
				    "{} DIGI 0x{:02X} read failed rc {}",
				    digiName,
				    check.address,
				    returnCode));
				continue;
			}

			const uint16_t value16 = static_cast<uint16_t>(value & 0xFFFF);
			valueText = std::format("0x{:04X}", value16);
			if((value16 & check.mask) != check.expected)
				failures.emplace_back(std::format(
				    "{} DIGI 0x{:02X} expected 0x{:04X} read 0x{:04X}",
				    digiName,
				    check.address,
				    check.expected,
				    value16));
		}
	}

	std::string checks = "PASS";
	if(!failures.empty())
	{
		checks = "FAIL: ";
		for(size_t i = 0; i < failures.size(); ++i)
			checks += (i ? "; " : "") + failures[i];
	}

	__SET_ARG_OUT__("Return code", std::to_string(failures.size()));
	__SET_ARG_OUT__("ROC FIFO status", std::format("0x{:04X}", rocFifoStatus));
	__SET_ARG_OUT__("ROC enabled lanes", std::format("0x{:X}", enabledLanes));
	__SET_ARG_OUT__("ROC alignment 0xB6", alignmentText);
	__SET_ARG_OUT__("CAL DIGI 0xA4", calValues[0]);
	__SET_ARG_OUT__("HV DIGI 0xA4", hvValues[0]);
	__SET_ARG_OUT__("CAL DIGI 0xA5", calValues[1]);
	__SET_ARG_OUT__("HV DIGI 0xA5", hvValues[1]);
	__SET_ARG_OUT__("CAL DIGI 0xA6", calValues[2]);
	__SET_ARG_OUT__("HV DIGI 0xA6", hvValues[2]);
	__SET_ARG_OUT__("CAL DIGI 0xC0", calValues[3]);
	__SET_ARG_OUT__("HV DIGI 0xC0", hvValues[3]);
	__SET_ARG_OUT__("CAL DIGI 0xD0", calValues[4]);
	__SET_ARG_OUT__("HV DIGI 0xD0", hvValues[4]);
	__SET_ARG_OUT__("CAL DIGI 0xD1", calValues[5]);
	__SET_ARG_OUT__("HV DIGI 0xD1", hvValues[5]);
	__SET_ARG_OUT__("CAL DIGI 0xD2", calValues[6]);
	__SET_ARG_OUT__("HV DIGI 0xD2", hvValues[6]);
	__SET_ARG_OUT__("Checks", checks);
}

void ROCTrackerInterface::DigiWrite(__ARGS__)
{
	int addr        = __GET_ARG_IN__("Address", int, -1);
	int hv_cal      = __GET_ARG_IN__("HvCal", int, -1);
	int data        = __GET_ARG_IN__("Data", int, -1);
	int print_level = 0;

	if(addr < 0 || addr > 0xFF)
	{
		__FE_SS__ << "DIGI address out of range [0, 255]: " << addr << __E__;
		__FE_SS_THROW__;
	}
	if(hv_cal != 1 && hv_cal != 2 && hv_cal != 3)
	{
		__FE_SS__ << "HvCal must be 1 (CAL), 2 (HV), or 3 (ROC internal): "
		          << hv_cal << __E__;
		__FE_SS_THROW__;
	}
	if(data < 0 || data > 0xFFFF)
	{
		__FE_SS__ << "Data out of range [0, 65535]: " << data << __E__;
		__FE_SS_THROW__;
	}

	trkdaq::NullStream null;
	auto               stream = std::ostream(&null);

	__FE_COUT__ << "ROCTrackerInterface::DigiWrite addr=" << addr << " hv_cal=" << hv_cal
	            << " data=" << data << __E__;
	auto rv =
	    _roc->DigiWrite(addr, hv_cal, static_cast<uint16_t>(data), print_level, stream);
	__SET_ARG_OUT__("Return code", std::to_string(rv));
}

void ROCTrackerInterface::ReadPanelID(__ARGS__)
{
	int print_level = 0;

	__FE_COUT__ << "ROCTrackerInterface::ReadPanelID" << __E__;
	auto rv = _roc->ReadPanelID(print_level);

	// ReadPanelID returns the panel ID on success, negative error code on failure
	if(rv < 0)
	{
		__SET_ARG_OUT__("Panel ID", std::string(""));
		__SET_ARG_OUT__("Success", std::to_string(rv));
	}
	else
	{
		__SET_ARG_OUT__("Panel ID", std::to_string(rv));
		__SET_ARG_OUT__("Success", std::to_string(0));
	}
}

void ROCTrackerInterface::ReadSerialNumber(__ARGS__)
{
	__FE_COUT__ << "ROCTrackerInterface::ReadSerialNumber" << __E__;
	auto rv = _roc->ReadSerialNumber();
	__SET_ARG_OUT__("Serial Number", rv);
}

std::string ROCTrackerInterface::FormatThresholdTable(
    const std::vector<float>& thresholds)
{
    if(thresholds.size() != 288)
    {
        throw std::runtime_error(std::format(
            "Threshold table requires 288 measured values; received {}.", thresholds.size()));
    }
	std::stringstream table;
	table << std::endl;
	table << std::format(" {:>7} {:>11} {:>11} {:>11}\n", "Channel", "Cal", "HV", "Sum");
	table << "--------------------------------------------\n";
	for(int channel = 0; channel < 96; ++channel)
	{
		size_t idx = 3 * static_cast<size_t>(channel);
		float  hv  = thresholds.at(idx + 0);
		float  cal = thresholds.at(idx + 1);
		float  tot = thresholds.at(idx + 2);
		table << std::format(
		    " {:4d} {:11.3f} {:11.3f} {:11.3f}\n", channel, cal, hv, tot);
	}
	return table.str();
}

void ROCTrackerInterface::MeasureThresholds(__ARGS__)
{
	// measure thresholds for all channels (all masks fully enabled)
	uint32_t           mask_lo     = 0xFFFFFFFF;
	uint32_t           mask_md     = 0xFFFFFFFF;
	uint32_t           mask_hi     = 0xFFFFFFFF;
	int                print_level = 0;
	std::ostringstream stream;

	std::vector<float> thresholds;
	__FE_COUT__ << "ROCTrackerInterface::MeasureThresholds" << __E__;
	auto rv =
	    _roc->ReadThresholds(thresholds, mask_lo, mask_md, mask_hi, print_level, stream);

	__SET_ARG_OUT__("Return code", std::to_string(rv));
    if(rv != 0 || thresholds.size() != 288)
    {
        throw std::runtime_error(std::format(
            "Measure Thresholds failed (return code {}, received {} of 288 values). {}",
            rv, thresholds.size(), stream.str()));
    }
	__SET_ARG_OUT__("Thresholds", FormatThresholdTable(thresholds));
}

void ROCTrackerInterface::FindThreshold(__ARGS__)
{
	int   channel      = __GET_ARG_IN__("Channel", int, -1);
	int   preamp       = __GET_ARG_IN__("Preamp", int, -1);
	float threshold_mv = __GET_ARG_IN__("Threshold (mV)", float, 0.0f);
	float tolerance_mv = __GET_ARG_IN__("Tolerance (mV)", float, 0.0f);

	if(channel < 0 || channel > 95)
	{
		__FE_SS__ << "Channel out of range [0, 95]: " << channel << __E__;
		__FE_SS_THROW__;
	}
	if(preamp != 0 && preamp != 1)
	{
		__FE_SS__ << "Preamp must be 0 or 1: " << preamp << __E__;
		__FE_SS_THROW__;
	}
	if(!std::isfinite(threshold_mv) || !std::isfinite(tolerance_mv) || tolerance_mv <= 0.0f)
	{
		__FE_SS__ << "Tolerance (mV) must be positive: " << tolerance_mv << __E__;
		__FE_SS_THROW__;
	}

	DTCLib::roc_data_t dac = 0;
	__FE_COUT__ << "ROCTrackerInterface::FindThreshold channel=" << channel
	            << " preamp=" << preamp << " threshold_mv=" << threshold_mv
	            << " tolerance_mv=" << tolerance_mv << __E__;
	trkdaq::ThresholdSearchResult detail;
	auto success = _roc->FindThreshold(channel, preamp, threshold_mv, tolerance_mv, dac, &detail);
	__SET_ARG_OUT__("Threshold diagnostics", trkdaq::FormatThresholdSearchResult(channel, preamp, detail));

	__SET_ARG_OUT__("Success", std::to_string(success));
	__SET_ARG_OUT__("DAC value", std::to_string(dac));
}

namespace {
std::string FormatSearchSummary(const std::vector<trkdaq::ThresholdSearchResult>& details,
                                float target) {
  size_t failed = 0;
  for (const auto& detail : details) if (!detail.success()) ++failed;
  std::string text = std::format("{} passed, {} failed.", details.size() - failed, failed);
  for (size_t i = 0; i < details.size(); ++i) {
    const auto& d = details[i];
    if (d.success()) continue;
    const char* reason = d.status == "unreachable" ? "target outside measured endpoint range" :
                         d.status == "unstable" ? "verification readings outside tolerance" :
                         "could not reach requested tolerance";
    const auto range = std::minmax_element(d.verification_mv.begin(), d.verification_mv.end());
    text += std::format("\n- Channel {} {}: {} ({}); measured {:.3f} mV, target {:.3f} mV, "
                        "error {:+.3f} mV, DAC {}; verification {:.3f} to {:.3f} mV.",
                        i / 2, i % 2 == 0 ? "CAL" : "HV", d.status, reason,
                        d.measured_mv, target, d.error_mv, d.dac, *range.first, *range.second);
  }
  return text;
}

std::string FormatSearchDetails(const std::vector<trkdaq::ThresholdSearchResult>& details) {
  std::string text = "Measured values use the requested threshold sign convention.\n";
  for (size_t i = 0; i < details.size(); ++i)
    text += trkdaq::FormatThresholdSearchResult(i / 2, i % 2, details[i]);
  return text;
}
}

std::string ROCTrackerInterface::FormatDacTable(
    const std::vector<DTCLib::roc_data_t>& dacs)
{
	std::stringstream table;
	table << std::endl;
	table << std::format(" {:>7} {:>11} {:>11}\n", "Channel", "Cal", "HV");
	table << "--------------------------------\n";
	for(int channel = 0; channel < 96; ++channel)
	{
		size_t idx = 2 * static_cast<size_t>(channel);
		auto   cal = dacs.at(idx + 0);
		auto   hv  = dacs.at(idx + 1);
		table << std::format(" {:7d} {:11d} {:11d}\n", channel, cal, hv);
	}
	return table.str();
}

void ROCTrackerInterface::FindThresholds(__ARGS__)
{
	float threshold_mv = __GET_ARG_IN__("Threshold (mV)", float, 0.0f);
	float tolerance_mv = __GET_ARG_IN__("Tolerance (mV)", float, 0.0f);

	if(!std::isfinite(threshold_mv) || !std::isfinite(tolerance_mv) || tolerance_mv <= 0.0f)
	{
		__FE_SS__ << "Tolerance (mV) must be positive: " << tolerance_mv << __E__;
		__FE_SS_THROW__;
	}

	std::vector<DTCLib::roc_data_t> dacs;
	__FE_COUT__ << "ROCTrackerInterface::FindThresholds threshold_mv=" << threshold_mv
	            << " tolerance_mv=" << tolerance_mv << __E__;
	std::vector<trkdaq::ThresholdSearchResult> details;
	auto n_failed = _roc->FindThresholds(threshold_mv, tolerance_mv, dacs, &details,
      [this](size_t completed, size_t total) {
        __SET_PCT_DONE__(std::min<size_t>(99, 100 * completed / total));
      });
	__SET_ARG_OUT__("Threshold diagnostics", FormatSearchDetails(details));

	__SET_ARG_OUT__("Failed count", std::to_string(n_failed));
	__SET_ARG_OUT__("DAC values", FormatDacTable(dacs));
    __SET_ARG_OUT__("Summary", FormatSearchSummary(details, threshold_mv));
    __SET_PCT_DONE__(100);
}

void ROCTrackerInterface::SetChannelMask(__ARGS__)
{
	const std::array<std::string, 3> argumentNames = {
	    "Mask channels 0-31", "Mask channels 32-63", "Mask channels 64-95"};
	std::array<std::string, 3> argumentValues = {
	    __GET_ARG_IN__("Mask channels 0-31", std::string, FEVInterface::DEFAULT),
	    __GET_ARG_IN__("Mask channels 32-63", std::string, FEVInterface::DEFAULT),
	    __GET_ARG_IN__("Mask channels 64-95", std::string, FEVInterface::DEFAULT)};

	auto usesConfiguration = [](const std::string& value) {
		return value.empty() || value == FEVInterface::DEFAULT || value == "DEFAULT" ||
		       value == "default";
	};

	std::array<uint32_t, 3> masks{};
	bool                     needConfiguredMasks = false;
	for(const auto& value : argumentValues)
		needConfiguredMasks |= usesConfiguration(value);
	if(needConfiguredMasks)
		masks = GetConfiguredChannelMasks();

	for(size_t i = 0; i < argumentValues.size(); ++i)
	{
		if(usesConfiguration(argumentValues[i]))
			continue;

		size_t             parsedCharacters = 0;
		unsigned long long parsedValue       = 0;
		try
		{
			parsedValue = std::stoull(argumentValues[i], &parsedCharacters, 0);
		}
		catch(const std::exception&)
		{
			throw std::runtime_error(std::format(
			    "Set Channel Mask: '{}' value '{}' is not a 32-bit integer.",
			    argumentNames[i],
			    argumentValues[i]));
		}
		if(parsedCharacters != argumentValues[i].size() ||
		   parsedValue > std::numeric_limits<uint32_t>::max())
			throw std::runtime_error(std::format(
			    "Set Channel Mask: '{}' value '{}' is not a 32-bit integer.",
			    argumentNames[i],
			    argumentValues[i]));
		masks[i] = static_cast<uint32_t>(parsedValue);
	}

	__FE_COUT__ << std::format(
	    "ROCTrackerInterface::SetChannelMask mask_lo=0x{:08x} mask_md=0x{:08x} mask_hi=0x{:08x}",
	    masks[0], masks[1], masks[2]) << __E__;
	auto rv = _roc->SetChannelMask(masks[0], masks[1], masks[2]);
	__SET_ARG_OUT__("Return code", std::to_string(rv));
}

std::array<uint32_t, 3> ROCTrackerInterface::GetConfiguredChannelMasks() const
{
	const auto selfNode = getSelfNode();
	const auto trackerParametersLink = selfNode.getNode("ROCTypeLinkTable");
	if(trackerParametersLink.isDisconnected())
		throw std::runtime_error(std::format(
		    "Set Channel Mask: ROC '{}' has no connected ROCTypeLinkTable record.",
		    getInterfaceUID()));

	const auto channelsLink =
	    trackerParametersLink.getNode("LinkToTrackerROCChannelsTable");
	if(channelsLink.isDisconnected())
		throw std::runtime_error(std::format(
		    "Set Channel Mask: tracker parameter record for ROC '{}' has no connected "
		    "LinkToTrackerROCChannelsTable group.",
		    getInterfaceUID()));

	const auto channelRows = channelsLink.getChildren();
	std::array<bool, 96> seen{};
	std::array<uint32_t, 3> masks{};
	for(const auto& channelRow : channelRows)
	{
		const unsigned int channel =
		    channelRow.second.getNode("Channel").getValue<unsigned int>();
		if(channel >= seen.size())
			throw std::runtime_error(std::format(
			    "Set Channel Mask: channel row '{}' has channel {}; expected 0-95.",
			    channelRow.first,
			    channel));
		if(seen[channel])
			throw std::runtime_error(std::format(
			    "Set Channel Mask: duplicate channel {} in the configured group for ROC '{}'.",
			    channel,
			    getInterfaceUID()));

		seen[channel] = true;
		if(channelRow.second.getNode("Enabled").getValue<bool>())
			masks[channel / 32] |= uint32_t(1) << (channel % 32);
	}

	for(size_t channel = 0; channel < seen.size(); ++channel)
		if(!seen[channel])
			throw std::runtime_error(std::format(
			    "Set Channel Mask: channel {} is missing from the configured group for ROC '{}'.",
			    channel,
			    getInterfaceUID()));

	return masks;
}

std::map<std::string, std::string> ROCTrackerInterface::getFEMacroInputDefaults(
    const std::string&                        feMacroName,
    const std::map<std::string, std::string>& /* currentInputValues */) const
{
	if(feMacroName != "Set Channel Mask")
		return {};

	const auto masks = GetConfiguredChannelMasks();
	return {{"Mask channels 0-31", std::format("0x{:08X}", masks[0])},
	        {"Mask channels 32-63", std::format("0x{:08X}", masks[1])},
	        {"Mask channels 64-95", std::format("0x{:08X}", masks[2])}};
}

std::map<std::string, std::string> ROCTrackerInterface::ParseNotoriousReadOutput(
    const std::string& text)
{
	auto trim = [](const std::string& s) -> std::string {
		size_t b = s.find_first_not_of(" \t\r\n");
		if(b == std::string::npos)
			return std::string();
		size_t e = s.find_last_not_of(" \t\r\n");
		return s.substr(b, e - b + 1);
	};

	std::map<std::string, std::string> fields;
	std::stringstream                  in(text);
	std::string                        line;
	while(std::getline(in, line))
	{
		// each parsed field is printed as "label : value(s)"
		size_t colon = line.find(':');
		if(colon == std::string::npos)
			continue;
		std::string key   = trim(line.substr(0, colon));
		std::string value = trim(line.substr(colon + 1));
		if(!key.empty())
			fields[key] = value;
	}
	return fields;
}

void ROCTrackerInterface::NotoriousRead(__ARGS__)
{
	uint16_t adc_mode      = __GET_ARG_IN__("ADC Mode", uint16_t, 0);
	uint16_t tdc_mode      = __GET_ARG_IN__("TDC Mode", uint16_t, 0);
	uint16_t num_lookback  = __GET_ARG_IN__("Waveform delay (lookback)", uint16_t, 8);
	uint16_t num_samples   = __GET_ARG_IN__("Additional Sample Packets", uint16_t, 1);
	uint32_t num_triggers  = __GET_ARG_IN__("Trigger count", uint32_t, 0);
	uint32_t mask_lo       = __GET_ARG_IN__("Channel mask lo", uint32_t, 0xFFFFFFFF);
	uint32_t mask_md       = __GET_ARG_IN__("Channel mask md", uint32_t, 0xFFFFFFFF);
	uint32_t mask_hi       = __GET_ARG_IN__("Channel mask hi", uint32_t, 0xFFFFFFFF);
	uint16_t enable_pulser = __GET_ARG_IN__("Enable pulser", uint16_t, 0);
	uint16_t marker_clock  = __GET_ARG_IN__("Fiber clock mask", uint16_t, 3);
	uint16_t mode          = __GET_ARG_IN__("\"Mode\" (deprecated)", uint16_t, 0);
	uint16_t clock         = __GET_ARG_IN__("\"Clock\" (deprecated)", uint16_t, 99);

	std::stringstream stream;
	__FE_COUT__ << "ROCTrackerInterface::NotoriousRead" << __E__;
	_roc->NotoriousRead(adc_mode,
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

	auto fields = ParseNotoriousReadOutput(stream.str());

	__SET_ARG_OUT__("ADC Mode", fields["adc_mode"]);
	__SET_ARG_OUT__("TDC Mode", fields["tdc_mode"]);
	__SET_ARG_OUT__("Waveform delay (lookback)", fields["num_lookback"]);
	__SET_ARG_OUT__("Trigger count", fields["num_triggers"]);
	__SET_ARG_OUT__("Channel masks", fields["ch_mask"]);
	__SET_ARG_OUT__("Additional Sample Packets", fields["num_samples"]);
	__SET_ARG_OUT__("Enable pulser", fields["enable_pulser"]);
	__SET_ARG_OUT__("Fiber clock mask", fields["marker_clock"]);
	__SET_ARG_OUT__("\"Mode\" (deprecated)", fields["mode"]);
	__SET_ARG_OUT__("\"Clock\" (deprecated)", fields["clock"]);
	__SET_ARG_OUT__("\"digi_read_0xb\"", fields["digi_read_0xb"]);
	__SET_ARG_OUT__("\"digi_read_0xe\"", fields["digi_read_0xe"]);
	__SET_ARG_OUT__("\"digi_read_0xd\"", fields["digi_read_0xd"]);
	__SET_ARG_OUT__("\"digi_read_0xc\"", fields["digi_read_0xc"]);
}

void ROCTrackerInterface::ConfigureDigis(__ARGS__)
{
	uint16_t tdc_mode     = __GET_ARG_IN__("TDC Mode", uint16_t, 0);
	uint16_t num_lookback = __GET_ARG_IN__("Waveform delay (lookback)", uint16_t, 8);
	uint16_t num_samples  = __GET_ARG_IN__("Additional Sample Packets", uint16_t, 1);
	uint32_t mask_lo      = __GET_ARG_IN__("Channel mask lo", uint32_t, 0xFFFFFFFF);
	uint32_t mask_md      = __GET_ARG_IN__("Channel mask md", uint32_t, 0xFFFFFFFF);
	uint32_t mask_hi      = __GET_ARG_IN__("Channel mask hi", uint32_t, 0xFFFFFFFF);

	trkdaq::NullStream null;
	auto               stream = std::ostream(&null);

	__FE_COUT__ << "ROCTrackerInterface::ConfigureDigis" << __E__;
	_roc->ConfigureDigis(tdc_mode, num_lookback, num_samples, mask_lo, mask_md, mask_hi, stream);
}

void ROCTrackerInterface::InitializeDigis(__ARGS__)
{
	__FE_COUT__ << "ROCTrackerInterface::InitializeDigis" << __E__;
	auto words = _roc->InitializeDigis();

	std::stringstream stream;
	for(size_t i = 0; i < words.size(); ++i)
		stream << std::format("0x{:04x}\n", words[i]);
	__SET_ARG_OUT__("Output", stream.str());
}

void ROCTrackerInterface::DigiRW(__ARGS__)
{
	uint16_t rw      = __GET_ARG_IN__("Read/Write", uint16_t, 0);
	uint16_t hv_cal  = __GET_ARG_IN__("HvCal", uint16_t, 0);
	uint16_t address = __GET_ARG_IN__("Address", uint16_t, 0);
	uint32_t data    = __GET_ARG_IN__("Data", uint32_t, 0);

	std::stringstream stream;
	__FE_COUT__ << "ROCTrackerInterface::DigiRW" << __E__;
	_roc->DigiRW(rw, hv_cal, address, data, stream);
	__SET_ARG_OUT__("Output", stream.str());
}

void ROCTrackerInterface::ReadSPI(__ARGS__)
{
	std::vector<uint16_t> rawData;
	std::stringstream     stream;
	__FE_COUT__ << "ROCTrackerInterface::ReadSPI" << __E__;
	// PrintLevel bit 1 prints only the named, converted monitoring values.
	// Bit 0 would prepend the raw hexadecimal SPI words.
	const int rc = _roc->ReadSpi(rawData, 0x2, stream);
	__SET_ARG_OUT__("Return Code", std::to_string(rc));
	__SET_ARG_OUT__("Output", stream.str());
	if(rc != 0)
		throw std::runtime_error(
		    std::format("Read SPI failed with return code {}.\n{}", rc, stream.str()));
}

void ROCTrackerInterface::PrintStatus(__ARGS__)
{
	std::stringstream stream;
	__FE_COUT__ << "ROCTrackerInterface::PrintStatus" << __E__;
	_roc->PrintStatus(1, stream);
	__SET_ARG_OUT__("Output", stream.str());
}

std::string ROCTrackerInterface::FormatRatesTable(
    const std::vector<trkdaq::ROC::rates_t>& rates)
{
	std::stringstream table;
	table << std::endl;
	table << std::format(" {:>7} {:>11} {:>11} {:>11}\n", "Channel", "Cal", "HV", "Coinc.");
	table << "--------------------------------------------\n";
	for(int channel = 0; channel < 96; ++channel)
	{
    auto channel_rates = rates[channel];
		float hiv = std::get<0>(channel_rates);
		float cal = std::get<1>(channel_rates);
		float coi = std::get<2>(channel_rates);
		table << std::format(
		    " {:4d} {:11.3f} {:11.3f} {:11.3f}\n", channel, cal, hiv, coi);
	}
	return table.str();
}

void ROCTrackerInterface::MeasureChannelRates(__ARGS__)
{
  constexpr uint16_t tdc_mode = 0;  // Use the existing rate-measurement default.

  __FE_COUT__ << "ROCTrackerInterface::MeasureChannelRates" << __E__;
  std::vector<trkdaq::ROC::rates_t> rates;
  std::array<uint16_t, 6> masksBeforeRead{};
  auto rv = _roc->ChannelRates(tdc_mode, rates, &masksBeforeRead);
  if(rv != 0)
    throw std::runtime_error(std::format("Read Rates failed with return code {}", rv));
  __SET_ARG_OUT__("Return Code", std::to_string(rv));
  __SET_ARG_OUT__("Rates", FormatRatesTable(rates));
  __SET_ARG_OUT__("Saved hardware masks", std::format(
      "ROC {}: CAL 0x{:04x} 0x{:04x} 0x{:04x}; HV 0x{:04x} 0x{:04x} 0x{:04x}",
      static_cast<unsigned int>(linkID_),
      masksBeforeRead[0], masksBeforeRead[1], masksBeforeRead[2],
      masksBeforeRead[3], masksBeforeRead[4], masksBeforeRead[5]));
}

/* --- */

void ROCTrackerInterface::writeEmulatorRegister(uint16_t address, uint16_t data_to_write)
{
	__FE_COUT__ << "Calling Tracker write ROC Emulator register: link number " << std::dec
	            << linkID_ << ", address = " << address
	            << ", write data = " << data_to_write << __E__;

	return;

}  // end writeEmulatorRegister()

uint16_t ROCTrackerInterface::readEmulatorRegister(uint16_t address)
{
	__CFG_COUT__ << "Tracker emulator read" << __E__;

	// if(address == 6 || address == 7)
	return ROCPolarFireCoreInterface::readEmulatorRegister(address);
	// if(address == ADDRESS_FIRMWARE_VERSION)
	// 	return 0x5;
	// else if(address == ADDRESS_MYREGISTER)
	//  	return temp1_.GetBoardTempC();
	// else
	return 0xBAFD;

}  // end readEmulatorRegister()

void ROCTrackerInterface::readEmulatorBlock(std::vector<uint16_t>& data,
                                            uint16_t               address,
                                            uint16_t               wordCount,
                                            bool                   incrementAddress)
{
	__CFG_COUT__ << "Tracker emulator block read "
	             << "wordCount= " << wordCount << __E__;

	// make up some bogus data. Right now hardwired, could be read in as a parameter...
	double input_data = 15;

	for(unsigned int i = 0; i < wordCount; ++i)
	{
		double rand_data =
		    input_data + 0.5 * (input_data * (((double)rand() / (RAND_MAX)) - 0.5));
		__CFG_COUT__ << "rand_data= " << rand_data << __E__;
		data.push_back(rand_data);
		//		data.push_back(address + (incrementAddress?i:0));
	}
}  // end readEmulatorBlock()

void ROCTrackerInterface::configure(void)
{
  auto config = nlohmann::basic_json(NULL);
	try
	{
		__CFG_COUT__
		    << "Tracker configure, first configure back-end communication with DTC... "
		    << __E__;
		ROCPolarFireCoreInterface::configure();

		//__COUT_INFO__ << "Tracker configure, next configure front-end... " << __E__;
		//__COUT_INFO__ << "..... write parameter 1 = " << TrackerParameter_1_ << __E__;
		//__COUT_INFO__ << "..... followed by parameter 2 = " << TrackerParameter_2_ <<
		//__E__;
	}
	catch(const std::runtime_error& e)
	{
		__FE_COUT__ << "Error caught: " << e.what() << __E__;
		throw;
	}
	catch(...)
	{
		__FE_SS__ << "Unknown error caught. Check printouts!" << __E__;
		try
		{
			throw;
		}  // one more try to printout extra info
		catch(const std::exception& e)
		{
			ss << "Exception message: " << e.what();
		}
		catch(...)
		{
		}
		__FE_SS_THROW__;
	}
}

void ROCTrackerInterface::start(std::string runNumber)
{
	//	DataProducerBase::registerToBuffer();

	return;
}

bool ROCTrackerInterface::running(void) { return true; }

void ROCTrackerInterface::stop()  // runNumber)
{
	return;
}

// return false to stop workloop thread
bool ROCTrackerInterface::emulatorWorkLoop(void)
{
	//__CFG_COUT__ << "emulator working..." << __E__;
	return true;  // true to keep workloop going
}  // end emulatorWorkLoop()


std::string ROCTrackerInterface::ResolveThresholdFilePath(
    const std::string& rootOverride, const std::string& setOverride)
{
	auto* configurationManager = getConfigurationManager();
	if(!configurationManager)
		throw std::runtime_error(
		    "Threshold file: no active ConfigurationManager is available.");

	// The XDAQ context is the stable key joining this FE instance to its tracker node.
	const std::string contextUID = getContextUID();
	unsigned int      slot       = 0;
	size_t            nodeMatches = 0;
	const auto nodeRows =
	    configurationManager->getNode("/SubsystemTrackerNodeMapTable").getChildren();
	for(const auto& row : nodeRows)
	{
		if(!row.second.isEnabled())
			continue;
		if(row.second.getNode("XDAQContextLinkUID").getValueAsString(
		       true /* return the UID stored in the node-map row */) != contextUID)
			continue;

		slot = row.second.getNode("Slot").getValue<unsigned int>();
		++nodeMatches;
	}
	if(nodeMatches != 1)
		throw std::runtime_error(
		    std::format("Threshold file: expected exactly one enabled "
		                "SubsystemTrackerNodeMapTable row for XDAQ context '{}', found {}.",
		                contextUID,
		                nodeMatches));
	if(slot > 17)
		throw std::runtime_error(std::format(
		    "Threshold file: slot {} for XDAQ context '{}' is outside 0-17.",
		    slot,
		    contextUID));

	auto getGlobalParameter = [&](const std::string& parameterName,
	                              const std::string& expectedType) {
		std::string value;
		size_t      matches = 0;
		const auto parameterRows = configurationManager
		                               ->getNode("/SubsystemTrackerGlobalParametersTable")
		                               .getChildren();
		for(const auto& row : parameterRows)
		{
			if(!row.second.isEnabled() || row.first != parameterName)
				continue;

			const std::string parameterType =
			    row.second.getNode("ParameterType").getValue<std::string>();
			if(parameterType != expectedType)
				throw std::runtime_error(std::format(
				    "Threshold file: parameter '{}' has type '{}'; expected '{}'.",
				    parameterName,
				    parameterType,
				    expectedType));
			value = row.second.getNode("ParameterValue").getValue<std::string>();
			++matches;
		}
		if(matches != 1)
			throw std::runtime_error(std::format(
			    "Threshold file: expected exactly one enabled global parameter "
			    "named '{}', found {}.",
			    parameterName,
			    matches));
		if(value.empty())
			throw std::runtime_error(std::format(
			    "Threshold file: global parameter '{}' is empty.", parameterName));
		return value;
	};

	const std::filesystem::path thresholdRoot(
	    rootOverride.empty() ? getGlobalParameter("ThresholdRoot", "PATH") : rootOverride);
	const std::filesystem::path thresholdSet(
	    setOverride.empty() ? getGlobalParameter("ThresholdSet", "STRING") : setOverride);
	if(!thresholdRoot.is_absolute())
		throw std::runtime_error(std::format(
		    "Threshold file: ThresholdRoot '{}' is not an absolute path.",
		    thresholdRoot.string()));
	if(thresholdSet.is_absolute() || thresholdSet.has_parent_path() ||
	   thresholdSet == "." || thresholdSet == "..")
		throw std::runtime_error(std::format(
		    "Threshold file: ThresholdSet '{}' must be one relative directory name.",
		    thresholdSet.string()));

	const int panelID = _roc->ReadPanelID();
	if(panelID < 0)
	{
		const std::string reason =
		    panelID == -2
		        ? "ROC link is enabled but not locked"
		        : "ROC link is disabled or the panel-ID reply was invalid";
		throw std::runtime_error(std::format(
		    "Threshold file: ReadPanelID failed with code {} ({}); "
		    "cannot select a threshold JSON file.",
		    panelID,
		    reason));
	}
	if(panelID > 999)
		throw std::runtime_error(
		    std::format("Threshold file: panel ID {} is outside 0-999.", panelID));

	const std::filesystem::path resolvedPath =
	    (thresholdRoot / std::format("slot_{:02d}", slot) / thresholdSet /
	     std::format("MN{:03d}.json", panelID))
	        .lexically_normal();
	return resolvedPath.string();
}

void ROCTrackerInterface::FindAndSerializeThresholds(__ARGS__)
{
  __SET_PCT_DONE__(0);
  const float threshold_mv = __GET_ARG_IN__("Threshold (mV)", float, 0.0f);
  const float tolerance_mv = __GET_ARG_IN__("Tolerance (mV)", float, 0.0f);
  const std::string path = __GET_ARG_IN__("Filesystem path", std::string, "");
  if(!std::isfinite(threshold_mv) || !std::isfinite(tolerance_mv) || tolerance_mv <= 0)
    throw std::runtime_error("Find thresholds: threshold must be finite and tolerance must be finite and positive.");

  __SET_ARG_OUT__("Serialization successful", "0");
  std::filesystem::path resolvedPath;
  if(path.empty())
    resolvedPath = ResolveThresholdFilePath();
  else
  {
    resolvedPath = std::filesystem::path(path);
    if(!resolvedPath.is_absolute())
      throw std::runtime_error("Find thresholds: Filesystem path must be absolute, or blank for the configured path.");
    if(resolvedPath.extension() == ".json")
    {
      // Preserve the explicit single-panel filename override.
      const int panelID = _roc->ReadPanelID();
      if(panelID < 0 || panelID > 999)
        throw std::runtime_error(std::format("Find thresholds: invalid panel ID {}, cannot select output file.", panelID));
      const auto filename = std::format("MN{:03d}.json", panelID);
      if(resolvedPath.filename() != filename)
        throw std::runtime_error(std::format("Find thresholds: output filename must be {} for this ROC.", filename));
    }
    else
      // Directory input is a root; use the same context-to-slot mapping as the loader.
      resolvedPath = ResolveThresholdFilePath(
          path, std::format("thresholds-{}-mV", threshold_mv == 0 ? 0.0f : threshold_mv));
  }
  resolvedPath = resolvedPath.lexically_normal();
  __SET_ARG_OUT__("Resolved path", resolvedPath.string());

  // Prepare a writable file and backup before changing any DAC. Each panel has
  // its own destination, avoiding shared JSON read/modify/write in global runs.
  std::filesystem::create_directories(resolvedPath.parent_path());
  const bool hadPrevious = std::filesystem::exists(resolvedPath);
  const std::filesystem::path backupPath(resolvedPath.string() + ".bak");
  if(hadPrevious)
  {
    if(!std::filesystem::is_regular_file(resolvedPath))
      throw std::runtime_error("Find thresholds: destination is not a regular file: " + resolvedPath.string());
    std::filesystem::copy_file(resolvedPath, backupPath,
                               std::filesystem::copy_options::overwrite_existing);
  }
  std::string temporaryPattern = resolvedPath.string() + ".tmp.XXXXXX";
  std::vector<char> temporaryName(temporaryPattern.begin(), temporaryPattern.end());
  temporaryName.push_back('\0');
  const int fd = ::mkstemp(temporaryName.data());
  if(fd < 0)
    throw std::runtime_error("Find thresholds: cannot create temporary output file beside " + resolvedPath.string());
  struct TemporaryFile
  {
    std::filesystem::path path;
    ~TemporaryFile() { std::error_code ignored; std::filesystem::remove(path, ignored); }
  } temporary{std::filesystem::path(temporaryName.data())};
  if(::close(fd) != 0)
    throw std::runtime_error("Find thresholds: failed to close temporary output file.");
  const auto filePermissions = hadPrevious ? std::filesystem::status(resolvedPath).permissions()
      : (std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
         std::filesystem::perms::group_read | std::filesystem::perms::others_read);
  std::filesystem::permissions(temporary.path, filePermissions);
  std::ofstream output(temporary.path);
  output.exceptions(std::ios::failbit | std::ios::badbit);

  std::vector<DTCLib::roc_data_t> dacs;
  std::vector<trkdaq::ThresholdSearchResult> details;
	auto n_failed = _roc->FindThresholds(threshold_mv, tolerance_mv, dacs, &details,
      [this](size_t completed, size_t total) {
        __SET_PCT_DONE__(std::min<size_t>(99, 100 * completed / total));
      });
	__SET_ARG_OUT__("Threshold diagnostics", FormatSearchDetails(details));
  __SET_ARG_OUT__("Failed count", std::to_string(n_failed));
  if(dacs.size() != 192 || details.size() != 192)
    throw std::runtime_error("Find thresholds: expected 192 CAL/HV DAC values.");
  __SET_ARG_OUT__("DAC values", FormatDacTable(dacs));
  // Save detailed measurements for both passing and failed searches. Use the
  // already-created temporary file and atomic rename; it is never a loader file.
  const std::filesystem::path diagnosticPath(resolvedPath.string() + ".diagnostics.txt");
  output << "threshold_mV=" << threshold_mv << " tolerance_mV=" << tolerance_mv
         << " failed=" << n_failed << '\n' << FormatSearchDetails(details);
  output.close();
  std::filesystem::rename(temporary.path, diagnosticPath);
  __SET_ARG_OUT__("Diagnostic path", diagnosticPath.string());

  // Exact schema consumed by DeserializeAndSetThresholds: one CAL and one HV
  // record for each logical channel, with integer 10-bit DAC thresholds.
  nlohmann::json value = nlohmann::json::array();
  for(size_t channel = 0; channel < 96; ++channel)
    for(size_t type = 0; type < 2; ++type)
    {
      const auto dac = dacs[2 * channel + type];
      if(dac > 1023)
        throw std::runtime_error("Find thresholds: refusing to save a DAC outside 0-1023.");
      const auto& detail = details.at(2 * channel + type);
      // Preserve the loader's required channel/type/threshold fields. Metadata
      // makes non-converged values explicit without preventing the user's save.
      value.push_back({{"channel", channel}, {"type", type == 0 ? "cal" : "hv"},
          {"threshold", dac}, {"calibration_status", detail.status},
          {"target_mv", threshold_mv}, {"tolerance_mv", tolerance_mv},
          {"measured_mv", detail.measured_mv}, {"error_mv", detail.error_mv},
          {"verification_mv", detail.verification_mv}});
    }
  output.open(temporary.path);
  std::filesystem::permissions(temporary.path, filePermissions);
  output << value.dump(2) << '\n';
  output.close();
  nlohmann::json readback;
  std::ifstream input(temporary.path);
  input >> readback;
  if(readback != value)
    throw std::runtime_error("Find thresholds: saved JSON verification failed.");
  input.close();
  std::filesystem::rename(temporary.path, resolvedPath);
  __SET_ARG_OUT__("Serialization successful", "1");
  __SET_ARG_OUT__("Summary", FormatSearchSummary(details, threshold_mv) + std::format(
      "\nSaved all 192 CAL/HV DAC values to {}, including {} failed searches. "
      "{}Hardware remains at the selected DAC values.",
      resolvedPath.string(), n_failed,
      hadPrevious ? "Previous file: " + backupPath.string() + ". " : ""));
  __SET_PCT_DONE__(100);
}

void ROCTrackerInterface::DeserializeAndSetThresholds(__ARGS__)
{
	const std::string validateOnlyInput =
	    __GET_ARG_IN__("Validate only", std::string, "");
	bool validateOnly = true;
	if(!validateOnlyInput.empty())
	{
		if(validateOnlyInput == "1" || validateOnlyInput == "true" ||
		   validateOnlyInput == "TRUE")
			validateOnly = true;
		else if(validateOnlyInput == "0" || validateOnlyInput == "false" ||
		        validateOnlyInput == "FALSE")
			validateOnly = false;
		else
			throw std::runtime_error(std::format(
			    "Deserialize and set thresholds: Validate only must be blank, 1/true, or "
			    "0/false; received '{}'.",
			    validateOnlyInput));
	}
	__SET_ARG_OUT__("Failed count", "0");

	const std::filesystem::path resolvedPath(ResolveThresholdFilePath());
	__SET_ARG_OUT__("Resolved path", resolvedPath.string());

	std::error_code filesystemError;
	if(!std::filesystem::is_regular_file(resolvedPath, filesystemError))
		throw std::runtime_error(std::format(
		    "Deserialize and set thresholds: resolved file '{}' is not a regular file{}{}.",
		    resolvedPath.string(),
		    filesystemError ? ": " : "",
		    filesystemError ? filesystemError.message() : ""));

	nlohmann::json json;
	{
		std::ifstream   input(resolvedPath);
		if(!input)
			throw std::runtime_error(std::format(
			    "Deserialize and set thresholds: failed to open '{}'.", resolvedPath.string()));
		try
		{
			input >> json;
		}
		catch(const nlohmann::json::exception& ex)
		{
			throw std::runtime_error(std::format(
			    "Deserialize and set thresholds: invalid JSON in '{}': {}",
			    resolvedPath.string(),
			    ex.what()));
		}
	}

	if(!json.is_array() || json.size() != 192)
		throw std::runtime_error(std::format(
		    "Deserialize and set thresholds: '{}' must contain an array of exactly 192 "
		    "records; found {}.",
		    resolvedPath.string(),
		    json.is_array() ? json.size() : 0));

	std::array<std::array<int, 2>, 96> dacs{};
	std::array<std::array<bool, 2>, 96> seen{};
	std::vector<std::string> thresholdWarnings;
	for(size_t recordIndex = 0; recordIndex < json.size(); ++recordIndex)
	{
		const auto& record = json.at(recordIndex);
		if(!record.is_object() || !record.contains("channel") ||
		   !record.at("channel").is_number_integer() || !record.contains("type") ||
		   !record.at("type").is_string() || !record.contains("threshold") ||
		   !record.at("threshold").is_number_integer())
			throw std::runtime_error(std::format(
			    "Deserialize and set thresholds: record {} must contain integer 'channel', "
			    "string 'type', and integer 'threshold' fields.",
			    recordIndex));

		const int         channel = record.at("channel").get<int>();
		const std::string type    = record.at("type").get<std::string>();
		int               threshold = record.at("threshold").get<int>();
		if(channel < 0 || channel >= 96)
			throw std::runtime_error(std::format(
			    "Deserialize and set thresholds: record {} has channel {}; expected 0-95.",
			    recordIndex,
			    channel));
		const int thresholdType = type == "cal" ? 0 : type == "hv" ? 1 : -1;
		if(thresholdType < 0)
			throw std::runtime_error(std::format(
			    "Deserialize and set thresholds: record {} has type '{}'; expected 'cal' or 'hv'.",
			    recordIndex,
			    type));
		if(threshold < 0 || threshold > 1023)
		{
			const std::string warning = std::format(
			    "record {}: channel {} {} threshold {} is outside 0-1023; using 400",
			    recordIndex,
			    channel,
			    type,
			    threshold);
			thresholdWarnings.push_back(warning);
			__FE_COUT_WARN__ << "Deserialize and set thresholds: " << warning << __E__;
			threshold = 400;
		}
		if(seen[channel][thresholdType])
			throw std::runtime_error(std::format(
			    "Deserialize and set thresholds: duplicate '{}' threshold for channel {}.",
			    type,
			    channel));

		dacs[channel][thresholdType] = threshold;
		seen[channel][thresholdType] = true;
	}
	for(size_t channel = 0; channel < seen.size(); ++channel)
		for(size_t thresholdType = 0; thresholdType < seen[channel].size(); ++thresholdType)
			if(!seen[channel][thresholdType])
				throw std::runtime_error(std::format(
				    "Deserialize and set thresholds: missing {} threshold for channel {}.",
				    thresholdType == 0 ? "cal" : "hv",
				    channel));

	auto appendThresholdWarnings = [&](std::string summary) {
		if(thresholdWarnings.empty())
			return summary;

		summary += std::format(
		    " WARNING: {} out-of-range threshold value{} substituted with 400:",
		    thresholdWarnings.size(),
		    thresholdWarnings.size() == 1 ? " was" : "s were");
		for(const auto& warning : thresholdWarnings)
			summary += "\n - " + warning;
		return summary;
	};

	if(validateOnly)
	{
		__SET_ARG_OUT__("Summary",
		                appendThresholdWarnings(std::format(
		                    "Validated 192 threshold records from {}; no DACs were programmed.",
		                    resolvedPath.string())));
		return;
	}

	// The complete file is validated before the first hardware write. Programming is
	// still not atomic: a hardware error can leave a subset of DACs updated.
	size_t nFailed = 0;
	for(size_t channel = 0; channel < dacs.size(); ++channel)
	{
		const int calReturnCode = _roc->SetThreshold(channel, 0, dacs[channel][0]);
		if(calReturnCode != 0)
			++nFailed;

		const int hvReturnCode = _roc->SetThreshold(channel, 1, dacs[channel][1]);
		if(hvReturnCode != 0)
			++nFailed;
	}

	__SET_ARG_OUT__("Failed count", std::to_string(nFailed));
	__SET_ARG_OUT__("Summary",
	                appendThresholdWarnings(std::format(
	                    "Programmed 192 threshold DACs from {}; {} writes returned failure.",
	                    resolvedPath.string(),
	                    nFailed)));
}

void ROCTrackerInterface::TestJSON(__ARGS__){
  std::string key   = __GET_ARG_IN__("Key", std::string, "");
  std::string value = __GET_ARG_IN__("Value", std::string, "");
  std::string path  = __GET_ARG_IN__("Path", std::string, "");

  auto written = ROCTrackerInterface::SafeSerialize(path, key, value);

  __SET_ARG_OUT__("Serialization successful", std::to_string(written));
}

bool ROCTrackerInterface::SafeSerialize(std::string path,
                                        std::string key,
                                        nlohmann::json value){
  nlohmann::json json;

  // read preexisting mappings from disk
  std::ifstream fi(path.c_str());
  if (!fi){
    return false;
  }
  fi >> json;
  fi.close();

  // add new mapping
  json[key] = value;

  // write back to disk
  std::ofstream fo(path.c_str());
  if (!fo){
    return false;
  }
  fo << json << std::endl;
  fo.close();

  return true;
}

nlohmann::json ROCTrackerInterface::SafeDeserialize(std::string path,
                                                    std::string key){
  nlohmann::json json;

  // read preexisting mappings from disk
  std::ifstream fi(path.c_str());
  if (!fi){
    return false;
  }
  fi >> json;
  fi.close();

  auto rv = json[key];
  return rv;
}

void ROCTrackerInterface::UpdateChannelThresholds(__ARGS__)
{
	std::string channelUID   = __GET_ARG_IN__("ChannelUID", std::string, "");
	std::string thresholdCal = __GET_ARG_IN__("ThresholdCal", std::string, "");
	std::string thresholdHV  = __GET_ARG_IN__("ThresholdHV", std::string, "");

	__FE_COUT__ << "Updating thresholds for ChannelUID=" << channelUID
	            << " ThresholdCal=" << thresholdCal
	            << " ThresholdHV=" << thresholdHV << __E__;

	if(channelUID.empty())
	{
		__SET_ARG_OUT__("Result", "Error: ChannelUID is required.");
		return;
	}

	std::map<std::string, std::map<std::string, std::string>> cellUpdates;
	if(!thresholdCal.empty())
		cellUpdates[channelUID]["ThresholdCal"] = thresholdCal;
	if(!thresholdHV.empty())
		cellUpdates[channelUID]["ThresholdHV"] = thresholdHV;

	if(cellUpdates[channelUID].empty())
	{
		__SET_ARG_OUT__("Result", "Error: At least one of ThresholdCal or ThresholdHV must be provided.");
		return;
	}

	try
	{
		ConfigurationManagerRW  cfgMgrInst("FEMacro_admin");
		ConfigurationManagerRW* cfgMgr = &cfgMgrInst;

		std::string accumulatedWarnings;
		cfgMgr->getAllTableInfo(true,
		                       &accumulatedWarnings,
		                       "",
		                       false,
		                       false,
		                       true /* initializeActiveGroups */);

		TableVersion newVersion = cfgMgr->updateTableCells(
		    "SubsystemTrackerChannelsTable",
		    cellUpdates,
		    "FEMacro" /* author */);

		std::stringstream result;
		result << "Success: " << channelUID << " updated. New version: v" << newVersion;
		__FE_COUT__ << result.str() << __E__;
		__SET_ARG_OUT__("Result", result.str());
	}
	catch(const std::runtime_error& e)
	{
		std::string errMsg = std::string("Error: ") + e.what();
		__FE_COUT_ERR__ << errMsg << __E__;
		__SET_ARG_OUT__("Result", errMsg);
	}
}

DEFINE_OTS_INTERFACE(ROCTrackerInterface)
