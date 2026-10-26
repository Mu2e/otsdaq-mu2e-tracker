find_package(SWIG REQUIRED)
include(${SWIG_USE_FILE})

find_package(Python COMPONENTS Interpreter Development REQUIRED)

set_source_files_properties(otsdaq-mu2e-tracker_Ui.i PROPERTIES
    CPLUSPLUS ON
    USE_TARGET_INCLUDE_DIRECTORIES ON)

swig_add_library(otsdaq-mu2e-tracker_Uip
    LANGUAGE python
    SOURCES otsdaq-mu2e-tracker_Uip.i)

# Replace this target with the actual Ui library target name from Ui/CMakeLists.txt
swig_link_libraries(otsdaq-mu2e-tracker_Uip
    otsdaq-mu2e-tracker::Ui
    Python::Python)

install(TARGETS otsdaq-mu2e-tracker_Uip DESTINATION lib)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/otsdaq-mu2e-tracker_Uip.py DESTINATION lib)
