#include <tcl.h>
#include <mpi.h>
#include <TCLInterpreter.h>
#include <TCLObjectProcessor.h>
#include <TCLObject.h>
#include <Exception.h>
#include <TCLLiveEventLoop.h>

#include <stdlib.h>
#include <iostream>
#include <stdexcept>

#include "mpitcl.h"

static Tcl_AppInitProc initInteractive;
static void startMpiReceiverThread(CTCLInterpreter& interp, Tcl_ThreadId mainThread);

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
  void stopNotifier(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
  void startNotifier(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
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
 * stopNotifier
 *    Only legal for rank 0 - stop the notifier thread.  This is done
 *    by sending ourselves a zer length message with MPI_TAG_STOPTHREAD.
 *    On receipt of this, the probe thread exits.
 *
 * @param interp -  interpreter running the thread.
 * @param objv   -  command parameters (none but the command word).
 * @note An error is signalled if this is called in a non rank0 process.
 * @note Calling this method when there is no notifier thread will result in
 *      an handled message with the MPI_TAG_STOPTHREAD tag and zero length
 *      queued to RANK 0 --  in general this is not desirable.
 * @note - If for some reason (see startNotifier) more than one notifier thread
 *         is running, this method will only stop one of them.  It's also possible
 *         in that case for MPI_Recv to block in one or all of the other notifier
 *         threads -- in other words; Dont't. Do. It.
 */
void
CTclMpi::stopNotifier(CTCLInterpreter& interp, std::vector<CTCLObject>& objv)
{
  requireExactly(objv, 2);
  if (myrank() != 0) {
    throw std::string("stopnotifier can only be used in rank 0");
  }
  char buf='0';
  MPI_Send(&buf, 0, MPI_CHAR, 0, MPI_TAG_STOPTHREAD, MPI_COMM_WORLD);
}
/**
 * startNotifier
 *    Starts the notifier thread.  This is only legal in rank 0 processes.
 *    See  the notes in stopNotifier for som eof the pitfalls to avoid.
 *
 *  @param interp -the interpreter executing the command.
 *  @param objv   - The command parametesrs.
 */
void
CTclMpi::startNotifier(CTCLInterpreter& interp, std::vector<CTCLObject>& objv)
{
  requireExactly(objv, 2);
  if (myrank() != 0) {
    throw std::string("startnotifier can only be used in rank 0");
  }
  startMpiReceiverThread(interp, Tcl_GetCurrentThread());
  
}
/**
 * operator()
 *   Executes the mpi::mpi command.
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
    } else if (subcommand == "stopnotifier") {
      stopNotifier(interp, objv);
    } else if (subcommand == "startnotifier") {
      startNotifier(interp, objv);
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
 * mpiEventProcessor
 *   Called to process an MPI event.
 *   @param interp - references the TCL interpeter we're running.
 *   @param probeStat - references probe status that caused this to be
 *                      called.
 */
void
mpiEventProcessor(CTCLInterpreter& interp, MPI_Status& probeStat)
{
  int tag = probeStat.MPI_TAG;             // Type of message.
  int        count;

  MPI_Get_count(&probeStat, MPI_CHAR, &count);
  
  char msg[count];          // For now stack allocate.
  
  MPI_Recv(msg, count, MPI_CHAR, probeStat.MPI_SOURCE, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
  
  switch(tag) {
  case MPI_TAG_SCRIPT:
    {
      int myrank;
      MPI_Comm_rank(MPI_COMM_WORLD, &myrank);
      interp.GlobalEval(msg);
      break;
    }
  case MPI_TAG_TCLDATA:
    if (gpMpiCommand->m_pDataHandler) {
      CTCLObject fullCommand;
      fullCommand.Bind(interp);
      fullCommand = *gpMpiCommand->m_pDataHandler;   // base command.
      fullCommand += probeStat.MPI_SOURCE;
      fullCommand += msg;
      std::string result = interp.GlobalEval(std::string(fullCommand));
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
  int        myrank;  
  MPI_Comm_rank(MPI_COMM_WORLD, &myrank);
  try {
  
    while(1) {			// Exit will be done by tcl command e.g.
      MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,  &probeStat);
      mpiEventProcessor(interp, probeStat);
    }
  } catch (CException& e) {
    std::cerr << myrank << " Exception: " << e.ReasonText() << std::endl;
  }
}

static void finalize(ClientData d)
{
  MPI_Finalize();
  exit((long int)d);
}

struct mpiThreadData {
  Tcl_ThreadId     s_mainId;
  CTCLInterpreter* s_pInterp;
};

struct mpiEvent {
  Tcl_Event        s_event;
  CTCLInterpreter* s_pInterp;
  MPI_Status       s_status;
};


int mpiEventHandler(Tcl_Event* pRawEvent, int flags)
{
  mpiEvent* pEvent = reinterpret_cast<mpiEvent*>(pRawEvent);
  mpiEventProcessor(*pEvent->s_pInterp, pEvent->s_status);
  startMpiReceiverThread(*pEvent->s_pInterp, Tcl_GetCurrentThread());   // Restart the receiver thread.

  return 1;
}

void mpiProbeThread(ClientData p)
{
  mpiThreadData* pData = static_cast<mpiThreadData*>(p);
  
  struct mpiEvent e;			//  Template event.
  e.s_event.proc   = mpiEventHandler;
  e.s_event.nextPtr= nullptr;
  e.s_pInterp      = pData->s_pInterp;

  
  MPI_Status probeStat;
  MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,  &probeStat);
  if (probeStat.MPI_TAG  == MPI_TAG_STOPTHREAD) {      // Being asked to exit.
    char buf[1];
    int  count;
    MPI_Recv(                                           // Recv the token msg.
      buf, count, MPI_CHAR, probeStat.MPI_SOURCE, probeStat.MPI_TAG,
      MPI_COMM_WORLD, MPI_STATUS_IGNORE
    );
    delete pData;
    return;
  }
  struct mpiEvent* pEvent =
    reinterpret_cast<struct mpiEvent*>(Tcl_Alloc(sizeof(mpiEvent)));
  memcpy(pEvent, &e, sizeof(struct mpiEvent));
  pEvent->s_status = probeStat;
  

  Tcl_ThreadQueueEvent(
      pData->s_mainId, reinterpret_cast<Tcl_Event*>(pEvent), TCL_QUEUE_TAIL
  );
  Tcl_ThreadAlert(pData->s_mainId);
  delete pData;
}

/**
 * startMpiReceiverThread
 *   Starts the thread that probes for mpi data available.
 * 
 * @param interp - references the interpreter of this thread -- I think
 *                 this can be ignored.
 * @param mainThread - thread to which events are queues.
 */
static void startMpiReceiverThread(CTCLInterpreter& interp, Tcl_ThreadId mainThread)
{
  
  mpiThreadData* pThreadData = new mpiThreadData;
  pThreadData->s_mainId = mainThread;
  pThreadData->s_pInterp = &interp;
  
  Tcl_ThreadId child;
  Tcl_CreateThread(
     &child, mpiProbeThread, reinterpret_cast<ClientData>(pThreadData),
     TCL_THREAD_STACK_DEFAULT, TCL_THREAD_NOFLAGS
   );
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
  startMpiReceiverThread(*pInterp, Tcl_GetCurrentThread());

  // Now run an event loop:

  //CTCLLiveEventLoop* pEventLoop =  CTCLLiveEventLoop::getInstance();
  //pEventLoop->start(pInterp);
  
  return TCL_OK;
}

void* gpTCLApplication(0);
