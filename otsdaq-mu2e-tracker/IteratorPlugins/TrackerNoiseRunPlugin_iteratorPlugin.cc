#include "otsdaq-mu2e-tracker/IteratorPlugins/TrackerNoiseRunPlugin.h"

#include "otsdaq/ConfigurationInterface/ConfigurationManagerRW.h"
#include "otsdaq/FiniteStateMachine/FiniteStateMachine.h"
#include "otsdaq/GatewaySupervisor/GatewaySupervisor.h"
#include "otsdaq/Macros/CoutMacros.h"
#include "otsdaq/Macros/IteratorPluginMacros.h"
#include "otsdaq/Macros/StringMacros.h"
#include "otsdaq/MessageFacility/MessageFacility.h"
#include "otsdaq/SOAPUtilities/SOAPParameters.h"
#include "otsdaq/SOAPUtilities/SOAPUtilities.h"
#include "otsdaq/SupervisorInfo/AllSupervisorInfo.h"
#include "otsdaq/XmlUtilities/HttpXmlDocument.h"

#include <chrono>
#include <regex>
#include <set>
#include <sstream>
#include <thread>

#undef __MF_SUBJECT__
#define __MF_SUBJECT__ "TrackerNoiseRun"

using namespace ots;
using tracker_noise_run::require;

//==============================================================================
TrackerNoiseRunPlugin::TrackerNoiseRunPlugin(const std::string& pluginName)
    : IteratorPluginVInterface(pluginName), planName_("---TRACKER_NOISE_RUN---")
{
}

//==============================================================================
void TrackerNoiseRunPlugin::handleCommand(const std::string&     command,
                                          HttpXmlDocument&       xml,
                                          IteratorPluginContext& context)
{
	if(command == "Start")
	{
		require(!owned_, "NoiseRun is already running.");
		auto& stateMachine = gatewayStateMachine(context.supervisor);
		require(!stateMachine.isInTransition() &&
		            stateMachine.getCurrentStateName() == "Configured",
		        "NoiseRun requires Tracker Configured and stopped.");
		{
			std::lock_guard<std::mutex> lock(statusMutex_);
			scans_         = 0;
			unresolvedOps_ = 0;
			lastSummary_.clear();
			excludedRocs_.clear();
		}
		stopping_ = false;
		owned_    = true;  // the Iterator sees owns() flip and runs the reserved plan
		xml.addTextElementToData("noise_run_accepted", "1");
		return;
	}

	if(command == "Stop" || command == "StopAcknowledge" || command == "Halt")
	{
		require(owned_,
		        "NoiseRun is not the active Iterator plan. No other plan was stopped.");
		std::lock_guard<std::mutex> lock(statusMutex_);
		if(unresolvedOps_ && command == "Stop")
			require(
			    false,
			    std::to_string(unresolvedOps_) +
			        " NoiseRun hardware operation(s) have an unknown outcome (frontend "
			        "did not answer). Check the frontend logs and ROC masks, then use "
			        "pluginCommand=StopAcknowledge to release the Iterator.");
		if(unresolvedOps_)
			__COUT_WARN__ << "Operator acknowledged " << unresolvedOps_
			              << " unresolved NoiseRun hardware operation(s); releasing the "
			                 "Iterator."
			              << __E__;
		unresolvedOps_ = 0;
		stopping_      = true;
		owned_         = false;  // the Iterator sees owns() flip and finishes the plan
		xml.addTextElementToData("noise_run_stop_requested", "1");
		return;
	}

	require(false, "Unknown NoiseRun command '" + command + "'.");
}

//==============================================================================
void TrackerNoiseRunPlugin::finish(IteratorPluginContext& /*context*/)
{
	// All scan jobs have settled before the Iterator reaches this boundary.
	targets_.clear();
	identity_.clear();
	owned_ = false;
	__COUT_INFO__ << "NoiseRun stopped after submitted requests settled. Initialize "
	                 "readout before normal data taking."
	              << __E__;
}

