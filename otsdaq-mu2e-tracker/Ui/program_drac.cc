//-----------------------------------------------------------------------------
// TLVL_DEBUG  : standard begin and end
// TLVL_DEBUG+1: spi_read_record end printouts (they come often)
// TLVL_DEBUG+2: spi_read_record start printouts (normally don't need both)
// bit 0x10000 : validate
// bit 0x00008 : spi_write_record: print failed to write record
//-----------------------------------------------------------------------------
#include "otsdaq-mu2e-tracker/Ui/program_drac.hh"

#include <boost/algorithm/string.hpp>
#include <thread>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include "nlohmann/json.hpp"
#include "TRACE/tracemf.h"
#define  TRACE_NAME "program_drac"

namespace {
trkdaq::DtcInterface* programming_target(trkdaq::DtcInterface* dtc, int link) {
  if (link < 0 || link >= 6) throw std::runtime_error("Programming requires one ROC link, 0-5");
  if (!dtc) dtc = trkdaq::DtcInterface::Instance(-1);
  if (!dtc || !dtc->fDtc || !dtc->LinkEnabled(link) || !dtc->LinkLocked(link))
    throw std::runtime_error("Programming requires an initialized DTC and an enabled, locked ROC link");
  return dtc;
}

// Every attempt has an I/O timeout; the attempt count bounds the whole poll.
// These are retry budgets, not strict wall-clock deadlines.
int wait_register(trkdaq::DtcInterface* dtc, int link, uint16_t address,
                  uint16_t mask, uint16_t expected, int attempts, int delayUs,
                  int ioTimeoutMs, uint16_t& last) {
  for (int attempt=0; attempt<attempts; ++attempt) {
    last = dtc->fDtc->ReadROCRegister(DTCLib::DTC_Link_ID(link), address, ioTimeoutMs);
    if ((last & mask) == expected) return 0;
    if (address == 128 && (last & 0x8000) && (last & 0x7fff)) {
      TLOG(TLVL_ERROR) << std::format("DTC:{} ROC:{} R128 command error:0x{:04x}",dtc->PcieAddr(),link,last);
      return -3;
    }
    if (attempt+1 < attempts) std::this_thread::sleep_for(std::chrono::microseconds(delayUs));
  }
  TLOG(TLVL_ERROR) << std::format("DTC:{} ROC:{} R{} polling exhausted {} attempts; value:0x{:04x}",
                                 dtc->PcieAddr(),link,address,attempts,last);
  return -2;
}

void check_image(const std::string& filename, int offset) {
  const auto size = std::filesystem::file_size(filename);
  if (offset < 0 || size == 0 || size > uint64_t(std::numeric_limits<int>::max()) ||
      size + uint64_t(offset) > uint64_t(std::numeric_limits<int>::max()))
    throw std::runtime_error("Invalid programming image size or flash range: " + filename);
  std::ifstream file(filename, std::ios::binary);
  if (!file) throw std::runtime_error("Programming image is not readable: " + filename);
}

std::string identity_hex(std::string value, size_t digits) {
  if (value.size() >= 2 && value.front() == '\'' && value.back() == '\'')
    value = value.substr(1,value.size()-2);
  if (value.starts_with("0x") || value.starts_with("0X")) value.erase(0,2);
  if (value.size() != digits || value.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
    throw std::runtime_error("Malformed expected or read-back firmware identity");
  for (auto& c : value) if (c >= 'A' && c <= 'F') c += 'a'-'A';
  return value;
}

void require_identity(const program_drac::FwVersion_t& version) {
  // Refuse activation before the first write if expected values are unavailable.
  identity_hex(version.expected_design_version,4);
  identity_hex(version.expected_usercode,8);
  if (version.bin_file.load_flag > 0 || !version.expected_git_commit.empty())
    identity_hex(version.expected_git_commit,40);
}
} // namespace


//-----------------------------------------------------------------------------
// GoldenVXX should always be the first image ( index 0, offset 0x10000)
// the rest versions could be overriding each other
// name = "": end of data
// assume that the image index is incremented monotonically
// 'FwVersion_t.index' is the image index in the directory catalog
//-----------------------------------------------------------------------------
// program_drac::FwVersion_t drac_fw[] = {
//     { "GoldenV11",  0,
//       { 1,    0x10000, "/home/mu2etrk/test_stand/spi_files/GoldenV11.spi"          }, // 9530672 },
//       {-1,         -1, ""                                                          }, //     -1 }
//     },
//     { "ROCV14",     1,
//       { 1,  0x1010000, "/home/mu2etrk/test_stand/spi_files/ROCV14.spi"             }, // 9482832 },
//       { 1,  0x5000000, "/home/mu2etrk/test_stand/spi_files/ROCV14_stage3init.bin"  }, //  86384 }
//     },
//     { "ROCV15",     2,
//       { 1,  0x2010000, "/home/mu2etrk/test_stand/spi_files/ROCV15.spi"             }, // 9482832 },
//       { 1,  0x5040000, "/home/mu2etrk/test_stand/spi_files/ROCV15_stage3init.bin"  }, //  86384 }
//     },
//                                         // end of data marker
//     { "",          -1,
//       {-1,         -1, ""                                                          }, //     -1 },
//       {-1,         -1, ""                                                          }, //     -1 }
//     }
//   };

// program_drac::FwVersion_t drac_fw[] = {
//     { "GoldenV17",  0,
//       { 1,    0x10000, "/home/mu2etrk/test_stand/spi_files/GoldenV17.spi"            }, // 9530672 },
//       {-1,         -1, ""                                                            }, //     -1 }
//     },
//     { "ROCV16",     1,
//       { 1,  0x1010000, "/home/mu2etrk/test_stand/spi_files/ROCV16.spi"               }, // 9482832 },
//       { 1,  0x5000000, "/home/mu2etrk/test_stand/spi_files/ROCV16_stage3init.bin"    }, //  86384 }
//     },
//                                         // end of data marker
//     { "ROCV16-1",   2,
//       { 1,  0x1010000, "/home/mu2etrk/test_stand/spi_files/ROCV16.spi"               }, // 9482832 },
//       { 1,  0x5000000, "/home/mu2etrk/test_stand/spi_files/ROCV16-1_stage3init.bin"  }, //  86384 }
//     },
//                                         // end of data marker
//     { "",          -1,
//       {-1,         -1, ""                                                          }, //     -1 },
//       {-1,         -1, ""                                                          }, //     -1 }
//     }
//  };

//-----------------------------------------------------------------------------
// default config file: $MU2E_DAQ_DIR/config/tracker/spi_directory.json
//-----------------------------------------------------------------------------
program_drac::program_drac(const char* ConfigFile, bool PrintConfig) {
  kSPI_CLEAR_SLEEP_US = 200000;       // 0.2 sec

  std::string fn = ConfigFile;
  if (fn == "") {
    const char* mu2e_daq_dir = getenv("MU2E_DAQ_DIR");
    if (mu2e_daq_dir) {
      fn = std::format("{}/config/tracker/spi_directory.json",mu2e_daq_dir);
    }
  }

  TLOG(TLVL_INFO) << std::format("using config file {}",fn);

  std::ifstream ifs(fn);
  if (ifs.is_open()) {
    nlohmann::json jf = nlohmann::json::parse(ifs);
    if (PrintConfig) std::cout << std::format("Available versions:\n--------------------\n");
    for (auto& e : jf.items()) {
      nlohmann::json o = e.value();

      FwVersion_t fv;

      fv.name               = o["version"];
      fv.index              = o["index"];
      const auto identity = o.value("expected_identity", nlohmann::json::object());
      fv.expected_design_version = identity.value("design_version", "");
      fv.expected_usercode = identity.value("usercode", "");
      fv.expected_git_commit = identity.value("git_commit", "");
      fv.spi_file.load_flag = o["fpga_image"]["load_flag"];
      std::string s         = o["fpga_image"]["offset"];
      fv.spi_file.offset    = std::stoi(s,nullptr,0);
      fv.spi_file.fn        = o["fpga_image"]["filename"];

      fv.bin_file.load_flag = o["bin_image"]["load_flag"];
      s                     = o["bin_image"]["offset"];
      fv.bin_file.offset    = std::stoi(s,nullptr,0);
      fv.bin_file.fn        = o["bin_image"]["filename"];

      _drac_fw.push_back(fv);

      if (PrintConfig) {
        std::cout << std::format("version: {:10} index:{}\n",fv.name,fv.index);
        std::cout << std::format("{:5} spi_file: load_flag:{:2} offset:0x{:08x} fn:{}\n",
                                 "",fv.spi_file.load_flag,fv.spi_file.offset,fv.spi_file.fn);
        std::cout << std::format("{:5} bin_file: load_flag:{:2} offset:0x{:08x} fn:{}\n",
                                 "",fv.bin_file.load_flag,fv.bin_file.offset,fv.bin_file.fn);
      }
      }
  }
  else {
    TLOG(TLVL_ERROR) << std::format("initialization failed");
  }
}

//-----------------------------------------------------------------------------
// if Spi=1, return SPI image, otherwise - bin
//----------------------------------------------------------------------------
const program_drac::FwVersion_t* program_drac::get_version(const std::string& Version) {

  FwVersion_t* fw(nullptr);

  int nv = _drac_fw.size();
  for (int i=0; i<nv; i++) {
    FwVersion_t* v = &_drac_fw[i];
    if (v->name == Version) {
      fw = v;
      break;
    }
  }

  if (fw == nullptr) TLOG(TLVL_ERROR) << "fw version:" << Version << " is not defined, return NULL pointer.";

  return fw;
}

//-----------------------------------------------------------------------------
// if Spi=1, return SPI image, otherwise - bin
//----------------------------------------------------------------------------
const program_drac::ImageData_t* program_drac::get_image_data(const std::string& Version, const std::string Spi) {

  const FwVersion_t* fw = get_version(Version);

  if (fw == nullptr) {
    TLOG(TLVL_ERROR) << std::format("fw version:{} is not defined, BAIL OUT.\n",Version);
    return (const ImageData_t*) nullptr;
  }

  if (Spi == "spi") return (const ImageData_t*) &fw->spi_file;
  else              return (const ImageData_t*) &fw->bin_file;
}

//-----------------------------------------------------------------------------
// clears SPI memory in 64k blocks
//-----------------------------------------------------------------------------
// void spi_clear(int Link, int Address, int NBytes) {
int program_drac::spi_clear_memory(trkdaq::DtcInterface* Dtc_i, int Link, int Offset, int NBytes, int DebugMode) {
  Dtc_i = programming_target(Dtc_i, Link);
  if (Offset < 0 || NBytes <= 0 || uint64_t(Offset)+uint64_t(NBytes) > uint64_t(std::numeric_limits<int>::max())) return -1;
  int rc(0);

  if (Dtc_i == nullptr) Dtc_i = trkdaq::DtcInterface::Instance(-1);

  bool increment_address(false);
                                              // 5 words
  std::vector<uint16_t> input;

  TLOG(TLVL_DEBUG+1) << std::format("clearing nbytes:{} starting from address 0x:{:08x}\n",NBytes,Offset);

  input.push_back(SPI_CLEAR);                 // SPI clear
  input.push_back( Offset         & 0xFFFF);  //
  input.push_back((Offset  >> 16) & 0xFFFF);  //
  input.push_back( NBytes         & 0xFFFF);  //
  input.push_back((NBytes  >> 16) & 0xFFFF);  //

  auto roc  = DTCLib::DTC_Link_ID(Link);
  Dtc_i->fDtc->WriteROCBlock(roc,RREG,input,false,increment_address,100);

  if (DebugMode & 0x1) {
    std::cout << __func__ << ": SPI_CLEAR input written " << std::endl;
  }

  uint16_t u;
  if (wait_register(Dtc_i, Link, 128, 0xffff, 0x8000, 5000, 5000, 100, u) != 0)
    return -2;

  if (u != 0x8000) {
    TLOG(TLVL_ERROR) << std::format("timeout reg:{:03d} val:0x{:04x}\n",128,u);
    rc = -1;
  }
                                        // sleep , by default , for 200 ms
  std::this_thread::sleep_for(std::chrono::microseconds( kSPI_CLEAR_SLEEP_US));

  TLOG(TLVL_DEBUG+1) << Form("reg:%03i val:0x%04x\n",128,u);
  return rc;
}

//-----------------------------------------------------------------------------
// program IAP
//-----------------------------------------------------------------------------
int program_drac::spi_program_roc(trkdaq::DtcInterface* Dtc_i, int Link, const std::string& Version) {
  Dtc_i = programming_target(Dtc_i, Link);
  const FwVersion_t* ver = get_version(Version);
  if (!ver || ver->index < 0 || ver->index >= 16 || ver->spi_file.load_flag <= 0) return -1;
  require_identity(*ver);
  const std::string expectedDesign = identity_hex(ver->expected_design_version,4);
  const std::string expectedUser = identity_hex(ver->expected_usercode,8);
  TLOG(TLVL_INFO) << std::format("Activating DTC:{} ROC:{} version:{} index:{}; parent DTC reset/readout initialization follows",
                                 Dtc_i->PcieAddr(), Link, Version, ver->index);
  std::vector<uint16_t> input{PROGRAM_IAP_W_INDEX, uint16_t(ver->index), 0, 0, 0};
  const auto roc = DTCLib::DTC_Link_ID(Link);
  Dtc_i->fDtc->WriteROCBlock(roc,RREG,input,false,false,100);
  sleep(35); // Existing recovery delay while the FPGA reloads.
  Dtc_i->fDtc->SoftReset();
  uint16_t status=0;
  if (wait_register(Dtc_i, Link, 128, 0xffff, 0x8000, 5000, 5000, 1000, status) != 0) return -2;
  const int programmingRC = Dtc_i->fDtc->ReadROCRegister(roc,REG_STATUS,1000);
  if (programmingRC != 0) return programmingRC;
  const int initRC = Dtc_i->InitReadout(-1,1);
  if (initRC != 0) return initRC;

  trkdaq::ControlRoc_DeviceID_t info;
  int rc = Dtc_i->ControlRoc_ReadDeviceID(Link,info,0,std::cout);
  if (rc != 0) return rc;
  std::vector<uint16_t> user;
  rc = Dtc_i->RocBlockRead(Link,280,user,4); // READUSERCODE
  if (rc != 0) return rc;
  if (user.size() != 4) return -4;
  uint32_t userCode=0;
  for (size_t i=0;i<4;++i) {
    if (user[i] > 0xff) return -4;
    userCode |= uint32_t(user[i]) << (8*i);
  }
  const auto actualDesign = identity_hex(info.DesignVer,4);
  const auto actualUser = std::format("{:08x}",userCode);
  TLOG(TLVL_INFO) << std::format("DTC:{} ROC:{} activated identity: DesignVer={} UserCode={}",
                                 Dtc_i->PcieAddr(),Link,actualDesign,actualUser);
  if (actualDesign != expectedDesign || actualUser != expectedUser) return -5;
  if (!ver->expected_git_commit.empty()) {
    std::string commit;
    rc = Dtc_i->ControlRoc_ReadGitCommit(commit,Link,0,std::cout);
    if (rc != 0) return rc;
    TLOG(TLVL_INFO) << std::format("DTC:{} ROC:{} activated git_commit={}",Dtc_i->PcieAddr(),Link,commit);
    if (identity_hex(commit,40) != identity_hex(ver->expected_git_commit,40)) return -5;
  }
  return 0;
}



//-----------------------------------------------------------------------------
void program_drac::spi_program_iap_w_index(trkdaq::DtcInterface* Dtc_i, int Link, int Index) {
  Dtc_i = programming_target(Dtc_i, Link);
  if (Index < 0 || Index >= 16) throw std::runtime_error("Invalid ROC IAP target");
  if (Dtc_i == nullptr) Dtc_i = trkdaq::DtcInterface::Instance(-1);

  bool increment_address(false);
                                              // 5 words
  std::vector<uint16_t> input;

  input.push_back(PROGRAM_IAP_W_INDEX);                 // SPI clear
  input.push_back( Index        & 0xFFFF);  //
  input.push_back((Index >> 16) & 0xFFFF);  //
  input.push_back(0);  //
  input.push_back(0);

  auto roc  = DTCLib::DTC_Link_ID(Link);
  Dtc_i->fDtc->WriteROCBlock(roc,RREG,input,false,increment_address,100);

  sleep(40);
                                        // wait till the command is executed
  uint16_t u;
  if (wait_register(Dtc_i, Link, 128, 0xffff, 0x8000, 5000, 5000, 1000, u) != 0)
    throw std::runtime_error("ROC IAP did not complete successfully");

  uint16_t status;
  status = Dtc_i->fDtc->ReadROCRegister(roc,REG_STATUS,1000);
  if (status != 0) throw std::runtime_error("ROC IAP returned a programming error");
  std::cout << __func__ << ":END status:" << status << std::endl;
}

//-----------------------------------------------------------------------------
// restricted functionality tool
// assumes that the ROC has already been programmed and the directory catalog for
// indices 0 and 1 has correct image offsets
// explicitly use versions at indices 0 and 1 for 'GoldenVXX' and 'ROCVXX'
// 'LinkMask' is in a 'hex' format, so 'all links' = 0x111111
//-----------------------------------------------------------------------------
int  program_drac::spi_reprogram_roc(trkdaq::DtcInterface* Dtc_i, int LinkMask) {
  int rc(0);
  if (LinkMask == 0) return 0;
  if ((LinkMask & ~0x111111) != 0 || _drac_fw.size() < 2) return -1;
  if (!Dtc_i) Dtc_i = trkdaq::DtcInterface::Instance(-1);
  if (!Dtc_i) return -1;
  require_identity(_drac_fw[1]);
  for (int lnk=0; lnk<6; lnk++) {
    int requested = (LinkMask >> 4*lnk) & 0x1;
    if (requested == 0)                                     continue;
    if (Dtc_i->LinkEnabled(lnk) == 0) {
      TLOG(TLVL_WARNING) << std::format("link:{} not enabled, SKIP",lnk);
      continue;
    }
    if (Dtc_i->LinkLocked (lnk) == 0) {
      TLOG(TLVL_WARNING) << std::format("link:{} enabled but not locked, SKIP",lnk);
      continue;
    }
                                        // upload 'GoldenVXX'
    FwVersion_t* v0 = &_drac_fw[0];
    rc = spi_write_version(Dtc_i,lnk,v0->name);
    if (rc != 0)                                             return rc;
                                        // upload 'ROCVXX'
    FwVersion_t* v1 = &_drac_fw[1];
    rc = spi_write_version(Dtc_i,lnk,v1->name);
    if (rc != 0)                                             return rc;
                                        // program 'ROCVXX'
    rc = spi_program_roc  (Dtc_i,lnk,v1->name);
    if (rc != 0)                                             return rc;
  }
  return rc;
}

//-----------------------------------------------------------------------------
// program IAP by address - not really needed
//-----------------------------------------------------------------------------
void program_drac::spi_program_iap_w_address(trkdaq::DtcInterface* Dtc_i, int Link, int ImageOffset) {
  Dtc_i = programming_target(Dtc_i, Link);
  if (ImageOffset < 0) throw std::runtime_error("Invalid ROC IAP target");
  if (Dtc_i == nullptr) Dtc_i = trkdaq::DtcInterface::Instance(-1);

  TLOG(TLVL_DEBUG) << std::format("START PCIE:{} Link:{}",Dtc_i->PcieAddr(),Link);

  bool increment_address(false);
                                              // 5 words
  std::vector<uint16_t> input;

  input.push_back(PROGRAM_IAP_W_ADDRESS);                 // SPI clear
  input.push_back((ImageOffset      ) & 0xFFFF);  //
  input.push_back((ImageOffset >> 16) & 0xFFFF);  //
  input.push_back(0);  //
  input.push_back(0);

  auto roc  = DTCLib::DTC_Link_ID(Link);
  Dtc_i->fDtc->WriteROCBlock(roc,RREG,input,false,increment_address,100);

                                        // wait till the command is executed
  uint16_t u;
  if (wait_register(Dtc_i, Link, 128, 0xffff, 0x8000, 5000, 5000, 1000, u) != 0)
    throw std::runtime_error("ROC IAP did not complete successfully");

  uint16_t status;
  status = Dtc_i->fDtc->ReadROCRegister(roc,REG_STATUS,1000);
  if (status != 0) throw std::runtime_error("ROC IAP returned a programming error");
  TLOG(TLVL_DEBUG) << std::format("END  PCIE:{} Link:{} status:{}",Dtc_i->PcieAddr(),Link,status);
}


//-----------------------------------------------------------------------------
// read one record, can't read more than 127 words (254 bytes) at a time
// DebugMode: bit0: print one-liner
// can't read records longer than 1K bytes, but NWords could be > 1024
// upon success returns rc=0, otherwise rc<0
//-----------------------------------------------------------------------------
int program_drac::spi_read_record(trkdaq::DtcInterface* Dtc_i, int Link, uint32_t SpiOffset, int NWords, uint16_t* Res, int DebugMode) {
  Dtc_i = programming_target(Dtc_i, Link);
  if (NWords <= 0 || uint64_t(SpiOffset)+2*uint64_t(NWords) > uint64_t(std::numeric_limits<int>::max())) return -1;
  const auto roc = DTCLib::DTC_Link_ID(Link);
  for (int offset=0;offset<NWords;offset+=127) {
    const int nw=std::min(127,NWords-offset);
    const uint32_t address=SpiOffset+2*uint32_t(offset);
    std::vector<uint16_t> input{SPI_FLASH_READ,uint16_t(address),uint16_t(address>>16),uint16_t(nw*2),0};
    Dtc_i->fDtc->WriteROCBlock(roc,RREG,input,false,false,100);
    uint16_t status=0;
    if (wait_register(Dtc_i,Link,128,0xffff,0x8000,5000,5000,100,status) != 0) return -2;
    if (wait_register(Dtc_i,Link,129,0xffff,nw+4,1000,5000,100,status) != 0) return -3;
    std::vector<uint16_t> buf;
    // Consume the pending reply directly; RocBlockRead would launch another command.
    Dtc_i->fDtc->ReadROCBlock(buf,roc,RREG,nw,false,1000);
    if (buf.size() != size_t(nw)) return -4;
    if (DebugMode & 0x8) Dtc_i->PrintBuffer(buf.data(),buf.size(),address);
    if (Res) std::copy(buf.begin(),buf.end(),Res+offset);
  }
  return 0;
}

//-----------------------------------------------------------------------------
// read one segment (or less) starting from 'SpiOffset' in SPI memory
// NBytes is 0x10000 or less
// actual reads from SPI memory are done using records of 'record_size'
// exit in case of an error
//-----------------------------------------------------------------------------
int program_drac::spi_read_segment(trkdaq::DtcInterface* Dtc_i, int Link, uint32_t SpiOffset, int NBytes, std::vector<char>& Res, int DebugMode) {
  Dtc_i = programming_target(Dtc_i, Link);
  if (NBytes <= 0 || NBytes > 0x10000 ||
      uint64_t(SpiOffset) + uint64_t(NBytes) > uint64_t(std::numeric_limits<int>::max())) return -1;
  Res.clear();
  Res.resize(NBytes);
  for (int offset=0; offset<NBytes; offset+=254) {
    const int nb = std::min(254, NBytes-offset);
    std::vector<uint16_t> words((nb+1)/2);
    const int rc = spi_read_record(Dtc_i, Link, SpiOffset+offset, words.size(), words.data(), DebugMode);
    if (rc != 0) { Res.clear(); return rc; }
    for (int i=0; i<nb; ++i) Res[offset+i] = char((words[i/2] >> (8*(i%2))) & 0xff);
  }
  return 0;
}

//-----------------------------------------------------------------------------
// validate image already writen to SPI memory
//-----------------------------------------------------------------------------
int program_drac::spi_validate_segment(trkdaq::DtcInterface* Dtc_i, int Link, const ImageData_t* Image, int Segment, int DebugMode) {
  Dtc_i = programming_target(Dtc_i, Link);
  if (!Image) return -1;
  check_image(Image->fn, Image->offset);
  int rc = 0;
//-----------------------------------------------------------------------------
// open input file and determine its size
//-----------------------------------------------------------------------------
  std::ifstream file(Image->fn, std::ios::binary);
  if (not file.is_open()) {
    TLOG(TLVL_ERROR) << std::format("failed to open file:{} . BAIL OUT",Image->fn);
    return -1;
  }

  file.seekg(0, std::ios::end);
  const auto fileSize = file.tellg();
  if (fileSize <= 0 || fileSize > std::numeric_limits<int>::max()) return -1;
  int fsize = static_cast<int>(fileSize);
  file.seekg(0, std::ios::beg);
//-----------------------------------------------------------------------------
// clear the spi memory for image at index *** ###
//-----------------------------------------------------------------------------
  TLOG(TLVL_DEBUG) << std::format("fn:{} fsize:{}",Image->fn,fsize);
//-----------------------------------------------------------------------------
// read the input file and upload its content to the SPI memory
// have to do everything in 64K blocks - this is the unit in which the SPI memory
// is getting reset
//-----------------------------------------------------------------------------
  int const segment_size(0x10000); // the SPI memory is cleared in 64K segments

  std::vector<char> fileData(fsize);
  file.read(fileData.data(), fsize);
  if (!file) return -1;

  int nb_read_tot(0);
                                                 // offset in the image file
  if (Segment < 0 || uint64_t(Segment)*segment_size >= uint64_t(fsize)) return -1;
  int segment_offset  = Segment*segment_size;

  int nb_to_read     = segment_size;
                                                 // handle last, potentially shorter, block
  if (segment_offset+segment_size > fsize) {
    nb_to_read = fsize-segment_offset;
  }
                                                 // first address in SPI memory
  int spi_offset   = Image->offset+segment_offset;
//-----------------------------------------------------------------------------
// read one segment, 64K or less
//-----------------------------------------------------------------------------
  std::vector<char> readback;
  rc = spi_read_segment(Dtc_i,Link,spi_offset,nb_to_read,readback);
  if (rc == 0) {
    nb_read_tot += nb_to_read;
                                                 // compare to the original
    for (int i=0; i<nb_to_read; ++i) {
      if (readback[i] != fileData[segment_offset+i]) {
//-----------------------------------------------------------------------------
// if an inconsistency has been detected, print diagnostics and try to rewrite up to 3 times
//-----------------------------------------------------------------------------
        TLOG(TLVL_ERROR) << std::format("PCIE:{} Link:{} segment SPI offset:0x{:08x}\n",Dtc_i->PcieAddr(),Link,spi_offset)
                         << std::format(" segment number {:5d} byte at file offset:0x{:08x}",Segment,segment_offset+i)
                         << std::format(" is:0x{:2x}",(uint8_t)fileData[segment_offset+i])
                         << std::format(" readback byte offset:0x{:08x} value:0x{:x}",i, (uint8_t) readback[i]);
        rc = -2;
        break;
      }
    }
    if (rc == 0) {
      TLOG(TLVL_INFO) << std::format("PCIE:{} Link:{} segment:{} : validation succeeded",Dtc_i->PcieAddr(),Link,Segment);
    }
  }
//-----------------------------------------------------------------------------
// if rc = 0, the readback and the validation succeeded, nothing more to do
//-----------------------------------------------------------------------------
  if (rc != 0) {
    TLOG(TLVL_ERROR) << std::format("validation failed, rc:{} . BAIL OUT",rc);
    return rc;
  }

  TLOG(TLVL_DEBUG) << std::format("END rc:{} nb_read_tot:{}",rc,nb_read_tot);
  return rc;
}

//-----------------------------------------------------------------------------
// validate image already writen to SPI memory
//-----------------------------------------------------------------------------
int program_drac::spi_validate_image(trkdaq::DtcInterface* Dtc_i, int Link, const std::string& Version, const std::string& Type, int DebugMode) {
  Dtc_i = programming_target(Dtc_i, Link);
  int rc = 0;
//-----------------------------------------------------------------------------
// open input file and determine its size
//-----------------------------------------------------------------------------
  const ImageData_t* image = get_image_data(Version, Type);
  if (image == nullptr) {
    TLOG(TLVL_ERROR) << std::format("image {}:{} not found. BAIL OUT",Version,Type);
    return -1;
  }

  std::ifstream file(image->fn, std::ios::binary);
  if (not file.is_open()) {
    TLOG(TLVL_ERROR) << std::format("failed to open file:{} . BAIL OUT",image->fn);
    return -1;
  }

  file.seekg(0, std::ios::end);
  const auto fileSize = file.tellg();
  if (fileSize <= 0 || fileSize > std::numeric_limits<int>::max()) return -1;
  int fsize = static_cast<int>(fileSize);
  file.seekg(0, std::ios::beg);

  TLOG(TLVL_INFO) << std::format("-- START validating PCIE:{} Link:{} Version:{} Type:{} fn:{} fsize:{}\n",
                                 Dtc_i->PcieAddr(),Link,Version,Type,image->fn,fsize);
 //-----------------------------------------------------------------------------
// read the input file and upload its content to the SPI memory
// have to do everything in 64K blocks - this is the unit in which the SPI memory
// is getting reset
//-----------------------------------------------------------------------------
  int const segment_size(0x10000); // the SPI memory is cleared in 64K segments

  std::vector<char> fileData(fsize);
  file.read(fileData.data(), fsize);
  if (!file) return -1;

  int n64k_segments = (fsize-1)/0x10000 + 1;

  int nb_read_tot(0);
  int first_addr    = image->offset;

  for (int iseg=0; iseg<n64k_segments; ++iseg) {
    int segment_offset = segment_size*iseg;
    int nb_to_read     = segment_size;
                                        // handle last, potentially shorter, block
    if (segment_offset+segment_size > fsize) {
      nb_to_read = fsize-segment_offset;
    }

    int spi_offset   = first_addr+segment_offset;
//-----------------------------------------------------------------------------
// read one segment, 64K or less
//-----------------------------------------------------------------------------
    std::vector<char> readback;
    rc = spi_read_segment(Dtc_i,Link,spi_offset,nb_to_read,readback);
    if (rc == 0) {
      nb_read_tot += nb_to_read;
                                        // compare to the original
      for (int i=0; i<nb_to_read; ++i) {
        if (readback[i] != fileData[segment_offset+i]) {
//-----------------------------------------------------------------------------
// if an inconsistency has been detected, print diagnostics and try to rewrite up to 3 times
//-----------------------------------------------------------------------------
          TLOG(TLVL_ERROR) << std::format("PCIE:{} Link:{} segment SPI offset:0x{:08x}\n",Dtc_i->PcieAddr(),Link,spi_offset)
                           << std::format(" segment number {:5d} byte at file offset:0x{:08x}",iseg,segment_offset+i)
                           << std::format(" is:0x{:2x}",(uint8_t)fileData[segment_offset+i])
                           << std::format(" readback byte offset:0x{:08x} value:0x{:x}",i, (uint8_t) readback[i]);
          rc = -2;
          break;
        }
      }
      if (rc == 0) {
        TLOG(TLVL_INFO) << std::format("PCIE:{} Link:{} segment:{:3} out of {:3}: validation succeeded",Dtc_i->PcieAddr(),Link,iseg,n64k_segments);
      }
    }
//-----------------------------------------------------------------------------
// if rc = 0, the readback and the validation succeeded, nothing more to do
//-----------------------------------------------------------------------------
    if (rc != 0) {
      TLOG(TLVL_ERROR) << std::format("validation failed, rc:{} segment:{}. BAIL OUT",rc,iseg);
      return rc;
    }
  }
  TLOG(TLVL_INFO) << std::format("-- END rc:{} nb_read_tot:{}",rc,nb_read_tot);
  return rc;
}

//-----------------------------------------------------------------------------
// mu2etrk@mu2edaq22:~/test_stand/daquser_001>cat ../monica_008/flash_nmap
// 0x10000
// 0x1010000
// 0x5000000
// works - can read back
// what's written is defined by the config file
//-----------------------------------------------------------------------------
int program_drac::spi_write_directory(trkdaq::DtcInterface* Dtc_i, int Link) {
  Dtc_i = programming_target(Dtc_i, Link);
  int catalogIndex=0;
  for (const auto& version : _drac_fw) {
    if (version.spi_file.load_flag <= 0) continue;
    if (catalogIndex >= 16 || version.index != catalogIndex || version.spi_file.offset < 0)
      return -1;
    ++catalogIndex;
  }
  if (catalogIndex == 0) return -1;
  int rc(0);
  if (Dtc_i == nullptr) Dtc_i = trkdaq::DtcInterface::Instance(-1);

  bool increment_address(false);

  TLOG(TLVL_INFO) << std::format("-- START: DTC:{} Link:{} clear SPI memory segment at 0x0000\n",Dtc_i->PcieAddr(),Link);

  rc = spi_clear_memory(Dtc_i,Link,0x0,0x10000);
  if (rc != 0) return rc;

  int nimages = 0;
  for (auto fw: _drac_fw) {
    if (fw.spi_file.load_flag > 0) ++nimages;
  }

  TLOG(TLVL_INFO) << std::format("write catalog, nimages:{}\n",nimages);

  std::vector<uint16_t> input;
                                           // 5 words
  input.push_back(SPI_WRITE_DIRECTORY);    // SPI flash read
  input.push_back(nimages & 0xFFFF);       // nimages LSB
  input.push_back(0               );       // nimages MSB
  input.push_back(0               );       // 2nd command : 2 16-bit words - uunused
  input.push_back(0               );       // unused
//-----------------------------------------------------------------------------
// to form the record to be written, assume that the image index is incremented monotonically
// directory catalog only contains offsets of the SPI images
//-----------------------------------------------------------------------------
  for (auto fw: _drac_fw) {
    if (fw.spi_file.load_flag > 0) {
      int offset = fw.spi_file.offset;
      input.push_back( offset        & 0xFFFF);                             //
      input.push_back((offset >> 16) & 0xFFFF);                             //
    }
  }

  auto roc  = DTCLib::DTC_Link_ID(Link);
  Dtc_i->fDtc->WriteROCBlock(roc,RREG,input,false,increment_address,100);
//-----------------------------------------------------------------------------
// hopefully, OK. lets see how it goes
//------------------------------------------------------------------------------
  int      wait_time_us(5000),  kMaxTries(5000);
  uint16_t u;

  int ntries = 0;
  while (((u = Dtc_i->fDtc->ReadROCRegister(roc,128,1000)) != 0x8000)  and (ntries < kMaxTries)) {
    std::this_thread::sleep_for(std::chrono::microseconds(wait_time_us));
    ntries++;
  };

  if (ntries > 0) {
    TLOG(TLVL_DEBUG+1) << std::format("DTC:{} ROC:{} polling R128 took {:4} extra attempts {} us each",
                                      Dtc_i->PcieAddr(),Link,ntries,wait_time_us);
    if (ntries == kMaxTries) {
      TLOG(TLVL_ERROR) << std::format("DTC:{} ROC:{} : R128 not ready after {:4} polls of {:4} us. BAIL OUT",
                                      Dtc_i->PcieAddr(),Link,ntries,wait_time_us);
      return -2;
    }
  }
                                        // read return code
  rc = Dtc_i->fDtc->ReadROCRegister(roc,132,1000);

  TLOG(TLVL_INFO) << std::format("-- END rc:{}\n",rc);

  return rc;
}

//-----------------------------------------------------------------------------
// returns 0 if OK and an error code otherwise
//-----------------------------------------------------------------------------
int program_drac::spi_write_record(trkdaq::DtcInterface* Dtc_i, int Link, int FirstAddr, int NWords, uint16_t* Data, int DebugMode) {
  Dtc_i = programming_target(Dtc_i, Link);
  if (NWords <= 0 || NWords > 512 || !Data || FirstAddr < 0 || uint64_t(FirstAddr)+2*uint64_t(NWords) > uint64_t(std::numeric_limits<int>::max())) return -1;
  if (Dtc_i == nullptr) Dtc_i = trkdaq::DtcInterface::Instance(-1);

  bool increment_address(false);
  bool done             (false);
  int  ntries           (0);

  TLOG(TLVL_DEBUG+1) << std::format("-- START: DTC:{} ROC:{}",Dtc_i->PcieAddr(),Link);

  auto roc  = DTCLib::DTC_Link_ID(Link);

  while (ntries < 3) {
                                              // 5 words
    std::vector<uint16_t> input;

    input.push_back(SPI_WRITE_RECORD);            // SPI clear
    input.push_back( FirstAddr        & 0xFFFF);  //
    input.push_back((FirstAddr >> 16) & 0xFFFF);  //
    input.push_back( NWords           & 0xFFFF);  //
    input.push_back((NWords    >> 16) & 0xFFFF);  //

    for (int i=0; i<NWords; ++i) {
      input.push_back(Data[i  ]);
    }

    //    Dtc_i->fDtc->WriteROCBlock(roc,RREG,input,true,increment_address,100);
    Dtc_i->fDtc->WriteROCBlock(roc,RREG,input,false,increment_address,100);
    ntries += 1;
//-----------------------------------------------------------------------------
// the suspicion is that sometimes the ROC doesn't recieve all data
// potential workaround: read ROC register 0 multiple times, till the read succeeds
// that provides "fake data" the ROC needs to complete reading of the missing part of
// the data
//-----------------------------------------------------------------------------
    bool     r01_ok(false);
    int      n_r01_reads(0);
    uint16_t r01(0);

    while ((not r01_ok) and (n_r01_reads < 1)) {
      try {
        n_r01_reads += 1;
        r01 = Dtc_i->fDtc->ReadROCRegister(roc,0,1000);
        if (r01 == 0x1234) {
                                        // success
          r01_ok = true;
          break;
        }
      }
      catch (...) {
//-----------------------------------------------------------------------------
// assume timeout and continue trying
//------------------------------------------------------------------------------
        TLOG(TLVL_ERROR) << std::format("n_r01_reads:{}, couldn't read register 0 afer writing NWords:{} to 0x:{:08x}",
                                        n_r01_reads,NWords,FirstAddr);
        if (DebugMode & 0x8) {
          Dtc_i->PrintBuffer(Data,NWords,FirstAddr);
        }
      }
    }

    if (r01_ok) {
      if (n_r01_reads == 1) {
                                        // succeeded from the first time, everything looks OK
        done = true;
        break;
      }
      else {
//-----------------------------------------------------------------------------
// first read failed, need to retry writing
//-----------------------------------------------------------------------------
        TLOG(TLVL_WARNING) << std::format("success after n_r01_reads:{}, retrying write to offset:0x{:08x}",
                                          n_r01_reads,FirstAddr);
      }
    }
    else  {
//-----------------------------------------------------------------------------
// read not successful , don't know what to do, bail out
//-----------------------------------------------------------------------------
      TLOG(TLVL_ERROR) << std::format("after nreads:{} r01:0x{:04x}. BAIL OUT",n_r01_reads,r01);
      return -2;
    }
//-----------------------------------------------------------------------------
// read of register 0 succeeded, try to write again
//-----------------------------------------------------------------------------
  }

  if (not done) {
                                        // tried to write record 5 times, didn't succeed, bail out
    TLOG(TLVL_ERROR) << std::format("couldn't read register 0. BAIL OUT");
    return -2;
  }

  int      wait_time_us(2000),  kMaxTries(1000);
  uint16_t u;

  ntries = 0;
  while (((u = Dtc_i->fDtc->ReadROCRegister(roc,128,1000)) != 0x8000)  and (ntries < kMaxTries)) {
    std::this_thread::sleep_for(std::chrono::microseconds(wait_time_us));
    ntries++;
  };

  if (ntries > 0) {
    TLOG(TLVL_DEBUG+1) << std::format("DTC:{} ROC:{} polling R128 took {:4} extra attempts {} us each",
                                      Dtc_i->PcieAddr(),Link,ntries,wait_time_us);
    if (ntries == kMaxTries) {
      TLOG(TLVL_ERROR) << std::format("DTC:{} ROC:{} : R128 not ready after {:4} polls. BAIL OUT", Dtc_i->PcieAddr(),Link,ntries);
      return -2;
    }
  }

  int rc = Dtc_i->fDtc->ReadROCRegister(roc,132,1000);

  if (rc != 0) {
    std::cout << std::format("[{}:{}] ERROR writing NWords:{} to SPI address 0x{:08x}, rc=:{}\n",
                             __func__,__LINE__,NWords,FirstAddr,rc);
    if (DebugMode & 0x2) {
      Dtc_i->PrintBuffer(Data,NWords,FirstAddr);
    }
  }

  TLOG(TLVL_DEBUG+1) << std::format("-- END: rc:{}",rc);
  return rc;
}

//-----------------------------------------------------------------------------
// write a single 64K byte segment (or less) . Return in case of an error
//-----------------------------------------------------------------------------
int program_drac::spi_write_segment(trkdaq::DtcInterface* Dtc_i, int Link, const char* Data, int NBytes, int SpiOffset, int DebugMode) {
  Dtc_i = programming_target(Dtc_i, Link);
  if (NBytes <= 0 || NBytes > 0x10000 || !Data || SpiOffset < 0 || uint64_t(SpiOffset)+uint64_t(NBytes) > uint64_t(std::numeric_limits<int>::max())) return -1;
  const int record_size (0x400);   // write 1Kbytes blocks
  int rc(0);

  TLOG(TLVL_DEBUG) << std::format("START: DTC:{} Link:{} writing NBytes:{} starting from offset 0x:{:08x} in SPI memory\n",
                                  Dtc_i->PcieAddr(),Link,NBytes,SpiOffset);
//-----------------------------------------------------------------------------
// can write only 1024 bytes (512 shorts) at a time
//-----------------------------------------------------------------------------
  int first_addr     = SpiOffset;
  int nb_written     = 0;

  bool done(false);
  while (not done) {
    int nb      = record_size;         // however, write in 2-byte words
    int nb_left = NBytes-nb_written;   // n bytes left to write within the segment
    if (nb_left < record_size) nb  = nb_left;

    if (nb > 0) {
//-----------------------------------------------------------------------------
// write next record
//-----------------------------------------------------------------------------
      TLOG(TLVL_DEBUG+1) << std::format(": nb:{:4d} first_addr:0x{:08x}",nb,first_addr)
                         << std::format(" nb_written:{:8d}\n",nb_written);

      // std::this_thread::sleep_for(std::chrono::microseconds(1000));
      int spi_offset = first_addr+nb_written;             // offset in bytes
      std::vector<uint16_t> words((nb+1)/2, 0xffff);
      for (int i=0; i<nb; ++i) {
        const uint16_t byte = static_cast<unsigned char>(Data[nb_written+i]);
        const int shift = 8*(i%2);
        words[i/2] = (words[i/2] & ~(0xffu << shift)) | (byte << shift);
      }
      rc = spi_write_record(Dtc_i,Link,spi_offset,words.size(),words.data(),DebugMode);

      std::this_thread::sleep_for(std::chrono::microseconds(1000));
      if (rc != 0) {
        TLOG(TLVL_ERROR) << std::format("ERROR to write to SPI offset:0x{:08x} . BAIL OUT\n",spi_offset);
        return rc;
      }
//-----------------------------------------------------------------------------
// write succeeded
//-----------------------------------------------------------------------------
      nb_written += nb;
    }
    else {
      done = true;
    }
  }
  TLOG(TLVL_INFO) << std::format("END rc:{} written_bytes:0x{:05x} starting_offset:0x{:08x}\n",rc,nb_written,SpiOffset);
  return rc;
}

//-----------------------------------------------------------------------------
// attn: not compete yet!
//
//  /home/mu2etrk/test_stand/spi_files:
//  drwxr-x---  2 mu2etrk mu2e    4096 Aug 26 16:20 .
//  drwxr-xr-x 60 mu2etrk mu2e    4096 Aug 20 16:47 ..
//  -rw-r-----  1 mu2etrk mu2e 9524032 Aug 20 16:47 GoldenV10.spi
//  -rw-r-----  1 mu2etrk mu2e 9480352 Aug 20 16:47 ROC.spi
//  -rw-r-----  1 mu2etrk mu2e   82272 Aug 20 16:48 ROCV12-3_stage3init.bin
//  -rw-r-----  1 mu2etrk mu2e 9480352 Aug 20 16:47 ROCV12.spi
//  -rw-r-----  1 mu2etrk mu2e 9480352 Aug 20 16:47 ROCV13.spi
//
// Index       : index in the spi_data table of the image to load
// TestMode = 1: don't write to memory, just clear the memory and count
// NWrites     : number of records to write, -1: write all
//-----------------------------------------------------------------------------
int program_drac::spi_write_image(trkdaq::DtcInterface* Dtc_i, int Link, const ImageData_t* Image, int DebugMode) {
  Dtc_i = programming_target(Dtc_i, Link);
  if (!Image) return -1;
  check_image(Image->fn, Image->offset);
  int rc = 0;
//-----------------------------------------------------------------------------
// open input file and determine its size
//-----------------------------------------------------------------------------
  TLOG(TLVL_INFO) << std::format("-- START load image to DTC:{} link:{} from Image->fn:{} offset:0x{:08x}\n",
                                 Dtc_i->PcieAddr(),Link,Image->fn,Image->offset);

  std::ifstream file(Image->fn, std::ios::binary);
  if (not file.is_open()) {
    TLOG(TLVL_ERROR) << std::format("failed to open file:{} . BAIL OUT\n",Image->fn);
    return -1;
  }

  file.seekg(0, std::ios::end);
  const auto fileSize = file.tellg();
  if (fileSize <= 0 || fileSize > std::numeric_limits<int>::max()) return -1;
  int fsize = static_cast<int>(fileSize);
  file.seekg(0, std::ios::beg);
//-----------------------------------------------------------------------------
// read the input file and upload its content to the SPI memory
// have to do everything in 64K blocks - this is the unit in which the SPI memory
// is getting reset
//-----------------------------------------------------------------------------
  int const segment_size(0x10000); // the SPI memory is cleared in 64K segments

  std::vector<char> fileData(fsize);
  file.read(fileData.data(), fsize);
  if (!file) return -1;

  int n64k_segments = (fsize-1)/0x10000 + 1;

  int first_addr    = Image->offset;
  int nb_written    = 0;

  TLOG(TLVL_DEBUG) << std::format("[{}:{}]: n64k_segments:{} first_addr:0x{:08x}\n",__func__,__LINE__,n64k_segments,first_addr);

  for (int ib=0; ib<n64k_segments; ++ib) {
    int segment_offset = segment_size*ib;
    int nb_to_write    = segment_size;
                                        // handle last, potentially shorter, block
    if (segment_offset+segment_size > fsize) {
      nb_to_write = fsize-segment_offset;
    }

    int spi_offset   = first_addr+segment_offset;
    const char* data = fileData.data()+segment_offset;
//-----------------------------------------------------------------------------
// writing one segment, 64K or less
//-----------------------------------------------------------------------------
    int attempt(1);
    while (attempt < 4) {
//-----------------------------------------------------------------------------
// for the first time, the memory is reset by the microcontroller, when re-trying,
// need to clear the memory here
//-----------------------------------------------------------------------------
      if (DebugMode & 0x1) {
        std::cout << std::format("{}:{}: attempt {} to write segment {:d}\n",__func__,__LINE__,attempt, ib);
      }

      rc = spi_clear_memory(Dtc_i,Link,spi_offset,0x10000,DebugMode);
      if (rc != 0) return rc;
      rc = spi_write_segment(Dtc_i,Link,data,nb_to_write,spi_offset,DebugMode);
      if (rc != 0) {
                                        // detected error
        TLOG(TLVL_ERROR) << std::format("{}:{}  attempt:{} ERROR writing segment number {:4d} rc:{}\n",
                                        __func__,__LINE__,attempt,ib,rc);
      }
      else {
//-----------------------------------------------------------------------------
// segment has been written w/o detected errors, validate if requested
//-----------------------------------------------------------------------------
        // compare to the original
        if (DebugMode & 0x10000) {
          TLOG(TLVL_DEBUG) << std::format("{}:{}: validation: read segment:{} back\n",__func__,__LINE__,ib);

          std::vector<char> readback;
          rc = spi_read_segment(Dtc_i,Link,spi_offset,nb_to_write,readback,DebugMode);
          if (rc == 0) {
            for (int i=0; i<nb_to_write; ++i) {
              if (readback[i] != fileData[segment_offset+i]) {
//-----------------------------------------------------------------------------
// if an inconsistency has been detected, print diagnostics and try to rewrite up to 3 times
//-----------------------------------------------------------------------------
                int ic1 = readback[i] & 0xff;
                int ic2 = fileData[segment_offset+i] &0xff;

                std::cout << std::format("[{}:{}] ERROR validating written segment starting from SPI offset:0x{:x}\n",
                                         __func__,__LINE__,spi_offset)
                          << std::format("i:{} readback[i]:0x{:x} fileData[segment_offset+i]:0x{:x}\n",
                                         i,ic1,ic2);
                Dtc_i->PrintBuffer((void*) readback.data(),64,0);
                Dtc_i->PrintBuffer((void*) (fileData.data()+segment_offset),64,0);
                rc = -2;
                break;
              }
            }
          }
        }
        if (DebugMode & 0x1) {
          std::cout << std::format("{:s}:{:d}: end validation, rc:{}\n",__func__,__LINE__,rc);
        }
      }
//-----------------------------------------------------------------------------
// if rc = 0, the readback and the validation succeeded, nothing more to do
//-----------------------------------------------------------------------------
      if (rc == 0) break;
      attempt++;
    }
//-----------------------------------------------------------------------------
// 3 attempts to rewrite have failed
//-----------------------------------------------------------------------------
    if (rc != 0) {
      printf("[%s] 3 attempts to write segment failed, rc: %i. BAIL OUT\n",__func__,rc);
      return rc;
    }
    nb_written += nb_to_write;
  }

  std::cout << std::format("[{}:{}]: END: rc:{} total nb_writen:{}\n",__func__,__LINE__,rc,nb_written);
  return rc;
}


//-----------------------------------------------------------------------------
// upload images into SPI memory, but do not program the FPGA
// keep the programming step - program_iap_w_index - separate, as it needs validation
// Version defines one or two images to uploas
//-----------------------------------------------------------------------------
int program_drac::spi_write_version(trkdaq::DtcInterface* Dtc_i, int Link, const std::string& Version, int DebugMode) {
  Dtc_i = programming_target(Dtc_i, Link);
  int rc(0);
                                        // find firmware version
  const FwVersion_t* fw = get_version(Version);

  if (fw == nullptr) {
    TLOG(TLVL_ERROR) << std::format("fw version:{} is not defined, BAIL OUT.\n",Version);
    return -1;
  }

  TLOG(TLVL_INFO) << std::format("-- START programming DTC{} link:{} with the ROC fw version:{}\n",Dtc_i->PcieAddr(),Link,Version);
//-----------------------------------------------------------------------------
// firmware to be uploaded found, proceed with the upload.
// 1. initialize the DTC
//-----------------------------------------------------------------------------
  // int  link_mask = (1 << 4*Link);
  //  bool skip_init(false);

  trkdaq::DtcInterface* dtc_i = Dtc_i;
  if (dtc_i == nullptr) {
    dtc_i = trkdaq::DtcInterface::Instance(-1);
  }

  check_image(fw->spi_file.fn, fw->spi_file.offset);
  if (fw->bin_file.load_flag > 0) check_image(fw->bin_file.fn, fw->bin_file.offset);
  gSystem->Setenv("DTCLIB_DTC",Form("%i",dtc_i->PcieAddr()));
//-----------------------------------------------------------------------------
// 2. spi directory has always to be mapped in full ,
//    as the directory doesn't need to be rewritten every time, make it a separate step
//    this is why the next line is commented out
//-----------------------------------------------------------------------------
//  spi_write_directory(dtc_i,Link);
                                        // upload the .spi image

  rc = spi_write_image(dtc_i,Link,&fw->spi_file,DebugMode);

  if (rc != 0) {
    return rc;
  }

                                        // upload the .bin image, if needed
  if (fw->bin_file.load_flag > 0) {
    rc = spi_write_image(dtc_i,Link,&fw->bin_file,DebugMode);
    if (rc != 0) return rc;
  }
                                        // activate the image as a separate step after validating
                                        // the memory, thus the following is commented out
  // spi_program_iap_w_index(dtc_i,Link,fw->spi_file.index);

  TLOG(TLVL_INFO) << std::format("-- END rc:{}\n",rc);
  return rc;
}
//-----------------------------------------------------------------------------
int program_drac::spi_validate_version(trkdaq::DtcInterface* Dtc_i, int Link, const std::string& Version, const std::string& Type, int DebugMode) {
  Dtc_i = programming_target(Dtc_i, Link);
  if (Type != "" && Type != "spi" && Type != "bin") return -1;
  // find firmware version
  int rc(0);

  const FwVersion_t* fw = get_version(Version);

  if (fw == nullptr) {
    TLOG(TLVL_ERROR) << std::format("fw version:{} is not defined, BAIL OUT.\n",Version);
    return -1;
  }
  TLOG(TLVL_INFO) << std::format("-- START validating version:{} type:{}\n",Version,Type);
//-----------------------------------------------------------------------------
// firmware to be uploaded found, proceed with the upload.
// 1. initialize the DTC
//-----------------------------------------------------------------------------
  trkdaq::DtcInterface* dtc_i = Dtc_i;
  if (dtc_i == nullptr) {
    dtc_i = trkdaq::DtcInterface::Instance(-1);
  }

  gSystem->Setenv("DTCLIB_DTC",Form("%i",dtc_i->PcieAddr()));

                                        // validate the .spi image
  if (Type != "bin") {
    rc = spi_validate_image(dtc_i,Link,Version,"spi",DebugMode);

                                        // don't proceed in case of any failure
    if (rc != 0) return rc;
  }
                                        // validate the .bin image, if needed
  if ((Type != "spi") and (fw->bin_file.load_flag > 0)) {
    rc = spi_validate_image(dtc_i,Link,Version,"bin",DebugMode);
    if (rc != 0) return rc;
  }

  TLOG(TLVL_INFO) << std::format("-- END rc:{}\n",rc);
  return rc;
}

//-----------------------------------------------------------------------------
int program_drac::spi_print_digi_id(const std::vector<uint16_t>& Dat) {
  int nw = Dat.size();
  if (nw != 22) {
    TLOG(TLVL_ERROR) << std::format("nw:{} not 22. BAIL OUT",nw);
    return -1;
  }

  uint32_t fpga_id = ((uint32_t) Dat[3]) | (((uint32_t) Dat[4]) << 16);

  std::ostringstream oss;
  for (int i=21; i>=6; --i) {
    oss << std::format("{:02x}",(uint8_t) (Dat[i] & 0xff));
  }

  TLOG(TLVL_INFO) << std::format("cal/hv:{} op:{} err_code:0x{:04x}",Dat[0],Dat[1],Dat[2]);
  TLOG(TLVL_INFO) << std::format("FPGA id:0x{:08x} revision:0x{:04x} serial number:0x{}",fpga_id,Dat[5],oss.str());

  return 0;
}

//-----------------------------------------------------------------------------
int program_drac::spi_print_digi_info(const std::vector<uint16_t>& Dat) {
  int nw = Dat.size();
  if (nw != 153) {
    TLOG(TLVL_ERROR) << std::format("nw:{} not 153. BAIL OUT",nw);
    return -1;
  }
                                        // for using it as a reference
  int cal_hv   = Dat[0];
  int op       = Dat[1];
  int err_code = Dat[2];

  std::ostringstream silicon_signature;
  for (int i=6; i>=3; --i) {
    silicon_signature << std::format("{:02x}",(uint8_t) (Dat[i] & 0xff));
  }

  std::ostringstream design_name;
  for (int i=36; i>=7; --i) {
    design_name << std::format("{:02x}",(uint8_t) (Dat[i] & 0xff));
  }

  uint32_t checksum = ((uint32_t) Dat[37]) | (((uint32_t) Dat[38]) << 16);
  int      design_ver = Dat[39];
  int      back_level = Dat[40];

  std::ostringstream security_lock;
  for (int i=136; i>=121; --i) {
    security_lock << std::format("{:02x}",(uint8_t) (Dat[i] & 0xff));
  }
  std::ostringstream serial_number;
  for (int i=152; i>=137; --i) {
    serial_number << std::format("{:02x}",(uint8_t) (Dat[i] & 0xff));
  }

  TLOG(TLVL_INFO) << std::format("cal/hv:{} op:{} err_code:0x{:04x}",cal_hv,op,err_code);
  TLOG(TLVL_INFO) << std::format("silicon signature:0x{}",silicon_signature.str());
  TLOG(TLVL_INFO) << std::format("design_name      :0x{}",design_name.str());
  TLOG(TLVL_INFO) << std::format("design_ver       :  {}",design_ver);
  TLOG(TLVL_INFO) << std::format("back_level       :  {}",back_level);
  TLOG(TLVL_INFO) << std::format("checksum         :0x{:08x}",checksum);
  TLOG(TLVL_INFO) << std::format("security lock    :0x{}",security_lock.str());
  TLOG(TLVL_INFO) << std::format("serial number    :0x{}",serial_number.str());

  return 0;
}

//-----------------------------------------------------------------------------
// CalHV: 0:CAL, 1:HV
// DebugMode: bit 2: print hex
//            bit 4: print formatted
//-----------------------------------------------------------------------------
int program_drac::spi_read_digi_id(trkdaq::DtcInterface* Dtc_i, int Link, uint16_t CalHV, int DebugMode) {
  Dtc_i = programming_target(Dtc_i, Link);
  if (CalHV > 1) return -1;
  auto roc = DTCLib::DTC_Link_ID(Link);
  std::vector<uint16_t> input{CalHV, READ_DIGI_ID};
  Dtc_i->fDtc->WriteROCBlock(roc, REG_DIGI, input, false, false, 1000);
  uint16_t status = 0;
  if (wait_register(Dtc_i, Link, 128, 0xffff, 0x8000, 1000, 5000, 10000, status) != 0) return -2;
  if (wait_register(Dtc_i, Link, 129, 0xffff, 26, 1000, 5000, 10000, status) != 0) return -3;
  std::vector<uint16_t> res;
  Dtc_i->fDtc->ReadROCBlock(res, roc, REG_DIGI, 22, false, 1000);
  if (res.size() != 22 || res[0] != CalHV || res[1] != READ_DIGI_ID || res[2] != 0) return -4;
  if (DebugMode & 0x2) Dtc_i->PrintBuffer(res.data(), res.size());
  if (DebugMode & 0x4) spi_print_digi_id(res);
  return 22;
}

//-----------------------------------------------------------------------------
int program_drac::spi_read_digi_info(trkdaq::DtcInterface* Dtc_i, int Link, uint16_t CalHV, int DelayUs, int DebugMode) {
  Dtc_i = programming_target(Dtc_i, Link);
  if (CalHV > 1) return -1;
  auto roc = DTCLib::DTC_Link_ID(Link);
  std::vector<uint16_t> input{CalHV, READ_DIGI_INFO};
  Dtc_i->fDtc->WriteROCBlock(roc, REG_DIGI, input, false, false, 1000);
  uint16_t status = 0;
  if (wait_register(Dtc_i, Link, 128, 0xffff, 0x8000, 1000, 5000, 10000, status) != 0) return -2;
  if (wait_register(Dtc_i, Link, 129, 0xffff, 157, 1000, 5000, 10000, status) != 0) return -3;
  std::vector<uint16_t> res;
  Dtc_i->fDtc->ReadROCBlock(res, roc, REG_DIGI, 153, false, 1000);
  if (res.size() != 153 || res[0] != CalHV || res[1] != READ_DIGI_INFO || res[2] != 0) return -4;
  if (DebugMode & 0x2) Dtc_i->PrintBuffer(res.data(), res.size());
  if (DebugMode & 0x4) spi_print_digi_info(res);
  return 153;
}

//-----------------------------------------------------------------------------
// Cal:0  HV:1
// Fn: "CalV6.dat" or "HVV6.dat" , or smth similar
//-----------------------------------------------------------------------------
int program_drac::spi_program_digis(trkdaq::DtcInterface* Dtc_i, int Link, const std::string& Fn, int DebugMode) {
  Dtc_i = programming_target(Dtc_i, Link);
  const std::filesystem::path name(Fn);
  if (name.empty() || name.has_parent_path() || name.extension() != ".dat") return -1;
  std::string prefix=Fn.substr(0,3);
  boost::algorithm::to_upper(prefix);
  uint16_t calHV=0;
  if (prefix == "CAL") calHV=0;
  else if (prefix.starts_with("HV")) calHV=1;
  else return -1; // Never silently route an unknown filename to HV.
  const auto path = std::filesystem::path("/home/mu2etrk/test_stand/spi_files") / name;
  check_image(path.string(),0);
  std::ifstream file(path,std::ios::binary | std::ios::ate);
  const auto size=file.tellg();
  if (size <= 0 || size > std::numeric_limits<int>::max()) return -1;
  std::vector<char> fileData(static_cast<size_t>(size));
  file.seekg(0);
  if (!file.read(fileData.data(),fileData.size())) return -1;
  const auto roc=DTCLib::DTC_Link_ID(Link);
  std::vector<uint16_t> input{calHV,PROGRAM_DIGI};
  TLOG(TLVL_INFO) << std::format("DTC:{} ROC:{} programming DIGI target:{} file:{} bytes:{}",
                                 Dtc_i->PcieAddr(),Link,calHV,path.string(),fileData.size());
  Dtc_i->fDtc->WriteROCBlock(roc,REG_DIGI,input,false,false,1000);
  // More than one request per file byte indicates a stalled/repeating transfer.
  for (size_t pass=0;pass<=fileData.size();++pass) {
    uint16_t status=0;
    if (wait_register(Dtc_i,Link,128,0xffff,0x8000,1000,5000,10000,status) != 0) return -2;
    if (wait_register(Dtc_i,Link,129,0x1000,0,1000,5000,10000,status) != 0) return -3;
    if (wait_register(Dtc_i,Link,129,0xffff,8,1000,5000,10000,status) != 0) return -4;
    std::vector<uint16_t> res;
    Dtc_i->fDtc->ReadROCBlock(res,roc,REG_DIGI,4,false,1000);
    if (res.size() != 4) return -5;
    const uint32_t offset=uint32_t(res[0]) | (uint32_t(res[1])<<16);
    const uint32_t count=uint32_t(res[2]) | (uint32_t(res[3])<<16);
    if (offset == 0 && count == 0) return 0;
    if (count == 0 || offset > fileData.size() || count > fileData.size()-offset) return -5;
    input.assign((size_t(count)+1)/2,0);
    for (size_t i=0;i<count;++i)
      input[i/2] |= uint16_t(static_cast<unsigned char>(fileData[size_t(offset)+i])) << (8*(i%2));
    TLOG(TLVL_DEBUG+1) << std::format("DTC:{} ROC:{} pass:{} offset:0x{:08x} bytes:{}",
                                     Dtc_i->PcieAddr(),Link,pass,offset,count);
    if (DebugMode & 0x8) Dtc_i->PrintBuffer(input.data(),input.size());
    Dtc_i->fDtc->WriteROCBlock(roc,REG_DIGI,input,false,false,1000);
  }
  TLOG(TLVL_ERROR) << "DIGI transfer exceeded request budget without completion";
  return -6;
}

//-----------------------------------------------------------------------------
int program_drac::test_spi_write_record(trkdaq::DtcInterface* Dtc_i, int Link, int FirstAddr, int NWords, int DelayUs) {
  Dtc_i = programming_target(Dtc_i, Link);
  int rc(0);

  trkdaq::DtcInterface* dtc_i = Dtc_i;
  if (dtc_i == nullptr) {
    dtc_i = trkdaq::DtcInterface::Instance(-1);
    if (dtc_i == nullptr) {
      TLOG(TLVL_ERROR) << std::format("dtc_i not defined, EXIT\n");
      return -1;
    }
  }

  std::vector<uint16_t> dat;
  for (int i=0; i<NWords; i++) dat.push_back(i+3);

  // memory is organized in 4K (0x1000) sectors,
  // according to ChatGPT, a sector corresponding to the record being written needs to be erased
  // assume that the 'FirstAddr' starts on a 4K boundary and for testing purposes clear just one sector

  // spi_clear_memory(Dtc_i, Link, FirstAddr,0x1000);
  // rc = spi_clear_memory(Dtc_i, Link, FirstAddr,0x10000);

  if (rc != 0) {
                                        // the diagnostics printed in the function where the error was first detected
    return -1;
  }

  if (DelayUs != 0) {
    std::this_thread::sleep_for(std::chrono::microseconds(DelayUs));
  }

  rc = spi_write_record(Dtc_i, Link, FirstAddr, NWords, dat.data());

  TLOG(TLVL_INFO) << std::format("after spi_write_record rc:{}\n",rc);

  dat.clear();

  if (DelayUs != 0) {
    std::this_thread::sleep_for(std::chrono::microseconds(DelayUs));
  }

  // dat.reserve(NWords);
  // spi_read_record(Dtc_i,Link,FirstAddr,NWords,dat.data(),0x3);

  return 0;
}


//-----------------------------------------------------------------------------
int program_drac::test_spi_read_record(trkdaq::DtcInterface* Dtc_i, int Link, int FirstAddr, int NWords) {
  Dtc_i = programming_target(Dtc_i, Link);

  trkdaq::DtcInterface* dtc_i = Dtc_i;
  if (dtc_i == nullptr) {
    dtc_i = trkdaq::DtcInterface::Instance(-1);
    if (dtc_i == nullptr) {
      TLOG(TLVL_ERROR) << std::format("dtc_i not defined, EXIT\n");
    }
  }

  std::vector<uint16_t> dat(NWords);

  spi_read_record(dtc_i,Link,FirstAddr,NWords,dat.data(),0x3);

  return 0;
}

//-----------------------------------------------------------------------------
void program_drac::test_read_file(const char* Fn) {
//-----------------------------------------------------------------------------
// read the input file in memory  -- 10 MBytes is nothing
//-----------------------------------------------------------------------------
  std::ifstream file(Fn, std::ios::binary);

  file.seekg(0, std::ios::end);
  const auto fileSize = file.tellg();
  if (fileSize <= 0 || fileSize > std::numeric_limits<int>::max()) throw std::runtime_error("Invalid or incomplete image file");
  int fsize = static_cast<int>(fileSize);

  std::cout << "fsize:" << fsize << std::endl;
  file.seekg(0, std::ios::beg);

  std::vector<char> fileData(fsize);
  file.read(fileData.data(), fsize);
  if (!file) throw std::runtime_error("Invalid or incomplete image file");
  file.close();
}
