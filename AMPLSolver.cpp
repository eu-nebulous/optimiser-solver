/*==============================================================================
AMPL Solver

This file provides the implementation of the methods of the AMLP Solver actor 
that is instantiated by the Solution Manager and used to obtain solutions for 
optimisation problems in the queue managed by the Solution Manager.

Author and Copyright: Geir Horn, University of Oslo
Contact: Geir.Horn@mn.uio.no
License: MPL2.0 (https://www.mozilla.org/en-US/MPL/2.0/)
==============================================================================*/

#include <fstream>                // For file I/O
#include <sstream>                // For formatted errors
#include <stdexcept>              // Standard exceptions
#include <system_error>           // Error codes

#include "Utility/ConsolePrint.hpp"

#include "AMPLSolver.hpp"

namespace NebulOuS
{

// -----------------------------------------------------------------------------
// Utility functions
// -----------------------------------------------------------------------------
//
// There are two situations when it is necessary to store a file from a message:
// Firstly when the AMPL model is defined, and second every time a data file 
// is received updating AMPL model parameters. Hence the common file creation
// is taken care of by a dedicated function.

std::string AMPLSolver::SaveFile( const JSON & TheMessage, 
                                  const std::source_location & Location )
{
  if( TheMessage.is_object() )
  {
    std::string TheFileName 
                = ProblemFileDirectory / TheMessage.at( AMPLSolver::FileName );

    std::fstream ProblemFile( TheFileName, std::ios::out | std::ios::binary );
                        
    if( ProblemFile.is_open() )
    {
      ProblemFile << TheMessage.at( AMPLSolver::FileContent );
      ProblemFile.close();
      return TheFileName;
    }
    else
    {
      std::source_location Location = std::source_location::current();
      std::ostringstream ErrorMessage;

      ErrorMessage << "[" << Location.file_name() << " at line " 
                  << Location.line()
                  << "in function " << Location.function_name() <<"] " 
                  << "The AMPL file at "
                  << TheFileName
                  << " could not be opened for output!";

      throw std::system_error( static_cast< int >( std::errc::io_error ),
                               std::system_category(), ErrorMessage.str() );
    }
  }
  else
  {
    std::source_location Location = std::source_location::current();
    std::ostringstream ErrorMessage;

    ErrorMessage << "[" << Location.file_name() << " at line " 
                << Location.line()
                << "in function " << Location.function_name() <<"] " 
                << "The JSON message is not an object. The received "
                << "message is " << std::endl
                << TheMessage.dump(2)
                << std::endl;

    throw std::system_error( static_cast< int >( std::errc::io_error ),
                              std::system_category(), ErrorMessage.str() );
  }
}

// Setting named AMPL parameters from JSON objects requires that the JSON object
// is converted to the same type as the AMPL parameter. This conversion 
// requires that the type of the parameter is tested, and there is a shared 
// function to set a named parameter from the JSON object.

void AMPLSolver::SetAMPLParameter( const std::string & ParameterName, 
                                   const JSON & ParameterValue )
{
  ampl::Parameter 
  TheParameter = ProblemDefinition.getParameter( ParameterName );
  
  switch ( ParameterValue.type() )
  {
    case JSON::value_t::number_integer :
    case JSON::value_t::number_unsigned :
    case JSON::value_t::boolean :
      TheParameter.set( ParameterValue.get< long >() );
      break;
    case JSON::value_t::number_float :
      TheParameter.set( ParameterValue.get< double >() );
      break;
    case JSON::value_t::string :
      TheParameter.set( ParameterValue.get< std::string >() );
      break;
    default:
      {
        std::source_location Location = std::source_location::current();
        std::ostringstream ErrorMessage;

        ErrorMessage  << "[" << Location.file_name() << " at line " 
                      << Location.line()
                      << "in function " << Location.function_name() <<"] " 
                      << "The JSON value " << ParameterValue 
                      << " has JSON type " 
                      << static_cast< int >( ParameterValue.type() )
                      << " which is not supported"
                      << std::endl;

        throw std::invalid_argument( ErrorMessage.str() );
      }
      break;
  }
}

// -----------------------------------------------------------------------------
// Problem definition
// -----------------------------------------------------------------------------
//
// The first step in solving an optimisation problem is to define the problme 
// involving the decision variables, the parameters, and the constraints over
// these entities. The AMPL Domoain Specific Language (DSL) defining the 
// problem is received as a JSON message where the File Name and the File 
// Content is managed by the file reader utility function. 
//
// After reading the file the name of the default objective function is taken
// from the message. Not that this is a mandatory field and the solver will 
// throw an exception if the field does not exist.
//
// Finally, the optimisation happens relative to the current configuration as 
// baseline aiming to improve the variable values. However, this may need that
// candidate variable values are compared with the current values of the same 
// variables. Hence, the current variable values are defined to be "constants"
// of the optimisation problem. These constants must be set by the solver for 
// a found solution that will be deployed, and this requires a mapping between
// the name of a constant and the name of the variable used to initialise the 
// constant. This map is initialised from the message, if it is provided, and 
// the initial values are set for the corresponding "constant" parameters in 
// the problem definition. The constant field holds a JSON map where the keys
// are the names of the constants defined as parameters in the problem 
// definition, and the value is again a map with two fields: The variable name
// and the variable's intial value.

void AMPLSolver::DefineProblem(const Solver::OptimisationProblem & TheProblem,
                               const Address TheOracle)
{
  Theron::ConsoleOutput Output;
  Output << "AMPL Solver received the AMPL problem: " << std::endl
         << TheProblem.dump(2)
         << std::endl;

  // First storing the AMPL problem file from its definition in the message
  // and read the file back to the AMPL interpreter.

  ProblemDefinition.read( SaveFile( TheProblem ) );

  // The next is to read the label of the default objective function and 
  // store this. An invalid argument exception is thrown if the field is missing

  if( TheProblem.contains( Solver::ObjectiveFunctionLabel ) )
    DefaultObjectiveFunction = TheProblem.at( Solver::ObjectiveFunctionLabel );
  else
  {
    std::source_location Location = std::source_location::current();
    std::ostringstream ErrorMessage;

    ErrorMessage  << "[" << Location.file_name() << " at line " 
                  << Location.line()
                  << "in function " << Location.function_name() <<"] " 
                  << "The problem definition must contain a default objective "
                  << "function under the key [" 
                  << Solver::ObjectiveFunctionLabel
                  << "]" << std::endl;

    throw std::invalid_argument( ErrorMessage.str() );
  }

  // After all the manatory fields have been processed, the set of constants 
  // will be processed storing the mapping from variable value to constant.

  if( TheProblem.contains( ConstantsLabel ) &&
      TheProblem.at( ConstantsLabel ).is_object() )
    for( const auto & [ ConstantName, ConstantRecord ] :
                      TheProblem.at( ConstantsLabel ).items() )
    {
      VariablesToConstants.emplace( ConstantRecord.at( VariableName ), 
                                    ConstantName );
      SetAMPLParameter( ConstantName, 
                        ConstantRecord.at( InitialConstantValue ) );
    }

  Output << "Problem loaded!" << std::endl;
}

// -----------------------------------------------------------------------------
// Optimimsation parameter values
// -----------------------------------------------------------------------------
//
// The data file(s) corresponding to the current optimisation problem will be 
// sent in the same way and separately file by file. The logic is the same as 
// the Define Problem message handler: The save file is used to store the 
// received file, which is then loaded as the data problem.

void AMPLSolver::DataFileUpdate( const DataFileMessage & TheDataFile, 
                                 const Address TheOracle )
{
  ProblemDefinition.readData( SaveFile( TheDataFile ) );
}

// -----------------------------------------------------------------------------
// Solving
// -----------------------------------------------------------------------------
//
// The solver function is more involved as must set the metric values received
// in the application execution context message as parameter values for the 
// optimisation problem, then solve for the optimal objective value, and finally
// report the solution back to the entity requesting the solution, typically an
// instance of the Solution Manager actor.

void AMPLSolver::SolveProblem( 
  const ApplicationExecutionContext & TheContext, const Address TheRequester )
{
  // Setting the metric values one by one. In the setting of NebulOuS a metric
  // is either a numerical value or a string. Vectors are currently not
  // supported as values.

  for( const auto & [ TheName, MetricValue ] : 
       Solver::MetricValueType( TheContext.at( Solver::ExecutionContext ) ) )
    SetAMPLParameter( TheName, MetricValue );

  // Setting the given objective as the active objective and all other
  // objective functions as 'dropped'. Note that this is experimental code
  // as the multi-objective possibilities in AMPL are not well documented.

  std::string OptimisationGoal;

  if( TheContext.contains( Solver::ObjectiveFunctionLabel ) )
    OptimisationGoal = TheContext.at( Solver::ObjectiveFunctionLabel );
  else if( !DefaultObjectiveFunction.empty() )
    OptimisationGoal = DefaultObjectiveFunction;
  else
  {
    std::source_location Location = std::source_location::current();
    std::ostringstream ErrorMessage;

    ErrorMessage  << "[" << Location.file_name() << " at line " 
                  << Location.line()
                  << "in function " << Location.function_name() <<"] " 
                  << "No default objective function is defined and "
                  << "the Application Execution Context message did "
                  << "not define an objective function:"
                  << std::endl << TheContext.dump(2)
                  << std::endl;

    throw std::invalid_argument( ErrorMessage.str() );
  }

  // The objective function name given must correspond to a function 
  // defined in the model, which implies that one function must be 
  // activated.

  bool ObjectiveFunctionActivated = false;

  for( auto TheObjective : ProblemDefinition.getObjectives() )
    if( TheObjective.name() == OptimisationGoal )
    {
      TheObjective.restore();
      ObjectiveFunctionActivated = true;
    }
    else
      TheObjective.drop();

  // An exception is thrown if there is no objective function activated

  if( !ObjectiveFunctionActivated )
  {
    std::source_location Location = std::source_location::current();
    std::ostringstream ErrorMessage;

    ErrorMessage  << "[" << Location.file_name() << " at line " 
                  << Location.line()
                  << "in function " << Location.function_name() <<"] " 
                  << "The objective function label " << OptimisationGoal 
                  << " does not correspond to any objective function in the "
                  << "model" << std::endl;

    throw std::invalid_argument( ErrorMessage.str() );
  }

  // The problem is valid and can then be solved.

  Optimize();

  // Once the problem has been optimised, the objective values can be 
  // be obtained from the objectives

  Solver::Solution::ObjectiveValuesType ObjectiveValues;

  for( auto TheObjective : ProblemDefinition.getObjectives() )
    ObjectiveValues.emplace( TheObjective.name(), TheObjective.value() );

  // The variable values are obtained in the same way, but each variable 
  // is checked to see if there is a constant that has to be initialised 
  // with the variable value. The AMPL parameter whose name corresponds 
  // with the constant name mapped from the variable name, will then 
  // be initialised. The constant values are only to be updated if the 
  // application execution context has the deployment flag set.

  Solver::Solution::VariableValuesType VariableValues;
  bool DeploymentFlagSet = TheContext.at( DeploymentFlag ).get<bool>();

  for( auto Variable : ProblemDefinition.getVariables() )
  {
    VariableValues.emplace( Variable.name(), Variable.value() );

    if( DeploymentFlagSet && VariablesToConstants.contains( Variable.name() ) )
      SetAMPLParameter( VariablesToConstants.at( Variable.name() ),
                        JSON( Variable.value() ) );
  }

  // The found solution can then be returned to the requesting actor or topic

  Send( Solver::Solution(
    TheContext.at( Solver::TimeStamp ).get< Solver::TimePointType >(),
    OptimisationGoal, ObjectiveValues, VariableValues, 
    DeploymentFlagSet
  ), TheRequester ); 
}

// -----------------------------------------------------------------------------
// Constructor and destructor
// -----------------------------------------------------------------------------
//
// The constructor initialises the base classes and sets the AMPL installation 
// directory and the path for the problem related files. The message handlers 
// for the data file updates must be registered since the inherited handlers 
// for the application execution context and the problem definition were already
// defined by the generic solver. Note that no publisher is defined for the 
// solution since the solution message is just returned to the requester actor,
// which is assumed to be a Solution Manager on the local endpoint because 
// multiple solvers may run in parallel. The external publication of solutions
// will be made by the Solution Manager for all solvers on this endpoint. 

AMPLSolver::AMPLSolver( const std::string & TheActorName, 
                        const ampl::Environment & InstallationDirectory,
                        const std::filesystem::path & ProblemPath,
                        const std::string TheSolverType )
: Actor( TheActorName ),
  StandardFallbackHandler( Actor::GetAddress().AsString() ),
  NetworkingActor( Actor::GetAddress().AsString() ),
  Solver( Actor::GetAddress().AsString() ),
  ProblemFileDirectory( ProblemPath ),
  ProblemDefinition( InstallationDirectory ),
  DefaultObjectiveFunction(), VariablesToConstants()
{
  RegisterHandler( this, &AMPLSolver::DataFileUpdate );

  ProblemDefinition.setOption( "solver", TheSolverType );

  Send( Theron::AMQ::NetworkLayer::TopicSubscription(
    Theron::AMQ::NetworkLayer::TopicSubscription::Action::Subscription,
    Theron::AMQ::TopicName( DataFileTopic )
  ), GetSessionLayerAddress() );
}

// In case the network is still running when the actor is closing, the data file
// subscription should be closed.

AMPLSolver::~AMPLSolver()
{
  if( HasNetwork() )
    Send( Theron::AMQ::NetworkLayer::TopicSubscription(
      Theron::AMQ::NetworkLayer::TopicSubscription::Action::CloseSubscription,
      Theron::AMQ::TopicName( DataFileTopic )
    ), GetSessionLayerAddress() );
}

} // namespace NebulOuS
