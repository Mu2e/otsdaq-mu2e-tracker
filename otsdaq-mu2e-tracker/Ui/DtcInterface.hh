//-----------------------------------------------------------------------------
// interactive interface for ROOT-based GUI
// mixes high- and low-level commands
// assume everything is happening on one node
// there could be one or two DTCs and only one CFO
// TODO : interface change :
// 1. std::ostream& --> std::ostream* or use std::ostream(nullptr)
//    to avoid ifs in the functions being called
//-----------------------------------------------------------------------------
#ifndef __trkdaq_dtc_interface_hh__
#define __trkdaq_dtc_interface_hh__

#define __CLING__ 1

#include <string>
#include <vector>
#include <sstream>
#include <iostream>

#include "dtcInterfaceLib/DTC.h"
#include "artdaq-core-mu2e/Overlays/DTC_Types/DTC_Link_ID.h"
#include "artdaq-core-mu2e/Overlays/DTC_Packets/DTC_RocDataHeaderPacket.h"

#include "otsdaq-mu2e-tracker/Ui/ControlRocTypes.hh"

#include "otsdaq-mu2e-tracker/ParseAlignment/Alignment.hh"
#include "otsdaq-mu2e-tracker/ParseAlignment/PrintLegacyTable.hh"
#include "otsdaq-mu2e-tracker/Ui/DtcInterfaceBase.hh"
#include "otsdaq-mu2e-tracker/Ui/ProgramRoc.hh"

#include "otsdaq-mu2e-tracker/Ui/BisectionSearch.hh"
#include "otsdaq-mu2e-tracker/Ui/ThresholdSearch.hh"

namespace trkdaq {
  using roc_serial_t = std::string;

     // some ROC registers are listed in decimal format, and some - in hex
    static const std::vector<int> RocRegisters = {
       0,   18,    8,   15,   16,    7,      6,    4,
      23,   24,   25,   26,   11,   12,     65,   65,   17,   28,
      29,   30,   31,   32,   33,   34,      9,   10,   35,   36,
      13,
      37,   38,   38,   40,   41,   42,     43,   44,   45,   46,
      48,   49,   51,   52,   54,   55,     57,   58,
      72,   73,   74,   75,
      0x90, 0x91, 0x92, 0x93, 0x94, 0x95
  };

  class DtcInterface : public mu2edaq::DtcInterface {
  private:
    DtcInterface(int PcieAddr, uint LinkMask, bool SkipInit);
  public:

    DtcInterface(DTCLib::DTC* Dtc);

    using DtcConfiguration_t = mu2edaq::DtcInputData_t;
    void PostInitialize(const DtcConfiguration_t&);

    struct RocData_t {                    // 8 16-bit words in total
      RocDataHeaderPacket_t header;
      uint16_t              data[1];
    };
//-----------------------------------------------------------------------------
// ROC functions : if LinkMask=0, use fLinkMask
//-----------------------------------------------------------------------------
    int                        RocConfigurePatternMode();
    int                        RocSetDataVersion      (int Version, int LinkMask=0);

    int                        fRocLaneMask;        // 0xf : all of them
    int                        fRocNHitsPerLane;    // NHits per lane for Mode=2
    static const char*         fgSpiVarName[TrkSpiDataNWords]; //
    static const char*         fgKeyVarName[TrkKeyDataNWords]; //
    static const char*         fgIlpVarName[TrkIlpDataNWords]; //
    static       int           fgFpga[96];                     // 0:CAL or 1:HV
    static       RocFwData_t   fgRocFwData;
//-----------------------------------------------------------------------------
// functions
//-----------------------------------------------------------------------------
  public:
    static       DtcInterface* Instance             (int PcieAddr, uint LinkMask = 0x11, bool SkipInit = false);
    static const RocFwData_t*  RocFwData            ()      { return (const RocFwData_t*) &fgRocFwData; }

