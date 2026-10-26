# ======================================================================
#  otsdaq-mu2e-tracker main build file
#
#  cd .../path/to/build/directory
#  source .../path/to/otsdaq-mu2e-tracker/ups/setup_for_development
#  buildtool
# ======================================================================
cmake_minimum_required (VERSION 3.19 FATAL_ERROR)
find_package(cetmodules)

project(otsdaq-mu2e-tracker VERSION 3.03.01)

include(CetCMakeEnv)
cet_cmake_env()
SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

cet_set_compiler_flags(
        DIAGS VIGILANT
        # WERROR
        NO_UNDEFINED
        EXTRA_FLAGS -fpermissive -Wno-unused-parameter -pthread -g  -O0 # -O0 -v Equivalent to debug qualifier, for testing
)
#------------------------------------------------------------------------------
# it looks that these packages are looked for only after cet_cmake_env
#------------------------------------------------------------------------------
find_package(art               3.15.00 REQUIRED EXPORT)
find_package(artdaq            3.12.07 REQUIRED EXPORT)
find_package(artdaq-core       3.09.16 REQUIRED EXPORT)
find_package(artdaq-core-demo  1.11.00 REQUIRED EXPORT)
# find_package(otsdaq-mu2e       8.00.00 REQUIRED EXPORT)
find_package(mu2e-pcie-utils   3.02.00 REQUIRED EXPORT)
find_package(ROOT              6.32.08 REQUIRED EXPORT)

# message("---------- otsdaq-mu2e-tracker: PRINT CMAKE VARIABLES " )
# 
# get_cmake_property(_variableNames VARIABLES)
# list (SORT _variableNames)
# foreach (_variableName ${_variableNames})
#   message(STATUS "${_variableName}=${${_variableName}}")
# endforeach()
  
# XDAQ Extra setup
include_directories($ENV{XDAQ_ROOT}/include)

find_package(Offline           10.00.00 REQUIRED)
include_directories($ENV{BTRK_INC} $ENV{KINKAL_INC})
link_directories($ENV{BTRK_LIB} $ENV{KINKAL_LIB})

#string(TOUPPER ${CMAKE_BUILD_TYPE} BTYPE_UC )
#if( ${BTYPE_UC} MATCHES "DEBUG" )
#  cet_add_compiler_flags(-fsanitize=address)
#endif()

#cet_report_compiler_flags()

#string(REGEX MATCHALL "[0-9][0-9]?[0-9]?" ART_VERSION_LIST "$ENV{ART_VERSION}")
#list(GET ART_VERSION_LIST 2 ART_PATCH_VERSION)

#message("-DART_HEX_VERSION=0x${ART_MAJOR_VERSION}${ART_MINOR_VERSION}${ART_PATCH_VERSION}")
#set(ART_HEX_VERSION 0x${ART_MAJOR_VERSION}${ART_MINOR_VERSION}${ART_PATCH_VERSION})
#add_definitions(-DART_HEX_VERSION=0x${ART_MAJOR_VERSION}${ART_MINOR_VERSION}${ART_PATCH_VERSION})

include(ArtDictionary)
include(BuildPlugins)

find_package(SWIG)
if(DEFINED $ENV{BUILD_PYTHON_INTERFACE})
if (${SWIG_FOUND})
        set(BUILD_PYTHON_INTERFACE TRUE)
	include(${SWIG_USE_FILE})
endif()
endif()

# Code
add_subdirectory(otsdaq-mu2e-tracker)

# Useful scripts
#add_subdirectory(tools)

# Test Programs
#add_subdirectory(test)

cet_cmake_config()
