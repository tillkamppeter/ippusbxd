SHELL := /bin/bash

################################################################################
BuildTargets := all clean configure redep distclean 
.PHONY: $(BuildTargets)

################################################################################
all:
ifeq ($(wildcard exe/Makefile),)
	$(MAKE) configure
	$(MAKE) -C exe
else
	$(MAKE) -C exe
endif

################################################################################
configure:
	rm -rf ./exe ; mkdir -p exe
	cd exe/ ; cmake ../src

################################################################################
redep:
	cd exe/ ; cmake ../src ; cd ..

################################################################################
clean:
	$(MAKE) -C exe clean

################################################################################
distclean:
	if [ -f exe/Makefile ]; \
		then $(MAKE) -C exe clean ;\
	fi
	rm -rf exe

	
