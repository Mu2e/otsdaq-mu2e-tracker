// Ed Callaghan
// Shared DTC ownership; callers must execute hardware operations sequentially
// March 2026

#include "otsdaq-mu2e-tracker/Ui/SharedDtcInterface.hh"

namespace trkdaq{
	SharedDtcInterface::SharedDtcInterface(DTCLib::DTC* dtc):
			_interface(std::make_unique<trkdaq::DtcInterface>(dtc)),
			_initialized(false){
		trkdaq::DtcInterface::DtcConfiguration_t dummy;
		dummy.fLinkMask = 0x111111;
		this->PostInitialize(dummy);
	}

	// initialize store of preconstructed instances
	std::map< void*, std::shared_ptr<SharedDtcInterface> > SharedDtcInterface::instances;

	std::shared_ptr<SharedDtcInterface>& SharedDtcInterface::Get(DTCLib::DTC* dtc){
		auto& instances = SharedDtcInterface::instances;
		void* address = static_cast<void*>(dtc);
		if (instances.count(address) < 1){
			instances[address] = std::make_shared<SharedDtcInterface>(dtc);
		}
		auto& rv = instances[address];
		return rv;
	}

	bool SharedDtcInterface::PostInitialize(const trkdaq::DtcInterface::DtcConfiguration_t& config){
		bool rv = false;
		if (!_initialized){
			_interface->PostInitialize(config);
			_initialized = true;
			rv = true;
		}
		return rv;
	}
} // namespace trkdaq
