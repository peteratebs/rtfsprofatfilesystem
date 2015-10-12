# GNU Makefile for WebC
incpath = .

### Set this to find mkgnu.inc, rulesgnu.inc and lib_xxx dirs in mkgnu
RTFS_PATH = ../../..

include ../mkgnu.inc

includes = . $(lib_to_rtfs_include) $(lib_to_linuxrun)

########################################################################
# SOURCE PATHS
########################################################################
vpath %.c   . $(lib_to_linuxrun) $(lib_to_linuxtarg) $(lib_to_rtfs_commonsource_path) $(lib_to_rtfs_commonapps_path) $(lib_to_rtfs_pro_path) $(lib_to_rtfs_failsafe_path) $(lib_to_rtfs_commonapps_path) $(lib_to_rtfs_proplussource_path) $(lib_to_rtfs_proplusdvr_path) $(lib_to_rtfs_proplusapps_path) $(lib_to_rtfs_ramdisk_path) $(lib_to_rtfs_hostdisk_path) $(lib_to_rtfs_hostdev_path)

vpath %.h   .. $(lib_to_rtfs_include) $(lib_to_linuxrun)

########################################################################
# OPERATING SYSTEM/ENVIRONMENT SPECIFIC MODULES
########################################################################
# The only files that should go here are files that are specific to one
# particular target, operating system, compiler, etc. and must go into
# the library.  Make sure your conditional(s) are as general as possible.

ifeq ($(os),LINUX)
#modules +=
endif

########################################################################
# EBS PACKAGES
########################################################################

ifeq ($(dortupnp),"Y")
# defines +=
defines +=
endif


#############################################################################
# SETUP
#############################################################################

# The basic required list of modules, defines, includes.

# clean up the modules, includes, defines
# dirs = $(lib_to_webc_path)


dirs := $(lib_to_rtfs_commonsource_path)
dirs += $(lib_to_rtfs_pro_path)
dirs += $(lib_to_rtfs_failsafe_path)
dirs += $(lib_to_rtfs_proplussource_path)
dirs += $(lib_to_rtfs_proplusdvr_path)
dirs += $(lib_to_rtfs_ramdisk_path)
dirs += $(lib_to_linuxtarg)

appdirs += $(lib_to_rtfs_commonapps_path)
appdirs += $(lib_to_rtfs_proplusapps_path)


#(error dirs = $(dirs))
#(error appdirs = $(appdirs))

cpfiles := $(foreach dir,$(dirs),$(wildcard $(dir)/*.cpp))
cpmodules := $(patsubst %.cpp,%,$(cpfiles))
cfiles := $(foreach dir,$(dirs),$(wildcard $(dir)/*.c))
cfilesapp := $(foreach dir,$(appdirs),$(wildcard $(dir)/*.c))


#EXAMPLES C FILES USED
cfilesapp += $(lib_to_linuxrun)/rtfscallbacks.c
cfilesapp += $(lib_to_linuxrun)/rtfsconfig.c
cfilesapp += $(lib_to_linuxrun)/runrtfsdemo.c
#DEVICE DRIVERS
cfilesapp += $(lib_to_rtfs_hostdev_path)/drlindev.c
cfilesapp += $(lib_to_rtfs_hostdisk_path)/drhostdsk.c


cmodules := $(patsubst %.c,%,$(cfiles))
cappmodules := $(patsubst %.c,%,$(cfilesapp))

modules += $(cpmodules)
modules += $(cmodules)

modules  := $(sort $(modules))
#includes := $(sort $(includes))
defines  := $(sort $(defines))

# now set up the object list
blah1 = $(modules:%=%.$(obj))
objects = $(notdir $(blah1))
OUT_DIR=./bin
OUT_OBJS = $(addprefix $(OUT_DIR)/,$(objects))

blah2 = $(cappmodules:%=%.$(obj))
appobjects = $(notdir $(blah2))
APPOUT_DIR=./appbin
APPOUT_OBJS = $(addprefix $(APPOUT_DIR)/,$(appobjects))



$(warning CFILES = $(cfiles))
$(warning CFILESAPP = $(cfilesapp))
#############################################################################
# RULESa
#############################################################################
all: rtfsdemo.out
$(OUT_DIR)/rtfs.a: $(OUT_OBJS)
rtfsdemo.out: rtfsdemo.o $(APPOUT_OBJS) $(OUT_DIR)/rtfs.a

rtfsdemo.o: rtfsdemo.c $(APPOUT_OBJS)
	echo COMPILING $<
	gcc	$(cflags) $(defines:%=-D%) $(includes:%=-I%) -c $<	-o $(notdir $@)


include ../rulesgnu.inc
#############################################################################
