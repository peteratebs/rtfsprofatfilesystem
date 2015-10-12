#include $(targ).inc

cflags  += -g
ccflags += -g

ifndef $(OUT_DIR)
	OUT_DIR=./bin
endif
#### Creating an output directory
$(shell mkdir $(OUT_DIR))

ifndef $(APPOUT_DIR)
	APPOUT_DIR=./appbin
endif
#### Creating an output directory
$(shell mkdir $(APPOUT_DIR))

ifeq ($(compiler),GNUCOMPILER)
%.$(lib):
	echo ARCHIVING $@
	ar ru $@ $^

$(OUT_DIR)/%.$(obj): %.c
	echo COMPILING $<
	echo CURRENT_PATH=$(shell pwd)
	gcc	$(cflags) $(defines:%=-D%) $(includes:%=-I%) -c $<	-o $(OUT_DIR)/$(notdir $@)

$(APPOUT_DIR)/%.$(obj): %.c
	echo COMPILING $<
	echo CURRENT_PATH=$(shell pwd)
	gcc	$(cflags) $(defines:%=-D%) $(includes:%=-I%) -c $<	-o $(APPOUT_DIR)/$(notdir $@)

#%.$(obj): %.cxxa
#	echo COMPILING $<
#	$(toolprefix)gcc $(ccflags)	$(defines:%=-D%) $(includes:%=-I%) -c $< -o $(notdir $@)

$(OUT_DIR)/%.$(obj): %.cpp
	echo xxx %.cpp COMPILING $<
	echo CURRENT_PATH=$(shell pwd)
	$(toolprefix)gcc $(ccflags)	$(defines:%=-D%) $(includes:%=-I%) -c $< -o $(OUT_DIR)/$(notdir $@)

%.$(exe):
	echo LINKING $@
	g++ $(ldflags) -o $(notdir $@)	$^ -lm -lc -lgcc -pthread

endif #	GNUCOMPILER

#### Generic Stuff


# Make the web page	files
#%.c %.h: %.htm	%.cla
#	echo BINTOC	$<
#	../webpage/bintoc $(subst /,\\,$<) $(subst /,\\,$(basename $(notdir	$<)))

# Make dependency files
%.d: %.c
	echo UPDATING DEPENDENCIES FOR $<
	gcc	-undef -M $< $(defines:%=-D%) $(builtins:%=-D%)	$(includes:%=-I%) |	depend $(obj) >	$@

.INTERMEDIATE: $(modules:%=%.d)
$(targ).inc: $(modules:%=%.d)
	echo MAKING	$@
	cat	*.d	> $@

.PHONY:	CLEAN
CLEAN:
	echo CLEANING
ifneq ($(dist),SRCONLY)
	rm -f	$(OUT_DIR)
endif
	rm -f	$(APPOUT_DIR)/*
	rm -f   *.o
	rm -f	*.a
	rm -f   *.out

.PHONY:	clean
clean:
	echo CLEANING
ifneq ($(dist),SRCONLY)
	rm -f	$(OUT_DIR)
endif
	rm -f	$(APPOUT_DIR)/*
	rm -f   *.o
	rm -f	*.a
	rm -f   *.out


.PHONY:	dep
dep:
	echo DEPENDENCIES UPDATED
