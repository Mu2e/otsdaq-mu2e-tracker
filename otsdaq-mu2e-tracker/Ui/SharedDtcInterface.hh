// Ed Callaghan
// Shared DTC ownership; callers must execute hardware operations sequentially
// March 2026

#ifndef __trkdaq_shared_dtc_interface_hh__
#define __trkdaq_shared_dtc_interface_hh__

// stl
#include <map>

// otsdaq-mu2e-tracker
#include "otsdaq-mu2e-tracker/Ui/DtcInterface.hh"

namespace trkdaq{
	class SharedDtcInterface{
		public:
			// real constructor --- should hide, tbd
			SharedDtcInterface(DTCLib::DTC*);
			// lazy constructor
			static std::shared_ptr<SharedDtcInterface>& Get(DTCLib::DTC*);

			bool PostInitialize(const trkdaq::DtcInterface::DtcConfiguration_t&);

			// below are thin forwards automagically completed by variadic templates

			// basics
			template<typename... Args>
			uint16_t ReadROCRegister(Args&&... args);
			template<typename... Args>
			void WriteROCRegister(Args&&... args);
			template<typename... Args>
			int ResetLink(Args&&... args);
			template<typename... Args>
			int ResetDigis(Args&&... args);
			template<typename... Args>
			int RebootMcu(Args&&... args);

			// readout / lane configuration
			template<typename... Args>
			int InitReadoutROC(Args&&... args);
			template<typename... Args>
			int InitRocReadoutMode(Args&&... args);
			template<typename... Args>
			void SetRocLaneMask(Args&&... args);
			template<typename... Args>
			void SetRocNHitsPerLane(Args&&... args);
			template<typename... Args>
			int SetRocDelay(Args&&... args);
			template<typename... Args>
			int SetRocDigitizationWindow(Args&&... args);

			// digi register access
			template<typename... Args>
			int DigiRW(Args&&... args);
			template<typename... Args>
			int DigiRead(Args&&... args);
			template<typename... Args>
			int DigiWrite(Args&&... args);

			// block read
			template<typename... Args>
			int RocBlockRead(Args&&... args);

			// status
			template<typename... Args>
			int PrintRocStatus(Args&&... args);

			// identity
			template<typename... Args>
			int ReadPanelID(Args&&... args);
			template<typename... Args>
			roc_serial_t ReadSerialNumber(Args&&... args);

			// ControlROC paradigm
			template<typename... Args>
			int ReadSettings(Args&&... args);
			template<typename... Args>
			int PulserOn(Args&&... args);
			template<typename... Args>
			int PulserOff(Args&&... args);
			template<typename... Args>
			int Rates(Args&&... args);
			template<typename... Args>
			int Read(Args&&... args);
			template<typename... Args>
			int ReadDeviceID(Args&&... args);
			template<typename... Args>
			int ReadGitCommit(Args&&... args);
			template<typename... Args>
			int ReadIlp(Args&&... args);
			template<typename... Args>
			int GetKey (Args&&... args);
			template<typename... Args>
			int ReadSpi(Args&&... args);
			template<typename... Args>
			int MeasureThresholds(Args&&... args);
			template<typename... Args>
			int ReadThresholds(Args&&... args);
			template<typename... Args>
			int SetCalDac(Args&&... args);
			template<typename... Args>
			int SetThreshold(Args&&... args);

			// alignment
			template<typename... Args>
			int FindAlignment (Args&&... args);
			template<typename... Args>
			int FindAlignments(Args&&... args);

			// threshold-finding
			template<typename... Args>
			bool FindThreshold(Args&&... args);
			template<typename... Args>
			float ProgramAndQueryThreshold(Args&&... args);

			// ots api?
			template<typename... Args>
			std::vector<std::string> GetRocRegistersNames(Args&&... args);
			template<typename... Args>
			std::vector<uint32_t> GetRocRegisters(Args&&... args);
			template<typename... Args>
			std::vector<float> GetConvertedRocRegisters(Args&&... args);

			// non-conforming return-by-values
			template<typename... Args>
			std::string GetRocID(Args&&... args);
			template<typename... Args>
			std::string GetRocDesignInfo(Args&&... args);
			template<typename... Args>
			std::string GetRocFwGitCommit(Args&&... args);

