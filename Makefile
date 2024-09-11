#
#   Assumes DAQ Env vars have been setup  and SPECTCL_HOME are defined to point
#   at a specific DAQ root and SpecTcl root dir.
#
# Building requires that SPECTCLROOT be defines as an env var or on the
# make line to specify the SpecTcl we build against.

#SPECTCLROOT=/usr/opt/spectcl/5.3-003

SPECLIB=$(SPECTCLROOT)/lib
SPECINC=-I$(SPECTCLROOT)/include

ROOTCXXFLAGS=$(shell $(ROOTSYS)/bin/root-config --cflags)
ROOTLDFLAGS=$(shell $(ROOTSYS)/bin/root-config --libs)

TCLCXXFLAGS=-I/usr/include/tcl8.6
TCLLDFLAGS=-ltcl8.6

CXX=mpiCC

all:   mpitcl libMpiSpectcl.so

mpitcl: mpitcl.cpp
	 $(CXX) -g  -o mpitcl mpitcl.cpp -I/usr/include/tcl8.6 \
	$(SPECINC) -I$(DAQINC) -L$(DAQLIB) $(ROOTCXXFLAGS) -ltclPlus -lException -Wl,-rpath=$(DAQLIB) \
	$(TCLLDFLAGS) -std=c++11 $(ROOTLDFLAGS)


libMpiSpectcl.so: mpiSpecTclPackage.cpp
	$(CXX) -g -c $(SPECINC) $(ROOTCXXFLAGS) $(TCLCXXFLAGS) -fPIC $^
	$(CXX) -g -shared -o $@ $(^:.cpp=.o) \
	-L$(SPECLIB) -lSpectcl -lTclGrammerApp \
	$(ROOTLDFLAGS) $(TCLLDFLAGS)
	echo package ifneeded mpispectcl 1.0 [list load [file join \$$dir libMpiSpectcl.so]] > pkgIndex.tcl


install:
	install -d $(PREFIX)
	install -d $(PREFIX)/bin
	install -d $(PREFIX)/TclLibs
	install -d $(PREFIX)/include
	install -m 0755 mpitcl $(PREFIX)/bin
	install -m 0755 libMpiSpectcl.so pkgIndex.tcl $(PREFIX)/TclLibs
	install -m 0644 mpitcl.h $(PREFIX)/include



clean:
	rm -f mpitcl
	rm -f *.o *.so
