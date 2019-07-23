/*
    This software is Copyright by the Board of Trustees of Michigan
    State University (c) Copyright 2017.

    You may use this software under the terms of the GNU public license
    (GPL).  The terms of this license are described at:

     http://www.gnu.org/licenses/gpl.txt

     Authors:
             Ron Fox
             Giordano Cerriza
	     NSCL
	     Michigan State University
	     East Lansing, MI 48824-1321
*/

/** @file:  mpiSpecTclPackage.cpp
 *  @brief: provide mpispectcl loadable package. Requires mpitcl.
 */
#include "mpitcl.h"
#include <mpi.h>
#include <TCLInterpreter.h>
#include <TCLObjectProcessor.h>
#include <TCLObject.h>
#include <Exception.h>
#include <CDataGetter.h>
#include <CDataDistributor.h>
#include <CAnalyzeCommand.h>

#include <tcl.h>
#include <stdexcept>
#include <string>
#include <set>
#include <iostream>

//////////////////////////////////////////////////////////////////////////////
// MPIGetter and MPIDistributor.
//
//   The distributor is rank 0 and getter all other ranks.
//

/**
 * @class CMPIDataGetter
 *     Gets data from an MPI data source (usually rank 0).
 *     This uses a pull protocol:
 *     -  We send a request for data to some rank with MPI_TAG_BIN_DATA.
 *     -  That rank always replies with something.  The reply is a zero length
 *        block of data if there's no more data to give.
 *
 */
class CMPIDataGetter : public CDataGetter
{
private:
    int m_sourceRank;
public:
    CMPIDataGetter(int rank);
    
    virtual std::pair<size_t, void*> read();
    virtual void free(std::pair<size_t, void*>& data);
};

// Implementation:

/**
 * constructor
 *   @param rank - the MPI rank of the process from which we get data.
 */
CMPIDataGetter::CMPIDataGetter(int rank) :
    m_sourceRank(rank)
{}

/**
 * read
 *   - Send a data request to rank 0 for a block of data.
 *   - Use MPI_Probe to figure out how much data I'm going to get.
 *   - Read the data
 * @return std::pair<size_t, void*> - describing the read data.
 *                                    size == 0 means expect no more data.
 */
std::pair<size_t, void*>
CMPIDataGetter::read()
{
    char dummy;
    MPI_Send(&dummy, 0, MPI_CHAR, 0, MPI_TAG_BINDATA, MPI_COMM_WORLD); // data req.
    
    MPI_Status stat;
    int        nBytes;
    MPI_Probe(0, MPI_TAG_BINDATA, MPI_COMM_WORLD, &stat);
    MPI_Get_elements(&stat, MPI_CHAR, &nBytes);
    
    char* pData = new char[nBytes];
    MPI_Recv(
        pData, nBytes, MPI_CHAR, 0, MPI_TAG_BINDATA, MPI_COMM_WORLD,
        MPI_STATUS_IGNORE
    );
    std::pair<size_t, void*> result;
    result.first = nBytes;
    result.second= pData;
    
    return result;
}

/**
 * free
 *    Free dynamic data gotten by read
 * @param data - descriptor of  data gotten from read.
 */
void
CMPIDataGetter::free(std::pair<size_t, void*>& data)
{
    char* pBytes = static_cast<char*>(data.second);
    delete []pBytes;
}

////////////////////////////////////////////////////////////////////////////////

/**
 * @class CMPIDistributor
 *    Distributes data to  parallel workers.
 *    - Waits for a data request.
 *    - Remebers the requestor in the set of requestors.
 *    - If there's more data send it to the requestor otherwise,
 *      send end of data indicators to requestors until none are left
 */
class CMPIDistributor : public CDataDistributor
{
private:
    std::set<int>   m_clientRanks;
public:
    
    virtual void handleData(std::pair<size_t, void*>& info);
    
private:
    void runDownConsumers();
    void endFileToConsumer(int rank);
};

// CMPIDistributor implementation.

/**
 * handleData
 *    Distribute the data we've been given to the next requestor or,
 *    in the case of an end data indicator to all currently known consumers.
 *
 * @param info - size and pointer to the data.
 */
void
CMPIDistributor::handleData(std::pair<size_t, void*>& info)
{
    // If the data are an end rundown the consumers:
    if(info.first == 0) {
        runDownConsumers();
    } else {
        // Get the next request
        
        char data;
        MPI_Status stat;
        MPI_Recv(
            &data, 0, MPI_CHAR, MPI_ANY_SOURCE, MPI_TAG_BINDATA,  MPI_COMM_WORLD,
            &stat
        );
        int to = stat.MPI_SOURCE;
        
        MPI_Send(
            info.second, info.first, MPI_CHAR, to, MPI_TAG_BINDATA, MPI_COMM_WORLD
        );
        m_clientRanks.insert(to);
    }
}
/**
 * runDownConsumers
 *     Send end datas to all known consumers.
 */
