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

#include "AMPLSolver.hpp"

namespace NebulOuS
{

// -----------------------------------------------------------------------------
// Utility function
// -----------------------------------------------------------------------------
//

std::string AMPLSolver::SaveFile( const JSON & TheMessage, 
                                  const std::source_location & Location )
{
  if( TheMessage.is_object() )
  {
    // Writing the problem file based on the message content that should be 
    // only a single key-value pair. If the file could not be opened, a run
    // time exception is thrown. 

    std::string TheFileName 
                = ProblemFileDirectory / TheMessage.begin().key();

    std::fstream ProblemFile( TheFileName, std::ios::out );
                        
    if( ProblemFile.is_open() )
    {
      ProblemFile << TheMessage.begin().value();
      ProblemFile.close();
      return TheFileName;
    }
    else
    {
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

// -----------------------------------------------------------------------------
// Optimisation
// -----------------------------------------------------------------------------
//
// The first step in solving an optimisation problem is to define the problme 
// involving the decision variables, the parameters, and the constraints over
// these entities. The problem is received as an AMQ JSON message where where
// the only key is the file name and the value is the AMPL model file. This file
// is first saved, and if there is no exception thrown form the save file 
// function, the filename will be returned and read back into the problem 
// definition.

void AMPLSolver::DefineProblem(const Solver::OptimisationProblem & TheProblem,
                               const Address TheOracle)
{
    ProblemDefinition.read( SaveFile( TheProblem ) );
}

// The data file(s) corresponding to the current optimisation problem will be 
// sent in the same way and separately file by file. The logic is the same as 
// the Define Problem message handler: The save file is used to store the 
// received file, which is then loaded as the data problem.

void AMPLSolver::DataFileUpdate( const DataFileMessage & TheDataFile, 
                                 const Address TheOracle )
{
  ProblemDefinition.readData( SaveFile( TheDataFile ) );
}

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
  {
    ampl::Parameter TheParameter = ProblemDefinition.getParameter( TheName );
    
    switch ( MetricValue.type() )
    {
      case JSON::value_t::number_integer :
      case JSON::value_t::number_unsigned :
      case JSON::value_t::boolean :
        TheParameter.set( MetricValue.get< long >() );
        break;
      case JSON::value_t::number_float :
        TheParameter.set( MetricValue.get< double >() );
        break;
      case JSON::value_t::string :
        TheParameter.set( MetricValue.get< std::string >() );
        break;
      default:
        {
          std::source_location Location = std::source_location::current();
          std::ostringstream ErrorMessage;

          ErrorMessage  << "[" << Location.file_name() << " at line " 
                        << Location.line()
                        << "in function " << Location.function_name() <<"] " 
                        << "The JSON value " << MetricValue 
                        << " has JSON type " 
                        << static_cast< int >( MetricValue.type() )
                        << " which is not supported"
                        << std::endl;

          throw std::invalid_argument( ErrorMessage.str() );
        }
        break;
    }
  }

  // Setting the given objective as the active objective and all other
  // objective functions as 'dropped'. Note that this is experimental code
  // as the multi-objective possibilities in AMPL are not well documented.

  std::string 
  OptimisationGoal = TheContext.at( Solver::ObjectiveFunctionLabel );

  for( auto TheObjective : ProblemDefinition.getObjectives() )
    if( TheObjective.name() == OptimisationGoal )
      TheObjective.restore();
    else
      TheObjective.drop();
 
  // The problem can then be solved.

  Optimize();

  // Once the problem has been optimised, the objective values can be 
  // be obtained from the objectives

  Solver::Solution::ObjectiveValuesType ObjectiveValues;

  for( auto TheObjective : ProblemDefinition.getObjectives() )
    ObjectiveValues.emplace( TheObjective.name(), TheObjective.value() );

  // The variable values are obtained in the same way

  Solver::Solution::VariableValuesType VariableValues;

  for( auto Variable : ProblemDefinition.getVariables() )
    VariableValues.emplace( Variable.name(), Variable.value() );

  // The found solution can then be returned to the requesting actor or topic

  Send( Solver::Solution(
    TheContext.at( Solver::ContextIdentifier ),
    TheContext.at( Solver::TimeStamp ).get< Solver::TimePointType >(),
    TheContext.at( Solver::ObjectiveFunctionLabel ),
    ObjectiveValues, VariableValues
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
                        const std::filesystem::path & ProblemPath )
: Actor( TheActorName ),
  StandardFallbackHandler( Actor::GetAddress().AsString() ),
  NetworkingActor( Actor::GetAddress().AsString() ),
  Solver( Actor::GetAddress().AsString() ),
  ProblemFileDirectory( ProblemPath ),
  ProblemDefinition( InstallationDirectory )
{
  RegisterHandler( this, &AMPLSolver::DataFileUpdate );

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