    static const char*         SpiVarName           (int I) { return fgSpiVarName[I]; }
    static const char*         SpiVarNamePrintBuffer(int I) { return fgSpiVarName[I]; }
    static const char*         KeyVarName           (int I) { return fgKeyVarName[I]; }
    static const char*         IlpVarName           (int I) { return fgIlpVarName[I]; }
//-----------------------------------------------------------------------------
// generic interface to control_ROC.py commands.
// When/if we figure how to do it better, we'll implement a better solution
//-----------------------------------------------------------------------------
    int          ControlRoc(const char* Command, void* Parameters);

    // need: digi_rw -h 0 -w 1 -a 0x82 -d 0x1388
    // call with std::ostream(nullptr) to suppress printout
    int          ControlRoc_DigiRW (ControlRoc_DigiRW_Input_t*  Input          ,
                                    ControlRoc_DigiRW_Output_t* Output         ,
                                    int                         LinkMask   = -1,
                                    int                         PrintLevel =  0,
                                    std::ostream&               Stream     = std::cout);

                                        // result as an integer
    int          DigiRead (int Addr, int HvCal, uint32_t& Res, int Link = -1, int PrintLevel = 0, std::ostream& Stream = std::cout);
    int          DigiWrite(int Addr, int HvCal, uint16_t  Dat, int Link = -1, int PrintLevel = 0, std::ostream& Stream = std::cout);
//-----------------------------------------------------------------------------
// Channel = 0-95: read settings of a given preamp channel: gain_cal, gain_hv, thr_cal, thr_hv,
//                 4 words in total
//         = -1  : read settings of all channels : gain_cal[96], gain_hv[96], thr_cal[hv], thr_hv[cal],
//                 386 words in total
//-----------------------------------------------------------------------------
    int          ControlRoc_DumpSettings(int                   Link           ,
                                         int                   Channel    = -1,
                                         int                   PrintLevel =  0,
                                         std::ostream&         Stream     = std::cout);

    int          ControlRoc_ReadSettings(int                    Link           ,
                                         int                    Channel        ,
                                         std::vector<uint16_t>& Data           ,
                                         int                    PrintLevel =  0,
                                         std::ostream&          Stream     = std::cout);

    int          ControlRoc_PulserOn (int Link              ,
                                      int FirstChannelMask = 0x10,      // first channel:- #4
                                      int DutyCycle        = 10  ,
                                      int PulserDelay      = 1000,
                                      int PrintLevel       = 0x2,
                                      std::ostream& Stream = std::cout);

    int          ControlRoc_PulserOff(int Link, int PrintLevel = 0, std::ostream& Stream= std::cout);

                                        // always a single link
    int          ControlRoc_Rates    (int                     Link            ,
                                      std::vector<uint16_t>*  Output          ,
                                      int                     PrintLevel = 0x2,
                                      ControlRoc_Rates_t*     Par        = nullptr,
                                      std::ostream&           Stream     = std::cout);

    int          ControlRoc_Read   (ControlRoc_Read_Input_t0* Par        = nullptr,
                                    int                       Link       = 0    ,
                                    int                       PrintLevel = 0    ,
                                    std::ostream&             Stream     = std::cout);

    int          ControlRoc_ReadDeviceID(int                    Link,
                                         ControlRoc_DeviceID_t& DevId,
                                         int                    PrintLevel = 0,
                                         std::ostream&          Stream     = std::cout);
//-----------------------------------------------------------------------------
// if Line = -1, not interested in the output, only in the printout
// if OK, the read functions return Nwords
//   -1: failure
//-----------------------------------------------------------------------------
    int          ControlRoc_ReadGitCommit(std::string&       GitCommit       ,
                                          int                Link       = -1 ,
                                          int                PrintLevel = 0  ,
                                          std::ostream&      Stream     = std::cout);

    int          ControlRoc_ReadIlp(std::vector<uint16_t>&   RawData         ,
                                    int                      Link       = -1 ,
                                    int                      PrintLevel = 0  ,
                                    std::ostream&            Stream     = std::cout);

    int          ControlRoc_GetKey (std::vector<uint16_t>&   RawData         ,
                                    int                      Link       = -1 ,
                                    int                      PrintLevel = 0  ,
                                    std::ostream&            Stream     = std::cout);

