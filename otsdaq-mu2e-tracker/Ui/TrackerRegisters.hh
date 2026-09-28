// Ed Callaghan
// Factor out registers on the ROC and Digi FPGAs
// June 2026

#ifndef __trkdaq_trackerregisters_hh__
#define __trkdaq_trackerregisters_hh__

// stl
#include <cstdint>

namespace trkdaq{
  using fpga_t = int;
  namespace fpga{
	namespace digi{
	  extern const fpga_t cal;
	  extern const fpga_t hv;
	}; // namespace digi
  }; // namespace fpgas
  namespace registers{
	using address_t = uint8_t;
	namespace roc{
	  extern const registers::address_t readout_configuration;
	  extern const registers::address_t internal_digi_reset;
	  extern const registers::address_t from_digi_fifo_reset;
	} //namespace roc
	namespace digi{
	  extern const registers::address_t channel_mask_lo;
	  extern const registers::address_t channel_mask_md;
	  extern const registers::address_t channel_mask_hi;
	} // namespace digi
  }; // namespace registers
} // namespace trkdaq

#endif
