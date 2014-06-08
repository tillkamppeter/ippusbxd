SHELL := /bin/bash

ifndef VERBOSE
	CMD_VERB := @
	NOPRINTD := --no-print-directory
endif

################################################################################
BuildTargets := all clean configure redep distclean 
.PHONY: $(BuildTargets)

################################################################################
all:
ifeq ($(wildcard exe/Makefile),)
	$(CMD_VERB) $(MAKE) $(NOPRINTD) configure
	$(CMD_VERB) $(MAKE) $(NOPRINTD) -C exe
else
	$(CMD_VERB) $(MAKE) $(NOPRINTD) -C exe
endif

################################################################################
configure:
	$(CMD_VERB) rm -rf ./exe ; mkdir -p exe
	$(CMD_VERB) cd exe/ ; cmake ../src

################################################################################
redep:
	$(CMD_VERB) cd exe/ ; cmake ../src ; cd ..

################################################################################
clean:
	$(CMD_VERB) $(MAKE) $(NOPRINTD) -C exe clean

################################################################################
distclean:
	$(CMD_VERB) if [ -f exe/Makefile ]; \
		then $(MAKE) $(NOPRINTD) -C exe clean ;\
	fi
	$(CMD_VERB) rm -rf exe

	
