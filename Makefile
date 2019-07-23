#
#   Assumes DAQ Env vars have been setup  and SPECTCL_HOME are defined to point
#   at a specific DAQ root and SpecTcl root dir.
#

SPECTCLROOT=/usr/opt/spectcl/5.2
SPECLIB=$(SPECTCLROOT)/lib
SPECINC=-I$(SPECTCLROOT)/include

ROOTCXXFLAGS=$(shell $(ROOTSYS)/bin/root-config --cflags)
ROOTLDFLAGS=$(shell $(ROOTSYS)/bin/root-config --libs)

TCLCXXFLAGS=-I/usr/include/tcl8.6
TCLLDFLAGS=-ltcl

CXX=mpiCC

all:   mpitcl libMpiSpectcl.so

mpitcl: mpitcl.cpp
	 $(CXX) -g  -o mpitcl mpitcl.cpp -I/usr/include/tcl8.6 \
	-I$(DAQINC) -L$(DAQLIB) -ltclPlus -lException -Wl,-rpath=$(DAQLIB) \
	-ltcl  -std=c++11


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
	install -m 0755 mpitcl $(PREFIX)/bin
	install -m 0755 libMpiSpectcl.so pkgIndex.tcl $(PREFIX)/TclLibs



clean:
	rm -f mpitcl
	rm -f *.o *.so
