
This is the beta release of the SmartMedia device driver layer.

To use the the SmartMedia driver with ERTFS, in portconf.h, set:

#define INCLUDE_SMARTMEDIA      1

Then add all files in this subdirectory to your project file and 
compile and link.

This has already been done for the Microsoft C/Windows reference build  of
ERTFS. In the subdirectory rtfsdemo there is a Microsoft C project 
file that will build ERTFS with the SmartMedia included.

This is a beta release of the SmartMedia driver for ERTFS pro. It has been 
certified to compile and link and it has been used in the field before by
one specific customer but it has not been tested in the current ERTFS release.

In this beta release there is no documention to guide you in porting the
driver to other environments, but the Hardware interface appears to be
 segregated to the files SMARTHW.H and SMARTHW.C.

The ERTFS device driver wrapper layer is contained in SMTMEDIA.C.

The driver does not currently support removable media. To support
removable media you must edit the code section for DEVCTL_CHECKSTATUS 
in smtmedia.c, querying the SmartMedia API, returning DEVTEST_CHANGED 
if the media has changed.

I'm anxious to follow your progress using this driver and to offer any help
I can.


Thanks

Peter Van Oudenaren
978 448 9340
peter@ebsnetinc.com
