//==============================================================================
void TrackerNoiseRunPlugin::addStatus(HttpXmlDocument&       xml,
                                      IteratorPluginContext& context)
{
	std::lock_guard<std::mutex> lock(statusMutex_);
	const std::string           errorMessage =
	    xml.getMatchingValue("error_message");  // already added by the Iterator
	xml.addTextElementToData("noise_run_supported", "1");
	xml.addTextElementToData("noise_run_fsm",
	                         gatewayActiveStateMachineName(context.supervisor));
	xml.addTextElementToData("noise_run_owned", owned_ ? "1" : "0");
	xml.addNumberElementToData("noise_run_scans", scans_);
	xml.addTextElementToData("noise_run_last_summary", lastSummary_);
	xml.addTextElementToData("noise_run_excluded_rocs", excludedRocs_);
	xml.addNumberElementToData("noise_run_unresolved_ops", unresolvedOps_);
	xml.addTextElementToData("noise_run_state",
	                         !owned_ && !stopping_   ? "Inactive"
	                         : unresolvedOps_        ? "Unresolved"
	                         : !errorMessage.empty() ? "Error"
	                         : stopping_             ? "Stopping"
	                         : scans_                ? "Running"
	                                                 : "Starting");
}

//==============================================================================
void TrackerNoiseRunPlugin::guard(IteratorPluginContext& context)
{
	auto& stateMachine = gatewayStateMachine(context.supervisor);
	require(!stateMachine.isInTransition() &&
	            stateMachine.getCurrentStateName() == "Configured",
	        "Tracker left Configured or is transitioning; NoiseRun stopped.");
	require(tracker_noise_run::persistedIdentity(
	            ConfigurationManager::ACTIVE_GROUPS_FILENAME) == identity_,
	        "Persisted Context/Configuration changed; NoiseRun stopped.");
	const auto& configuredGroup = gatewayConfigurationTableGroup(context.supervisor);
	require(configuredGroup.first == identity_.at(2) &&
	            configuredGroup.second.toString() == identity_.at(3),
	        "Gateway Configure group does not match persisted configuration. Configure "
	        "the selected group first.");
	std::set<unsigned int> supervisors;
	for(const auto& target : targets_)
		supervisors.insert(target.supervisor);
	const auto& supervisorInfo = gatewaySupervisorInfo(context.supervisor);
	for(auto id : supervisors)
	{
		auto reply = gatewaySendWithSOAPReply(
		    context.supervisor,
		    supervisorInfo.getAllFETypeSupervisorInfo().at(id).getDescriptor(),
		    "ApplicationStatusRequest");
		SOAPParameters rx;
		rx.addParameter("Status");
		rx.addParameter("Progress");
		rx.addParameter("Error");
		rx.addParameter("ConfiguredGroup");
		const auto command = SOAPUtilities::receive(reply, rx);
		require(
		    command == "applicationStatusRequestReply" && rx.getValue("Error").empty() &&
		        rx.getValue("Status") == "Configured" && rx.getValue("Progress") == "100",
		    "Frontend is not Configured/ready: " + std::to_string(id));
		// exact match on the structured field (not the human-readable Detail text,
		// which drops the group after Stop and would also match e.g. TrackerConfig(7))
		require(rx.getValue("ConfiguredGroup") ==
		            identity_.at(2) + "(" + identity_.at(3) + ")",
		        "Frontend Configure group '" + rx.getValue("ConfiguredGroup") +
		            "' does not match persisted configuration: " + std::to_string(id));
	}
}

