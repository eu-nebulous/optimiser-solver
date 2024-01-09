/*==============================================================================
Execution control

The source file implements the static variables and functions of the Execution
control actor.

Author and Copyright: Geir Horn, University of Oslo
Contact: Geir.Horn@mn.uio.no
License: MPL2.0 (https://www.mozilla.org/en-US/MPL/2.0/)
==============================================================================*/

#include "Actor.hpp"
#include "Communication/NetworkEndpoint.hpp"
#include "ExecutionControl.hpp"

namespace NebulOuS
{

// -----------------------------------------------------------------------------
// Static variables
// -----------------------------------------------------------------------------

bool                    ExecutionControl::Running = true;
std::mutex              ExecutionControl::TerminationLock;
std::condition_variable ExecutionControl::ReadyToTerminate;

// -----------------------------------------------------------------------------
// Waiting function
// -----------------------------------------------------------------------------
//
// The function used to wait for the termination message simply waits on the
// condition variable until it is signalled by the message handler. As there 
// could be spurious wake-ups it is necessary to check if the actor is still 
// running  when the condition variable is signalled, and if so the calling 
// thread will just block again in another wait.

void ExecutionControl::WaitForTermination( void )
{
  while( Running )
  {
    std::unique_lock< std::mutex > Lock( TerminationLock );
    ReadyToTerminate.wait( Lock );
  }
}

// -----------------------------------------------------------------------------
// Stop message handler
// -----------------------------------------------------------------------------
//
// The stop message handler will first send the network stop message to the 
// session layer requesting it to coordinate the network shutdown and close all
// externally communicating actors. 

void ExecutionControl::StopMessageHandler( const StopMessage & Command, 
                                           const Address Sender )
{
  std::lock_guard< std::mutex > Lock( TerminationLock );

  Send( Theron::Network::ShutDown(), 
        Theron::Network::GetAddress( Theron::Network::Layer::Session ) );

  Running = false;
  ReadyToTerminate.notify_all();
}

// -----------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------
// 
// The only action taken by the constructor is to register the handler for the
// stop message.

ExecutionControl::ExecutionControl( const std::string & TheActorName )
: Actor( TheActorName ),
  StandardFallbackHandler( Actor::GetAddress().AsString() )
{
  RegisterHandler( this, &ExecutionControl::StopMessageHandler );
}

} // namespace NebulOuS