
# nssharechannel --
#
#      Allow to shared TCL channel between interpreters
#

ifdef INST
    NSHOME ?= $(INST)
else
    NSHOME ?= ..
endif

#
# Module name
#
MOD       =  nssharechannel.so

#
# Objects to build.
#
MODOBJS   =  nssharechannel.o


include  $(NSHOME)/include/Makefile.build

