#
# include_directories($ENV{ROOTSYS}/include)
# link_directories($ENV{ROOTSYS}/lib)
#
cet_make_library(
        SOURCE
        DtcInterfaceBase.cc DtcInterfaceBase_print.cc
        CfoInterface.cc
        DtcInterface.cc DtcInterface_ProgramRoc.cc DtcInterface_ControlRoc.cc DtcInterface_print.cc
        SharedDtcInterface.cc ROC.cc
        # PreampChannel.cc PreampThreshold.cc
        program_drac.cc
        LIBRARIES PUBLIC
        otsdaq-mu2e-tracker::ParseAlignment
        artdaq::DAQdata
        artdaq-core::Data
        # artdaq-core-demo::Overlays
        cetlib_except::cetlib_except
        mu2e-pcie-utils::CFOInterface
        mu2e-pcie-utils::DTCInterface
        nlohmann_json::nlohmann_json
        ROOT::Core
        )

include(ArtDictionary)

art_dictionary(
  DICTIONARY_LIBRARIES
  otsdaq-mu2e-tracker::Ui
  artdaq-core-mu2e::Overlays
  mu2e-pcie-utils::CFOInterface
  mu2e-pcie-utils::DTCInterface
)

get_target_property(DictCompileOptions otsdaq-mu2e-tracker_Ui_dict COMPILE_OPTIONS)

if(DictCompileOptions)
  string(REPLACE "-fsanitize=thread"  "" DictCompileOptions "${DictCompileOptions}")
  string(REPLACE "-fsanitize=address" "" DictCompileOptions "${DictCompileOptions}")
  string(REPLACE "-ltsan"             "" DictCompileOptions "${DictCompileOptions}")
  string(REPLACE "-lasan"             "" DictCompileOptions "${DictCompileOptions}")

  set_target_properties(otsdaq-mu2e-tracker_Ui_dict PROPERTIES COMPILE_OPTIONS "${DictCompileOptions}")
endif()

cet_add_compiler_flags(-g -O0)

install_headers()
install_source()
