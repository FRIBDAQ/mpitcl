#ifndef MPITCL_H
#define MPITCL_H

typedef void (*MPIBinDataHandler)(int, int, void*);

void MPITcl_setBinaryDataHandler(MPIBinDataHandler handler);

static const int MPI_TAG_SCRIPT(1);                    // Tag for sending a script.
static const int MPI_TAG_TCLDATA(2);                   // Tag for sending Tcl encoded data.
static const int MPI_TAG_BINDATA(3);                   // Tag for sending Binary data.


#endif