		protected:
			// data members
			std::unique_ptr<trkdaq::DtcInterface> _interface;
			bool _initialized;

		private:
			// store of preconstructed instances
			static std::map< void*, std::shared_ptr<SharedDtcInterface> > instances;
	};

	template<typename... Args>
	uint16_t SharedDtcInterface::ReadROCRegister(Args&&... args){
		uint16_t rv = _interface->ReadROCRegister(std::forward<Args>(args)...);
		return rv;
	}

	template<typename... Args>
	void SharedDtcInterface::WriteROCRegister(Args&&... args){
		_interface->WriteROCRegister(std::forward<Args>(args)...);
	}

	template<typename... Args>
	int SharedDtcInterface::ResetLink(Args&&... args){
		int rv = _interface->ResetLink(std::forward<Args>(args)...);
		return rv;
	}

	template<typename... Args>
	int SharedDtcInterface::ResetDigis(Args&&... args){
		int rv = _interface->ResetDigis(std::forward<Args>(args)...);
		return rv;
	}

	template<typename... Args>
	int SharedDtcInterface::RebootMcu(Args&&... args){
		int rv = _interface->RebootMcu(std::forward<Args>(args)...);
		return rv;
	}

	template<typename... Args>
	int SharedDtcInterface::InitReadoutROC(Args&&... args){
		int rv = _interface->InitReadoutROC(std::forward<Args>(args)...);
		return rv;
	}

	template<typename... Args>
	int SharedDtcInterface::InitRocReadoutMode(Args&&... args){
		int rv = _interface->InitRocReadoutMode(std::forward<Args>(args)...);
		return rv;
	}

	template<typename... Args>
	void SharedDtcInterface::SetRocLaneMask(Args&&... args){
		_interface->SetRocLaneMask(std::forward<Args>(args)...);
	}

	template<typename... Args>
	void SharedDtcInterface::SetRocNHitsPerLane(Args&&... args){
		_interface->SetRocNHitsPerLane(std::forward<Args>(args)...);
	}

	template<typename... Args>
	int SharedDtcInterface::SetRocDelay(Args&&... args){
		int rv = _interface->SetRocDelay(std::forward<Args>(args)...);
		return rv;
	}

	template<typename... Args>
	int SharedDtcInterface::SetRocDigitizationWindow(Args&&... args){
		int rv = _interface->SetRocDigitizationWindow(std::forward<Args>(args)...);
		return rv;
	}

	template<typename... Args>
	int SharedDtcInterface::DigiRW(Args&&... args){
		int rv = _interface->ControlRoc_DigiRW(std::forward<Args>(args)...);
		return rv;
	}

	template<typename... Args>
	int SharedDtcInterface::DigiRead(Args&&... args){
		int rv = _interface->DigiRead(std::forward<Args>(args)...);
		return rv;
	}

	template<typename... Args>
	int SharedDtcInterface::DigiWrite(Args&&... args){
		int rv = _interface->DigiWrite(std::forward<Args>(args)...);
		return rv;
	}

	template<typename... Args>
	int SharedDtcInterface::RocBlockRead(Args&&... args){
		int rv = _interface->RocBlockRead(std::forward<Args>(args)...);
		return rv;
	}

	template<typename... Args>
	int SharedDtcInterface::PrintRocStatus(Args&&... args){
		int rv = _interface->PrintRocStatus(std::forward<Args>(args)...);
		return rv;
	}

	template<typename... Args>
	int SharedDtcInterface::ReadPanelID(Args&&... args){
		int rv = _interface->ReadPanelID(std::forward<Args>(args)...);
		return rv;
	}

	template<typename... Args>
	roc_serial_t SharedDtcInterface::ReadSerialNumber(Args&&... args){
		roc_serial_t rv = _interface->ReadSerialNumber(std::forward<Args>(args)...);
		return rv;
	}

	template<typename... Args>
	int SharedDtcInterface::ReadSettings(Args&&... args){
		int rv = _interface->ControlRoc_ReadSettings(std::forward<Args>(args)...);
		return rv;
	}

	template<typename... Args>
	int SharedDtcInterface::PulserOn(Args&&... args){
		int rv = _interface->ControlRoc_PulserOn(args...);
		return rv;
	}

