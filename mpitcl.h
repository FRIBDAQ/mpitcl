#ifndef MPITCL_H
#define MPITCL_H

typedef void (*MPIBinDataHandler)(int, int, void*);

void MPITcl_setBinaryDataHandler(MPIBinDataHandler handler);

#endif
