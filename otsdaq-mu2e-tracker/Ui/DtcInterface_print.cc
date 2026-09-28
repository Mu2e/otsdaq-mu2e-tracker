///////////////////////////////////////////////////////////////////////////////
// separate print routines
///////////////////////////////////////////////////////////////////////////////
#include "iostream"
#include "vector"
#include "DtcInterface.hh"
#include "TString.h"

#include "TRACE/tracemf.h"
#define  TRACE_NAME "DtcInterface"

using namespace DTCLib;
using namespace std;

#include "otsdaq-mu2e-tracker/Ui/ControlRocTypes.hh"

namespace trkdaq {

//-----------------------------------------------------------------------------
// print value of the register Reg, for multiple ROCs
//-----------------------------------------------------------------------------
  void DtcInterface::PrintRocRegister(uint Reg, std::string& Desc, int Format, int LinkMask,std::ostream& Stream) {
    TLOG(TLVL_DEBUG+1) << std::format("-- START: Reg:{} Format:{} LinkMask:0x{:08x}",Reg,Format,LinkMask);

    std::string text;
    for (int i=0; i<6; i++) {
      int used = (LinkMask >> 4*i) & 0x1;
      if (used == 0)                                        continue;
      // need this if accidentally called directly
      if (not LinkLocked(i)) {
        TLOG(TLVL_ERROR) << std::format("link:{} enabled but not locked",i);
        continue;
      }

      DTC_Link_ID link = DTC_Link_ID(i);
      uint32_t dat;

      dat = fDtc->ReadROCRegister(link,Reg,100);
      if (Format & 0x2) text += Form("%11u",static_cast<unsigned int>(dat));
      else              text += Form("     0x%04x",dat);
    }
    std::string sreg = Form("reg(%2i)",Reg);

    if (Format & 0x1) text += Form(" %s",Desc.data());
    Stream << Form("%-18s %s\n",sreg.data(),text.data());

    TLOG(TLVL_DEBUG+1) << std::format("-- END");
  }

