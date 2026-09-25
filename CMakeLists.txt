# ======================================================================
#  otsdaq-mu2e-tracker main build file
#
#  cd .../path/to/build/directory
#  source .../path/to/otsdaq-mu2e-tracker/ups/setup_for_development
#  buildtool
# ======================================================================
cmake_minimum_required (VERSION 3.19 FATAL_ERROR)
find_package(cetmodules)

project(otsdaq-mu2e-tracker VERSION 9.00.00)

include(CetCMakeEnv)
cet_cmake_env()
SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

cet_set_compiler_flags(
        DIAGS VIGILANT
        # WERROR
        NO_UNDEFINED
        EXTRA_FLAGS -fpermissive -Wno-unused-parameter -pthread -g  -O0 # -O0 -v Equivalent to debug qualifier, for testing
)


string(TOUPPER ${CMAKE_BUILD_TYPE} BTYPE_UC)
if(BTYPE_UC STREQUAL DEBUG)
  add_definitions(-DDEBUGME)
	if(DEFINED ENV{USE_ASAN})
		if($ENV{USE_ASAN} GREATER 0)
			message("!!! Address Sanitizer Activated!!!")
			message("!!! Note: To run, add 'export ASAN_OPTIONS=verify_asan_link_order=0' to setup !!!")
			add_compile_options(-fsanitize=address -Wno-unused-command-line-argument)
			add_link_options(-lasan)
		endif()
	endif()
	if(DEFINED ENV{USE_TSAN})
		if($ENV{USE_TSAN} GREATER 0)
			message("!!! Thread Sanitizer Activated!!!")
			add_compile_options(-fsanitize=thread -Wno-unused-command-line-argument -Wno-tsan)
			add_link_options(-ltsan)
		endif()
	endif()
	if(DEFINED ENV{USE_UBSAN})
		if($ENV{USE_UBSAN} GREATER 0)
			message("!!! \"Undefined Behavior\" Sanitizer Activated!!!")
			add_compile_options(-fsanitize=undefined -Wno-unused-command-line-argument)
			add_link_options(-lubsan)
		endif()
	endif()
	if(DEFINED ENV{USE_GCOV})
		add_compile_options(-fprofile-arcs -ftest-coverage --coverage)
		add_link_options(--coverage)
	endif()
endif()

#exeption stack trace
if(DEFINED ENV{USE_EXCEPTIONSTACKTRACE})
	if($ENV{USE_EXCEPTIONSTACKTRACE} GREATER 0)
		message("!!! Exception StackTrace Activated!!!")
		add_definitions(-D EXCEPTIONSTACKTRACE)
		set( EXCEPTIONSTACKTRACE_LIB  artdaq-core_Utilities_ExceptionStackTrace )
		add_compile_options( -rdynamic -Wno-unused-command-line-argument )
		add_link_options( -rdynamic )
	endif()
endif()

#------------------------------------------------------------------------------
# it looks that these packages are looked for only after cet_cmake_env
#------------------------------------------------------------------------------
find_package(artdaq            3.12.07 REQUIRED EXPORT)
find_package(artdaq-core       3.09.16 REQUIRED EXPORT)
find_package(artdaq-core-demo  1.11.00 REQUIRED EXPORT)
find_package(otsdaq-mu2e       8.00.00 REQUIRED EXPORT)
find_package(mu2e-pcie-utils   3.02.00 REQUIRED EXPORT)
find_package(nlohmann_json             REQUIRED EXPORT)
# find_package(ROOT              6.32.08 REQUIRED EXPORT)

# message("---------- otsdaq-mu2e-tracker: PRINT CMAKE VARIABLES " )
#
# get_cmake_property(_variableNames VARIABLES)
# list (SORT _variableNames)
# foreach (_variableName ${_variableNames})
#   message(STATUS "${_variableName}=${${_variableName}}")
# endforeach()

# if (DEFINED ENV{MIDASSYS})
#    message(-- P.Murat: otsdaq-mu2e-tracker: MIDASSYS=.$ENV{MIDASSYS}. )
#    find_package(Midas REQUIRED)
# else()
#    message("-- P.Murat: no MIDASSYS")
# endif()

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

# Code
add_subdirectory(otsdaq-mu2e-tracker)

# Useful scripts
add_subdirectory(tools)

# Test Programs
add_subdirectory(test)

cet_cmake_config()
