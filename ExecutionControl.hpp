/*==============================================================================
Execution control

The Solver Component should run as long as the application being optimised is 
running. This requires an external message to the Solver Component about when 
the Solver Component should shut down, and a way to stop other threads from 
progressing until the shut down message has been processed. 

The following Actor may run on its own, but it may also be included with 
another Actor to avoid running a separate thread just waiting for a single shut
down message. This Actor will therefore be base class for the Solver Manager
actor, but the implementation cannot be done there since the Solver Manager is
a templated actor, and knowlege about the template parameter would be necessary
to call the function to wait for termination. 

The threads calling the function to wait for termination will block until the 
required message is received.

The Agent is also involved with the general component status messages to be 
sent to the Solver's status topic.

Author and Copyright: Geir Horn, University of Oslo
Contact: Geir.Horn@mn.uio.no
License: MPL2.0 (https://www.mozilla.org/en-US/MPL/2.0/)
==============================================================================*/

#ifndef NEBULOUS_EXECUTION_CONTROL
#define NEBULOUS_EXECUTION_CONTROL

// Standard headers

#include <string_view>                          // For constant strings
#include <map>                                  // Standard maps
#include <sstream>                              // Stream conversion
#include <chrono>                               // For standard time points
#include <condition_variable>                   // Execution stop management
#include <mutex>                                // Lock the condtion variable

// Theron++ headers

#include "Actor.hpp"                            // Actor base class
#include "Utility/StandardFallbackHandler.hpp"  // Exception unhanded messages

// AMQ communication

#include "Communication/NetworkingActor.hpp"    // The networking actor
#include "Communication/AMQ/AMQMessage.hpp"
#include "Communication/AMQ/AMQjson.hpp"        // JSON messages to be sent

namespace NebulOuS 
{

/*==============================================================================

 Execution control

==============================================================================*/

class ExecutionControl
: virtual public Theron::Actor,
  virtual public Theron::StandardFallbackHandler,
  virtual public Theron::NetworkingActor< 
    typename Theron::AMQ::Message::PayloadType >
{
  // The mechanism used for blocking other threads will be to make them wait 
  // for a condition variable until the message handler for the exit message
  // will trigger and notifiy this variable.

private:

  static bool                    Running;
  static std::mutex              TerminationLock;
  static std::condition_variable ReadyToTerminate;

protected:

  // There is a status message class that can be used to send the status to 
  // other components.

  class StatusMessage
  : virtual public Theron::AMQ::JSONMessage
  {
  public:

    enum class State
    {
      Starting,
      Started,
      Stopping,
      Stopped
    };

  private:

    std::string ToString( State TheSituation )
    {
      static const std::map< State, std::string > StateString {
        {State::Starting, "starting"}, {State::Started, "started"}, 
        {State::Stopping, "stopping"}, {State::Stopped, "stopped"}  };

      return StateString.at( TheSituation );
    }

    std::string UTCNow( void )
    {
      std::ostringstream TimePoint;
      TimePoint << std::chrono::system_clock::now();
      return TimePoint.str();
    }

  public:

    StatusMessage( State TheSituation, 
                   std::string AdditionalInformation = std::string() )
    : JSONMessage( std::string( StatusTopic ),
                   { {"when", UTCNow() }, {"state", ToString( TheSituation ) },
                     {"message", AdditionalInformation } } )
    {}
  };

  // The status of the solver is communicated on the dedicated status topic

  static constexpr std::string_view StatusTopic 
                                     = "eu.nebulouscloud.solver.state";

public:

  // The function used to wait for the termination message simply waits on the
  // condition variable until it is signalled by the message handler. As there 
  // could be spurious wake-ups it is necessary to check if the actor is still 
  // running  when the condition variable is signalled, and if so the calling 
  // thread will just block again in another wait.
  //
  // Note that returning from this function does not imply that all actors have
  // closed and finished processing. One should wait for the local actor system
  // to close before deleting the local actors, see the normal function 
  // Actor::WaitForGlobalTermination()
  
  static void WaitForTermination( void );

  // The stop message has not yet been defined and it is defined as an empty
  // class here as a named placeholder for a better future definition.

  class StopMessage
  {
  public:

    StopMessage() = default;
    StopMessage( const StopMessage & Other )  = default;
    ~StopMessage() = default;
  };

protected:

  // The message handler will change the value of the flag indicating that the
  // Actor is running, and signalling the condition variable to indicate that 
  // the termination has started.

  virtual void StopMessageHandler( const StopMessage & Command, 
                                   const Address Sender );

  // The constructor is simply taking the name of the actor as parameter and
  // initialises the base classes.

public:

  ExecutionControl( const std::string & TheActorName );

  ExecutionControl() = delete;
  virtual ~ExecutionControl() = default;
};
        
}      // namespace NebulOuS 
#endif // NEBULOUS_EXECUTION_CONTROL
