include(otsdaq::iteratorPlugin)

# Gateway-side orchestration of the tracker NoiseRun, loaded by name on request:
#   StateMachineXgiHandler?StateMachine=iteratePluginCommand&plugin=TrackerNoiseRun&pluginCommand=Start
cet_build_plugin(TrackerNoiseRun otsdaq::iteratorPlugin
    SOURCE TrackerNoiseRunPlugin_iteratorPlugin.cc
    LIBRARIES REG
    otsdaq::GatewaySupervisor
    otsdaq::FiniteStateMachine   # Gateway links these privately; the plugin calls them directly
    otsdaq::SupervisorInfo
    otsdaq::ConfigurationInterface
    otsdaq::SOAPUtilities
    otsdaq::XmlUtilities
    otsdaq::Macros
)

install_headers()
install_source()
