/*==============================================================================
Solver

The solver is a generic base class for all solvers defining the interface with 
the Solution Manager actor. The solver reacts to an Application Execution 
Context messagedefined in the class. The application execution context is 
defined to be independent metric values that has no, or little, correlation 
with the application configuration and that are involved in the utility 
expression(s) or in the constraints of the optimisation problem.

Receiving this message triggers the search for an optimial solution to the 
given named objective. Once the solution is found, the Solution message should 
be returned to the actor making the request. The solution message will contain 
the configuration being the feasible assignment to all variables of the 
problem, all the objective values in this problem, and the identifier for the 
application execution context.

The messages are essentially JSON objects defined using  Niels Lohmann's 
library [1] and in particular the AMQ extension for the Theron++ Actor 
library [2] allowing JSON messages to be transmitted as Qpid Proton AMQ [3]
messages to renote requestors.

References:
[1] https://github.com/nlohmann/json
[2] https://github.com/GeirHo/TheronPlusPlus
[3] https://qpid.apache.org/proton/

Author and Copyright: Geir Horn, University of Oslo
Contact: Geir.Horn@mn.uio.no
License: MPL2.0 (https://www.mozilla.org/en-US/MPL/2.0/)
==============================================================================*/

#ifndef NEBULOUS_SOLVER
#define NEBULOUS_SOLVER

// Standard headers

#include <string_view>                          // Constant strings
#include <string>                               // Normal strings
#include <unordered_map>                        // To store metric-value maps
#include <concepts>                             // To test template parameters

// Other packages

#include <nlohmann/json.hpp>                    // JSON object definition
using JSON = nlohmann::json;                    // Short form name space

// Theron++ headers

#include "Actor.hpp"                            // Actor base class
#include "Utility/StandardFallbackHandler.hpp"  // Exception unhanded messages
#include "Communication/PolymorphicMessage.hpp" // The network message type
#include "Communication/NetworkingActor.hpp"    // External communications

// AMQ communication headers

#include "Communication/AMQ/AMQjson.hpp"         // For JSON metric messages
#include "Communication/AMQ/AMQEndpoint.hpp"     // Enabling AMQ communication
#include "Communication/AMQ/AMQSessionLayer.hpp" // For topic subscriptions

