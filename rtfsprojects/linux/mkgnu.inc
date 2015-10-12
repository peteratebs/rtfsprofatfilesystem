# This file evaluates the variables set on the command line and acts on them.
# targ is set to something such as POLG68K, RTPXMSP3, etc.
# RELENV should be N for ebsenv, Y otherwise.
# v=v for debug, v=o for optimized.

########################################################################
targ = LINUX

dist = SRCONLY

# set the following to "Y" for RTUPnP, "N" otherwise.
dortupnp = "Y"


########################################################################
# Select the version (o)ptimzed or (dbv)debug here or pass it in on the command line
dbv = v

root    = ..
kernel  = c:


#source path
lib_to_rtfs_commonsource_path   = $(RTFS_PATH)/rtfscommon/source
lib_to_rtfs_commonapps_path     = $(RTFS_PATH)/rtfscommon/apps
lib_to_rtfs_pro_path            = $(RTFS_PATH)/rtfspro
lib_to_rtfs_failsafe_path       = $(RTFS_PATH)/rtfsfailsafe
lib_to_rtfs_proplussource_path  = $(RTFS_PATH)/rtfspackages/rtfsproplus
lib_to_rtfs_proplusdvr_path     = $(RTFS_PATH)/rtfspackages/rtfsproplusdvr
lib_to_rtfs_proplusapps_path    = $(RTFS_PATH)/rtfspackages/apps
lib_to_rtfs_ramdisk_path        = $(RTFS_PATH)/rtfsdrivers/ramdisk
lib_to_rtfs_hostdisk_path       = $(RTFS_PATH)/rtfsdrivers/hostdisk
lib_to_rtfs_hostdev_path        = $(RTFS_PATH)/rtfsdrivers/hostdev
lib_to_linuxtarg                = $(RTFS_PATH)/rtfstargets/linux
lib_to_linuxrun                 = $(RTFS_PATH)/rtfsprojects/linux/source

#include path
lib_to_rtfs_include   = $(RTFS_PATH)/include
########################################################################

known = "N"


ifeq ($(targ),LINUX)
known = "Y"
# Set the compiler type for later conditionals
compiler = GNUCOMPILER
endif

ifneq ($(known),"Y")
.PHONY: error
error:
ifneq ($(targ),)
	@echo Incorrect target: $(targ)
else
	@echo Please set the targ and v variables.
endif
	@echo Please edit
endif

########################################################################

ifeq ($(compiler),GNUCOMPILER)
# Set the file extensions
obj = o
lib = a
exe = out

ifeq ($(dbv),v)
ccflags += -g
ldflags += -g
endif
endif