//==============================================================================
void TrackerNoiseRunPlugin::initialize(IteratorPluginContext& context)
{
	ConfigurationManagerRW* configurationManager = context.configurationManager;
	identity_                                    = tracker_noise_run::persistedIdentity(
        ConfigurationManager::ACTIVE_GROUPS_FILENAME);
	std::string errors;
	configurationManager->init(&errors);
	require(errors.empty(), errors);
	require(configurationManager->getActiveGroupName() == identity_[2] &&
	            configurationManager->getActiveGroupKey().toString() == identity_[3],
	        "Persisted configuration changed during load");
	std::set<std::string>    seen;
	std::vector<std::string> excluded;
	std::map<std::string, std::vector<std::string>>
	            disabledFeRocs;  // reported as excluded only if no enabled twin exists
	const auto& supervisors =
	    gatewaySupervisorInfo(context.supervisor).getAllFETypeSupervisorInfo();
	targets_.clear();
	for(const auto& contextNode :
	    configurationManager->getNode("XDAQContextTable").getChildren())
	{
		for(const auto& app :
		    contextNode.second.getNode("LinkToApplicationTable").getChildren())
		{
			if(app.second.getNode("Class").getValue<std::string>() != "ots::FESupervisor")
				continue;
			const bool parentEnabled = contextNode.second.status() && app.second.status();
			unsigned int id          = 0;
			bool         found       = false;
			if(parentEnabled)
			{
				for(const auto& sup : supervisors)
					if(sup.second.getName() == app.first &&
					   sup.second.getContextName() == contextNode.first)
					{
						require(!found, "Ambiguous frontend supervisor");
						found = true;
						id    = sup.first;
					}
				require(found,
				        "Enabled frontend supervisor missing from runtime: " + app.first);
			}
			for(const auto& fe : app.second.getNode("LinkToSupervisorTable")
			                         .getNode("LinkToFEInterfaceTable")
			                         .getChildren())
			{
				if(!std::regex_match(fe.first, std::regex("Trk(0[1-9]|1[0-8])_DTC[01]")))
					continue;
				// only enabled associations can collide: XDAQContextTable enforces
				// application uniqueness for enabled contexts/apps only, so a disabled
				// alternate context may legitimately point at the same FE group
				const bool feEnabled = parentEnabled && fe.second.status();
				if(feEnabled)
					require(seen.insert(fe.first).second,
					        "Duplicate enabled tracker FE association: " + fe.first);
				require(
				    fe.second.getNode("FEInterfacePluginName").getValue<std::string>() ==
				        "DTCFrontEndInterface",
				    "Unsupported tracker FE plugin");
				tracker_noise_run::Target target;
				target.uid        = fe.first;
				target.supervisor = id;
				auto dtc          = fe.second.getNode("LinkToFETypeTable");
				for(const auto& roc : dtc.getNode("LinkToROCGroupTable").getChildren())
				{
					if(feEnabled && roc.second.status())
						target.links.push_back(
						    roc.second.getNode("linkID").getValue<int>());
					else if(feEnabled)
						excluded.push_back(
						    roc.first);  // disabled ROC under an enabled FE
					else
						disabledFeRocs[fe.first].push_back(roc.first);
				}
				if(!target.links.empty())
				{
					std::sort(target.links.begin(), target.links.end());
					targets_.push_back(target);
				}
			}
		}
	}
	for(const auto& disabledFe : disabledFeRocs)
		if(seen.count(disabledFe.first) == 0)
			excluded.insert(
			    excluded.end(), disabledFe.second.begin(), disabledFe.second.end());
	std::sort(targets_.begin(),
	          targets_.end(),
	          [](const tracker_noise_run::Target& a, const tracker_noise_run::Target& b) {
		          return a.uid < b.uid;
	          });
	tracker_noise_run::validateTargets(targets_);
	guard(context);
	// Discover every signature before submitting any hardware operation.
	std::map<unsigned int, GatewaySupervisor::RemoteFEMacroInfo> info;
	for(auto& target : targets_)
	{
		if(!info.count(target.supervisor))
		{
			SOAPParameters tx;
			tx.addParameter("Request", "GetInterfaceMacros");
			auto reply = gatewaySendWithSOAPReply(
			    context.supervisor,
			    supervisors.at(target.supervisor).getDescriptor(),
			    "MacroMakerSupervisorRequest",
			    tx);
			SOAPParameters rx;
			rx.addParameter("FEMacros");
			rx.addParameter("Error");
			auto command = SOAPUtilities::receive(reply, rx);
			require(command == "FESupervisorResponse" && rx.getValue("Error").empty(),
			        "Cannot discover frontend macros");
			HttpXmlDocument    doc;
			std::istringstream lines(rx.getValue("FEMacros"));
			std::string        line;
			while(std::getline(lines, line))
				if(!line.empty())
					doc.addTextElementToData("FEMacros", line);
			std::ostringstream xml;
			doc.outputXmlDocument(&xml, false);
			info.emplace(target.supervisor,
			             GatewaySupervisor::parseFEMacroInfo(xml.str()));
		}
		const auto& macros  = info.at(target.supervisor).fes.at(target.uid).macros;
		const auto& measure = macros.at("ROC FEMacro - Measure Channel Rates");
		require(measure.inputs.size() == 1 &&
		            measure.inputs[0].find("Target ROC or Mask") == 0,
		        "Unsupported rate macro signature");
		for(const auto& output :
		    {"Target ROC", "Return Code", "Rates", "Saved hardware masks"})
			require(std::find(measure.outputs.begin(), measure.outputs.end(), output) !=
			            measure.outputs.end(),
			        "Missing rate macro output");
		target.inputs  = measure.inputs;
		target.outputs = measure.outputs;
	}
	auto&       publisher = targets_.front();
	const auto& macro     = info.at(publisher.supervisor)
	                        .fes.at(publisher.uid)
	                        .macros.at("ROC FEMacro - Publish Rate Summary");
	require(macro.inputs.size() == 2 && macro.inputs[0].find("Target ROC or Mask") == 0 &&
	            macro.inputs[1] == "Summary JSON",
	        "Rates publisher not loaded or unsupported");
	require(std::find(macro.outputs.begin(), macro.outputs.end(), "Published") !=
	            macro.outputs.end(),
	        "Missing publisher confirmation output");
	publisher.publisherInputs  = macro.inputs;
	publisher.publisherOutputs = macro.outputs;
	guard(context);
	{
		std::lock_guard<std::mutex> lock(statusMutex_);
		excludedRocs_.clear();
		for(const auto& roc : excluded)
			excludedRocs_ += (excludedRocs_.empty() ? "" : ", ") + roc;
	}
	context.setActiveCommandType("NoiseRun");
}

