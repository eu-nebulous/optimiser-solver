/*==============================================================================
AMPL Solver

This instantiates the purely virtual Solver providing an interface to the 
mathematical domain specific language "A Mathematical Programming Language" 
(AMPL) [1] allowing to use the wide range of solvers, free or commercial, that 
support AMPL descriptions of the optimisation problem.

The AMPL problem description and associated data files are received by handlers
and stored locally as proper files to ensure that the problem is always solved 
for the lates problem descriptions received. When the actor receives an 
Application Execution Context message, the AMPL description and data files will
be loaded and the appropriate solver called from the AMPL library. When the 
solution is returned from AMPL, the solution message is returned to the sender 
of the application execution context message, typically the Solution Manager 
actor for publishing the solution to external subscribers.

References:
[1] https://ampl.com/

Author and Copyright: Geir Horn, University of Oslo
Contact: Geir.Horn@mn.uio.no
License: MPL2.0 (https://www.mozilla.org/en-US/MPL/2.0/)
==============================================================================*/

#ifndef NEBULOUS_AMPL_SOLVER
#define NEBULOUS_AMPL_SOLVER

// Standard headers

#include <string_view>                          // Constant strings
#include <string>                               // Standard strings
#include <list>                                 // To store names
#include <filesystem>                           // For problem files
#include <source_location>                      // For better errors
#include <map>                                  // Storing key-value pairs

// Other packages

#include <nlohmann/json.hpp>                    // JSON object definition
using JSON = nlohmann::json;                    // Short form name space

// Theron++ files

#include "Actor.hpp"                            // Actor base class
#include "Utility/StandardFallbackHandler.hpp"  // Exception unhanded messages
#include "Communication/NetworkingActor.hpp"    // Actor to receive messages
#include "Communication/PolymorphicMessage.hpp" // The network message type

// AMQ communication files

#include "Communication/AMQ/AMQjson.hpp"         // For JSON metric messages
#include "Communication/AMQ/AMQEndpoint.hpp"     // AMQ endpoint
#include "Communication/AMQ/AMQSessionLayer.hpp" // For topic subscriptions

// NebulOuS files

#include "Solver.hpp"                            // The generic solver base

// AMPL Application Programmer Interface (API)

#include "ampl/ampl.h"