    int          ControlRoc_ReadSpi(std::vector<uint16_t>&   SpiRawData     ,
                                    int                      Link       = -1,
                                    int                      PrintLevel = 0 ,
                                    std::ostream&            Stream     = std::cout);

    int          ControlRoc_ReadSpi_1(TrkSpiData_t*          Spi,
                                      int                    Link       = -1,
                                      int                    PrintLevel = 0 ,
                                      std::ostream&          Stream     = std::cout);
//-----------------------------------------------------------------------------
// measure thresholds returns an array of thresholds, which needs to be parsed
// so far, do it internally
// PrintLevel: bit 0: hex printout, bit 1: parsed printout
//-----------------------------------------------------------------------------
    int          ControlRoc_MeasureThresholds(int           Link                   ,
                                              uint32_t      MaskC      = 0xFFFFFFFF,
                                              uint32_t      MaskD      = 0xFFFFFFFF,
                                              uint32_t      MaskE      = 0xFFFFFFFF,
                                              int           PrintLevel = 0x2       ,
                                              std::ostream& Stream     = std::cout);

    int          ControlRoc_ReadThresholds   (int                        Link      ,
                                              std::vector<float>&        Thr       ,
                                              uint32_t      MaskC      = 0xFFFFFFFF,
                                              uint32_t      MaskD      = 0xFFFFFFFF,
                                              uint32_t      MaskE      = 0xFFFFFFFF,
                                              int           PrintLevel = 0x2       ,
                                              std::ostream& Stream     = std::cout );

//-----------------------------------------------------------------------------
// supposedly, PulseHeight = V/3.3*1024 or 1024 = 3.3V
//-----------------------------------------------------------------------------
    int          ControlRoc_SetCalDac        (int Link, int FirstChannelMask, int PulseHeight,
                                              int PrintLevel = 0, std::ostream& Stream = std::cout);
//-----------------------------------------------------------------------------
// PreampType: 0:HV 1:CAL, or vice versa
// do one channel at a time
// shall we think of a block operation ? or not ? - channels could be masked OFFx
//-----------------------------------------------------------------------------
    int          ControlRoc_SetGain      (int Link, int ChannelID, int PreampType, int Gain     , int PrintLevel = 0);
    int          ControlRoc_SetThreshold (int Link, int ChannelID, int PreampType, int Threshold, int PrintLevel = 0);
//-----------------------------------------------------------------------------
// block operation: set thresholds and gains for all channels on a given DRAC
// 4 x 96 16 bit words. Gain cal, Gain HV, threshold CAL, threshold HV
// It is a block write of 384 values to address 275 (0x113). It does not return anything
// but you should still request that reg=128 read 0x8000 while reg=129 should stay at the default empty value of 0x1000
//-----------------------------------------------------------------------------
    int          ControlRoc_SetThresholds(int Link, uint16_t* TG, int PrintLevel = 0, std::ostream& Stream = std::cout);

    int          ConvertSpiData(const std::vector<uint16_t>& RawData,
                                TrkSpiData_t*                Data   ,
                                int                          PrintLevel = 0,
                                std::ostream&                Stream     = std::cout);
// ejc
    int          FindAlignment (DTCLib::DTC_Link_ID Link, Alignment& Res);
    int          FindAlignments(int Link, int& NBitSlips, int PrintLevel, std::ostream& Stream = std::cout);

    bool         FindThreshold(const int           Link        ,
                               const int           ChannelID   ,
                               const int           PreampType  ,
                               const float         threshold_mv,
                               const float         tolerance_mv,
                               DTCLib::roc_data_t& out,
                               ThresholdSearchResult* result = nullptr);

    bool         FindThreshold(const int   Link,
                               const int   ChannelID,
                               const int   PreampType,
                               const float threshold_mv,
                               const float tolerance_mv);

    float        QueryThreshold(int Link, int ChannelID, int PreampType);

    float        ProgramAndQueryThreshold(const int Link,
                                          const int ChannelID,
                                          const int PreampType,
                                          const DTCLib::roc_data_t dac);