//==============================================================================
tracker_noise_run::Outputs TrackerNoiseRunPlugin::runMacro(
    IteratorPluginContext&           context,
    const tracker_noise_run::Target& target,
    const std::string&               name,
    const std::string&               inputs,
    const std::vector<std::string>&  outputs)
{
	SOAPParameters tx;
	tx.addParameter("Request", "RunInterfaceMacro");
	tx.addParameter("InterfaceID", target.uid);
	tx.addParameter("feMacroName", name);
	tx.addParameter("inputArgs", inputs);
	std::string names;
	for(const auto& outputName : outputs)
		names +=
		    (names.empty() ? "" : ",") + StringMacros::encodeURIComponent(outputName);
	tx.addParameter("outputArgs", names);
	tx.addParameter("userPermissions", "allUsers:1");
	tx.addParameter("AsyncSupported", "1");
	const auto* descriptor = gatewaySupervisorInfo(context.supervisor)
	                             .getAllFETypeSupervisorInfo()
	                             .at(target.supervisor)
	                             .getDescriptor();
	// Once the frontend has (or may have) started the macro, this function must not
	// return until the frontend reports it finished: scan() only waits on these calls,
	// and Stop releases Iterator ownership as soon as scan() returns. Leaving early
	// would let a new scan or a DAQ transition overlap a measurement or a mask restore.
	//	- a lost reply to the initial dispatch is an UNKNOWN outcome: the macro may be
	//	  running. Never resubmit it; flag an unresolved hardware operation instead.
	//	- a CheckMacro transport error is retried (completion-only polling). If the
	//	  frontend stays unreachable past the poll timeout, flag unresolved and give up.
	// An unresolved flag blocks ordinary Stop until the operator acknowledges it.
	auto markUnresolved = [&](const std::string& why) {
		{
			std::lock_guard<std::mutex> lock(statusMutex_);
			++unresolvedOps_;
		}
		__COUT_ERR__ << "NoiseRun hardware operation outcome unknown on " << target.uid
		             << " ('" << name << "'): " << why
		             << ". Verify the frontend/ROC masks before any further operation; "
		                "ordinary Stop is blocked until acknowledged."
		             << __E__;
		throw std::runtime_error(target.uid + ": unresolved hardware operation: " + why);
	};
	std::string result, task;
	const auto  pollDeadline =
	    std::chrono::steady_clock::now() + std::chrono::seconds(pollTimeoutSeconds_);
	for(;;)
	{
		xoap::MessageReference reply;
		try
		{
			reply = gatewaySendWithSOAPReply(
			    context.supervisor, descriptor, "MacroMakerSupervisorRequest", tx);
		}
		catch(const std::exception& e)
		{
			if(task.empty())
				markUnresolved(std::string("no reply to macro dispatch: ") + e.what());
			if(std::chrono::steady_clock::now() > pollDeadline)
				markUnresolved("frontend unreachable while polling task " + task +
				               " for " + std::to_string(pollTimeoutSeconds_) +
				               " s: " + e.what());
			__COUT_WARN__ << target.uid << ": CheckMacro for task " << task
			              << " failed, retrying: " << e.what() << __E__;
			std::this_thread::sleep_for(std::chrono::seconds(1));
			continue;
		}
		SOAPParameters rx;
		rx.addParameter("Error");
		rx.addParameter("outputArgs");
		rx.addParameter("NotDoneTaskID");
		const auto command = SOAPUtilities::receive(reply, rx);
		// a definite error reply from the frontend means the task is not running
		// (dispatch rejected, or CheckMacro found it finished with an error)
		require(command == "FESupervisorResponse" && rx.getValue("Error").empty(),
		        target.uid + ": macro request failed: " + rx.getValue("Error"));
		const auto next = rx.getValue("NotDoneTaskID");
		if(next.empty())
		{
			result = rx.getValue("outputArgs");
			break;
		}
		require(std::regex_match(next, std::regex("[1-9][0-9]*")) &&
		            (task.empty() || task == next),
		        "Invalid async macro task ID");
		task = next;
		tx   = SOAPParameters();
		tx.addParameter("Request", "CheckMacro");
		tx.addParameter("TaskID", task);
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		// Poll only: Stop never cancels a hardware macro or launches it again.
	}
	tracker_noise_run::Outputs decoded;
	std::istringstream         stream(result);
	std::string                pair;
	while(std::getline(stream, pair, ';'))
	{
		const auto comma = pair.find(',');
		require(comma != std::string::npos, "Malformed macro result");
		require(decoded
		            .emplace(StringMacros::decodeURIComponent(pair.substr(0, comma)),
		                     StringMacros::decodeURIComponent(pair.substr(comma + 1)))
		            .second,
		        "Duplicate macro output");
	}
	require(!decoded.empty(), "Empty macro result");
	return decoded;
}