void
CMPIDistributor::runDownConsumers()
{
    
    MPI_Status stat;
    char       data;

    while (!m_clientRanks.empty()) {
    
        MPI_Recv(
            &data, 0, MPI_CHAR, MPI_ANY_SOURCE, MPI_TAG_BINDATA, MPI_COMM_WORLD,
            &stat
        );
        endFileToConsumer(stat.MPI_SOURCE);
    }
}
/**
 * endFileToConsumer
 *    Send and end of file to a consumer.
 *
 *    @param rank - the rank of the consumer.
*/
void
CMPIDistributor::endFileToConsumer(int rank)
{
    char data;
    MPI_Send(&data, 0, MPI_CHAR, rank, MPI_TAG_BINDATA, MPI_COMM_WORLD);
    m_clientRanks.erase(rank);
}
///////////////////////////////////////////////////////////////////////////////
// Commands to set the data getter and the data distributor.

/**
 * @class CMPISourceCommand
 *     Command processor that sets the data source to be an MPI data
 *     source.  This is normally done rank not zero workers.
 */
class CMPISourceCommand : public CTCLObjectProcessor
{
public:
    CMPISourceCommand(CTCLInterpreter& interp);
    
    int operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
};
/**
 * constructor
 *    Construct with the command mpisource
 *
 * @param interp - references the interpreter on which the command is being
 *                 registered.
 */
CMPISourceCommand::CMPISourceCommand(CTCLInterpreter& interp) :
    CTCLObjectProcessor(interp, "mpisource", true)
{}

/**
 * operator()
 *     Execute the mpisource command.
 *     - Ensure there are no parameters.
 *     - Create an MPIDataGetter object.
 *     - Set it as the data getter for the analyze command.
 * @param interp - references the interpreter running the command.
 * @param objv   - Referencew the vector of commannd words.
 * @return int   - Tcl command status.
 * @note - in future implementations where a more complex data flow is possible,
 *         we may want to support a from rank parameter.
 */
int
CMPISourceCommand::operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv)
{
    try {
        requireExactly(objv, 1);
        
        CAnalyzeCommand::setDataGetter(new CMPIDataGetter(0));
    }
    catch (CException& e) {
        interp.setResult(e.ReasonText());
        return TCL_ERROR;
    } catch (std::exception& e) {
        interp.setResult(e.what());
        return TCL_ERROR;
    } catch (std::string msg) {
        interp.setResult(msg);
        return TCL_ERROR;
    } catch (const char* msg) {
        interp.setResult(msg);
        return TCL_ERROR;
    } catch(...) {
        interp.setResult("Unanticipated exception type thrown");
        return TCL_ERROR;
    }

    return TCL_OK;

}
/**
 * @class CMPISinkCommand
 *    The mpisink command provides a way to set the analyzer's sink to
 *    an MPI distributor.  This is normally done in rank  0
 */
class CMPISinkCommand : public CTCLObjectProcessor
{
public:
    CMPISinkCommand(CTCLInterpreter& interp);
    int operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
};
/**
 * constructor
 *    
 *    @param interp -references the interpreter on which the command will be
 *                   registered.
 *    @note the command is hard-coded to "mpisink"
 */
CMPISinkCommand::CMPISinkCommand(CTCLInterpreter& interp) :
    CTCLObjectProcessor(interp,"mpisink", true)
{
}
/**
 * operator()
 *    Run the command.
 *  @param interp -the interpreter in which the command is being run.
 *  @param objv   -the vector of command words (there can be only one).
 *  @return int   - Tcl status of the command.
 */
int
CMPISinkCommand::operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv)
{
    try {
       requireExactly(objv, 1);
       CAnalyzeCommand::setDistributor(new CMPIDistributor);
    } catch (CException& e) {
        interp.setResult(e.ReasonText());
        return TCL_ERROR;
    } catch (std::exception& e) {
        interp.setResult(e.what());
        return TCL_ERROR;
    } catch (std::string msg) {
        interp.setResult(msg);
        return TCL_ERROR;
    } catch (const char* msg) {
        interp.setResult(msg);
        return TCL_ERROR;
    } catch(...) {
        interp.setResult("Unanticipated exception type thrown");
        return TCL_ERROR;
    }

    return TCL_OK;
}


///////////////////////////////////////////////////////////////////////////////
//  Package initialization.


const char* packageName = "mpispectcl";
const char* version = "1.0";

extern "C" {
    int Mpispectcl_Init(Tcl_Interp* pRawInterp)
    {
        Tcl_PkgRequire(pRawInterp, "spectcl", "1.0", 0);   // We depend on the spectcl pkg.
        Tcl_PkgProvide(pRawInterp, packageName, version);
        
        
        CTCLInterpreter* pInterp = new CTCLInterpreter(pRawInterp);
        
        new CMPISourceCommand(*pInterp);     // add mpisource command.
        new CMPISinkCommand(*pInterp);       
        
        
        return TCL_OK;              // Package successful init.
    }
}
