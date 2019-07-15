#include <tcl.h>
#include <mpi.h>
#include <TCLInterpreter.h>
#include <TCLObjectProcessor.h>
#include <TCLObject.h>
#include <Exception.h>

#include <stdlib.h>
#include <iostream>
#include <stdexcept>

#include "mpitcl.h"

static Tcl_AppInitProc initInteractive;
static const int MPI_TAG_SCRIPT(1);                    // Tag for sending a script.
static const int MPI_TAG_TCLDATA(2);                   // Tag for sending Tcl encoded data.
static const int MPI_TAG_BINDATA(3);                   // Tag for sending Binary data.


/**
 * MPI extension class.
 *   mpi size    - returns size of application
 *   mpi rank    - returns my rank
 *   mpi execute rank script - sends script to rank.
 *   mpi send    rank data   - Sends Tcl text data to rank.
 *   mpi handle              - Specify event handler for data.
 *               the handler is invoked with two parameters:
 *               - the sender's rank
 *               - the data that was received from the sender.
 *
 *  Note that compiled code can TclMpi_SetDataHandler to catch binary data
 *  sent by other bits of the computation.
 */

class CTclMpi : public CTCLObjectProcessor
{
public:
  CTclMpi(const char* command, CTCLInterpreter& interp);

  int operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
protected:
  void size(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
  void rank(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
  void execute(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
  void send(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
  void handle(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
private:
  void executeScript(int rank, const std::string&  script) {
    MPI_Send(
       script.c_str(), script.size() + 1, MPI_CHAR, rank, MPI_TAG_SCRIPT,
       MPI_COMM_WORLD
    );
  }
  int  myrank() {
    
    int r;
    MPI_Comm_rank(MPI_COMM_WORLD, &r);
    return r;
  }

  int  appsize() {
     int size;
     MPI_Comm_size(MPI_COMM_WORLD, &size);
     return size;
  }
  void sendData(int rank, const std::string& data) {
    MPI_Send(
      data.c_str(), data.size() + 1, MPI_CHAR, rank, MPI_TAG_TCLDATA,
      MPI_COMM_WORLD
    );
  }
public:
  CTCLObject*  m_pDataHandler;
};

/**
 * size subcommand.
 */
void
CTclMpi::size(CTCLInterpreter& interp, std::vector<CTCLObject>& objv)
{
  requireExactly(objv, 2);
  int size = appsize();
  CTCLObject result;
  result.Bind(interp);
  result = size;
  interp.setResult(result);
}
/**
 *  rank subcommand.
 */
void
CTclMpi::rank(CTCLInterpreter& interp, std::vector<CTCLObject>& objv)
{
  requireExactly(objv, 2);
  int r = myrank();
  CTCLObject result;
  result.Bind(interp);
  result = r;
  interp.setResult(result);
}
/**
 * execute
 *  Requires a rank and a script.
 *  Special ranks are:
 *     all - Every process including this one.
 *     others - Every process except this one.
 *  For any other process but this, the script is executed by sending a message
 *  with the script tag (MPI_TAG_SCRIPT). For this process, we just
 *  directly execute the script in the interpreter at the global level.
 */
void
CTclMpi::execute(CTCLInterpreter& interp, std::vector<CTCLObject>& objv)
{
  requireExactly(objv, 4);
  bindAll(interp, objv);
  
  std::string rank = objv[2];
  std::string script = objv[3];

  int s    = appsize();
  int r    = myrank();

  // Check for special ranks:

  if (rank == "all") {
    for (int i =0; i < s; i++) {
      if (i != r) {
	executeScript(i, script);
      }
    }
    interp.GlobalEval(script);	//  we're always last so e.g. exit works.
  } else if (rank == "others") {
      for (int i =0; i < s; i++) {
	if (i != r) {
	  executeScript(i, script);
	}
      }
  } else {
      // Rank must be a numeric rank < s.
    
    int receiver = objv[2];
    if ((receiver < s) && (receiver >= 0)) {
      if (receiver != r) {
	executeScript(receiver, script);
      } else {
	interp.GlobalEval(script);
      }
    } else {
      throw std::string("Invalid rank for execute");
    }
  }
  
}
/**
 * send
 *   Execute the subcommand to send Tcl formatted data.
 *   As with execute, the special ranks others and all
 *   Send data to all other ranks and to ourselves.
 */
void
CTclMpi::send(CTCLInterpreter& interp, std::vector<CTCLObject>& objv)
{
  requireExactly(objv, 4);          // cmd, sub, rank, data.
  bindAll(interp, objv);

  std::string sRank = objv[2];
  std::string data  = objv[3];
  
  // The special ranks other and all apply:
  
  if (sRank == "others") {
    for (int i =0; i < appsize(); i++) {
      if (i != myrank()) {
	sendData(i, data);
      }
    }
  } else if (sRank == "all") {
    for (int i =0; i < appsize(); i++) {
      sendData(i, data);
    }
  } else {
    int r = objv[2];
    if ((r < 0) || (r >= appsize())) {
      throw std::string("Invalid rank for send");
    }
    sendData(r, data);
  }
}

/**
 * handle
 *   Data receive handler manipulation:
 *      - If there's a nonempty extra parameter, that's the new handler script.
 *      - If there's an empty handler parameter, the current handler is removed.
 *      - If there's no handler parameter, the current handler script is passed back
 *        as the result.
 */
void
CTclMpi::handle(CTCLInterpreter& interp, std::vector<CTCLObject>& objv)
{
  requireAtMost(objv, 3);
  bindAll(interp, objv);

  //  if there's no script parameter just return the current one if there is or
  //  an empty string if there isn't

  if (objv.size() == 2) {
    if (m_pDataHandler != nullptr) {
      interp.setResult(*m_pDataHandler);
    } else {
      interp.setResult("");                    // ther isn't a current handler.
    }
  } else {
    // There's a script.  Is it an empty string:
    
    if (std::string(objv[2]) == "") {
      delete m_pDataHandler;
      m_pDataHandler = nullptr;
    } else {
      // Set the data handler script -- creating the object if needed.
      
      if (m_pDataHandler == nullptr) {
	m_pDataHandler = new CTCLObject;
	m_pDataHandler->Bind(interp);
      }
      
      (*m_pDataHandler) = objv[2].getObject();
    }
    
    
  }
}
/**
 * CtclMpi constructor  just register us.
 */
CTclMpi::CTclMpi(const char* command, CTCLInterpreter& interp) :
  CTCLObjectProcessor(interp, command, true), m_pDataHandler(nullptr)
{
}

/**
 * operator()
 *   Executes the command.
 */
int
CTclMpi::operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv)
{
  // require a subcommand:

  try {
    requireAtLeast(objv, 2);
    std::string subcommand = objv[1];
    if (subcommand == "size") {
      size(interp, objv);
    } else if (subcommand == "rank") {
      rank(interp, objv);
    } else if (subcommand == "execute") {
      execute(interp, objv);
    } else if (subcommand == "send" ) {
      send(interp, objv);
    } else if (subcommand == "handle") {
      handle(interp, objv);
    } else {
      std::string msg = "Unrecognized subcommand: ";
      msg += std::string(objv[0]);
      msg += " " ;
      msg += subcommand;
      throw msg;
    }
  }
  catch (CException& e) {
    interp.setResult(e.ReasonText());
    return TCL_ERROR;
  }
  catch (std::string msg) {
    interp.setResult(msg);
    return TCL_ERROR;
  }
  catch (const char* msg) {
    interp.setResult(msg);
    return TCL_ERROR;
  }
  catch (std::exception& e) {
    interp.setResult(e.what());
    return TCL_ERROR;
  }
  catch (...) {
    interp.setResult("Unexpected exception type");
    return TCL_ERROR;
  }
  

  return TCL_OK;
}

//////////////////////////////////////////////////////////////////////////////////////


CTclMpi* gpMpiCommand(nullptr);
void loadMPIExtensions(CTCLInterpreter& interp)
{
  Tcl_CreateNamespace(interp.getInterpreter(), "mpi", nullptr, nullptr);

  gpMpiCommand = new CTclMpi("mpi::mpi", interp);
}

MPIBinDataHandler gpBinaryDataHandler(nullptr);

void
MPITcl_setBinaryDataHandler(MPIBinDataHandler handler)
{
  gpBinaryDataHandler = handler;
}


/**
 * Main loop of non rank 0  processes
 *
 *   Probe for messages and dispatch them according to the tag
 *   Messages can be scripts that get executed in our interpreter.
 *   Tcl data that can be passed to a tcl script established via mpi handle
 *   and binary data that can be passed to compiled code set via
 *   TclMpi_SetDataHandler e.g.
 */
void childMainLoop(CTCLInterpreter& interp)
{
  MPI_Status probeStat;
  int        count;
  int        myrank;  
  MPI_Comm_rank(MPI_COMM_WORLD, &myrank);

  
  while(1) {			// Exit will be done by tcl command e.g.
    MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,  &probeStat);
    int tag = probeStat.MPI_TAG;             // Type of message.
    MPI_Get_count(&probeStat, MPI_CHAR, &count);

    char msg[count];          // For now stack allocate.

    MPI_Recv(msg, count, MPI_CHAR, probeStat.MPI_SOURCE, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    switch(tag) {
    case MPI_TAG_SCRIPT:
      {
	interp.GlobalEval(msg);
	break;
      }
    case MPI_TAG_TCLDATA:
      if (gpMpiCommand->m_pDataHandler) {
	CTCLObject fullCommand;
	fullCommand.Bind(interp);
	fullCommand += *gpMpiCommand->m_pDataHandler;   // base command.
	fullCommand += probeStat.MPI_SOURCE;
	fullCommand += msg;
	fullCommand();
      }
      break;
    case MPI_TAG_BINDATA:
      if (gpBinaryDataHandler) {
	(*gpBinaryDataHandler)(probeStat.MPI_SOURCE, count, msg);
      }
      break;
    default:
      std::cerr << "Unrecognized MPI tag type : " << tag << " message ignored\n";
    }
  }
}

static void finalize(ClientData d)
{
  MPI_Finalize();
  exit((long int)d);
}

static void startMpiReceiverThread()
{
}

/**
 * main
 *   For the rank 0 process, we create an interactive interpreter.
 *   For all other processes, we create a captive interpreter.
 *   Both interpreters get MPI extensions.
 *   The non-rank 0 process runs a main loop that consists of getting
 *   MPI messages, ensuring they are command tags and executing
 *   them in the interpreter.
 */

int main(int argc, char** argv)
{

  int myRank;
  int type;
  MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &type);
  MPI_Comm_rank(MPI_COMM_WORLD, &myRank);


  
  if (myRank == 0) {

    Tcl_Main(argc, argv , initInteractive);
  } else {
    CTCLInterpreter interp;                     // Make a new interp.
    Tcl_Init(interp.getInterpreter());          // Init the interpreter. as well.
    Tcl_SetExitProc(finalize);
    loadMPIExtensions(interp);
    childMainLoop(interp);
  }

  MPI_Finalize();
}



/**
 * Rank 0 interpreter intialization handler.
 */
static int initInteractive(Tcl_Interp* pRawInterpreter)
{
  Tcl_Init(pRawInterpreter);
  CTCLInterpreter* pInterp = new CTCLInterpreter(pRawInterpreter);
  loadMPIExtensions(*pInterp);

  Tcl_SetExitProc(finalize);
  startMpiReceiverThread();

  return TCL_OK;
}

void* gpTCLApplication(0);
