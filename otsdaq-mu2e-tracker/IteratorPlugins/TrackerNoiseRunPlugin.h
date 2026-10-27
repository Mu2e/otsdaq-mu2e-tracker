#ifndef _ots_TrackerNoiseRunPlugin_h_
#define _ots_TrackerNoiseRunPlugin_h_

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "otsdaq-mu2e-tracker/IteratorPlugins/TrackerNoiseRun.h"
#include "otsdaq/GatewaySupervisor/IteratorPluginVInterface.h"

namespace ots
{
/// ~~ TrackerNoiseRunPlugin ~~
///	Gateway-side orchestration of the tracker NoiseRun, run as a reserved Iterator plan.
///	Every ~3 s it asks each tracker DTC frontend to run "ROC FEMacro - Measure Channel
///	Rates" (all DTCs in parallel), checks that nothing moved (FSM state, persisted
///	configuration, frontends' configured group), sums the rates, and publishes the
///	summary to Graphite through "ROC FEMacro - Publish Rate Summary" on the first DTC.
///	It deliberately performs no configuration activation or DAQ FSM transition.
///
///	Commands (StateMachine=iteratePluginCommand&plugin=TrackerNoiseRun&pluginCommand=...):
///		Start            -> take the Iterator; replies noise_run_accepted=1
///		Stop             -> release after every submitted macro settles; replies noise_run_stop_requested=1
///		StopAcknowledge  -> same, but also clears "unresolved" hardware operations (operator has checked the frontend)
///		Halt             -> what iterateHalt becomes while this plugin owns the Iterator (same as Stop)
///	Status fields appended to getIterationPlanStatus while owning: noise_run_*.
class TrackerNoiseRunPlugin : public IteratorPluginVInterface
{
  public:
	explicit TrackerNoiseRunPlugin(const std::string& pluginName);

	const std::string& getPlanName(void) const override { return planName_; }

	void handleCommand(const std::string& command, HttpXmlDocument& xml, IteratorPluginContext& context) override;
	void initialize(IteratorPluginContext& context) override;
	void step(IteratorPluginContext& context) override;
	void finish(IteratorPluginContext& context) override;
	void addStatus(HttpXmlDocument& xml, IteratorPluginContext& context) override;

	bool owns(void) const override { return owned_; }
	bool blocksDaqTransitions(void) const override { return owned_; }

  private:
	void                       guard(IteratorPluginContext& context);
	tracker_noise_run::Outputs runMacro(IteratorPluginContext& context, const tracker_noise_run::Target& target, const std::string& name, const std::string& inputs, const std::vector<std::string>& outputs);

	const std::string planName_;

	std::atomic<bool> owned_{false};
	std::atomic<bool> stopping_{false};

	// worker-thread state (set in initialize(), read in step())
	std::vector<tracker_noise_run::Target> targets_;
	std::vector<std::string>               identity_;  ///< persisted Context/Configuration name+key

	// shared between worker and status requests
	mutable std::mutex statusMutex_;
	uint64_t           scans_         = 0;
	unsigned int       unresolvedOps_ = 0;  ///< hardware macros whose outcome is unknown; blocks ordinary Stop
	std::string        lastSummary_, excludedRocs_;

	static constexpr unsigned int pollTimeoutSeconds_ = 600;  ///< how long to keep polling an unreachable frontend before declaring the macro unresolved
};

}  // namespace ots

#endif /* _ots_TrackerNoiseRunPlugin_h_ */
