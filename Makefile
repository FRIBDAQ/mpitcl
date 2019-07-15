#
#   Assumes DAQ Env vars have been setup  and SPECTCL_HOME are defined to point
#   at a specific DAQ root and SpecTcl root dir.
#


all:   mpitcl

mpitcl: mpitcl.cpp
	 mpiCC  -o mpitcl mpitcl.cpp -I/usr/include/tcl8.6 \
	-I$(DAQINC) -L$(DAQLIB) -ltclPlus -lException -Wl,-rpath=$(DAQLIB) \
	-ltcl  -std=c++11



clean:
	rm -f mpitcl
	rm -f *.o

