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

// Other packages

#include <nlohmann/json.hpp>                    // JSON object definition
using JSON = nlohmann::json;                    // Short form name space

// Theron++ headers

#include "Actor.hpp"                            // Actor base class
#include "Utility/StandardFallbackHandler.hpp"  // Exception unhanded messages

// AMQ communication headers

#include "Communication/AMQ/AMQjson.hpp"         // For JSON metric messages

namespace NebulOuS
{
/*==============================================================================

 Solver Actor

==============================================================================*/
//

class Solver
: virtual public Theron::Actor,
  virtual public Theron::StandardFallbackHandler
{

private:

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


};

} // namespace NebulOuS
#endif // NEBULOUS_SOLVER