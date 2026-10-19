//-----------------------------------------------------------------------------
// interactive interface for ROOT-based GUI
// mixes high- and low-level commands
// assume everything is happening on one node
// there could be one or two DTCs and only one CFO
// most repeats Ryan's code, but w/o dependency on xdaq, GUI-based UI, and otsdaq everything
//-----------------------------------------------------------------------------
#ifndef __trkdaq_cfo_interface_cc__
#define __trkdaq_cfo_interface_cc__

#define __CLING__ 1

#include "TString.h"
#include "iostream"
#include "CfoInterface.hh"
#include "cfoInterfaceLib/CFO_Compiler.hh"

#include "TRACE/tracemf.h"
#define  TRACE_NAME "CfoInterface"

using namespace CFOLib;
using namespace DTCLib;

namespace trkdaq {

  CfoInterface* CfoInterface::fgInstance = nullptr;

//-----------------------------------------------------------------------------
  CfoInterface::CfoInterface(int PcieAddr, uint LinkMask, DTC_SimMode SimMode, bool SkipInit) {
    std::string expected_version("");              // dont check
    std::string sim_file        ("mu2esim.bin");
    std::string uid             ("");

    fPcieAddr = PcieAddr;
    fCfo      = new CFO(SimMode,PcieAddr,expected_version,SkipInit,uid);
//-----------------------------------------------------------------------------
// init DTC mask, less 16 DTCs per tine chain
//-----------------------------------------------------------------------------
    fLinkMask  = LinkMask;
    fJAMode   = 0x11;
    //    int link_mask = 0;
    for (int i=0; i<8; i++) {
      int ndtcs = (fLinkMask >> 4*i) & 0xf;
      if (ndtcs > 0) fCfo->EnableLink (CFO_Link_ID(i),DTC_LinkEnableMode(true ,true ));
      else           fCfo->DisableLink(CFO_Link_ID(i),DTC_LinkEnableMode(false,false));
    }
  }

//-----------------------------------------------------------------------------
  CfoInterface::~CfoInterface()  {
    if (fgInstance) {
      delete fgInstance->fCfo;
      fgInstance = nullptr;
    }
  }

//-----------------------------------------------------------------------------
  CfoInterface* CfoInterface::Instance(int PcieAddr, uint LinkMask) {
    int pcie_addr = PcieAddr;
    if (pcie_addr < 0) {
//-----------------------------------------------------------------------------
// PCIE address is not specified, check environment
//-----------------------------------------------------------------------------
      if (getenv("CFOLIB_CFO") != nullptr) pcie_addr = atoi(getenv("CFOLIB_CFO"));
      else {
        TLOG(TLVL_ERROR) << Form("PcieAddr < 0 and $CFOLIB_CFO is not defined. BAIL out\n");
        return nullptr;
      }
    }

    if (fgInstance == nullptr) fgInstance = new CfoInterface(pcie_addr,LinkMask,DTC_SimMode_NoCFO);

    if (fgInstance->PcieAddr() != pcie_addr) {
      TLOG(TLVL_ERROR) << Form("CfoInterface::Instance has been already initialized with PcieAddress = %i. BAIL out\n",
                               fgInstance->PcieAddr());
      return nullptr;
    }
    else return fgInstance;
  }


//-----------------------------------------------------------------------------
  void CfoInterface::CompileRunPlan(const std::string& InputFn, const std::string& OutputFn, int PrintLevel, std::ostream& Stream) {
    CFOLib::CFO_Compiler compiler;
    TLOG(TLVL_DEBUG+1) << std::format("-- START: InputFn:{} OutputFn:{}",InputFn,OutputFn);

    std::string res = compiler.processFile(InputFn,OutputFn);
    if (PrintLevel & 0x1) Stream << res;

    TLOG(TLVL_DEBUG+1) << std::format("-- END");
  }

//-----------------------------------------------------------------------------
// Source = 0:sync to internal clock ; 1:RTF
// on success, returns 0
// CFO JA CSR :0x9500
//-----------------------------------------------------------------------------
  int CfoInterface::ConfigureJA(std::ostream& Stream) {
    int rc(0);

    int clock_source = (fJAMode >> 4) & 0xf;
    int reset        = fJAMode & 0xf;

    TLOG(TLVL_DEBUG) << std::format("-- START: clock_source:{} reset:{}",clock_source,reset);


    fCfo->SetJitterAttenuatorSelect(clock_source,reset);     // 0:internal clock sync, 1:RTF
    usleep(100000);

    bool ok(false);
    int max_tries(3);
    for (int i=0; i<max_tries; i++) {
      ok = fCfo->ReadJitterAttenuatorLocked();              // in case of success, returns true
      usleep(100000);
      if (ok) {
        std::string msg = std::format("CFO JA configured with clock_source:{} reset:{}",clock_source,reset);
        Stream << msg << std::endl;
        TLOG(TLVL_DEBUG) << msg;
        break;
      }
    }

    if (not ok) {
      TLOG(TLVL_ERROR) << std::format("failed to configure the CFO JA after {} tries, BAIL OUT",max_tries);
      rc = -1;
    }

    TLOG(TLVL_DEBUG) << std::string("-- END  : rc:{}",rc);
    return rc;
  }

//-----------------------------------------------------------------------------
// really ?
//-----------------------------------------------------------------------------
  int CfoInterface::Halt() {
    int rc(0);
  // these functions don't use CFO_Link_ALL
    fCfo->DisableBeamOnMode (CFO_Link_ID::CFO_Link_ALL);
    fCfo->DisableBeamOffMode(CFO_Link_ID::CFO_Link_ALL);
    return rc;
  }

//-----------------------------------------------------------------------------
// launch is a separate step, could be repeated multiple times
// this is a one-time initialization
// CFO soft reset apparently restarts the execution , so keep the beam modes disabled
//-----------------------------------------------------------------------------
  int CfoInterface::InitReadout(const std::string& RunPlanFn, int TimingChainMask, std::ostream& Stream) {
    int rc(0);

    TLOG(TLVL_DEBUG+1) << std::format("-- START: runplan_fn:{} TimeChainMask:0x{:08x}",RunPlanFn,fLinkMask);

    fCfo->DisableLinks();               // Ryan says this is important
    fCfo->DisableEmbeddedClockMarker();
                                        // I guess, Halt disables transmission?
    Halt();
                                        // for convenience: to pass one parameter instead of two
    ConfigureJA(Stream);

    fCfo->SoftReset();
    SetRunPlan(RunPlanFn);
    usleep(10);
//-----------------------------------------------------------------------------
// in the end, re-initialize the time chains defined by the DTC mask
//-----------------------------------------------------------------------------
    if (TimingChainMask > 0) fLinkMask = TimingChainMask;
    for (int lnk=0; lnk<8; lnk++) {
      int ndtcs = (fLinkMask >> 4*lnk) & 0xf;
      if (ndtcs > 0) {
        fCfo->EnableLink (CFO_Link_ID(lnk),DTC_LinkEnableMode(true,true));
        TLOG(TLVL_INFO) << std::format("enabled DTC time chain {} with {} DTCs\n",lnk,ndtcs);
      }
    }
    TLOG(TLVL_DEBUG+1) << Form("-- END");
    return rc;
  }

//-----------------------------------------------------------------------------
// looks that it is only for the off-spill
// [at this point] disabling the BeamOn mode may be an overkill, but...
//-----------------------------------------------------------------------------
  void CfoInterface::LaunchRunPlan() {
    Halt();
//-----------------------------------------------------------------------------
// soft reset is commmon for the DTC and CFO - set bit31 of 0x9100 to reset,
// then set bit31 back to zero
//-----------------------------------------------------------------------------
    fCfo->SoftReset();
    usleep(20);

    fCfo->EnableBeamOffMode (CFO_Link_ID::CFO_Link_ALL); // what does that really do beyond writing to a register?
    // fCfo->EnableBeamOnMode (CFO_Link_ID::CFO_Link_ALL); // what does that really do beyond writing to a register?
  }

//-----------------------------------------------------------------------------
  uint32_t CfoInterface::ReadRegister(uint16_t Register) {

    uint32_t data;
    int      timeout(150);

    mu2edev* dev = fCfo->GetDevice();
    dev->read_register(Register,timeout,&data);

    return data;
  }


//-----------------------------------------------------------------------------
  void CfoInterface::PrintRegister(uint16_t Register, const char* Title, std::ostream& Stream) {
    Stream << Form("(0x%04x): 0x%08x : %s\n",Register,ReadRegister(Register),Title);
  }

//-----------------------------------------------------------------------------
  void CfoInterface::PrintStatus(std::ostream& Stream) {
    TLOG(TLVL_DEBUG+1) << std::format("-- START");
    std::cout << Form("-----------------------------------------------------------------\n");
    PrintRegister(0x9004,"CFO version                                ",Stream);
    PrintRegister(0x9030,"Kernel driver version                      ",Stream);
    PrintRegister(0x9100,"CFO control register                       ",Stream);
    PrintRegister(0x9104,"DMA Transfer Length                        ",Stream);
    PrintRegister(0x9108,"SERDES loopback enable                     ",Stream);
    PrintRegister(0x9114,"CFO link enable                            ",Stream);
    PrintRegister(0x9128,"CFO PLL locked                             ",Stream);
    PrintRegister(0x9140,"SERDES RX CDR lock                         ",Stream);
    PrintRegister(0x9144,"Beam On Timer Preset                       ",Stream);
    PrintRegister(0x9148,"Enable Beam On Mode                        ",Stream);
    PrintRegister(0x914c,"Enable Beam Off Mode                       ",Stream);
    PrintRegister(0x918c,"Number of DTCs                             ",Stream);

    PrintRegister(0x9200,"Receive  Byte   Count Link 0               ",Stream);
    PrintRegister(0x9220,"Receive  Packet Count Link 0               ",Stream);
    PrintRegister(0x9240,"Transmit Byte   Count Link 0               ",Stream);
    PrintRegister(0x9260,"Transmit Packet Count Link 0               ",Stream);

    PrintRegister(0x9500,"CFO Jitter Attenuator CSR                  ",Stream);  // CFO_Register_JitterAttenuatorCSR = 0x9500,
    TLOG(TLVL_DEBUG+1) << std::format("-- END");
  }

//-----------------------------------------------------------------------------
// TODO
//-----------------------------------------------------------------------------
  void CfoInterface::SetOffspillRunPlan(int NEvents, int EWLength) {
    TLOG(TLVL_ERROR) << std::format("NEvents:{} EWLength:{} ... not implemented yet",NEvents,EWLength);
  }

//-----------------------------------------------------------------------------
// Fn is a binary file
// first 8 bytes contain nbytes, but written into the CFO are 0x10000 bytes
// (sizeof(mu2e_databuff_t)
//-----------------------------------------------------------------------------
  void CfoInterface::SetRunPlan(const std::string& RunPlanFn) {

    TLOG(TLVL_DEBUG+1) << std::format("-- START, run plan fn:{}",RunPlanFn);
    std::ifstream file(RunPlanFn, std::ios::binary | std::ios::ate);

    if (! file) {
      TLOG(TLVL_ERROR) << std::format("failed to open RunPlanFn:{}, BAIL OUT",RunPlanFn);
      return;
    }
//-----------------------------------------------------------------------------
// read binary file and write
//-----------------------------------------------------------------------------
    std::string buf;
    auto input_size = file.tellg();
    buf.resize(input_size);
    file.seekg(0);

    file.read(buf.data(),input_size);
    file.close();
                                        // doesn't return anything
    fCfo->SetRunPlanData(buf,0);

    TLOG(TLVL_DEBUG+1) << Form("-- END\n");
  }

};

#endif