	template<typename... Args>
	int SharedDtcInterface::PulserOff(Args&&... args){
		int rv = _interface->ControlRoc_PulserOff(args...);
		return rv;
	}

	template<typename... Args>
	int SharedDtcInterface::Rates(Args&&... args){
		int rv = _interface->ControlRoc_Rates(args...);
		return rv;
	}

	template<typename... Args>
	int SharedDtcInterface::Read(Args&&... args){
		int rv = _interface->ControlRoc_Read(args...);
		return rv;
	}

	template<typename... Args>
	int SharedDtcInterface::ReadDeviceID(Args&&... args){
		int rv = _interface->ControlRoc_ReadDeviceID(args...);
		return rv;
	}

	template<typename... Args>
	int SharedDtcInterface::ReadGitCommit(Args&&... args){
		int rv = _interface->ControlRoc_ReadGitCommit(args...);
		return rv;
	}

	template<typename... Args>
	int SharedDtcInterface::ReadIlp(Args&&... args){
		int rv = _interface->ControlRoc_ReadIlp(args...);
		return rv;
	}

	template<typename... Args>
	int SharedDtcInterface::GetKey (Args&&... args){
		int rv = _interface->ControlRoc_GetKey (args...);
		return rv;
	}

	template<typename... Args>
	int SharedDtcInterface::ReadSpi(Args&&... args){
		int rv = _interface->ControlRoc_ReadSpi(args...);
		return rv;
	}

	template<typename... Args>
	int SharedDtcInterface::MeasureThresholds(Args&&... args){
		int rv = _interface->ControlRoc_MeasureThresholds(args...);
		return rv;
	}

	template<typename... Args>
	int SharedDtcInterface::ReadThresholds(Args&&... args){
		int rv = _interface->ControlRoc_ReadThresholds(args...);
		return rv;
	}

	template<typename... Args>
	int SharedDtcInterface::SetCalDac(Args&&... args){
		int rv = _interface->ControlRoc_SetCalDac(args...);
		return rv;
	}

	template<typename... Args>
	int SharedDtcInterface::SetThreshold(Args&&... args){
		int rv = _interface->ControlRoc_SetThreshold(std::forward<Args>(args)...);
		return rv;
	}

	template<typename... Args>
	int SharedDtcInterface::FindAlignment (Args&&... args){
		int rv = _interface->FindAlignment(std::forward<Args>(args)...);
		return rv;
	}

	template<typename... Args>
	int SharedDtcInterface::FindAlignments(Args&&... args){
		int rv = _interface->FindAlignments(std::forward<Args>(args)...);
		return rv;
	}

	template<typename... Args>
	bool SharedDtcInterface::FindThreshold(Args&&... args){
		bool rv = _interface->FindThreshold(args...);
		return rv;
	}

	template<typename... Args>
	float SharedDtcInterface::ProgramAndQueryThreshold(Args&&... args){
		float rv = _interface->ProgramAndQueryThreshold(args...);
		return rv;
	}

	template<typename... Args>
	std::vector<std::string> SharedDtcInterface::GetRocRegistersNames(Args&&... args){
		std::vector<std::string> rv = _interface->GetRocRegistersNames(args...);
		return rv;
	}

	template<typename... Args>
	std::vector<uint32_t> SharedDtcInterface::GetRocRegisters(Args&&... args){
		std::vector<uint32_t> rv = _interface->GetRocRegisters(args...);
		return rv;
	}

	template<typename... Args>
	std::vector<float> SharedDtcInterface::GetConvertedRocRegisters(Args&&... args){
		std::vector<float> rv = _interface->GetConvertedRocRegisters(args...);
		return rv;
	}

	template<typename... Args>
	std::string SharedDtcInterface::GetRocID(Args&&... args){
		std::string rv = _interface->GetRocID(args...);
		return rv;
	}

	template<typename... Args>
	std::string SharedDtcInterface::GetRocDesignInfo(Args&&... args){
		std::string rv = _interface->GetRocDesignInfo(args...);
		return rv;
	}

	template<typename... Args>
	std::string SharedDtcInterface::GetRocFwGitCommit(Args&&... args){
		std::string rv = _interface->GetRocFwGitCommit(args...);
		return rv;
	}
} // namespace trkdaq

#endif
