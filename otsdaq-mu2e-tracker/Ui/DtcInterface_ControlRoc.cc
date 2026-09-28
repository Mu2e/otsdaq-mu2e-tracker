///////////////////////////////////////////////////////////////////////////////
// dafault error handling policy: exit on the first encountered error
///////////////////////////////////////////////////////////////////////////////

#include <vector>
#include "otsdaq-mu2e-tracker/Ui/DtcInterface.hh"

#include "TRACE/tracemf.h"
#define  TRACE_NAME "DtcInterface_ControlRoc"

#include "TString.h"     // includes ROOT's Form

using namespace DTCLib;

namespace  trkdaq {
//-----------------------------------------------------------------------------
// a boilerplate for a generic control_ROC.py CLI command - do we need it at  all ?
//-----------------------------------------------------------------------------
  int DtcInterface::ControlRoc(const char* Command, void* Par) {
    return 0;
  }

//-----------------------------------------------------------------------------
// digi_rw over the fiber: reg 263
// Link = -1 : 'all enabled links
//-----------------------------------------------------------------------------
  int DtcInterface::ControlRoc_DigiRW(ControlRoc_DigiRW_Input_t*  Input     ,
                                      ControlRoc_DigiRW_Output_t* Output    ,
                                      int                         Link      ,
                                      int                         PrintLevel,
                                      std::ostream&               Stream    ) {
//-----------------------------------------------------------------------------
    TLOG(TLVL_DEBUG+1) << std::format("-- START:");

    if ((Input == nullptr) or (Output == nullptr)) {
      TLOG(TLVL_ERROR) << "ControlRoc_DigiRW requires non-null input and output";
      Stream << "ERROR: ControlRoc_DigiRW requires non-null input and output\n";
      return -1;
    }

    *Output = {};
    int rc = 0;

    std::vector<uint16_t> vec;

    vec.push_back(Input->rw     );
    vec.push_back(Input->hvcal  );
    vec.push_back(Input->address);
    vec.push_back(Input->data[0]);
    vec.push_back(Input->data[1]);

    if (PrintLevel > 0) {
      Stream << std::format("input: rw:{} hvcal:{} address:0x{:04x} data[0]:0x{:04x} data[1]:0x{:04x}\n",
                            Input->rw, Input->hvcal, Input->address, Input->data[0], Input->data[1]);
    }

    bool increment_address(false);
//-----------------------------------------------------------------------------
// if Link != -1, use it, but don't redefine fLinkMask - that would be wa-a-ay too smart !
//-----------------------------------------------------------------------------
    int link1(Link), link2(Link+1);
    if (Link == -1) {
      link1 = 0;
      link2 = 6;
    }
//-----------------------------------------------------------------------------
// loop over the links and execute
//-----------------------------------------------------------------------------
    for (int i=link1; i<link2; i++) {
      if (not LinkEnabled(i))                                 continue;
      auto roc  = DTC_Link_ID(i);
      try {
        fDtc->WriteROCBlock   (roc,REG_DIGIRW,vec,false,increment_address,100);
        std::this_thread::sleep_for(std::chrono::microseconds(fSleepTimeROCWrite));

        uint16_t u;
        while ((u = fDtc->ReadROCRegister(roc,128,1000)) != 0x8000) {};
        if (PrintLevel & 0x8) Stream << Form("reg:%03i val:0x%04x\n",128,u);
//-----------------------------------------------------------------------------
// register 129: number of words to read, currently-  (+ 4) (ask Monica)
//-----------------------------------------------------------------------------
        int nw = fDtc->ReadROCRegister(roc,129,100);
        if (PrintLevel & 0x8) Stream << Form("reg:%03i val:0x%04x\n",129,nw);

        nw = nw-4;
        std::vector<uint16_t> v2;
        fDtc->ReadROCBlock(v2,roc,REG_DIGIRW,nw,false,100);

        constexpr int expectedWords =
            sizeof(ControlRoc_DigiRW_Output_t)/sizeof(uint16_t);
        if ((nw != expectedWords) or
            (v2.size() != static_cast<size_t>(expectedWords))) {
          TLOG(TLVL_ERROR) << std::format(
              "DIGIRW link:{} returned {} words; expected {}",i,v2.size(),expectedWords);
          Stream << std::format(
              "ERROR: DIGIRW link {} returned {} words; expected {}\n",
              i,v2.size(),expectedWords);
          rc = -2;
          continue;
        }

        Output->rw       = v2[0];
        Output->hvcal    = v2[1];
        Output->address  = v2[2];
        Output->data[0]  = v2[3];
        Output->data[1]  = v2[4];
        Output->adc_num  = v2[5];
        Output->adc_mask = v2[6];

        if (PrintLevel > 0) {
          if (PrintLevel & 0x8) Stream << " ---------------- link:" << i << ":";
          if (PrintLevel & 0x1) PrintBuffer(v2.data(),nw,0x0,Stream);
          if (PrintLevel & 0x2) {
            Stream << std::endl;

            trkdaq::ControlRoc_DigiRW_Output_t* o = (trkdaq::ControlRoc_DigiRW_Output_t*) v2.data();
            Stream << std::format("output: link:{} output: rw:{} hvcal:{} address:0x{:04x}",i,o->rw,o->hvcal,o->address)
                   << std::format(" data[0]:0x{:04x} data[1]:0x{:04x} adc_num:0x{:04x} adc_mask:0x{:04x}\n",
                                  o->data[0],o->data[1],o->adc_num,o->adc_mask);
          }
        }
      }
      catch (...) {
        TLOG(TLVL_ERROR) << "ERROR reading link:" << i;
        Stream << std::format("ERROR reading link:{}",i) << std::endl;
        rc = -3;
      }
    }
    Stream << std::endl;
//-----------------------------------------------------------------------------
// no need to reset the ROCs
//-----------------------------------------------------------------------------
    TLOG(TLVL_DEBUG+1) << std::format("-- END");
    return rc;
  }

//-----------------------------------------------------------------------------
// convenience
//-----------------------------------------------------------------------------
  int DtcInterface::DigiRead(int Addr, int HvCal, uint32_t& Res, int Link, int PrintLevel, std::ostream& Stream) {
    int rc(0);
    TLOG(TLVL_DEBUG+1) << std::format("-- START: Addr:0x{:04x} HvCal:{} Link:{}",Addr,HvCal,Link);

    ControlRoc_DigiRW_Input_t  ip;
    ControlRoc_DigiRW_Output_t op;

    ip.address = Addr;
    ip.rw      = 0;                    // -a
    ip.hvcal   = HvCal;                // -t
    ip.data[0] = 0;
    ip.data[1] = 0;

    rc = ControlRoc_DigiRW(&ip,&op,Link,PrintLevel,Stream);

                                        // if no error, the result is 16 bit, so -1 never happens
    if (rc == 0) Res = op.data[0];
    else         Res = 0xffffffff;

    TLOG(TLVL_DEBUG+1) << std::format("-- END rc:{}",rc);
    return rc;
  }

//-----------------------------------------------------------------------------
// convenience
//-----------------------------------------------------------------------------
  int DtcInterface::DigiWrite(int Addr, int HvCal, uint16_t Dat, int Link, int PrintLevel, std::ostream& Stream) {
    int rc(0);
    TLOG(TLVL_DEBUG+1) << std::format("-- START: Addr:0x{:04x} HvCal:{} Data:0x{:08x} Link:{}",Addr,HvCal,Dat,Link);

    ControlRoc_DigiRW_Input_t  ip;
    ControlRoc_DigiRW_Output_t op;

    ip.address = Addr;
    ip.rw      = 1;                    // -a
    ip.hvcal   = HvCal;                 // -t
    ip.data[0] = Dat;
    ip.data[1] = 0;                     // DIGIs get 16 bits only

    rc = ControlRoc_DigiRW(&ip,&op,Link,PrintLevel,Stream);

    TLOG(TLVL_DEBUG+1) << std::format("-- END rc:{}",rc);
    return rc;
  }


//-----------------------------------------------------------------------------
// from https://github.com/bonventre/trackerScripts/blob/master/control_ROC.py
// chan_mask = int(get_key_value(keys,"C"),16)
// channel   = int(get_key_value(keys,"c",-1)) - not used any more !!
// fvalue    = float(get_key_value(keys,"v",-1)); dvalue = fvalue/3.3*1023  // obsolete
// dvalue    = int(get_key_value(keys,"d"))
// FirstChanelMask : a bit mask, defines the first pulsed channel, the rest pulsed: first+8*i
//                   0x10 : first pulsed channel is channel 4, max value : 0x80
//-----------------------------------------------------------------------------
  int DtcInterface::ControlRoc_DumpSettings(int Link, int Channel, int PrintLevel, std::ostream& Stream) {
    int rc (0);
    TLOG(TLVL_DEBUG+1) << std::format("-- START: Link:{} Channel:{}",Link,Channel);
    Stream           << "Link:" << Link << " Channel:" << Channel << std::endl;
//-----------------------------------------------------------------------------
// Link = -1 means all links
//-----------------------------------------------------------------------------
    int l1(Link), l2(Link+1);
    if (Link == -1) {
      l1 = 0;
      l2 = 6;
    }

    for (int i=l1; i<l2; i++) {
      if (not LinkEnabled(i))   continue;
      TLOG(TLVL_DEBUG+1) << "      -- i:" << i << std::endl;

      std::vector<uint16_t> settings;
      rc = ControlRoc_ReadSettings(i,Channel,settings,PrintLevel,Stream);
      if (rc != 0) {
                                        // return immediately
        break;
      }
//-----------------------------------------------------------------------------
// print
//-----------------------------------------------------------------------------
      int first_channel(Channel);
      if ((Channel < 0) or (Channel >= 96)) first_channel = 0;

      Stream << " ich gain_cal gain_hv thr_cal thr_hv" << std::endl;
      Stream << "------------------------------------" << std::endl;

      int nch = settings.size()/4;
      for (int ich=0; ich < nch; ++ich) {
        int loc      = first_channel+4*ich;

        int gain_cal = settings[loc  ];
        int gain_hv  = settings[loc+1];
        int thr_cal  = settings[loc+2];
        int thr_hv   = settings[loc+3];
        Stream << std::format("{:3d}  {:5d}   {:5d}   {:5d}   {:5d}",ich,gain_cal,gain_hv,thr_cal,thr_hv) << std::endl;
      }
    }

    TLOG(TLVL_DEBUG+1) << std::format("-- END: rc:{}",rc);
    return rc;
  }

//-----------------------------------------------------------------------------
// Link=-1: execute the comamnd for all enabled links
//-----------------------------------------------------------------------------
  int DtcInterface::ControlRoc_Read(ControlRoc_Read_Input_t0* Par       ,
                                    int                       Link      ,
                                    int                       PrintLevel,
                                    std::ostream&             Stream    ) {
//-----------------------------------------------------------------------------
// write parameters into reg 266 (via block write), sleep for some time,
// then wait till reg 128 returns 0x8000
/*
            adc_mode        = dtcbuffer[0];                         // -a
            tdc_mode        = dtcbuffer[1];                         // -t
            num_lookback    = dtcbuffer[2];                         // -l
            num_triggers    = (dtcbuffer[4] << 16) + dtcbuffer[3];  // -T
            channel_mask[0] = (dtcbuffer[6] << 16) + dtcbuffer[5];  // -C
            channel_mask[1] = (dtcbuffer[8] << 16) + dtcbuffer[7];  // -D
            channel_mask[2] = (dtcbuffer[10] << 16) + dtcbuffer[9]; // -E
            num_samples     = dtcbuffer[11];                        // -s
            enable_pulser   = (uint8_t) dtcbuffer[12];              // -p
            max_total_delay = dtcbuffer[13];                        // -d (def 1)
            marker_clock    = (uint8_t) dtcbuffer[14];              // -m
*/
//-----------------------------------------------------------------------------
    std::vector<uint16_t> vec;

    TLOG(TLVL_DEBUG+1) << "Link: 0x" << std::hex << Link << std::dec << " PrintLevel:" << PrintLevel;

    int link1(Link), link2(Link+1);
    if (Link == -1) {
      link1 = 0;
      link2 = 6;
    }
    else if ((Link > 5) or (Link < -1)){
      TLOG(TLVL_ERROR) << "wrong Link: 0x" << std::hex << Link ;
      return -1;
    }
                                        // rely on supplied constructor
    ControlRoc_Read_Input_t0 default_par, *par;

    if (Par != nullptr) par = Par;
    else                par = &default_par;

    vec.push_back(par->adc_mode);
    vec.push_back(par->tdc_mode);
    vec.push_back(par->num_lookback);

    if (par->num_samples > 63) {
      TLOG(TLVL_WARNING) << "num_samples:" << par->num_samples << " gt 63, truncate to 63" ;
      par->num_samples = 63;
    }

    vec.push_back(par->num_samples);

    uint16_t w1 = par->num_triggers[0];
    uint16_t w2 = par->num_triggers[1];

    vec.push_back(w1);
    vec.push_back(w2);

    for (int i=0; i<6; i++) vec.push_back(par->ch_mask[i]);

    vec.push_back(par->enable_pulser);
    vec.push_back(par->marker_clock );
    vec.push_back(par->mode  );
    vec.push_back(par->clock );

    bool increment_address(false);

    for (int i=link1; i<link2; ++i) {
      if (not LinkEnabled(i))                            continue;
      auto roc  = DTC_Link_ID(i);
      fDtc->WriteROCBlock(roc,REG_READ,vec,false,increment_address,100);
      std::this_thread::sleep_for(std::chrono::microseconds(fSleepTimeROCWrite));

                                        // 0x86 = 0x82 + 4
      uint16_t u;
      while ((u = fDtc->ReadROCRegister(roc,128,1000)) != 0x8000) {};
      TLOG(TLVL_DEBUG+1) << Form("reg:%03i val:0x%04x\n",128,u);
//-----------------------------------------------------------------------------
// register 129: number of words to read, currently-  (+ 4) (ask Monica)
//-----------------------------------------------------------------------------
      int nw = fDtc->ReadROCRegister(roc,129,100);
      TLOG(TLVL_DEBUG+1) << Form("reg:%03i val:0x%04x\n",129,nw);

      nw = nw-4;
      std::vector<uint16_t> vout;
      fDtc->ReadROCBlock(vout,roc,REG_READ,nw,false,100);
//-----------------------------------------------------------------------------
// bits 0 and 1 - this function
//-----------------------------------------------------------------------------
      if (PrintLevel & 0x3) {

        Stream << "--------------- link :" << i << std::endl;

        if (PrintLevel & 0x1) PrintBuffer(vout.data(),nw,0x0,Stream);

        if (PrintLevel & 0x2) {
          trkdaq::ControlRoc_Read_Output_t0* o = (trkdaq::ControlRoc_Read_Output_t0*) vout.data();
          Stream << Form("adc_mode      : %i\n",o->adc_mode);
          Stream << Form("tdc_mode      : %i\n",o->tdc_mode);
          Stream << Form("num_lookback  : %i\n",o->num_lookback);
          Stream << Form("num_triggers  : %5i %5i\n",o->num_triggers[0],o->num_triggers[1]);
          Stream << Form("ch_mask       : 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x\n",
                         o->ch_mask[0],o->ch_mask[1],o->ch_mask[2],
                         o->ch_mask[3],o->ch_mask[4],o->ch_mask[5]);
          Stream << Form("num_samples   : %i\n"     ,o->num_samples);
          Stream << Form("enable_pulser : %i\n"    ,o->enable_pulser);
          Stream << Form("marker_clock  : %i\n"    ,o->marker_clock);
          Stream << Form("mode          : %i\n"    ,o->mode);
          Stream << Form("clock         : %i\n"    ,o->clock);
          Stream << Form("digi_read_0xb : 0x%04x\n",o->digi_read_0xb);
          Stream << Form("digi_read_0xe : 0x%04x\n",o->digi_read_0xe);
          Stream << Form("digi_read_0xd : 0x%04x\n",o->digi_read_0xd);
          Stream << Form("digi_read_0xc : 0x%04x\n",o->digi_read_0xc);
        }
      }
    }
//-----------------------------------------------------------------------------
//  is it really needed to reser the ROC in the end ? - no
//-----------------------------------------------------------------------------
    // ResetLinks();
    return 0;
  }

//-----------------------------------------------------------------------------
// from https://github.com/bonventre/trackerScripts/blob/master/control_ROC.py
// chan_mask = int(get_key_value(keys,"C"),16)
// oddoreven = int(get_key_value(keys,"P"),16)
// channel   = int(get_key_value(keys,"c",-1)) - not used any more !!
// delay     = int(get_key_value(keys,"d",1000))
// dutycycle = int(get_key_value(keys,"y",10))
// FirstChanelMask : a bit mask, defines the first pulsed channel, the rest pulsed: first+8*i
//                   0x10 : first pulsed channel is channel 4, max value : 0x80
//-----------------------------------------------------------------------------
  int DtcInterface::ControlRoc_PulserOn(int Link, int FirstChannelMask, int DutyCycle, int PulserDelay,
                                        int PrintLevel, std::ostream& Stream) {
    int rc (0); //, reg(268);
    TLOG(TLVL_DEBUG+1) << " -- START: Link:" << Link << " FirstChannelMask:0x" << std::hex << FirstChannelMask
                     << std::dec << " DutyCycle:" << DutyCycle << " PulserDelay:" << PulserDelay;

    Stream << "Link:" << Link << " FirstChannelMask:0x" << std::hex << FirstChannelMask
           << std::dec << " DutyCycle:" << DutyCycle << " PulserDelay:" << PulserDelay << std::endl;
//-----------------------------------------------------------------------------
// write parameters into reg 268 (block write) , sleep for some time,
// then wait till reg 128 returns 0x8000
//-----------------------------------------------------------------------------
    std::vector<uint16_t> vec;
    vec.push_back(uint16_t(FirstChannelMask));
    vec.push_back(uint16_t(DutyCycle));
    vec.push_back(uint16_t((PulserDelay >>  0) & 0xffff));
    vec.push_back(uint16_t((PulserDelay >> 16) & 0xffff));

    bool increment_address(false);
//-----------------------------------------------------------------------------
// Link = -1 means all links
//-----------------------------------------------------------------------------
    int l1(Link), l2(Link+1);
    if (Link == -1) {
      l1 = 0;
      l2 = 6;
    }

    for (int i=l1; i<l2; i++) {
      if (not LinkEnabled(i))   continue;
      TLOG(TLVL_DEBUG+1) << "      -- i:" << i << std::endl;
//-----------------------------------------------------------------------------
// convert into enum
//-----------------------------------------------------------------------------
      auto roc  = DTC_Link_ID(i);
      try {
        fDtc->WriteROCBlock   (roc,REG_PULSERON,vec,false,increment_address,100);
        std::this_thread::sleep_for(std::chrono::microseconds(fSleepTimeROCWrite));
      }
      catch(...) {
        TLOG(TLVL_ERROR) << "failed WriteROCBlock, continue" << std::endl;
        continue;
      }

                                        // 0x86 = 0x82 + 4
      uint16_t u;
      while ((u = fDtc->ReadROCRegister(roc,128,100)) != 0x8000) {};
      TLOG(TLVL_DEBUG+1) << Form("reg:%03i val:0x%04x\n",128,u);
//-----------------------------------------------------------------------------
// register 129: number of words to read, currently-  (+ 4) (ask Monica)
//-----------------------------------------------------------------------------
      int nw = fDtc->ReadROCRegister(roc,129,100);
      TLOG(TLVL_DEBUG+1) << Form("reg:%03i val:0x%04x\n",129,nw);

      nw = nw-4;
      if (nw == 4) {
        std::vector<uint16_t> v2;
        fDtc->ReadROCBlock(v2,roc,REG_PULSERON,nw,false,100);

        if (PrintLevel & 0x1) {
          PrintBuffer(v2.data(),nw,0x0,Stream);
        }
      }
      else {
        TLOG(TLVL_ERROR) << Form("wrong number of words : nw = %i, expected = 4",nw);
        rc = -1;
        break;
      }
    }
    TLOG(TLVL_DEBUG+1) << " -- END rc:" << rc;
    return rc;
  }

//-----------------------------------------------------------------------------
// Link=-1: all enabled links
//-----------------------------------------------------------------------------
  int DtcInterface::ControlRoc_PulserOff(int Link, int PrintLevel, std::ostream& Stream) {
    int rc(0);
    TLOG(TLVL_DEBUG+1) << "-- START:" << __func__;
    int l1(Link), l2(Link+1);
    if (Link == -1) {
      l1 = 0;
      l2 = 6;
    }

    for (int i=l1; i<l2; i++) {
      if (not LinkEnabled(i)) continue;
//-----------------------------------------------------------------------------
// PULSER_OFF: reg 269
//-----------------------------------------------------------------------------
      std::vector<uint16_t> res;
      int rc = RocBlockRead(i,REG_PULSEROFF,res);

      if (rc == 0) {
        int nw = res.size();
        TLOG(TLVL_DEBUG+1) << "link:" << i << " nw:" << nw;

        if (PrintLevel & 0x1) {
          PrintBuffer(res.data(),nw,0x0,Stream);
        }
      }
      else {
        Stream << "ERROR:" << rc << " blockread link:" << i << " register:" << REG_PULSEROFF << std::endl;
      }
    }
    TLOG(TLVL_DEBUG+1) << " -- END " << __func__ << " rc:" << rc;
    return rc;
  }


//-----------------------------------------------------------------------------
  int DtcInterface::ControlRoc_ReadSettings(int Link, int Channel, std::vector<uint16_t>& Settings,
                                            int PrintLevel, std::ostream& Stream) {
    int rc (0);
    TLOG(TLVL_DEBUG+1) << "-- START: Link:" << Link << " Channel:" << Channel;

    Stream << "Link:" << Link << " Channel:" << Channel << std::endl;
//-----------------------------------------------------------------------------
// write parameters into reg 268 (block write) , sleep for some time,
// then wait till reg 128 returns 0x8000
//-----------------------------------------------------------------------------
    std::vector<uint16_t> vec;
    vec.push_back(uint16_t(Channel));

    bool increment_address(false);
//-----------------------------------------------------------------------------
// Link = -1 : doesn't make sense - creates too many degrees of freedom
//-----------------------------------------------------------------------------
    if ((Link < 0) || (Link > 5)) {
      TLOG(TLVL_ERROR) << "wrong link:" << Link;
      return -1;
    }

    if (LinkEnabled(Link)) {
      TLOG(TLVL_DEBUG+1) << "      -- Link:" << Link << std::endl;
//-----------------------------------------------------------------------------
// convert into enum
//-----------------------------------------------------------------------------
      auto link_id  = DTC_Link_ID(Link);
      try {
        fDtc->WriteROCBlock   (link_id,REG_DUMPSETTINGS,vec,false,increment_address,100);
        std::this_thread::sleep_for(std::chrono::microseconds(fSleepTimeROCWrite));
      }
      catch(...) {
        TLOG(TLVL_ERROR) << "failed WriteROCBlock" << std::endl;
        rc = -1;
        return rc;
      }

      uint16_t u;
      try {
        while ((u = fDtc->ReadROCRegister(link_id,128,100)) != 0x8000) {};
        TLOG(TLVL_DEBUG+1) << Form("reg:%03i val:0x%04x\n",128,u);
      }
      catch(...) {
        TLOG(TLVL_ERROR) << "failed ReadROCRegister(link_id,128,100)" << std::endl;
        rc = -2;
        return rc;
      }
//-----------------------------------------------------------------------------
// register 129: number of words to read, currently-  (+ 4) (ask Monica)
//-----------------------------------------------------------------------------
      int nw(-1);
      try {
        nw = fDtc->ReadROCRegister(link_id,129,100);
      }
      catch(...) {
        TLOG(TLVL_ERROR) << "failed ReadROCRegister(link_id,129,100), nw:" << nw << std::endl;
        rc = -2;
        return rc;
      }

      TLOG(TLVL_DEBUG+1) << Form("reg:%03i val:0x%04x\n",129,nw);
      nw = nw-4;
      Settings.clear();
      try {
        fDtc->ReadROCBlock(Settings,link_id,REG_DUMPSETTINGS,nw,false,100);
      }
      catch(...) {
        TLOG(TLVL_ERROR) << "failed ReadROCBlock(Settings,link_id,REG_SETCALDAC,nw,100), nw:" << nw << std::endl;
        rc = -3;
        return rc;
      }
//-----------------------------------------------------------------------------
// everything was OK
//-----------------------------------------------------------------------------
      if (PrintLevel & 0x1) {
        PrintBuffer(Settings.data(),nw,0x0,Stream);
      }
    }

    return rc;
  }

//-----------------------------------------------------------------------------
// from https://github.com/bonventre/trackerScripts/blob/master/control_ROC.py
// chan_mask = int(get_key_value(keys,"C"),16)
// channel   = int(get_key_value(keys,"c",-1)) - not used any more !!
// fvalue    = float(get_key_value(keys,"v",-1)); dvalue = fvalue/3.3*1023  // obsolete
// dvalue    = int(get_key_value(keys,"d"))
// FirstChanelMask : a bit mask, defines the first pulsed channel, the rest pulsed: first+8*i
//                   0x10 : first pulsed channel is channel 4, max value : 0x80
//-----------------------------------------------------------------------------
  int DtcInterface::ControlRoc_SetCalDac(int Link, int FirstChannelMask, int PulseHeight,
                                         int PrintLevel, std::ostream& Stream) {
    int rc (0); //, reg(268);
    TLOG(TLVL_DEBUG+1) << "Link:" << Link << " FirstChannelMask:0x" << std::hex << FirstChannelMask
                     << std::dec << " PulseHeight:" << PulseHeight;

    Stream << "Link:" << Link << " FirstChannelMask:0x" << std::hex << FirstChannelMask
           << std::dec << " PulseHeight:" << PulseHeight << std::endl;
//-----------------------------------------------------------------------------
// write parameters into reg 268 (block write) , sleep for some time,
// then wait till reg 128 returns 0x8000
//-----------------------------------------------------------------------------
    std::vector<uint16_t> vec;
    vec.push_back(uint16_t(FirstChannelMask));
    vec.push_back(uint16_t(PulseHeight));

    bool increment_address(false);
//-----------------------------------------------------------------------------
// Link = -1 means all links
//-----------------------------------------------------------------------------
    int l1(Link), l2(Link+1);
    if (Link == -1) {
      l1 = 0;
      l2 = 6;
    }

    for (int i=l1; i<l2; i++) {
      if (not LinkEnabled(i))   continue;
      TLOG(TLVL_DEBUG+1) << "      -- i:" << i << std::endl;
//-----------------------------------------------------------------------------
// convert into enum
//-----------------------------------------------------------------------------
      auto roc  = DTC_Link_ID(i);
      try {
        fDtc->WriteROCBlock   (roc,REG_SETCALDAC,vec,false,increment_address,100);
        std::this_thread::sleep_for(std::chrono::microseconds(fSleepTimeROCWrite));
      }
      catch(...) {
        TLOG(TLVL_ERROR) << "failed WriteROCBlock, continue" << std::endl;
        continue;
      }

      uint16_t u;
      while ((u = fDtc->ReadROCRegister(roc,128,100)) != 0x8000) {};
      TLOG(TLVL_DEBUG+1) << Form("reg:%03i val:0x%04x\n",128,u);
//-----------------------------------------------------------------------------
// register 129: number of words to read, currently-  (+ 4) (ask Monica)
//-----------------------------------------------------------------------------
      int nw = fDtc->ReadROCRegister(roc,129,100);
      TLOG(TLVL_DEBUG+1) << Form("reg:%03i val:0x%04x\n",129,nw);

      nw = nw-4;
      std::vector<uint16_t> v2;
      fDtc->ReadROCBlock(v2,roc,REG_SETCALDAC,nw,false,100);

      if (PrintLevel & 0x1) {
        PrintBuffer(v2.data(),nw,0,Stream);
      }
    }

    return rc;
  }

//-----------------------------------------------------------------------------
  int DtcInterface::ControlRoc_SetGain(int Link, int ChannelID, int PreampType, int Gain, int PrintLevel) {
//-----------------------------------------------------------------------------
// convert into enum
//-----------------------------------------------------------------------------
    auto roc  = DTC_Link_ID(Link);
//-----------------------------------------------------------------------------
// write parameters into reg 266 (block write) , sleep for some time,
// then wait till reg 128 returns 0x8000
//-----------------------------------------------------------------------------
    std::vector<uint16_t> vec;
    vec.push_back(uint16_t(ChannelID ));
    vec.push_back(uint16_t(Gain      ));
    vec.push_back(uint16_t(PreampType));

    bool increment_address(false);
    fDtc->WriteROCBlock   (roc,REG_SETGAIN,vec,false,increment_address,100);
    std::this_thread::sleep_for(std::chrono::microseconds(fSleepTimeROCWrite));

                                        // 0x86 = 0x82 + 4
    uint16_t u;
    while ((u = fDtc->ReadROCRegister(roc,128,100)) != 0x8000) {};
    TLOG(TLVL_DEBUG+1) << Form("reg:%03i val:0x%04x\n",128,u);
//-----------------------------------------------------------------------------
// register 129: number of words to read, currently-  (+ 4) (ask Monica)
//-----------------------------------------------------------------------------
    int nw = fDtc->ReadROCRegister(roc,129,100);
    TLOG(TLVL_DEBUG+1) << Form("reg:%03i val:0x%04x\n",129,nw);

    nw = nw-4;
    std::vector<uint16_t> v2;
    fDtc->ReadROCBlock(v2,roc,REG_SETGAIN,nw,false,100);

    if (PrintLevel != 0) PrintBuffer(v2.data(),nw);
//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
    // ResetLink(Link);
    return 0;
  }


//-----------------------------------------------------------------------------
  int DtcInterface::ControlRoc_SetThreshold(int Link, int ChannelID, int PreampType, int Threshold, int PrintLevel) {
//-----------------------------------------------------------------------------
// convert into enum
//-----------------------------------------------------------------------------
    auto roc  = DTC_Link_ID(Link);
//-----------------------------------------------------------------------------
// write parameters into reg 267 (block write) , sleep for some time,
// then wait till reg 128 returns 0x8000
//-----------------------------------------------------------------------------
    std::vector<uint16_t> vec;
    vec.push_back(uint16_t(ChannelID));
    vec.push_back(uint16_t(Threshold));
    vec.push_back(uint16_t(PreampType));

    bool increment_address(false);
    fDtc->WriteROCBlock   (roc,REG_SET_THR,vec,false,increment_address,100);
    std::this_thread::sleep_for(std::chrono::microseconds(fSleepTimeROCWrite));

                                        // 0x86 = 0x82 + 4
    uint16_t u;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
      while ((u = fDtc->ReadROCRegister(roc,128,100)) != 0x8000) {
        if (std::chrono::steady_clock::now() >= deadline)
          throw std::runtime_error("Threshold command timed out waiting for ROC R128.");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    TLOG(TLVL_DEBUG+1) << Form("reg:%03i val:0x%04x\n",128,u);
//-----------------------------------------------------------------------------
// register 129: number of words to read, currently-  (+ 4) (ask Monica)
//-----------------------------------------------------------------------------
    int nw = fDtc->ReadROCRegister(roc,129,100);
    TLOG(TLVL_DEBUG+1) << Form("reg:%03i val:0x%04x\n",129,nw);

    if (nw < 4) throw std::runtime_error("Invalid threshold write reply length.");
    nw = nw-4;
    std::vector<uint16_t> v2;
    fDtc->ReadROCBlock(v2,roc,REG_SET_THR,nw,false,100);

    if (v2.size() != static_cast<size_t>(nw))
      throw std::runtime_error("Truncated threshold write reply.");
    if (PrintLevel != 0) PrintBuffer(v2.data(),nw);
//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
    return 0;
  }


//-----------------------------------------------------------------------------
// order:  4 x 96 16 bit words. Gain cal, Gain HV, threshold CAL, threshold HV
//-----------------------------------------------------------------------------
  int DtcInterface::ControlRoc_SetThresholds(int Link, uint16_t* GT_Cal_HV,
                                             int PrintLevel, std::ostream& Stream) {
//-----------------------------------------------------------------------------
// convert into enum
//-----------------------------------------------------------------------------
    if (Link < 0) {
      TLOG(TLVL_ERROR) << "negative link:" << Link;
      return -1;
    };
//-----------------------------------------------------------------------------
// write parameters into reg 267 (block write) , sleep for some time,
// then wait till reg 128 returns 0x8000
//-----------------------------------------------------------------------------
    std::vector<uint16_t> vec(GT_Cal_HV,GT_Cal_HV+4*96);

    bool increment_address(false);
    auto roc  = DTC_Link_ID(Link);
    fDtc->WriteROCBlock(roc,REG_SETGAINTHR,vec,false,increment_address,100);
    std::this_thread::sleep_for(std::chrono::microseconds(fSleepTimeROCWrite));

    uint16_t u;
    while ((u = fDtc->ReadROCRegister(roc,128,100)) != 0x8000) {};
    TLOG(TLVL_DEBUG+1) << Form("reg:%03i val:0x%04x\n",128,u);
//-----------------------------------------------------------------------------
// register 129: number of words to read, if empty: 0x1000
//-----------------------------------------------------------------------------
    int nw = fDtc->ReadROCRegister(roc,129,100);
    TLOG(TLVL_DEBUG+1) << Form("reg:%03i val:0x%04x\n",129,nw);

    return 0;
  }


//-----------------------------------------------------------------------------
// Link: link number
//-----------------------------------------------------------------------------
  int DtcInterface::ControlRoc_MeasureThresholds(int           Link ,
                                                 uint32_t      MaskC,
                                                 uint32_t      MaskD,
                                                 uint32_t      MaskE,
                                                 int           PrintLevel,
                                                 std::ostream& Stream) {
//-----------------------------------------------------------------------------
// convert into enum
//-----------------------------------------------------------------------------
//    auto roc  = DTC_Link_ID(Link);
//-----------------------------------------------------------------------------
// write parameters into reg 264 (block write) , sleep for some time,
// then wait till reg 128 returns 0x8000
//-----------------------------------------------------------------------------
    if (PrintLevel & 0x8) Stream << std::format("--------- link:{:d} thresholds ----------------",Link) << std::endl;

    std::vector<float> thr; // 3*96

    ControlRoc_ReadThresholds(Link,thr,MaskC,MaskD,MaskE,PrintLevel,Stream);
    PrintThresholds          (Link,thr,MaskC,MaskD,MaskE,PrintLevel,Stream);
    return 0;
  }

//-----------------------------------------------------------------------------
// Link: link number, operate on one ROC
// expect that in most cases read all channels : all masks are set to 0xFFFFFFFF
//-----------------------------------------------------------------------------
  int DtcInterface::ControlRoc_ReadThresholds(int                 Link      ,
                                              std::vector<float>& Thr       ,
                                              uint32_t            MaskC     ,
                                              uint32_t            MaskD     ,
                                              uint32_t            MaskE     ,
                                              int                 PrintLevel,
                                              std::ostream&       Stream    ) {
//-----------------------------------------------------------------------------
// convert into enum
//-----------------------------------------------------------------------------
    auto roc  = DTC_Link_ID(Link);
    Thr.clear();
//-----------------------------------------------------------------------------
// write parameters into reg 264 (block write) , sleep for some time,
// then wait till reg 128 returns 0x8000
//-----------------------------------------------------------------------------
    if (PrintLevel & 0x8) Stream << std::format("--------- link:{:d} thresholds ----------------",Link) << std::endl;

    std::vector<uint16_t> vec;

    uint32_t mask[3];
    mask[0] = MaskC;
    mask[1] = MaskD;
    mask[2] = MaskE;

    vec.push_back((MaskC      ) & 0xffff);
    vec.push_back((MaskC >> 16) & 0xffff);
    vec.push_back((MaskD      ) & 0xffff);
    vec.push_back((MaskD >> 16) & 0xffff);
    vec.push_back((MaskE      ) & 0xffff);
    vec.push_back((MaskE >> 16) & 0xffff);

    bool increment_address(false);
    fDtc->WriteROCBlock   (roc,REG_MEAS_THR,vec,false,increment_address,100);
    std::this_thread::sleep_for(std::chrono::microseconds(fSleepTimeROCWrite));

    // 0x86 = 0x82 + 4
    uint16_t u = 0;
    unsigned selectedChannels = 0;
    for (uint32_t word : mask)
      for (; word; word &= word - 1) ++selectedChannels;
    // A full-panel measurement previously took about 8.4 seconds including
    // transport. Keep a short bound for single-channel calibration probes, but
    // allow multi-channel measurements to finish their sequential ADC work.
    const auto completionTimeout = std::chrono::seconds(selectedChannels > 1 ? 30 : 5);
    try {
      const auto deadline = std::chrono::steady_clock::now() + completionTimeout;
      while ((u = fDtc->ReadROCRegister(roc,128,100)) != 0x8000) {
        if (std::chrono::steady_clock::now() >= deadline)
          throw std::runtime_error(std::format(
              "Threshold measurement on link {} timed out after {} s ({} selected channels, R128=0x{:04x}).",
              Link, completionTimeout.count(), selectedChannels, u));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      if (PrintLevel & 0x1) {
        Stream << std::format("reg:{:03d} val:0x{:04x}",128,u) << std::endl;
      }
    }
    catch(const std::exception& error) {
      TLOG(TLVL_ERROR) << "threshold R128 completion/read failed: " << error.what();
      Stream << "Threshold R128 completion/read failed: " << error.what() << std::endl;
      return -1;
    }
    catch(...) {
      TLOG(TLVL_ERROR) << "failure to read R128";
      Stream << "failed to read ROC R128" << std::endl;
      return -1;
    }
//-----------------------------------------------------------------------------
// register 129: number of words to read, currently-  (+ 4) (ask Monica)
//-----------------------------------------------------------------------------
    int nw(-1);
    try {
      nw = fDtc->ReadROCRegister(roc,129,100);
    }
    catch (...) {
      TLOG(TLVL_ERROR) << "failure to read R129";
      Stream << "failed to read ROC R129, nw:" << nw << std::endl;
      return -2;
    }
//-----------------------------------------------------------------------------
// get here only if all reads didn't lead to exceptions
//-----------------------------------------------------------------------------
    if (PrintLevel & 0x1) Stream << std::format("reg:{:03d} val:0x{:04x}",129,nw) << std::endl;
//-----------------------------------------------------------------------------
// read raw numbers
// expect nw=288 = 96*3, if not - in trouble
//-----------------------------------------------------------------------------
    if (nw != 292) {
      Stream << "Invalid threshold reply word count: " << nw << " (expected 292)\n";
      return -4;
    }
    nw = nw-4;
    std::vector<uint16_t> v2;
    try {
      fDtc->ReadROCBlock(v2,roc,REG_MEAS_THR,nw,false,100);
    }
    catch (...) {
      TLOG(TLVL_ERROR) << "failure to read R" << REG_MEAS_THR;
      Stream << "failed to read ROC R" << REG_MEAS_THR << ", nw:" << nw << std::endl;
      return -3;
    }

    if (v2.size() != 288) {
      Stream << "Truncated threshold reply: " << v2.size() << " words\n";
      return -5;
    }
    if (PrintLevel & 0x1) PrintBuffer(v2.data(),nw,0x0,Stream);
//-----------------------------------------------------------------------------
// convert to floats
//-----------------------------------------------------------------------------
    for (int i=0; i<96; i++) {
      int iw = i/32;
      int ib = i-iw*32;

      float hv(0), cal(0), tot(0);
      if (((mask[iw] >> ib) & 0x1) == 1) {
        hv  = (-1000. + v2[    i]*2000./1024.)/10.;
        cal = (-1000. + v2[96 +i]*2000./1024.)/10.;
        tot = (-1000. + v2[192+i]*2000./1024.)/10.;
      }
      Thr.push_back(hv );
      Thr.push_back(cal);
      Thr.push_back(tot);
    }
    return 0;
  }

//-----------------------------------------------------------------------------
  int DtcInterface::ConvertSpiData(const std::vector<uint16_t>& Data, TrkSpiData_t* Spi, int PrintLevel, std::ostream& Stream) {
    // const char* keys[] = {
    //   "I3.3","I2.5","I1.8HV","IHV5.0","VDMBHV5.0","V1.8HV","V3.3HV" ,"V2.5"    ,
    //   "A0"  ,"A1"  ,"A2"    ,"A3"    ,"I1.8CAL"  ,"I1.2"  ,"ICAL5.0","ADCSPARE",
    //   "V3.3","VCAL5.0","V1.8CAL","V1.0","ROCPCBTEMP","HVPCBTEMP","CALPCBTEMP","RTD",
    //   "ROC_RAIL_1V(mV)","ROC_RAIL_1.8V(mV)","ROC_RAIL_2.5V(mV)","ROC_TEMP(CELSIUS)",
    //   "CAL_RAIL_1V(mV)","CAL_RAIL_1.8V(mV)","CAL_RAIL_2.5V(mV)","CAL_TEMP(CELSIUS)",
    //   "HV_RAIL_1V(mV)","HV_RAIL_1.8V(mV)","HV_RAIL_2.5V(mV)","HV_TEMP(CELSIUS)"
    // };
//-----------------------------------------------------------------------------
// primary source : https://github.com/bonventre/trackerScripts/blob/master/constants.py#L99
//-----------------------------------------------------------------------------
    struct constants_t {
      float iconst  = 3.3 /(4096*0.006*20);
      float iconst5 = 3.25/(4096*0.500*20);
      float iconst1 = 3.25/(4096*0.005*20);
      float toffset = 0.509;
      float tslope  = 0.00645;
      float tconst  = 0.000806;
      float tlm45   = 0.080566;
    } constants;

    int nw = Data.size();

    float* val = (float*) Spi;

    for (int i=0; i<nw; i++) {
      if (i==20 or i==21 or i==22) {
        val[i] = Data[i]*constants.tlm45;
      }
      else if (i==0 or i==1 or i==2 or i==12 or i==13) {
        val[i] = Data[i]*constants.iconst;
      }
      else if (i==3 or i==14) {
        val[i] = Data[i]*constants.iconst5 ;
      }
      else if (i==4 or i==5 or i==6 or i==7 or i==16 or i==17 or i==18 or i==19) {
        val[i] = Data[i]*3.3*2/4096 ;
      }
      else if (i==15) {
        val[i] = Data[i]*3.3/4096;
      }
      else if (i==23) {
        val[i] = Data[i]*3.3/4096;
      }
      else if (i==8 or i==9 or i==10 or i==11) {
        val[i] = Data[i];
      }
      else if (i > 23) {
        if   ((i%4) < 3) val[i] = Data[i]/8.;
        else             val[i] = Data[i]/16.-273.15;
      }

      if (PrintLevel > 0) {
        //        Stream << Form("%-20s : %10.3f\n",keys[i],val[i]);
        Stream << Form("%-20s : %10.3f\n",fgSpiVarName[i],val[i]);
      }
    }

    return 0;
  }

//-----------------------------------------------------------------------------
// read SPI, return vector of short's, optionally print
// ReadSPI: reg 258
//-----------------------------------------------------------------------------
  int DtcInterface::ControlRoc_ReadSpi(std::vector<uint16_t>& SpiRawData, int Link, int PrintLevel, std::ostream& Stream) {
    int rc(0);

    int l1 = Link;
    int l2 = l1+1;

    if (Link == -1) {
      l1 = 0;
      l2 = 6;
    }

    for (int i=l1; i<l2; ++i) {
      if (not LinkEnabled(i)) continue;
      if (not LinkLocked(i)) {
        TLOG(TLVL_ERROR) << std::format("link:{} enabled but not locked",i);
        return -1;
      }

      rc = RocBlockRead(i,REG_READSPI,SpiRawData);
      if (rc < 0) {
        TLOG(TLVL_ERROR) << std::format("RocBlockRead returned rc:{}. BAIL OUT",rc);
        return rc;
      }

      int nw = SpiRawData.size();

      if (nw != TrkSpiDataNWords) {
        TLOG(TLVL_ERROR) << "expected N(words)=" << TrkSpiDataNWords << " , reported nw=" << nw;
        rc = -2;
      }
//-----------------------------------------------------------------------------
// PrintLevel bit 0: print SPI data in hex
//-----------------------------------------------------------------------------
      if ((PrintLevel & 0x1) != 0) {
        PrintBuffer(SpiRawData.data(),nw,0x0,Stream);
      }
//-----------------------------------------------------------------------------
// PrintLevel bit 1: parse SPI data and print them
//-----------------------------------------------------------------------------
      if ((rc == 0) and (PrintLevel & 0x2) != 0) {
        struct TrkSpiData_t spi;
        Stream << "link " << i << std::endl;
        ConvertSpiData(SpiRawData,&spi,PrintLevel,Stream);  // &spi[0]
      }
    }

    return rc;
  }

//-----------------------------------------------------------------------------
// read SPI, convert into floats
//-----------------------------------------------------------------------------
  int DtcInterface::ControlRoc_ReadSpi_1(TrkSpiData_t* Spi, int Link, int PrintLevel, std::ostream& Stream) {
    int rc(0);
//-----------------------------------------------------------------------------
// ReadSPI: reg 258
//-----------------------------------------------------------------------------
    int l1 = Link;
    int l2 = l1+1;

    if (Link == -1) {
      l1 = 0;
      l2 = 6;
    }

    for (int i=l1; i<l2; i++) {
      if (not LinkEnabled(i)) continue;
      if (not LinkLocked(i)) {
        TLOG(TLVL_ERROR) << std::format("link:{} enabled but not locked",i);
        rc += -10;
        continue;
      }
      std::vector<uint16_t> data;
      rc = RocBlockRead(i,REG_READSPI,data,TrkSpiDataNWords);
//-----------------------------------------------------------------------------
// PrintLevel bit 0: print SPI data in hex
//-----------------------------------------------------------------------------
      if ((PrintLevel & 0x1) != 0) {
        int nw = data.size();
        PrintBuffer(data.data(),nw,0x0,Stream);
      }
//-----------------------------------------------------------------------------
// do not perform conversion, if wrong number of words
// PrintLevel bit 1: parse SPI data and print them
//-----------------------------------------------------------------------------
      if (rc == 0) {
        ConvertSpiData(data,Spi,PrintLevel,Stream);  // &spi[0]
      }
    }

    return rc;
  }


//-----------------------------------------------------------------------------
// pay with performance for compactness
// if Link = -1, a git commit for last enabled link is returned
//-----------------------------------------------------------------------------
  int DtcInterface::ControlRoc_ReadGitCommit(std::string& GitCommit, int Link, int PrintLevel, std::ostream& Stream) {
    int rc(0);
    int nw_expected(40);
//-----------------------------------------------------------------------------
// ReadGitCommit: reg
//-----------------------------------------------------------------------------
    std::vector<uint16_t> data;

    int link_mask = (Link == -1) ? fLinkMask : (1 << 4*Link) ;

    for (int i=0; i<6; i++) {
      int link_enabled = (link_mask >> 4*i) & 0x1;
      // Stream << "link:" << i << " link_enabled:" << link_enabled << std::endl;
      if (link_enabled) {
        int rc = RocBlockRead(i,REG_READGITCOMMIT,data,nw_expected);
        if (rc < 0) {
          GitCommit = "READ_ERROR";
        }
        else {
          std::stringstream ss;
          int nw = data.size();
          if (PrintLevel & 0x1) PrintBuffer(data.data(),nw,0x0,Stream);

          for (int iw=0; iw<nw; iw++) ss << std::format("{:c}",data[iw]);
          GitCommit = ss.str();
        }

        if (PrintLevel & 0x2) Stream << std::format("GitCommit:{}\n",GitCommit);
      }
    }

    return rc;
  }

//-----------------------------------------------------------------------------
// if Link = -1, interested in printing
//-----------------------------------------------------------------------------
  int DtcInterface::ControlRoc_ReadIlp(std::vector<uint16_t>& Data, int Link, int PrintLevel, std::ostream& Stream) {
    int rc(0);
//-----------------------------------------------------------------------------

    int link_mask = (Link == -1) ? fLinkMask : (1 << 4*Link) ;

    if (Link == -1) {
      for (int i=0; 0<6; i++) {
        int link_enabled = (link_mask >> 4*i) & 0x1;
        if (link_enabled) {
          RocBlockRead(i,REG_READILP,Data);
          int nw = Data.size();
          if (PrintLevel & 0x1) PrintBuffer(Data.data(),nw,0x0,Stream);
        }
      }
    }
    else {
      RocBlockRead(Link,REG_READILP,Data);
      int nw = Data.size();
      if (PrintLevel & 0x1) PrintBuffer(Data.data(),nw,0x0,Stream);
    }

    return rc;
  }

//-----------------------------------------------------------------------------
// no data conversion
//-----------------------------------------------------------------------------
  int DtcInterface::ControlRoc_GetKey (std::vector<uint16_t>& Data, int Link, int PrintLevel, std::ostream& Stream) {
    int rc(0);

    int link_mask = (Link == -1) ? fLinkMask : (1 << 4*Link) ;

    if (Link == -1) {
      for (int i=0; 0<6; i++) {
        int link_enabled = (link_mask >> 4*i) & 0x1;
        if (link_enabled) {
          RocBlockRead(i,REG_GETKEY,Data);
          int nw = Data.size();
          if (PrintLevel & 0x1) PrintBuffer(Data.data(),nw,0x0,Stream);
        }
      }
    }
    else {
      RocBlockRead(Link,REG_GETKEY,Data);
      int nw = Data.size();
      if (PrintLevel & 0x1) PrintBuffer(Data.data(),nw,0x0,Stream);
    }

    return rc;
  }

//-----------------------------------------------------------------------------
// at this point, assume just one Link. If needed, make it more general (a mask) later
// only unformatted printout internally,
//-----------------------------------------------------------------------------
  int  DtcInterface::ControlRoc_Rates(int                    Link,
                                      std::vector<uint16_t>* V2,
                                      int                    PrintLevel,
                                      ControlRoc_Rates_t*    Par,
                                      std::ostream&         Stream) {
    int                 rc(0);
    ControlRoc_Rates_t  par;   // default construction : (num_lookback=100,num_samples=10,ch_mask=6x0xffff)

    TLOG(TLVL_DEBUG+1) << " -- START PrintLevel:" << PrintLevel;

    if (Par != nullptr) par = *Par;

    TLOG(TLVL_DEBUG+1) << "Link:" << Link << " par:{" << par.num_lookback << ","
                       << par.num_samples << ","
                       << std::hex
                       << "0x" << par.ch_mask[0] << ","
                       << "0x" << par.ch_mask[1] << ","
                       << "0x" << par.ch_mask[2] << ","
                       << "0x" << par.ch_mask[3] << ","
                       << "0x" << par.ch_mask[4] << ","
                       << "0x" << par.ch_mask[5] << "}";

    if (Link == -1) {
      TLOG(TLVL_ERROR) << "negative link:" << Link << "  ... BAIL OUT";
      return -1;
    }
    else if (LinkEnabled(Link) == 0) {
      TLOG(TLVL_ERROR) << "link:" << Link << "  is not enabled, BAIL OUT";
      return -2;
    }
//-----------------------------------------------------------------------------
// write parameters into reg ***  (block write) , sleep for some time,
// then wait till reg 128 returns 0x8000
// ch_mask always includes the first channel
//-----------------------------------------------------------------------------
    std::vector<uint16_t> vec;

    vec.push_back(par.num_lookback);
    vec.push_back(par.num_samples );

    for (int i=0; i<6; i++) {
      vec.push_back(par.ch_mask[i]);
    }

    TLOG(TLVL_DEBUG+1) << " -- 002";

    auto roc  = DTC_Link_ID(Link);
    fDtc->WriteROCBlock   (roc,REG_READRATES,vec,false,false,1000);
    std::this_thread::sleep_for(std::chrono::microseconds(1000));

    // 0x86 = 0x82 + 4
    uint16_t u;
    while ((u = fDtc->ReadROCRegister(roc,128,5000)) != 0x8000) {};
    if (PrintLevel) printf("reg:%03i val:0x%04x\n",128,u);
//-----------------------------------------------------------------------------
// register 129: number of words to read, currently (+ 4) (ask Monica)
//-----------------------------------------------------------------------------
    int nw = fDtc->ReadROCRegister(roc,129,100);
    if (PrintLevel) printf("reg:%03i val:0x%04x\n",129,nw);

    TLOG(TLVL_DEBUG+1) << " -- 003 from nw(reg_129):" << nw;
    nw = nw-4;
    fDtc->ReadROCBlock(*V2,roc,REG_READRATES,nw,false,100);

    TLOG(TLVL_DEBUG+1) << " -- 004 read nw:" << nw;
//-----------------------------------------------------------------------------
// print output - in two formats
//-----------------------------------------------------------------------------
    if (PrintLevel & 0x1) {
      PrintBuffer(V2->data(),nw,0x0,Stream);
    }

    TLOG(TLVL_DEBUG+1) << " -- END";
    return rc;
  }

//-----------------------------------------------------------------------------
  int  DtcInterface::ControlRoc_ReadDeviceID(int                    Link      ,
                                             ControlRoc_DeviceID_t& DevId     ,
                                             int                    PrintLevel,
                                             std::ostream&          Stream    ) {

    DevId = {};
    if (Link < 0 || Link >= 6 || !LinkEnabled(Link)) {
      Stream << "READDEVICE: invalid or disabled ROC link " << Link << '\n';
      return -1;
    }

    std::vector<uint16_t> dat = ReadDeviceID(DTC_Link_ID(Link));
    // READDEVICE contains 52 byte values, each transported in a 16-bit word.
    // Validate before indexing; a short reply must not access past the vector.
    if (dat.size() != 52) {
      Stream << "READDEVICE: expected 52 payload words, received " << dat.size() << '\n';
      return -1;
    }
    for (const auto value : dat) {
      if (value > 0xff) {
        Stream << "READDEVICE: payload word is not a byte value\n";
        return -1;
      }
    }

    std::stringstream ss;
    // first 16 bytes are the serial number
    for (int i = 15 ; i >= 0 ; i--) ss << std::format("{:02x}",dat[i]);

    DevId.DeviceSerial = "'"+ss.str()+"'";

    ss.str({});
    ss.clear();
    //    ss << "0x";
    // next 32 bytes are the designInfo
    for (int i = 47 ; i >= 16 ; i--) ss << std::format("{:02x}",dat[i]);
    DevId.DesignInfo = "'"+ss.str()+"'";

    ss.str({});
    ss.clear();
    // ss << "0x";
    // next 2 bytes are the design version
    for (int i = 49 ; i >= 48 ; i--) ss << std::format("{:02x}",dat[i]);
    DevId.DesignVer = "'"+ss.str()+"'";

    ss.str({});
    ss.clear();
    // ss << "0x";
    // next 2 bytes are the backlevel vesrsion, whatever it is
    for (int i = 51 ; i >= 50 ; i--) ss << std::format("{:02x}",dat[i]);
    DevId.BackLevelVer = "'"+ss.str()+"'";


    if (PrintLevel & 0x1) {
        Stream << std::format("BackLevelVer:{}\n",DevId.BackLevelVer);
        Stream << std::format("DesignInfo  :{}\n",DevId.DesignInfo);
        Stream << std::format("DesignVer   :{}\n",DevId.DesignVer);
        Stream << std::format("DeviceSerial:{}\n",DevId.DeviceSerial);
    }

    return 0;
  }
};