namespace NebulOuS
{
/*==============================================================================

 Solver Actor

==============================================================================*/

class Solver
: virtual public Theron::Actor,
  virtual public Theron::StandardFallbackHandler,
  virtual public Theron::NetworkingActor< 
    typename Theron::AMQ::Message::PayloadType >
{

public:

  // --------------------------------------------------------------------------
  // Application Execution Context
  // --------------------------------------------------------------------------
  //
  // The message is defined as a JSON message representing an attribute-value 
  // object. The attributes expected are defined as constant strings so that 
  // the actual textual representation can be changed without changing the code
  // 
  // "Identifier" It can be anything corresponding to the need of the sender
  // and returned to the sender with the found solution

  static constexpr std::string_view ContextIdentifier = "Identifier";

  // "Timestamp" : This is the field giving the implicit order of the 
  // different application execution execution contexts waiting for being 
  // solved when there are more requests than there are solvers available to 
  // work on the different problems. 

  static constexpr std::string_view TimeStamp = "Timestamp";

  // There is also a definition for the objective function label since a multi-
  // objective optimisation problem can have multiple objective functions and 
  // the solution is found for only one of these functions at the time even 
  // though all objective function values will be returned with the solution, 
  // the solution will maximise only the objective function whose label is 
  // given in the application execution context request message.

  static constexpr std::string_view 
  ObjectiveFunctionLabel = "ObjectiveFunction";

  // Finally, there is another JSON object that defines all the metric name and
  // value pairs that define the actual execution context. Note that there must
  // be at least one metric-value pair for the request to be valid.

  static constexpr std::string_view ExecutionContext = "ExecutionContext";

  // To ensure that the execution context is correctly provided by the senders
  // The expected metric value structure is defined as a type based on the 
  // standard unsorted map based on a JSON value object since this can hold 
  // various value types.

  using MetricValueType = std::unordered_map< std::string, JSON >;

  // The identification type for the application execution context is defined
  // so that other classes may use it, but also so that it can be easily 
  // changed if needed. It is assumed that the type must have a hash function
  // so that the type can be used in ordered data structures.

  using ContextIdentifierType = std::string;

  // The same goes for the time point type. This is defined as the number of 
  // microseconds since the POSIX time epoch (1 January 1970) and stored as a
  // long integral value.

  using TimePointType = unsigned long long;

  // The message is a simple JSON object where the various fields of the 
  // message struct are set by the constructor to ensure that all fields are
  // given when the message is constructed. The message is a JSON Topic Message
  // received on the topic with the same name as the message identifier.

  class ApplicationExecutionContext
  : public Theron::AMQ::JSONTopicMessage
  {
  public:

    static constexpr 
    std::string_view MessageIdentifier = "Solver::ApplicationExecutionContext";

    ApplicationExecutionContext( const ContextIdentifierType & TheIdentifier, 
                                 const TimePointType MicroSecondTimePoint,
                                 const std::string ObjectiveFunctionID,
                                 const MetricValueType & TheContext )
    : JSONTopicMessage( std::string( MessageIdentifier ),
    { { std::string( ContextIdentifier ), TheIdentifier },
      { std::string( TimeStamp ), MicroSecondTimePoint },
      { std::string( ObjectiveFunctionLabel ), ObjectiveFunctionID },
      { std::string( ExecutionContext ), TheContext } }
     ) {}

    ApplicationExecutionContext( const ApplicationExecutionContext & Other )
    : JSONTopicMessage( Other )
    {}

    ApplicationExecutionContext()
    : JSONTopicMessage( std::string( MessageIdentifier ) )
    {}

    virtual ~ApplicationExecutionContext() = default;
  };

  // The handler for this message is virtual as it where the real action will
  // happen and the search for the optimal solution will hopefully lead to a
  // feasible soltuion that can be returned to the sender of the applicaton 
  // context.

protected:

  virtual void SolveProblem( const ApplicationExecutionContext & TheContext, 
                             const Address TheRequester ) = 0;

  // --------------------------------------------------------------------------
  // Solution
  // --------------------------------------------------------------------------
  //
  // When a solution is found to a given problem, the solver should return the 
  // found optimal value for the given objective function, It should return 
  // this value together with the values assigned to the feasible variables
  // leading to this optimal objective value. Additionally, the message will 
  // contain the time point for which this solution is valid, and the 
  // application execution context as the optimal solution is conditioned 
  // on this solution.
  //
  // Since the probelm being resolved can be multi-objective, the values of all
  // objective values will be returned as a JSON map where the attributes are 
  // the names of the objective functions in the optimisation problem, and the 
  // values are the ones assigned by the optimiser. This JSON map object is 
  // passed under the global attribute "ObjectiveValues"

public:

  class Solution
  : public Theron::AMQ::JSONTopicMessage
  {
  public:

    using ObjectiveValuesType = MetricValueType;
    using VariableValuesType  = MetricValueType;

    static constexpr std::string_view ObjectiveValues = "ObjectiveValues";
    static constexpr std::string_view VariableValues  = "VariableValues";

    static constexpr std::string_view MessageIdentifier = "Solver::Solution";

    Solution( const ContextIdentifierType & TheIdentifier,
              const TimePointType MicroSecondTimePoint,
              const std::string ObjectiveFunctionID,
              const ObjectiveValuesType & TheObjectiveValues,
              const VariableValuesType & TheVariables )
    : JSONTopicMessage( std::string( MessageIdentifier ) ,
      { { std::string( ContextIdentifier ), TheIdentifier },
        { std::string( TimeStamp ), MicroSecondTimePoint   },
        { std::string( ObjectiveFunctionLabel ), ObjectiveFunctionID },
        { std::string( ObjectiveValues ) , TheObjectiveValues },
        { std::string( VariableValues ), TheVariables } } )
      {}
    
    Solution()
    : JSONTopicMessage( std::string( MessageIdentifier ) )
    {}

    virtual ~Solution() = default;
  };

  // --------------------------------------------------------------------------
  // Optimisation problem definition
  // --------------------------------------------------------------------------
  //
  // There are many ways the optimisation problem can be passed to the solver, 
  // and it is therefore not possible to give an exact format for the message
  // to define or update the optimisation problem. The message is basically 
  // left as a JSON message and it will be up to the actual solver algorithm
  // to implement this in a way appropriate for the algorithm. 

  class OptimisationProblem
  : public Theron::AMQ::JSONTopicMessage
  {
  public:

    static constexpr 
    std::string_view MessageIdentifier = "Solver::OptimisationProblem";

    OptimisationProblem( const JSON & TheProblem )
    : JSONTopicMessage( std::string( MessageIdentifier ), TheProblem )
    {}

    OptimisationProblem()
    : JSONTopicMessage( std::string( MessageIdentifier ) )
    {}

    virtual ~OptimisationProblem() = default;
  };

  // The handler for this message must also be defined by the algorithm that
  // implements the solver.

  virtual void DefineProblem( const OptimisationProblem & TheProblem, 
                              const Address TheOracle ) = 0;

  // --------------------------------------------------------------------------
  // Constructor and destructor
  // --------------------------------------------------------------------------
  //
  // The constructor defines the message handlers so that the derived solver
  // classes will not need to deal with the Actor specific details, and to 
  // ensure that the handlers are called when the Actor receives the various
  // messages. It should be noted that the problem definition can arrive from 
  // a remote actor on a topic corresponding to the message indentifier name.
  // However, no subscription will be made for application execution contexts 
  // since these should be sorted and sent in order by the Solution Manager 
  // actor, and external communication should go throug the Solution Manager.
  //
  // The constructor requires an actor name as the only parameter, and the 
  // destructor unsubscribes from the topics previously subscribed to by 
  // the constuctor.

  Solver( const std::string & TheSolverName )
  : Actor( TheSolverName ),
    StandardFallbackHandler( Actor::GetAddress().AsString() ),
    NetworkingActor( Actor::GetAddress().AsString() )
  {
    RegisterHandler( this, &Solver::SolveProblem  );
    RegisterHandler( this, &Solver::DefineProblem );

    Send( Theron::AMQ::NetworkLayer::TopicSubscription(
      Theron::AMQ::NetworkLayer::TopicSubscription::Action::Subscription,
      Theron::AMQ::TopicName( OptimisationProblem::MessageIdentifier )
    ), GetSessionLayerAddress() );
  }
  
  Solver() = delete;

  virtual ~Solver()
  {
    if( HasNetwork() )
      Send( Theron::AMQ::NetworkLayer::TopicSubscription(
        Theron::AMQ::NetworkLayer::TopicSubscription::Action::CloseSubscription,
        Theron::AMQ::TopicName( OptimisationProblem::MessageIdentifier )
      ), GetSessionLayerAddress() );
  }
};

/*==============================================================================

 Solver concept

==============================================================================*/
//
// A concept is defined to validate that solvers used inherits this standard 
// base class and that they implement the virtual methods.

template< class TheSolverType >
concept SolverAlgorithm = std::derived_from< TheSolverType, Solver >;

} // namespace NebulOuS
#endif // NEBULOUS_SOLVER