namespace NebulOuS
{
/*==============================================================================

 AMPL Solver actor

==============================================================================*/
//
// The AMPL solver is an Actor and a Solver. It provides handlers for messages
// defining the problem file and data file(s), and responds to an application 
// execution context message by optimising the saved problem for the given 
// context parameters. 

class AMPLSolver 
: virtual public Theron::Actor,
  virtual public Theron::StandardFallbackHandler,
  virtual public Theron::NetworkingActor< 
    typename Theron::AMQ::Message::PayloadType >,
  virtual public Solver
{
  // --------------------------------------------------------------------------
  // Utility methods
  // --------------------------------------------------------------------------
  //
  // Since both the optimisation problem file and the data file(s) will be sent
  // as JSON messages with a single key-value pair where the key is the filename
  // and the value is the file content, there is a common dfinition of the 
  // problem file directory and a function to read the file. The function will 
  // throw errors if the JSON message given is not an object, or of there are 
  // issues opening the file name given. If the file could be successfully 
  // saved, the functino will close the file and return the file name for 
  // further processing.

private:

  const std::filesystem::path ProblemFileDirectory;

  std::string SaveFile( const JSON & TheMessage, 
              const std::source_location  & Location 
                                          = std::source_location::current() );

  // There is also a utility function to look up a named AMPL parameter and 
  // sets it value based on a JSON scalar value.

  void SetAMPLParameter( const std::string & ParameterName, 
                         const JSON & ParameterValue );

  // --------------------------------------------------------------------------
  // The optimisation problem
  // --------------------------------------------------------------------------
  //
  // The problem is received as an AMPL file in a message. However, the AMPL 
  // interface allows the loading of problem and data files on an existing 
  // AMPL object, and the AMPL API object is therefore reused when a new 
  // problem file is received. The problem definition is protected so that 
  // derived classes may solve the problem directly.

protected:

  ampl::AMPL ProblemDefinition;

  // The problem is loaded by the handler defining the problem. This receives 
  // the standard optimisation problem definition.  Essentially, this message 
  // contains one tag, the name of the AMPL file and the body is a big string 
  // containing the file content. 

  virtual void DefineProblem( const Solver::OptimisationProblem & TheProblem, 
                              const Address TheOracle ) override;

  // The topic on which the problem file is posted is currently defined as a 
  // constant string

  static constexpr std::string_view AMPLProblemTopic 
                   = "eu.nebulouscloud.optimiser.solver.model";

  // The JSON message received on this topic is supposed to contain several 
  // keys in the JSON message
  // 1) The filename of the problem file
  // 2) The file content as a single string
  // 3) The default objective function (defined in the Solver class)
  // 4) An optional constants section containing constant names as keys
  //    and the values will be another map containing the variable 
  //    whose value should be passed to the constant, and the initial 
  //    value of the constant. 

  static constexpr std::string_view 
         FileName             = "FileName",
         FileContent          = "FileContent",
         ConstantsLabel       = "Constants",
         VariableName         = "Variable",
         InitialConstantValue = "Value";

  // The AMPL problem file can contain many objective functions, but can be 
  // solved only for one objective function at the time. The name of the 
  // default objective function is therefore stored together with the model 
  // in the above Define Problem handler. If the default objective function 
  // label is not provided with the optimisation problem message, an 
  // invalid argument exception will be thrown.

private:

  std::string DefaultObjectiveFunction;

  // To set the constant values to the right variable values, the mapping 
  // between the variable name and the constant name must be stored in 
  // a map. 

  std::map< std::string, std::string > VariablesToConstants;

  // --------------------------------------------------------------------------
  // Data file updates
  // --------------------------------------------------------------------------
  //
  // The message defining the data file is a JSON topic message with the same
  // structure as the optimisation problem message: It contains only one 
  // attribute, which is the name of the data file, and the data file 
  // content as the value. This content is just saved to the problem file 
  // directory before it is read back to the AMPL problem definition. 
  
public:

  class DataFileMessage
  : public Theron::AMQ::JSONTopicMessage
  {
  public:

    // The data files are assumed to be published on a dedicated topic for the 
    // optimiser

    static constexpr std::string_view MessageIdentifier 
                   = "eu.nebulouscloud.optimiser.solver.data";


    DataFileMessage( const std::string & TheDataFileName, 
                     const JSON & DataFileContent )
    : JSONTopicMessage( std::string( MessageIdentifier ), 
      { { FileName, TheDataFileName }, 
        { FileContent, DataFileContent } } )
    {}

    DataFileMessage( const DataFileMessage & Other )
    : JSONTopicMessage( Other )
    {}

    DataFileMessage()
    : JSONTopicMessage( std::string( MessageIdentifier ) )
    {}

    virtual ~DataFileMessage() = default;
  };

  // The handler for this message saves the received file and uploads the file
  // to the AMPL problem definition.

private:

  void DataFileUpdate( const DataFileMessage & TheDataFile, 
                       const Address TheOracle );

  // --------------------------------------------------------------------------
  // Solving the problem
  // --------------------------------------------------------------------------
  //
  // The real action happens when an Application Execution Context message is 
  // received. This defines the values of the independent metrics used in the 
  // objective functions and in the problem constraints, and one objective 
  // function name indicating which objective to optimise. The actual solution
  // is provided by a small helper function. The reason is that this may 
  // use the AMPL problem but not the solver, and as such other solvers can 
  // be build on this class. The standard definition just asks AMPL to call 
  // the solver.

protected:

  virtual void Optimize( void )
  { ProblemDefinition.solve(); }

  // The handler for the application execution context will first set all the
  // parameter values for the contex metrics to the received values, and then
  // optimise the problem. When a solution is found it will be sent back to 
  // the Agent providing the application execution context as a solution value
  // message. The message format is defined in the Solver base class.

  virtual void SolveProblem( const ApplicationExecutionContext & TheContext, 
                             const Address TheRequester ) override;

  // --------------------------------------------------------------------------
  // Constructor and destructor
  // --------------------------------------------------------------------------
  //
  // The AMPL solver requires the name of the actor, an AMPL environment class
  // pointing to the AMPL installation directory. If this is given as empty,
  // then the path is taken from the corresponding environment variables. There
  // is also a path to the directory where the optimisation problem file will 
  // be stored together with any required data files.
  //
  // Note that the constructors are declared as explicit because in theory 
  // a string could be converted to an Environment class or a Path and so to 
  // be able to distinquish what a string means, the actual classes must be 
  // given constructed on the content string.

public:

  explicit AMPLSolver( const std::string & TheActorName, 
                       const ampl::Environment & InstallationDirectory,
                       const std::filesystem::path & ProblemPath,
                       std::string  TheSolverType );

  // If the path to the problem directory is omitted, it will be initialised to
  // a temporary directory.

  explicit AMPLSolver( const std::string & TheActorName, 
                       const ampl::Environment & InstallationDirectory, 
                       std::string TheSolverType )
  : AMPLSolver( TheActorName, InstallationDirectory, 
                std::filesystem::temp_directory_path(), TheSolverType )
  {}

  // If the AMPL installation environment is omitted, the installation directory
  // will be taken form the environment variables.

  explicit AMPLSolver( const std::string & TheActorName, 
                       const std::filesystem::path & ProblemPath, 
                       std::string TheSolverType )
  : AMPLSolver( TheActorName, ampl::Environment(), ProblemPath, TheSolverType )
  {}

  // Finally, it is just the standard constructor taking only the name of the
  // actor

  AMPLSolver( const std::string & TheActorName )
  : AMPLSolver( TheActorName, ampl::Environment(),  
                std::filesystem::temp_directory_path(), "couenne" )
  {}

  // The solver will just close the open connections for listening to data file
  // updates since the subscriptions for the problem definition will be closed
  // by the generic solver
  
  virtual ~AMPLSolver();
};

} // namespace NebulOuS
#endif // NEBULOUS_AMPL_SOLVER