		uint16_t ReadROCRegister(int link, uint16_t address);
		void WriteROCRegister(int link, uint16_t address, uint16_t data);

    virtual std::vector<std::string> GetRocRegistersNames     (bool history = false)            override;
    virtual std::vector<uint32_t>    GetRocRegisters          (int ilink, bool history = false) override;
    virtual std::vector<float>       GetConvertedRocRegisters (int ilink, bool history = false) override;

    virtual std::string              GetRocID         (int Link) override;
    virtual std::string              GetRocDesignInfo (int Link) override;
    virtual std::string              GetRocFwGitCommit(int Link) override;

    virtual int                      InitRocReadoutMode(std::ostream& Stream = std::cout)      override;
    int                              InitReadoutROC(int                            Link,
                                                    int                            RocReadoutMode,
                                                    int                            DtcID,
                                                    uint16_t                       DigitizationStart5ns,
                                                    uint16_t                       DigitizationStop5ns,
                                                    uint16_t                       EventWindowDelay5ns,
                                                    const ControlRoc_Read_Input_t0& ReadSettings,
                                                    std::ostream&                  Stream = std::cout);
    int                              EnsureDigiRxLanesReady(int            Link,
                                                            uint16_t       EnabledLanes,
                                                            std::ostream& Stream = std::cout);
//-----------------------------------------------------------------------------
// ROC has 4 lanes: 2 CAL lanes (0x5) and 2 HV lanes (0xa)
//-----------------------------------------------------------------------------
    int          MonicaDigiClear       (std::ostream& Stream = std::cout);
    int          MonicaVarLinkConfig   (std::ostream& Stream = std::cout);
    int          MonicaVarPatternConfig(int LaneMask = -1, int NHits = -1, std::ostream& Stream = std::cout);
//-----------------------------------------------------------------------------
// internally called by ReadPanelID and WritePanelID
//-----------------------------------------------------------------------------
    int          PanelID_RW        (int Link, int Rw, int& PanelID, int PrintLevel = 0);
//-----------------------------------------------------------------------------
// reboot microcontroller unit, Link=-1: all active links
//-----------------------------------------------------------------------------
    int          RebootMcu          (int Link);
//-----------------------------------------------------------------------------
// assume that to be printed are 'nw' uint16_t words , in hex
// if Stream.rdbuf() == nullptr, PrintBuffer uses TRACE's TLOG
//-----------------------------------------------------------------------------
    void         PrintBuffer        (const void* ptr, int nw, int Offset = 0, std::ostream& Stream = std::cout);
    void         PrintRatesSingleRoc(std::vector<uint16_t>* Rates, std::vector<int>* ChMask = nullptr, std::ostream& Stream = std::cout);
    void         PrintRatesAllRocs  (std::vector<uint16_t>* Rates, std::vector<int>* ChMask, std::ostream& Stream = std::cout);
//-----------------------------------------------------------------------------
// Format = 0 : for each register, print a register and its value
// Format = 1 : add short description of each register
// if Link = -1, print a line per register for each ROC
//-----------------------------------------------------------------------------
    void         PrintRocRegister  (uint Reg, std::string& Desc, int Format = 1, int LinkMask = -1, std::ostream& Stream = std::cout);
    void         PrintRocRegister2 (uint Reg, std::string& Desc, int Format = 1, int LinkMask = -1, std::ostream& Stream = std::cout);
    virtual int  PrintRocStatus    (uint32_t Format = 1, int Link = -1, std::ostream& Stream = std::cout) override;
    void         PrintSpiAll       (trkdaq::TrkSpiData_t* Spi, std::ostream& Stream = std::cout);
    void         PrintSumThresholds(std::vector<float>* Thresholds, std::ostream& Stream);

    int          PrintThresholds  (int                        Link      ,
                                   std::vector<float>&        Thr       ,
                                   uint32_t      MaskC      = 0xFFFFFFFF,
                                   uint32_t      MaskD      = 0xFFFFFFFF,
                                   uint32_t      MaskE      = 0xFFFFFFFF,
                                   int           PrintLevel = 0x2       ,
                                   std::ostream& Stream     = std::cout );

