///////////////////////////////////////////////////////////////////////////////
// 1. spi_write_directory
// 2. spi_clear - done by spi_load_image .. Make sure the right address is used
// 3. spi_program_aip_w_index for the .spi file
// 4. wait for the upload to complete
// this is how the offsets should be:
//
// 0x0010000      : spi#0 ("Golden", no microcontroller code - why ?)
// 0x1010000      : spi#1
// 0x2010000      : spi#2
// 0x3010000      : spi#3
// 0x4010000      : spi#4
//
// 0x5000000      : bin#1 (no bin#0)
// 0x5040000      : bin#2
// 0x5080000      : bin#2
// 0x50c0000      : bin#2
// 0x6000000      : bin#2
//-----------------------------------------------------------------------------
#ifndef __program_drac_hh__
#define __program_drac_hh__

#include <iostream>
#include <vector>

#include "artdaq-core-mu2e/Overlays/DTC_Types/DTC_Link_ID.h"
#include "dtcInterfaceLib/DTC.h"
#include "otsdaq-mu2e-tracker/Ui/DtcInterface.hh"
#include "TString.h"
#include "TSystem.h"

class program_drac {
public:
                                        // registers
  enum {
    REG_STATUS            = 132,

    RREG                  = 384,
    REG_DIGI              = 385,
                                        // commands
    SPI_CLEAR             = 3,
    PROGRAM_IAP_W_INDEX   = 4,
    PROGRAM_IAP_W_ADDRESS = 5,
    IAP_AUTO_UPDATE       = 6,
    SPI_FLASH_READ        = 7,
    SPI_WRITE_RECORD      = 8,
    SPI_WRITE_DIRECTORY   = 9,
  };
                                        // digi programming commands
  enum {
    READ_DIGI_INFO        = 0,
    READ_DIGI_ID          = 1,
    ERASE_DIGI            = 2,          // unused
    PROGRAM_DIGI          = 3,
  };
                                        // description of the ROC image
  struct ImageData_t {
    int         load_flag;
    int         offset;
    std::string fn;
  };

  struct FwVersion_t {
    std::string name;
    int         index;                  // in spi_offsets and bin_offsets
    ImageData_t spi_file;
    ImageData_t bin_file;
    // Required for verified activation; catalog values are hexadecimal strings.
    std::string expected_design_version;
    std::string expected_usercode;
    std::string expected_git_commit; // required when bin_file.load_flag > 0

  };

  int kSPI_CLEAR_SLEEP_US;

  std::vector<FwVersion_t> _drac_fw;
//-----------------------------------------------------------------------------
// functions
//-----------------------------------------------------------------------------
  program_drac(const char* ConfigFile = "", bool PrintConfig = false);

  const program_drac::ImageData_t* get_image_data(const std::string& Version, const std::string Spi="spi");
  const program_drac::FwVersion_t* get_version   (const std::string& Version);

  int  spi_clear_memory         (trkdaq::DtcInterface* Dtc_i, int Link, int Offset, int NBytes, int DebugMode = 0);

  int  spi_print_digi_id        (const std::vector<uint16_t>& Dat);
  int  spi_print_digi_info      (const std::vector<uint16_t>& Dat);

                                        // Fn: "CalVX.dat" or "HVVX.dat", the name defines the FPGA

  int  spi_program_digis        (trkdaq::DtcInterface* Dtc_i, int Link, const std::string& Fn, int DebugMode = 0);

                                        // the next three functions do the FPGA programming, use the first one

  int  spi_program_roc          (trkdaq::DtcInterface* Dtc_i, int Link, const std::string& Version);
  void spi_program_iap_w_index  (trkdaq::DtcInterface* Dtc_i, int Link, int Index);
  void spi_program_iap_w_address(trkdaq::DtcInterface* Dtc_i, int Link, int ImageStartAddr);

  int  spi_read_digi_id         (trkdaq::DtcInterface* Dtc_i, int Link, uint16_t CalHV, int DebugMode = 0);
  int  spi_read_digi_info       (trkdaq::DtcInterface* Dtc_i, int Link, uint16_t CalHV, int DelayUs = 0, int DebugMode = 0);
  int  spi_read_record          (trkdaq::DtcInterface* Dtc_i, int Link, uint32_t SpiOffset, int NWords, uint16_t*          Res, int DebugMode = 0);
  int  spi_read_segment         (trkdaq::DtcInterface* Dtc_i, int Link, uint32_t SpiOffset, int NBytes, std::vector<char>& Res, int DebugMode = 0);
//-----------------------------------------------------------------------------
// restricted functionality tool
// reprogram ROC w/o rewriting the directory (saving NVM write cycles)
// spi_write_version('goldenxx')+spi_write_version('rocxx')+spi_program_roc('rocxx')
// uses images at indices 0 and 1
// 'LinkMask' is in a hex format, so 'all links' = 0x111111
// don't do anything by default, require the mask to be specified explicitly
//-----------------------------------------------------------------------------
  int  spi_reprogram_roc        (trkdaq::DtcInterface* Dtc_i, int LinkMask = 0x0);

  int  spi_validate_segment     (trkdaq::DtcInterface* Dtc_i, int Link, const program_drac::ImageData_t* SpiData, int Segment, int DebugMode = 0);
  int  spi_validate_image       (trkdaq::DtcInterface* Dtc_i, int Link, const std::string& Version, const std::string& Type, int DebugMode = 0);

                                        // if Type = "", validate both images, otherwise use the file extention: "spi" or "bin"

  int  spi_validate_version     (trkdaq::DtcInterface* Dtc_i, int Link, const std::string& Version, const std::string& Type = "", int DebugMode = 0);

  int  spi_write_directory      (trkdaq::DtcInterface* Dtc_i, int Link);

  int  spi_write_record         (trkdaq::DtcInterface* Dtc_i, int Link, int FirstAddr   , int NWords, uint16_t* Data, int DebugMode = 0);
  int  spi_write_segment        (trkdaq::DtcInterface* Dtc_i, int Link, const char* Data, int NBytes, int SpiOffset , int DebugMode = 0);
  int  spi_write_image          (trkdaq::DtcInterface* Dtc_i, int Link, const program_drac::ImageData_t* SpiData, int DebugMode = 0x10000);

                                        // this one only uploads image to SPI memory, but doesn't program the FPGA

  int  spi_write_version        (trkdaq::DtcInterface* Dtc_i, int Link, const std::string& Version, int DebugMode = 0x10000);

                                        // tests

  void test_read_file           (const char* Fn);
  int  test_spi_write_record    (trkdaq::DtcInterface* Dtc_i, int Link, int FirstAddr, int NWords, int DelayUs = 0);
  int  test_spi_read_record     (trkdaq::DtcInterface* Dtc_i, int Link, int FirstAddr, int NWords);


};
#endif