  //-----------------------------------------------------------------------------
  void DtcInterface::PrintRocRegister2(uint Reg, std::string& Desc, int Format, int LinkMask, std::ostream& Stream) {

    TLOG(TLVL_DEBUG+1) << std::format("-- START: Reg:{} Format:{} LinkMask:0x{:08x}",Reg,Format,LinkMask);

    std::string text;
    for (int i=0; i<6; i++) {
      int used = (LinkMask >> 4*i) & 0x1;
      if (used == 0)                                        continue;
      // need this if accidentally called directly
      if (not LinkLocked(i)) {
        TLOG(TLVL_ERROR) << std::format("link:{} enabled but not locked",i);
        continue;
      }

      DTC_Link_ID link = DTC_Link_ID(i);
      uint32_t iw1, iw2, iw;

      iw1 = fDtc->ReadROCRegister(link,Reg  ,100);
      iw2 = fDtc->ReadROCRegister(link,Reg+1,100);
      iw  = (iw2 << 16) | iw1;
      if (Format & 0x2) text += Form("%11u",static_cast<unsigned int>(iw));
      else              text += Form(" 0x%08x",iw);
    }

    if (Format & 0x1) text += Form(" %s",Desc.data());

    std::string sreg = Form("reg(%2i)<<16|reg(%2i)",Reg+1,Reg);

    Stream << Form("%-18s%s\n",sreg.data(),text.data());

    TLOG(TLVL_DEBUG+1) << std::format("-- END");
  }

//-----------------------------------------------------------------------------
// most of the time Link = -1 meaning 'all enabled links'
// otherwise it is the link to print
//-----------------------------------------------------------------------------
  int DtcInterface::PrintRocStatus(uint32_t Format, int Link, std::ostream& Stream) {
    int rc(0);

    TLOG(TLVL_DBG) << Form("Format=%i Link:%i \n",Format,Link);

    std::string desc;

    int lnk1(Link), lnk2(Link+1);
    if (Link == -1) {
      lnk1 = 0;
      lnk2 = 6;
    }

    uint reg;

    int link_mask(0);

    std::string text("        Register     ");
    for (int i=lnk1; i<lnk2; i++) {
      int enabled = LinkEnabled(i);
      if (enabled == 0)                                     continue;
      if (not LinkLocked(i)) {
        TLOG(TLVL_ERROR) << std::format("link:{} enabled but not locked",i);
        continue;
      }
      link_mask |= (1 << 4*i);
      text += Form("    ROC%i   ",i);
    }

    if (link_mask == 0) {
      std::string msg = std::format("dtc:{} link:{} : no locked links.",PcieAddr(),Link);
      Stream << " ERROR: " << msg << "\n";
      TLOG(TLVL_ERROR) << msg;
      return rc;
    }

    if (Format != 0) text += " Description";
    Stream << Form("%s\n",text.data());
    Stream << "------------------------------------------------------------------------\n";

    reg =  0; desc = "ALWAYS 0x1234";
    PrintRocRegister(reg,desc,Format,link_mask,Stream);

    reg = 18; desc = "ROC FIFO status";
    PrintRocRegister(reg,desc,Format,link_mask,Stream);

    reg =  8; desc = "ROC pattern mode ??";
    PrintRocRegister(reg,desc,Format,link_mask,Stream);

    // Keep the identity, FIFO status, and pattern-mode values above in hex.
    // The remaining values are counts, delays, positions, or tags.
    Format |= 0x2;

    reg = 60; desc = "ROC readout timeout delay ";
    PrintRocRegister(reg,desc,Format,link_mask,Stream);

    reg = 15; desc = "N simulated hits per lane";
    PrintRocRegister(reg,desc,Format,link_mask,Stream);

    reg = 16; desc = " ??";
    PrintRocRegister(reg,desc,Format,link_mask,Stream);

    reg =  7; desc = "Fiber loss/lock counter";
    PrintRocRegister(reg,desc,Format,link_mask,Stream);

    reg =  6; desc = "Bad Markers counter";
    PrintRocRegister(reg,desc,Format,link_mask,Stream);

    reg =  4; desc = "Loopback coarse delay";
    PrintRocRegister(reg,desc,Format,link_mask,Stream);

    reg = 23; desc = "SIZE_FIFO_FULL [28]+STORE_POS[25:24]+STORE_CNT[19:0]";
    PrintRocRegister2(reg,desc,Format,link_mask,Stream);

    reg = 25; desc = "SIZE_FIFO_EMPTY[28]+FETCH_POS[25:24]+FETCH_CNT[19:0]";
    PrintRocRegister2(reg,desc,Format,link_mask,Stream);

    reg = 11; desc = "Num EWM seen";
    PrintRocRegister2(reg,desc,Format,link_mask,Stream);

    reg = 64; desc = "Num windows seen";
    PrintRocRegister2(reg,desc,Format,link_mask,Stream);

    reg = 27; desc = "Num HB seen";
    PrintRocRegister2(reg,desc,Format,link_mask,Stream);

    reg = 29; desc = "Num null HB seen";
    PrintRocRegister2(reg,desc,Format,link_mask,Stream);

    reg = 31; desc = "Num HB on hold";
    PrintRocRegister2(reg,desc,Format,link_mask,Stream);

    reg = 33; desc = "Num PREFETCH seen";
    PrintRocRegister2(reg,desc,Format,link_mask,Stream);

    Stream << Form("\n");

    reg =  9; desc = "Num DATA REQ seen";
    PrintRocRegister2(reg,desc,Format,link_mask,Stream);

    reg = 35; desc = "Num DATA REQ written to DDR";
    PrintRocRegister2(reg,desc,Format,link_mask,Stream);

    reg = 13; desc = "Num skipped DATA REQ";
    PrintRocRegister(reg,desc,Format,link_mask,Stream);

    reg = 37; desc = "Num DATA REQ read from DDR";
    PrintRocRegister2(reg,desc,Format,link_mask,Stream);

    reg = 39; desc = "Num DATA REQ sent to DTC";
    PrintRocRegister2(reg,desc,Format,link_mask,Stream);

    reg = 41; desc = "Num DATA REQ with null data";
    PrintRocRegister2(reg,desc,Format,link_mask,Stream);

    Stream << Form("\n");

    reg = 43; desc = "Last spill tag";
    PrintRocRegister2(reg,desc,Format,link_mask,Stream);

    reg = 45; desc = "Last HB tag";
    PrintRocRegister2(reg,desc,Format,link_mask,Stream);

    reg = 48; desc = "Last PREFETCH tag";
    PrintRocRegister2(reg,desc,Format,link_mask,Stream);

    reg = 51; desc = "Last fetched tag";
    PrintRocRegister2(reg,desc,Format,link_mask,Stream);

    reg = 54; desc = "Last DATA REQ tag";
    PrintRocRegister2(reg,desc,Format,link_mask,Stream);

    reg = 57; desc = "OFFSET tag";
    PrintRocRegister2(reg,desc,Format,link_mask,Stream);

    Stream << std::endl;

    reg = 72; desc = "Num HB tag inconsistencies";
    PrintRocRegister(reg,desc,Format,link_mask,Stream);

    reg = 73; desc = "Num DATA REQ tag inconsistencies";
    PrintRocRegister(reg,desc,Format,link_mask,Stream);

    reg = 74; desc = "Num HB tag lost";
    PrintRocRegister(reg,desc,Format,link_mask,Stream);

    reg = 75; desc = "Num DATA REQ tag lost";
    PrintRocRegister(reg,desc,Format,link_mask,Stream);

    reg = 0x90; desc = "total N packets (DCS+data)";    // r_144
    PrintRocRegister(reg,desc,Format,link_mask,Stream); //
    reg = 0x91; desc = "N(DCS) packets sent to DTC";    // r_145
    PrintRocRegister(reg,desc,Format,link_mask,Stream); //
    reg = 0x92; desc = "Num of non-DCS packets";        // r_146
    PrintRocRegister(reg,desc,Format,link_mask,Stream); //
    reg = 0x93; desc = "Num of data header packets";    // r_147
    PrintRocRegister(reg,desc,Format,link_mask,Stream); //
    reg = 0x94; desc = "Num of data payload packets";   // r_148
    PrintRocRegister(reg,desc,Format,link_mask,Stream); //
    reg = 0x95; desc = "Num of empty data packets";     // r_149
    PrintRocRegister(reg,desc,Format,link_mask,Stream); //

    Stream << "------------------------------------------------------------------------\n";
    TLOG(TLVL_DEBUG+1) << std::format("-- END");
    return rc;
  }
//-----------------------------------------------------------------------------
// 'nw' : number of 16-bit words to print.
// if Stream == nullptr , use TLOG, otherwise - *Stream
//-----------------------------------------------------------------------------
  void DtcInterface::PrintBuffer(const void* ptr, int nw, int Offset, std::ostream& Stream) {

    ushort*      p16 = (ushort*) ptr;

    int          n(0);
    std::string  line;

    // if (Stream == nullptr) { TLOG(TLVL_DEBUG+1) << Form("-------- nw = %i\n",nw); }
    // else                   { (*Stream)        << Form("-------- nw = %i\n",nw); }

    for (int i=0; i<nw; i++) {
      if (n == 0) line = Form("0x%08x:",i*2+Offset);
      ushort  word = p16[i];
      line += Form(" 0x%04x",word);

      n   += 1;
      if (n == 8) {
        if (Stream.rdbuf() == nullptr) TLOG(TLVL_INFO) << line << std::endl;
        else {
          Stream             << line << std::endl;
          TLOG(TLVL_DEBUG+1) << line << std::endl;
        }
        n = 0;
      }
    }

    if (n != 0) {
      if (Stream.rdbuf() == nullptr) TLOG(TLVL_INFO) << line << std::endl;
      else {
        Stream             << line << std::endl;
        TLOG(TLVL_DEBUG+1) << line << std::endl;
      }
    }
  }

//-----------------------------------------------------------------------------
  void DtcInterface::PrintRatesSingleRoc(std::vector<uint16_t>* Rates, std::vector<int>* ChMask, std::ostream& Stream) {

//-----------------------------------------------------------------------------
// formatted printout
// should be 96*3*2+2*2 = 580 16-bit words
// 3 words per channel (straw)
//-----------------------------------------------------------------------------
    int nw = Rates->size();
    if (nw != 580) {
      TLOG(TLVL_ERROR) << "nw:" << nw << " != 580. BAIL OUT";
      return;
    }
//-----------------------------------------------------------------------------
// finally, the last two words - total counts
//-----------------------------------------------------------------------------
    float total[2];          // [0]:CAL  [1]:HV , as in lanes, an inversion takes place
    float clock_tick(5.e-9); // 5 ns <-> 200 MHz clock

    int loc = 576;   // = 96*6

    total[1]  = float((*Rates)[loc  ])+(int((*Rates)[loc+1]) << 16); // hv - check the order with Vadim
    total[0]  = float((*Rates)[loc+2])+(int((*Rates)[loc+3]) << 16); // cal

    Stream << " channel  mask Total(HV) Total(CAL) Total(HV.and.CAL)  Rate(HV)   Rate(CAL)   Rate(HV.and.CAL)\n";
    Stream << "-----------------------------------------------------------------------------------------\n";

    for (int ich=0; ich<96; ich++) {
      loc               = 6*ich;
      int   counts_hv   = int((*Rates)[loc  ])+(int((*Rates)[loc+1]) << 16);
      int   counts_cal  = int((*Rates)[loc+2])+(int((*Rates)[loc+3]) << 16);
      int   counts_coin = int((*Rates)[loc+4])+(int((*Rates)[loc+5]) << 16);
      int   fpga        = fgFpga[ich];
      float rate_hv     = counts_hv /total[fpga]/clock_tick/1000.;
      float rate_cal    = counts_cal/total[fpga]/clock_tick/1000.;
      float rate_coin   = counts_coin/(total[0]+total[1])*2/clock_tick/1000.;

      int ch_mask = 1;
      if ((ChMask != nullptr) and (ChMask->size() == 96) and (ChMask->at(ich) == 0)) {
        ch_mask = 0;
      }

      Stream << std::format("- {:5d} {:3d} {:10d} {:10d} {:10d}         {:10.3f} {:10.3f} {:10.3f}\n",
                            ich,ch_mask,counts_hv,counts_cal,counts_coin,
                            rate_hv,rate_cal,rate_coin);
    }

    Stream << std::format(" total_hv: {:10.0f} total_cal: {:10.0f}\n",total[1],total[0]);
  }

//-----------------------------------------------------------------------------
  void DtcInterface::PrintRatesAllRocs(std::vector<uint16_t>* Rates, std::vector<int>* ChMask, std::ostream& Stream) {
//-----------------------------------------------------------------------------
// do the printing
// bit 2: formattted printout, parallel
//-----------------------------------------------------------------------------
    float clock_tick(5.e-9); // 5 ns <-> 200 MHz clock

    Stream << "ch|   link 0     |   link 1     |   link 2     |   link 3     |   link 4     |   link 5     |\n";
    Stream << "  | counts rate  | counts rate  | counts rate  | counts rate  | counts rate  | counts rate  |\n";
    Stream << "--------------------------------------------------------------------------------------------\n";

    float total[6][2];          // [0]:CAL  [1]:HV , as in lanes, an inversion takes place

    int loc = 576;
    for (int lnk=0; lnk<6; lnk++) {
      std::vector<uint16_t>* dat = &Rates[lnk];
      int nw = dat->size();
      // Stream << std::dec << "lnk:" << lnk << " nw:" << nw <<  " ChMask[lnk].size():"<< ChMask[lnk].size() << std::endl;
      if ((LinkEnabled(lnk) == 0) or (nw != 580)) {
        total[lnk][0] = -1;
        total[lnk][1] = -1;
      }
      else {
        total[lnk][1]  = float((*dat)[loc  ])+(int((*dat)[loc+1]) << 16); // hv - check the order with Vadim
        total[lnk][0]  = float((*dat)[loc+2])+(int((*dat)[loc+3]) << 16); // cal
      }
    }

    for (int ich=0; ich<96; ich++) {
      int loc               = 6*ich;
      Stream << std::format("{:2d}|",ich);

      for (int lnk=0; lnk<6; lnk++) {
        std::vector<uint16_t>* dat = &Rates[lnk];
        int nw = dat->size();

        char c = '|';
        // int ch_mask = 1;
        if ((ChMask[lnk].size() == 96) and (ChMask[lnk].at(ich) == 0)) {
          // ch_mask = 0;
           c = '*';
        }
        if ((LinkEnabled(lnk) == 0) or (nw != 580)) {
          Stream << "              " << c;
        }
        else {
          int   counts_coin = int((*dat)[loc+4])+(int((*dat)[loc+5]) << 16);
          float rate_coin   = counts_coin/(total[lnk][0]+total[lnk][1])*2/clock_tick/1000.;
          Stream << std::format("     {:8.3f} {:c}",rate_coin,c);
        }
      }

      Stream << std::endl;
    }
  }

//-----------------------------------------------------------------------------
// assume always data from 6 ROCs, some may be disabled
//-----------------------------------------------------------------------------
  void DtcInterface::PrintSpiAll(trkdaq::TrkSpiData_t* Spi, std::ostream& Stream) {

    Stream << " SPI Parameter ";
    for (int lnk=0; lnk<6; lnk++) {
      if (not LinkEnabled(lnk)) continue;
      Stream << std::format("    link {:}",lnk);
    }
    Stream << std::endl
           << "--------------------------------------------------------------------------"
           << std::endl;

    for (int i=0; i<TrkSpiDataNWords; ++i) {
      Stream << std::format("{:15s}",fgSpiVarName[i]);
      for (int lnk=0; lnk<6; lnk++) {
        if (not LinkEnabled(lnk)) continue;
        Stream << std::format("{:10.3f}",Spi[lnk].Data(i));
      }
      Stream << std::endl;
    }
  }


//-----------------------------------------------------------------------------
  void DtcInterface::PrintSumThresholds(std::vector<float>* Thresholds, std::ostream& Stream) {
//-----------------------------------------------------------------------------
// do the printing
// bit 2: formattted printout, parallel
//-----------------------------------------------------------------------------
//    float clock_tick(5.e-9); // 5 ns <-> 200 MHz clock

    Stream << "ch|   link 0     |   link 1     |   link 2     |   link 3     |   link 4     |   link 5     |\n";
    Stream << "  |              |              |              |              |              |              |\n";
    Stream << "--------------------------------------------------------------------------------------------\n";

    // float total[6];          // [0]:CAL  [1]:HV , as in lanes, an inversion takes place

      // if the size in zero, don't print the link
    for (int ich=0; ich<96; ich++) {
      Stream << std::format("{:2d}|",ich);

      for (int lnk=0; lnk<6; lnk++) {
        // int loc               = 6*ich;

        std::vector<float>* dat = &Thresholds[lnk];
        int nw = dat->size();

        char c = '|';
        if (nw == 0) {
          Stream << "             " << c;
        }
        else {
          float sum_thr = dat->at(3*ich+2);
          Stream << std::format("     {:8.3f} {:c}",sum_thr,c);
        }
      }

      Stream << std::endl;
    }
  }

//-----------------------------------------------------------------------------
// Link: link number
// expect that in most cases read all channels : all masks are set to 0xFFFFFFFF
//-----------------------------------------------------------------------------
  int DtcInterface::PrintThresholds(int                 Link,
                                    std::vector<float>& Thr ,
                                    uint32_t            MaskC,
                                    uint32_t            MaskD,
                                    uint32_t            MaskE,
                                    int                 PrintLevel,
                                    std::ostream&       Stream) {
//-----------------------------------------------------------------------------
//  print, if requested
//-----------------------------------------------------------------------------
    if (PrintLevel & 0x2) {
      int mask[3];
      mask[0] = MaskC;
      mask[1] = MaskD;
      mask[2] = MaskE;
//-----------------------------------------------------------------------------
// to keep the output compact, print thresholds only for the channels defined by the mask
//-----------------------------------------------------------------------------
      printf(" chID     thr(CAL)    thr(HV)    sum  \n");
      printf("--------------------------------------\n");
      for (int i=0; i<96; i++) {
        int iw = i/32;
        int ib = i -iw*32;

        if (((mask[iw] >> ib) & 0x1) == 1) {
          float hw  = Thr[3*i  ];
          float cal = Thr[3*i+1];
          float tot = Thr[3*i+2];
          Stream << std::format(" {:4d} {:10.3f} {:10.3f} {:10.3f}",i,hw,cal,tot) << std::endl;
        }
      }
    }
    return 0;
  }

};