    std::vector<DTCLib::roc_data_t> ReadDeviceID(DTCLib::DTC_Link_ID Link,
                                                 int                 PrintLevel = 0,
                                                 std::ostream&       Stream     = std::cout);

    void         ReadSubevents     (std::vector<std::unique_ptr<DTCLib::DTC_SubEvent>>& Vsev,
                                    ulong             FirstTS               ,
                                    int               PrintLevel = 0        ,
                                    std::ostream&     Stream     = std::cout,
                                    int               Validate   = 0        ,
                                    const std::string Fn         = ""       );  // if "", do not write output

                                        // returns the panel MNID
    int          ReadPanelID       (int Link, int PrintLevel = 0);
    int          ReadRocDDR        (int Link, int Block, std::ostream& Stream = std::cout);
    roc_serial_t ReadSerialNumber  (const DTCLib::DTC_Link_ID& Link);
    virtual int  ResetLink         (int Link) override;

    int          ResetDigis        (int Link);
    int          RocBlockRead      (int Link, int Reg, std::vector<uint16_t>& Res, int NExpected = -1);

    std::vector<DTCLib::roc_data_t> ReadROCBlockEnsured(const DTCLib::DTC_Link_ID& Link,
                                                        const DTCLib::roc_address_t& address);

                                        // -1 = 'all enabled links'

    int          SetRocDtcID       (int Link = -1);

                                        // delay in units of 5n

    int          SetRocDelay       (int Link, uint16_t Delay5ns, std::ostream& Stream = std::cout);

                                        // TStart and  TStop in units of 5ns, no printout if Stream = nullptr

    int          SetRocDigitizationWindow(int Link, uint16_t TStart, uint16_t TStop, int PrintLevel, std::ostream& Stream = std::cout);

    void         SetRocLaneMask    (int Mask ) { fRocLaneMask     = Mask ; }
    void         SetRocNHitsPerLane(int NHits) { fRocNHitsPerLane = NHits; }
//-----------------------------------------------------------------------------
// programming ROC over the fiber (I guess, this code is obsolete,
// in use is the standalone version
//-----------------------------------------------------------------------------
    int          SpiProgramRoc     (int Link, const RocFwData_t* FwData, const char* Version, int Doit=0, int PrintLevel=0, std::ostream& Stream = std::cout);
    int          SpiClearMemory    (int Link, const roc_fw_data_t* Dir, int PrintLevel=0, std::ostream& Stream = std::cout);
    int          SpiLoadImage      (int Link, const roc_fw_data_t* Dir, int TestMode, int NWrites=-1, int PrintLevel=0, std::ostream& Stream = std::cout);
    int          SpiIapIndex       (int Link, const roc_fw_data_t* Dir, int PrintLevel=0, std::ostream& Stream = std::cout);
    int          SpiIapAddress     (int Link, const roc_fw_data_t* Dir, int PrintLevel=0, std::ostream& Stream = std::cout);

    int          SpiReadFlash      (int Link, int Address, int NWords, std::vector<uint16_t>* Res,
                                    int PrintLevel=0, std::ostream& Stream = std::cout);

    int          SpiWriteDirectory (int Link, const roc_fw_data_t* Dir, int PrintLevel=0, std::ostream& Stream = std::cout);
    int          SpiWriteRecord    (int Link, int FirstAddr, int NWords, const uint16_t* Data,
                                    int PrintLevel=0, std::ostream& Stream = std::cout);
//-----------------------------------------------------------------------------
// return number of found errors
//-----------------------------------------------------------------------------
    int          ValidateDigiPatterns (ushort* Data, ulong EwTag, ulong* Offset, int PrintLevel, int* NErrRoc);
    int          ValidateFixedPatterns(ushort* Data, ulong EwTag, ulong* Offset, int PrintLevel, int* NErrRoc);
    int          ValidateVarPatterns  (ushort* Data, ulong EwTag, ulong* Offset, int PrintLevel, int* NErrRoc);

                                        // writes the mnID
    int          WritePanelID      (int Link, int PanelID, int PrintLevel = 0);
  };
};

#endif
