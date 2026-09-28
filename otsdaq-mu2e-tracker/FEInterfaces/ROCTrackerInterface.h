#ifndef _ots_ROCTrackerInterface_h_
#define _ots_ROCTrackerInterface_h_

#include <fstream>
#include <iostream>
#include <string>

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

#include "otsdaq-mu2e-tracker/Ui/DtcInterface.hh"

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

	virtual inline DTCLib::DTC* getDTC(void) override
	{
		if(!thisDTC_)
		{
			__SS__ << "thisDTC_ pointer has not been initialized! "
			       << StringMacros::stackTrace();
			__SS_THROW__;
		}
		if(!trackerDTC_)
		{
			__FE_COUT__ << "Creating trackerDTC_ interface for Tracker DTC" << __E__;
			trackerDTC_ = std::make_unique<trkdaq::DtcInterface>(thisDTC_);
		}
		return thisDTC_;
	};

	// state machine
	//----------------
	void 									configure				(void) override;
	void 									start					(std::string runNumber) override;
	void 									stop					(void) override;
	bool 									running					(void) override;

	// write and read to registers
	virtual void 							writeEmulatorRegister	(uint16_t address, uint16_t data_to_write) override;
	virtual uint16_t						readEmulatorRegister	(uint16_t address) override;
	virtual void							readEmulatorBlock	(std::vector<uint16_t>& data, uint16_t address, uint16_t wordCount, bool incrementAddress) override;


	//------------ for tracker-specific Ui functions

	std::unique_ptr<trkdaq::DtcInterface>			trackerDTC_;

	// from DtcInterface.h :  ------------------
	int          fEnabled = 1;   // if comes from ODB, could be 0
	int          fPcieAddr = 0;  //
	int          fLinkMask = -1;  // int is OK, bit 31 is never used for arithmetics
	                         // for now assume that all ROCs are doing the same
	                         // fRocReadoutMode: (fixed_length << 4) | readout_mode
	int fRocReadoutMode = 0;     // 0: 'counter patterns' 1:digis 2:checkerboard patterns
	int fRocLaneMask = 0xf;        // 0xf : all of them
	int fRocNHitsPerLane = 2;    // NHits per lane for Mode=2
	int fSampleEdgeMode = 1;     // 0:force raising 1:force falling 2:auto
	int fEmulateCfo = 1;         // 1: this DTC operated in the emulated CFO mode
	int fJAMode = 0x01;             // clock_source << 4 | reset

	int fOnSpill = 0;    // 1:on-spill, 0:off-spill
	int fEventMode = 1;  // whatever it is, hopefully, together they make 5 bytes

	int fDtcID = 0;  // unique DTC ID used by the DAQ (0x9154)
	int fPartitionID = 0;
	int fMacAddrByte = 0;

	int fSleepTimeROCWrite = 0;  // the two are different
	int fSleepTimeROCReset = 0;  //

    int                  fCounter = 0;

    const char*         fgSpiVarName[trkdaq::TrkSpiDataNWords]; //
    const char*         fgKeyVarName[trkdaq::TrkKeyDataNWords]; //
    const char*         fgIlpVarName[trkdaq::TrkIlpDataNWords]; //
    int           		fgFpga[96];                     // 0:CAL or 1:HV
	bool 	   			fInitialized = false;

	int          LinkEnabled		  (int Link) 	{ return (fLinkMask >> 4*Link) & 0x1 ; }
    const char*  SpiVarName           (int I) 		{ return fgSpiVarName[I]; }
    const char*  SpiVarNamePrintBuffer(int I) 		{ return fgSpiVarName[I]; }
    const char*  KeyVarName           (int I) 		{ return fgKeyVarName[I]; }
    const char*  IlpVarName           (int I) 		{ return fgIlpVarName[I]; }

	struct RocDataHeaderPacket_t
	{  // 8 16-byte words in total
	// 16-bit word 0
		uint16_t byteCount : 16;
		// 16-bit word 1
		uint16_t unused : 4;
		uint16_t packetType : 4;
		uint16_t linkID : 3;
		uint16_t DtcErrors : 4;
		uint16_t valid : 1;
		// 16-bit word 2
		uint16_t packetCount : 11;
		uint16_t unused2 : 2;
		uint16_t subsystemID : 3;
		// 16-bit words 3-5
		uint16_t eventTag[3];
		// 16-bit word 6
		uint8_t status : 8;
		uint8_t version : 8;
		// 16-bit word 7
		uint8_t dtcID : 8;
		uint8_t onSpill : 1;
		uint8_t subrun : 2;
		uint8_t eventMode : 5;

		ulong ewtag()
		{
			ulong x1  = eventTag[0];
			ulong x2  = eventTag[1];
			ulong x3  = eventTag[2];
			ulong ewt = x1 | (x2 << 16) | (x3 << 32);
			return ewt;
		}

		// decoding status

		int empty() { return (status & 0x01) == 0; }
		int invalid_dr() { return (status & 0x02); }
		int corrupt() { return (status & 0x04); }
		int timeout() { return (status & 0x08); }
		int overflow() { return (status & 0x10); }

		int error_code() { return (status & 0x1e); }
	};

	struct RocData_t
	{  // 8 16-byte words in total
		RocDataHeaderPacket_t header;
		uint16_t              data[1];
	};

	const std::vector<int> RocRegisters = {
		0,   18,    8,   15,   16,    7,      6,    4,
		23,   24,   25,   26,   11,   12,     65,   65,   17,   28,
		29,   30,   31,   32,   33,   34,      9,   10,   35,   36,
		13,
		37,   38,   38,   40,   41,   42,     43,   44,   45,   46,
		48,   49,   51,   52,   54,   55,     57,   58,
		72,   73,   74,   75,
		0x90, 0x91, 0x92, 0x93, 0x94, 0x95
	};
	// end from DtcInterface.h  ------------------

	// from DtcInterface.cc :  ------------------
											// channel readout sequence
											// the first 48 are readout by the digi FPGA on the CAL side
											// the rest 48 - by the FPGA on the HV side
	const int adc_index[96] = {
		91, 85, 79, 73, 67, 61, 55, 49,          // lane 0
		43, 37, 31, 25, 19, 13,  7,  1,
		90, 84, 78, 72, 66, 60, 54, 48,

		42, 36, 30, 24, 18, 12,  6,  0,          // lane 1
		93, 87, 81, 75, 69, 63, 57, 51,
		45, 39, 33, 27, 21, 15,  9,  3,

		44, 38, 32, 26, 20, 14,  8,  2,          // lane 2
		92, 86, 80, 74, 68, 62, 56, 50,
		47, 41, 35, 29, 23, 17, 11,  5,

		95, 89, 83, 77, 71, 65, 59, 53,          // lane 3
		46, 40, 34, 28, 22, 16, 10,  4,
		94, 88, 82, 76, 70, 64, 58, 52
	};

	const char* kSpiVarName[trkdaq::TrkSpiDataNWords] = {
		"I3_3", "I2_5", "I1_8HV" , "IHV5_0",                          //  0
		"VDMBHV5_0", "V1_8HV"  , "V3_3HV", "V2_5" ,                   //  4
		"A0"     , "A1"  ,    "A2"  , "A3"  ,                         //  8
		"I1_8CAL", "I1_2"  , "ICAL5_0"  ,                             // 12
		"ADCSPARE",                                                   // 15
		"V3_3"  , "VCAL5_0", "V1_8CAL", "V1_0",                       // 16
		"ROCPCBTEMP", "HVPCBTEMP", "CALPCBTEMP", "RTD",               // 20
		"ROC_RAIL_1V", "ROC_RAIL_1_8V", "ROC_RAIL_2_5V", "ROC_TEMP",  // 24
		"CAL_RAIL_1V", "CAL_RAIL_1_8V", "CAL_RAIL_2_5V", "CAL_TEMP",  // 28
		"HV_RAIL_1V" , "HV_RAIL_1_8V" , "HV_RAIL_2_5V" , "HV_TEMP"    // 32
	};

	const char* kKeyVarName[trkdaq::TrkKeyDataNWords] = {
		"KEY_TEMP", "KEY_V2P5", "KEY_V5P1", "KEY_DCDCTEMP"
	};

	const char* kIlpVarName[trkdaq::TrkIlpDataNWords] = {
		"ILP_ID", "ILP_TEMP", "ILP_PRESSURE"
	};
	// end from DtcInterface.cc ------------------


	// from DtcInterface_ProgramRoc.cc :  ------------------
	                                     // registers
	int RREG                  = 384;
	int REG_STATUS            = 132;
											// commands
	int SPI_CLEAR             = 3;
	int PROGRAM_IAP_W_INDEX   = 4;
	int PROGRAM_IAP_W_ADDRESS = 5;
	int IAP_AUTO_UPDATE       = 6;
	int SPI_FLASH_READ        = 7;
	int SPI_WRITE_RECORD      = 8;
	int SPI_WRITE_DIRECTORY   = 9;

	trkdaq::RocFwData_t fgRocFwData = {
											// spi_directory
		{
		{ 0,   0x10000, "/home/mu2etrk/test_stand/spi_files/GoldenV10.spi"          }, // 9524032 },
		{ 1, 0x1010000, "/home/mu2etrk/test_stand/spi_files/ROCV12.spi"             }, // 9480352 },
		{ 2, 0x5000000, "/home/mu2etrk/test_stand/spi_files/ROCV12-3_stage3init.bin"}, //   82272 },
		{ 3, 0x2010000, "/home/mu2etrk/test_stand/spi_files/ROCV14.spi"             }, // 9482832 },
		{ 4, 0x5040000, "/home/mu2etrk/test_stand/spi_files/ROCV14_stage3init.bin"  }, //   86384 },
		{-1,        -1, ""                                                          },  //      -1 }
		{-1,        -1, ""                                                          },  //      -1 }
		{-1,        -1, ""                                                          },  //      -1 }
		{-1,        -1, ""                                                          },  //      -1 }
		{-1,        -1, ""                                                          }   //      -1 }
		},
											// firmware versions = pairs of images
		{
		{ "GoldenV10",  0, -1 },
		{ "ROCV12",     1,  2 },
		{ "ROCV14",     3,  4 },
		{ "",          -1, -1 },
		{ "",          -1, -1 },
		{ "",          -1, -1 },
		{ "",          -1, -1 },
		{ "",          -1, -1 },
		{ "",          -1, -1 },
		{ "",          -1, -1 }
		}
	};
	// end from DtcInterface_ProgramRoc.cc

	// UI_DEFINE_ROCTRACKERINTERFACE_FUNCTIONS

	#include "otsdaq-mu2e-tracker/FEInterfaces/ROCTrackerInterface_Ui_base.hxx"
	#include "otsdaq-mu2e-tracker/FEInterfaces/ROCTrackerInterface_Ui_base_declareFEMacros.hxx"

	#include "otsdaq-mu2e-tracker/FEInterfaces/ROCTrackerInterface_Ui.hxx"
	#include "otsdaq-mu2e-tracker/FEInterfaces/ROCTrackerInterface_Ui_declareFEMacros.hxx"

	#include "otsdaq-mu2e-tracker/FEInterfaces/ROCTrackerInterface_Ui_print.hxx"
	#include "otsdaq-mu2e-tracker/FEInterfaces/ROCTrackerInterface_Ui_print_declareFEMacros.hxx"

	#include "otsdaq-mu2e-tracker/FEInterfaces/ROCTrackerInterface_Ui_ControlRoc.hxx"
	#include "otsdaq-mu2e-tracker/FEInterfaces/ROCTrackerInterface_Ui_ControlRoc_declareFEMacros.hxx"

	#include "otsdaq-mu2e-tracker/FEInterfaces/ROCTrackerInterface_Ui_ProgramRoc.hxx"
	#include "otsdaq-mu2e-tracker/FEInterfaces/ROCTrackerInterface_Ui_ProgramRoc_declareFEMacros.hxx"


	//------------ end for tracker-specific Ui functions



	// For injection pulse readout
	//----------------
	//
	enum {
		REG_CONFIG      			= 8,
		REG_ROC_RESET      			= 14,
		REG_DIGI_SETUP_CHECK		= 18,
		REG_DATA_VERSION   			= 29,
		REG_UP_DONE       			= 128,
		REG_BLOCK_WRITE_CHECK      	= 129,
		REG_READSPI       			= 258,
		REG_DIGIRW        			= 263,
		REG_FINDALIGNMENT 			= 264,
		REG_READ          			= 265,
		REG_SETGAIN       			= 266,
		REG_SET_THR       			= 267,
		REG_PULSERON      			= 268,
		REG_PULSEROFF     			= 269,
		REG_MEAS_THR      			= 270,
		REG_READRATES     			= 271,
		REG_READGITCOMMIT 			= 272,
		REG_READILP       			= 273,
		REG_GETKEY        			= 274,
	};

	struct ControlRoc_Read_Input_t0 {
		uint16_t    adc_mode;               // -a 8   (defailt:  0)  [0]                   // *v2*
		uint16_t    tdc_mode;               // -t 8   (default:  0)  [1]                   // *v2*
		uint16_t    num_lookback;           // -l 8   (default:  8)  [2]                   // *v2*
		uint16_t    num_samples;            // -s 1   (default: 16)  [3] if>63, set to 63  // *v2*

		uint16_t    num_triggers[2];        // -T 10  (default:  0)  [4-5]                 // *v2*
		uint16_t    ch_mask[6];             // FFFF FFFF FFFF FFFF FFFF FFFF [6:11]        // *v2*

		uint16_t    enable_pulser;          // -p 1     (default: 0) [12]                  // *v2*

		uint16_t    marker_clock;           // -m 3 ??? )default: 0) [13]                  // *v2*
		uint16_t    mode;                   // [14] need to set mode=0                     // *v2*
		uint16_t    clock;                  // [15] need to set clock=99                   // *v2*
	};

	void 			dtc_control_roc_read(	int      LinkMask     = -1,
											int      AdcMode      = 0,
											int      TdcMode      = 0,
											int      NumLookback  = 0,
											int      EnablePulser = 0,
											int      MarkerClock  = 3,
											int      NumSamples   = 1,
											uint32_t MaskC        = 0xFFFFFFFF,
											uint32_t MaskD        = 0xFFFFFFFF,
											uint32_t MaskE        = 0xFFFFFFFF,
											int      PcieAddr     = -1);


    void         	ControlRoc_Read   (		ControlRoc_Read_Input_t0* Par        = nullptr,
											int                       LinkMask   = -1   ,
											int                       PrintLevel = 0    ,
											std::ostream&             Stream     = std::cout);

	// end For injection pulse readout
	//----------------


	bool emulatorWorkLoop(void) override;

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

	//	temperature--
	class Thermometer {
		private:
			double mnoiseTemp;

		public:
			void noiseTemp(double intemp) {
				mnoiseTemp = (double)intemp +
				   0.5 * (intemp * ((double)rand() / (RAND_MAX)) - 0.5);
				return;
			}
			double GetBoardTempC() { return mnoiseTemp; }
	};

	Thermometer temp1_;
	double inputTemp_;

	private:
		unsigned int TrackerParameter_1_;
		bool TrackerParameter_2_;

		unsigned int number_of_good_events_;
		unsigned int number_of_bad_events_;
		unsigned int number_of_empty_events_;
		std::ofstream datafile_;
		unsigned int event_number_;

  public:
	// void 			ReadTrackerFIFO			(__ARGS__);

	void 			ReadROCErrorCounter		(__ARGS__);
	virtual void 	GetStatus				(__ARGS__) override;
	void 			SetupForDigiDataTaking	(__ARGS__);
	void 			FindAlignment			(__ARGS__);
	void 			GetUIParameters			(__ARGS__);
	void 			SetUIParameters			(__ARGS__);

	// clang-format on
};

}  // namespace ots

#endif
