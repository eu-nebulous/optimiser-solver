/*==============================================================================
Solver Manager

This class handles the Execution Context mssage containing a time stamp and a 
set of variable value assignments.It manages a time sorted queue and dispatches
the first application execution context to the solver when the solver is ready.
The solution returned for a given execution context will be published together 
with the execution context and the maximal utility value found by the solver.

The solver actor class is given as a template argument to the solver manager,
and at least one solver actor is instantiated at start up. This to allow 
multiple solvers to run in parallel should this be necessary to serve properly
the queue of waiting application execution contexts. If there are multiple 
objects defined, they have to be optimised individualy, and for this purpose 
it would also be useful to have multiple solvers running in parallel working 
on the same problem, but for different objective functions. This will reduce 
the time to find the Pareto front [1] for the multi-objective optimisation 
problem.

The functionality of receiving and maintaining the work queue separately from 
the solver is done to avoid blocking the reception of new execution contexts 
while the solver searches for a solution in separate threads. This is done 
for other entities to use the solver to find the optised configuration, i.e.
feasible value assignments to all propblem variables, maximising the givne 
utiliy for a particular set of independent metric variables, i.e. the 
application execution context. The idea is that other components may use 
the solver in this way to produce training sets for machine learning methods
that aims to estimate the application's performance indicators or even the 
change in utility as a function of the varying the metric values of the 
application execution context. 

References:
[1] https://en.wikipedia.org/wiki/Pareto_front

Author and Copyright: Geir Horn, University of Oslo
Contact: Geir.Horn@mn.uio.no
License: MPL2.0 (https://www.mozilla.org/en-US/MPL/2.0/)
==============================================================================*/

#ifndef NEBULOUS_SOLUTION_MANAGER
#define NEBULOUS_SOLUTION_MANAGER

// Standard headers

#include <string_view>                          // Constant strings
#include <string>                               // Normal strings
#include <map>                                  // Multimap for the work queue
#include <unordered_set>                        // Solver ready status
#include <list>                                 // Pool of local solvers
#include <ranges>                               // Range based views
#include <algorithm>                            // Standard algorithms
#include <iterator>                             // For inserters
#include <sstream>                              // For nice error messages
#include <stdexcept>                            // Standard exceptions
#include <source_location>                      // Error location reporting
#include <condition_variable>                   // Execution stop management
#include <mutex>                                // Lock the condtion variable
#include <tuple>                                // For constructing solvers

// Other packages

#include <nlohmann/json.hpp>                    // JSON object definition
using JSON = nlohmann::json;                    // Short form name space
#include <boost/core/demangle.hpp>              // To print readable types

// Theron++ headers

#include "Actor.hpp"                            // Actor base class
#include "Utility/StandardFallbackHandler.hpp"  // Exception unhanded messages
#include "Communication/NetworkingActor.hpp"    // Networking actors

// AMQ communication headers

#include "Communication/AMQ/AMQjson.hpp"         // For JSON metric messages
#include "Communication/AMQ/AMQEndpoint.hpp"     // For AMQ related things

// NebulOuS headers

#include "ExecutionControl.hpp"                  // Shut down messages
#include "Solver.hpp"                            // The basic solver class

