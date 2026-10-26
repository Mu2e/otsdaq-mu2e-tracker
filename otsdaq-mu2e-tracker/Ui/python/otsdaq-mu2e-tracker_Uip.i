%module otsdaq-mu2e-tracker_Uip

%include "std_string.i"
%include "std_vector.i"
%include "std_unique_ptr.i"
%include "stdint.i"
%include "typemaps.i"

%{
#include "otsdaq-mu2e-tracker/Ui/DtcInterface.hh"
#include "otsdaq-mu2e-tracker/Ui/CfoInterface.hh"
#include "otsdaq-mu2e-tracker/Ui/DtcInterfaceBase.hh"
using namespace mu2e;
%}

%include "otsdaq-mu2e-tracker/Ui/DtcInterfaceBase.hh"
%include "otsdaq-mu2e-tracker/Ui/DtcInterface.hh"
%include "otsdaq-mu2e-tracker/Ui/CfoInterface.hh"
