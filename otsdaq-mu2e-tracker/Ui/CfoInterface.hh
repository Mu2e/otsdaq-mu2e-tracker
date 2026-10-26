//-----------------------------------------------------------------------------
// interactive interface for ROOT-based GUI
// mixes high- and low-level commands
// assume everything is happening on one node
// there could be one or two DTCs and only one CFO
//-----------------------------------------------------------------------------
#ifndef __trkdaq_cfo_interface_hh__
#define __trkdaq_cfo_interface_hh__

#define __CLING__ 1

#include "dtcInterfaceLib/DTC.h"
#include "cfoInterfaceLib/CFO.h"

namespace trkdaq {

  class CfoInterface { 
  public:
    static CfoInterface* fgInstance;

    CFOLib::CFO*         fCfo;
    int                  fPcieAddr;
    uint                 fLinkMask;     // time chain mask
    int                  fEventMode;    // event mode = 0: null heartbeats
    int                  fJAMode;       // clock_source << 4 | reset
    int                  fEnabled;
//-----------------------------------------------------------------------------
// functions
//-----------------------------------------------------------------------------
  private:
    CfoInterface(int PcieAddr = -1, uint DtcMask = 0x1, DTC_SimMode SimMode=DTCLib::DTC_SimMode_Disabled,
                 bool SkipInit = true /*false*/);
  public:
    
    virtual ~CfoInterface();

    static CfoInterface* Instance(int PcieAddr = -1, uint DtcMask = 0);

    CFOLib::CFO* Cfo     () { return fCfo     ; }

//-----------------------------------------------------------------------------
// input file is a .txt file
// output file is a binary file with precompiled instructions
//-----------------------------------------------------------------------------
    void         CompileRunPlan(const std::string& InputFn, const std::string& OutputFn, int PrintLevel = 0, std::ostream& = std::cout);

    int          ConfigureJA(int ClockSource = -1, int Reset = -1, std::ostream& Stream = std::cout);

                                        // if 
    int          InitReadout(const std::string& RunPlan, int TimingChainMask = 0xffffffff, std::ostream& Stream = std::cout);

    int          Halt();
    void         LaunchRunPlan();

    int          LinkEnabled(int Lnk) { return (fLinkMask >> 4*Lnk) & 0xf ; }
      
    void         SetRunPlan   (const std::string& Fn);

    int          PcieAddr     () { return fPcieAddr; }
    void         PrintRegister(uint16_t Register, const char* Title = "", std::ostream& Stream = std::cout) ;
    void         PrintStatus  (std::ostream& Stream = std::cout);
    uint32_t     ReadRegister (uint16_t Register);
//-----------------------------------------------------------------------------
// TODO: need one more function which would 
// 1. generate off-spill run plan for N evens, 
// 2. compile and load it
// 'EWLength' in units of 25 ns (40 MHz clock ticks)
//-----------------------------------------------------------------------------
    void         SetEventMode      (int Mode) { fEventMode  = Mode; }
    void         SetJAMode         (int Mode) { fJAMode     = Mode; }
    
    void         SetOffspillRunPlan(int NEvents, int EWLength);
  };

};

#endif