namespace NebulOuS
{
/*==============================================================================

 Solution Manager

==============================================================================*/

template< SolverAlgorithm SolverType >
class SolverManager
: virtual public Theron::Actor,
  virtual public Theron::StandardFallbackHandler,
  virtual public Theron::NetworkingActor< 
    typename Theron::AMQ::Message::PayloadType >,
  virtual public ExecutionControl
{
  // There is a topic name used to publish solutions found by the solvers. This 
  // topic is given to the constructor and kept as a constant during the class
  // execution. The same goes for the topic on which application execution 
  // contexts will arrive for processing.

private:

  const Theron::AMQ::TopicName SolutionReceiver, ContextTopic;

  // --------------------------------------------------------------------------
  // Solver management
  // --------------------------------------------------------------------------
  //
  // The solution manager dispatches the application execution contexts as 
  // requests for solutions to a pool of solvers. 

  std::list< SolverType > SolverPool;
  std::unordered_set< Address > ActiveSolvers, PassiveSolvers;

  // --------------------------------------------------------------------------
  // Application Execution Context management
  // --------------------------------------------------------------------------
  //
  // The contexts are dispatched in time sorted order. However, the time
  // to solve a problem depends on the complexity of the the context and the 
  // results may therefore become available out-of-order. Each application 
  // execution context should carry a unique identifier, and this is used as 
  // the index key for quickly finding the right execution context. There is 
  // a second view of the queue of application context where the identifiers 
  // are sorted based on their time stamp. 

  std::unordered_map< Solver::ContextIdentifierType, 
                      Solver:: ApplicationExecutionContext > Contexts;
  
  std::multimap< Solver::TimePointType, Solver::ContextIdentifierType > 
  ContextExecutionQueue;

  // When the new applicaton execution context message arrives, it will be 
  // queued, and its time point recoreded. If there are passive solvers, 
  // the handler will immediately dispatch the contexts to each of these in 
  // time order. Essentially, it implements a 'riffle' for the passive solvers
  // and the pending contexts.The issue is that there are likely different
  // cardinalities of the two sets, and the solvers should be marked as 
  // active after the dispatch and the context identifiers should be 
  // removed from the queue after the dispatch.

  void DispatchToSolvers( void )
  {
    if( !PassiveSolvers.empty() && !ContextExecutionQueue.empty() )
    {
      for( const auto & [ SolverAddress, ContextElement ] : 
          std::ranges::views::zip( PassiveSolvers, ContextExecutionQueue ) )
        Send( Contexts.at( ContextElement.second ), SolverAddress );

      // The number of contexts dispatched must equal the minimum of the 
      // available solvers and the available contexts.

      std::size_t DispatchedContexts 
        = std::min( PassiveSolvers.size(), ContextExecutionQueue.size() );

      // Then move the passive solver addresses used to active solver addresses

      std::ranges::move( 
        std::ranges::subrange( PassiveSolvers.begin(), 
                std::ranges::next( PassiveSolvers.begin(), DispatchedContexts, 
                                   PassiveSolvers.end() ) ),
        std::inserter( ActiveSolvers, ActiveSolvers.end() ) );

      // Then the dispatched context identifiers are removed from queue

      ContextExecutionQueue.erase( ContextExecutionQueue.begin(), 
        std::ranges::next( ContextExecutionQueue.begin(), DispatchedContexts,
                           ContextExecutionQueue.end() ) );
    }
  }

  // The handler function simply enqueues the received context, records its 
  // timesamp and dispatch as many contexts as possible to the solvers. Note
  // that the context identifiers must be unique and there is a logic error 
  // if there is already a context with the same identifier. Then an invalid
  // arguemtn exception will be thrown. This strategy should be reconsidered
  // if there will be multiple entities firing execution contexts. 

  void HandleApplicationExecutionContext( 
    const Solver:: ApplicationExecutionContext & TheContext,
    const Address TheRequester )
  {
    auto [_, Success] = Contexts.try_emplace( 
      TheContext[ Solver::ContextIdentifier.data() ], TheContext );

    if( Success )
    {
      ContextExecutionQueue.emplace( 
        TheContext[ Solver::TimeStamp.data() ],
        TheContext[ Solver::ContextIdentifier.data() ] );

      DispatchToSolvers();
    }
    else
    {
      std::source_location Location = std::source_location::current();
      std::ostringstream ErrorMessage;

      ErrorMessage << "[" << Location.file_name() << " at line " 
                  << Location.line()
                  << "in function " << Location.function_name() <<"] " 
                  << "An Application Execution Context with identifier "
                  << TheContext[ Solver::ContextIdentifier.data() ]
                  << " was received while there is already one with the same "
                  << "identifer. The identifiers must be unique!";

      throw std::invalid_argument( ErrorMessage.str() );
    }
  }

  // --------------------------------------------------------------------------
  // Solutions
  // --------------------------------------------------------------------------
  //
  // When a solution is received from a solver, it will be dispatched to all
  // entities subscribing to the solution topic, and the solver will be returned
  // to the pool of passive solvers. The dispatch function will be called at the 
  // end to ensure that the solver starts working on queued application execution
  // contexts, if any.

  void PublishSolution( const Solver::Solution & TheSolution, 
                        const Address TheSolver )
  {
    Send( TheSolution, SolutionReceiver );
    PassiveSolvers.insert( ActiveSolvers.extract( TheSolver ) );
    DispatchToSolvers();
  }

  // --------------------------------------------------------------------------
  // Constructor and destructor
  // --------------------------------------------------------------------------
  //
  // The constructor takes the name of the Solution Mnager Actor, the name of 
  // the topic where the solutions should be published, and the topic where the 
  // application execution contexts will be published. If the latter is empty,
  // the manager will not listen to any externally generated requests, only those
  // being sent from the Metric Updater supposed to exist on the same Actor 
  // system node as the manager.The final arguments to the constructor is a 
  // set of arguments to the solver type in the order expected by the solver
  // type and repeated for the number of (local) solvers that should be created.
  //
  // Currently this manager does not support dispatching configurations to
  // remote solvers and collect responses from these. However, this can be 
  // circumvented by creating a local "solver" transferring the requests to 
  // a remote solvers and collecting results from the remote solver.

public:

  template< typename ...SolverArgTypes >
  SolverManager( const std::string & TheActorName, 
                 const Theron::AMQ::TopicName & SolutionTopic,
                 const Theron::AMQ::TopicName & ContextPublisherTopic,
                 const unsigned int NumberOfSolvers,
                 const std::string SolverRootName,
                 SolverArgTypes && ...SolverArguments )
  : Actor( TheActorName ),
    StandardFallbackHandler( Actor::GetAddress().AsString() ),
    NetworkingActor( Actor::GetAddress().AsString() ),
    ExecutionControl( Actor::GetAddress().AsString() ),
    SolutionReceiver( SolutionTopic ),
    ContextTopic( ContextPublisherTopic ),
    SolverPool(), ActiveSolvers(), PassiveSolvers(),
    Contexts(), ContextExecutionQueue()
  {
    // The solvers are created by expanding the arguments for the solvers 
    // one by one creating new elements in the solver pool. The solvers 
    // will be named with a sequence number from 1 and up added to the 
    // root solver name, e.g., if the root name is "MySolver" the solvers
    // will have names "MySolver_1", "MySolver_2",... and so forth. Since
    // all solvers are of the same type they should take the same arguments
    // and so the given arguments are just fowarded to each solver constructor.

    for( unsigned int i = 1; i <= NumberOfSolvers; i++ )
    {
      std::ostringstream TheSolverName;

      TheSolverName << SolverRootName << "_" << i;
      SolverPool.emplace_back( TheSolverName.str(), 
                 std::forward< SolverArgTypes >(SolverArguments)... );
    }
    
    // If the solvers were successfully created, their addresses are recorded as
    // passive servers, and a publisher is made for the solution channel, and 
    // optionally, a subscritpion is made for the alternative context publisher 
    // topic. If the solvers could not be created, then an invalid argument 
    // exception will be thrown.

    if( !SolverPool.empty() )
    {
      std::ranges::transform( SolverPool, 
        std::inserter( PassiveSolvers, PassiveSolvers.end() ),
        [](const SolverType & TheSolver){ return TheSolver.GetAddress(); } );

      Send( Theron::AMQ::NetworkLayer::TopicSubscription( 
            Theron::AMQ::NetworkLayer::TopicSubscription::Action::Publisher, 
            SolutionTopic ), GetSessionLayerAddress() );

      if( !ContextPublisherTopic.empty() )
        Send( Theron::AMQ::NetworkLayer::TopicSubscription( 
              Theron::AMQ::NetworkLayer::TopicSubscription::Action::Subscription, 
              ContextPublisherTopic ), GetSessionLayerAddress() );

      Send( ExecutionControl::StatusMessage(
        ExecutionControl::StatusMessage::State::Started
      ), Address( std::string( ExecutionControl::StatusTopic ) ) );
    }
    else
    {
      std::source_location Location = std::source_location::current();
      std::ostringstream ErrorMessage;

      ErrorMessage << "[" << Location.file_name() << " at line " 
                  << Location.line()
                  << "in function " << Location.function_name() <<"] " 
                  << "It was not possible to construct any solver of type "
                  << boost::core::demangle( typeid( SolverType ).name() )
                  << " from the given constructor argument types: ";

    (( ErrorMessage << boost::core::demangle( typeid( SolverArguments ).name() ) << " " ), ... );

    throw std::invalid_argument( ErrorMessage.str() );
    }
  }

  // The destructor closes all the open topics if the network is still open 
  // when the destructor is invoked.

  virtual ~SolverManager( void )
  {
    if( HasNetwork() )
    {
      Send( Theron::AMQ::NetworkLayer::TopicSubscription(
        Theron::AMQ::NetworkLayer::TopicSubscription::Action::ClosePublisher,
        SolutionReceiver
      ), GetSessionLayerAddress() );

      Send( Theron::AMQ::NetworkLayer::TopicSubscription(
        Theron::AMQ::NetworkLayer::TopicSubscription::Action::CloseSubscription,
        ContextTopic
      ), GetSessionLayerAddress() );
    }
  }

};
  
} // namespace NebulOuS
#endif // NEBULOUS_SOLUTION_MANAGER