//==============================================================================
void TrackerNoiseRunPlugin::step(IteratorPluginContext& context)
{
	auto stopped = [&] { return stopping_ || context.stopRequested(); };
	if(stopped())
		return;
	auto input = [](const std::string& name, const std::string& value) {
		return StringMacros::encodeURIComponent(name) + "," +
		       StringMacros::encodeURIComponent(value);
	};
	const auto   start     = std::chrono::steady_clock::now();
	unsigned int guardPass = 0;
	const auto   summary   = tracker_noise_run::scan(
        targets_,
        [&](const tracker_noise_run::Target& target) {
            unsigned int mask = 0;
            for(int link : target.links)
                mask |= 1u << (4 * link);
            std::ostringstream value;
            value << "0x" << std::hex << mask;
            return runMacro(context,
                            target,
                            "ROC FEMacro - Measure Channel Rates",
                            input(target.inputs.at(0), value.str()),
                            target.outputs);
        },
        [&] {
            guard(context);
            if(guardPass++ == 0 && stopped())
                throw std::runtime_error("NoiseRun Stop received before scan dispatch");
        },
        [&](const tracker_noise_run::Summary& result) {
            const auto& target = targets_.front();
            const auto  out    = runMacro(
                context,
                target,
                "ROC FEMacro - Publish Rate Summary",
                input(target.publisherInputs.at(0),
                      std::to_string(target.links.front())) +
                    ";" + input(target.publisherInputs.at(1), result.json(targets_)),
                target.publisherOutputs);
            require(tracker_noise_run::scalar(out.at("Published")) == 1 &&
                        tracker_noise_run::scalar(out.at("Target ROC")) ==
                            static_cast<unsigned int>(target.links.front()),
                    "Publisher did not confirm Graphite send; not retried");
        });
	{
		std::lock_guard<std::mutex> lock(statusMutex_);
		++scans_;
		lastSummary_ = summary.json(targets_);
	}
	const auto next =
	    tracker_noise_run::nextScan(start, std::chrono::steady_clock::now());
	while(!stopped() && std::chrono::steady_clock::now() < next)
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

DEFINE_OTS_ITERATOR_PLUGIN(TrackerNoiseRunPlugin)
