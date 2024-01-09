/*==============================================================================
Solver Component

This is the main file for the Solver Component executable including the parsing
of command line arguments and the AMQ network interface. It first starts the 
AMQ interface actors of the Network Endpoint, then creates the actors of the 
solver component: The Metric Updater and the Solution Manager, which in turn 
will start the solver actor(s). All actors are executing on proper operating 
system threads, and they are scheduled for execution whenever they have a 
pending message.

The command line arguments that can be givne to the Solver Component are

-A or --AMPLDir <installation directory> for the AMPL model interpreter
-B or --broker <URL> for the location of the AMQ broker
-E or --endpoint <name> The endpoint name 
-M ir --ModelDir <directory> for model and data files
-N or --name The AMQ identity of the solver (see below)
-P or --port <n> the port to use on the AMQ broker URL
-U or --user <user> the user to authenticate for the AMQ broker
-Pw or --password <password> the AMQ broker password for the user
-? or --Help prints a help message for the options

Default values:

-A taken from the standard AMPL environment variables if omitted
-B localhost
-E <no default - must be given>
-M <temporary directory created by the OS>
-N "NebulOuS::Solver"
-P 5672
-U admin
-Pw admin

A note on the mandatory endpoint name defining the extension used for the 
solver component when connecting to the AMQ server. Typically the connection 
will be established as "name@endpoint" and so if there are several
solver components running, the endpoint is the only way for the AMQ solvers to 
distinguish the different solver component subscriptions.

Author and Copyright: Geir Horn, University of Oslo
Contact: Geir.Horn@mn.uio.no
License: MPL2.0 (https://www.mozilla.org/en-US/MPL/2.0/)
==============================================================================*/

// Standard headers

#include <string>           // For standard strings
// #include <memory>           // For smart pointers
#include <source_location>  // Making informative error messages
#include <sstream>          // To format error messages
#include <stdexcept>        // standard exceptions
#include <filesystem>       // Access to the file system
// #include <initializer_list> // To unpack variable arguments
// #include <concepts>         // To constrain types
// #include <vector>           // To store subscribed topics
// #include <thread>           // To sleep while waiting for termination
// #include <chrono>           // To have a concept of fime

// Theron++ headers

#include "Actor.hpp"
#include "Utility/StandardFallbackHandler.hpp"
#include "Utility/ConsolePrint.hpp"

#include "Communication/PolymorphicMessage.hpp"
#include "Communication/NetworkingActor.hpp"

// AMQ protocol related headers

#include "proton/connection_options.hpp"        // Options for the Broker
#include "Communication/AMQ/AMQMessage.hpp"     // The AMQP messages
#include "Communication/AMQ/AMQEndpoint.hpp"    // The AMP endpoint
#include "Communication/AMQ/AMQjson.hpp"        // Transparent JSON-AMQP

// The cxxopts command line options parser

#include "cxxopts.hpp"

// AMPL Application Programmer Interface (API)

#include "ampl/ampl.h"

// NegulOuS related headers

#include "MetricUpdater.hpp"
#include "SolverManager.hpp"
#include "AMPLSolver.hpp"

/*==============================================================================

 Main file

==============================================================================*/
//

int main( int NumberOfCLIOptions, char ** CLIOptionStrings )
{
  // --------------------------------------------------------------------------
  // Defining and parsing the Command Line Interface (CLI) options
  // --------------------------------------------------------------------------

  cxxopts::Options CLIOptions("./SolverComponent",
    "The NebulOuS Solver component");

  CLIOptions.add_options()
    ("A,AMPLDir", "The AMPL installation path",
        cxxopts::value<std::string>()->default_value("") )
    ("B,broker", "The URL of the AMQ broker", 
        cxxopts::value<std::string>()->default_value("localhost") )
    ("E,endpoint", "The endpoint name", cxxopts::value<std::string>() )
    ("M,ModelDir", "Directory to store the model and its data",
        cxxopts::value<std::string>()->default_value("") )
    ("N,name", "The name of the Solver Component",
        cxxopts::value<std::string>()->default_value("NebulOuS::Solver") )
    ("P,port", "TCP port on  AMQ Broker", 
        cxxopts::value<unsigned int>()->default_value("5672") )
    ("U,user", "The user name used for the AMQ Broker connection", 
        cxxopts::value<std::string>()->default_value("admin") )
    ("Pw,password", "The password for the AMQ Broker connection", 
        cxxopts::value<std::string>()->default_value("admin") )
    ("?,help", "Print help information");

  CLIOptions.allow_unrecognised_options();
 
  auto CLIValues = CLIOptions.parse( NumberOfCLIOptions, CLIOptionStrings );

  if( CLIValues.count("help") )
  {
    std::cout << CLIOptions.help() << std::endl;
    exit( EXIT_SUCCESS );
  }

  // --------------------------------------------------------------------------
  // Validating directories
  // --------------------------------------------------------------------------
  //
  // The directories are given as strings and they must be validated to see if 
  // the provided values correspond to an existing directory in the case of the 
  // AMPL directory. The model directory will be created if it is not an empty
  // string, for which a temparary directory will be created. 

  std::filesystem::path TheAMPLDirectory( CLIValues["AMPLDir"].as<std::string>() );

  if( !std::filesystem::exists( TheAMPLDirectory ) )
  {
    std::source_location Location = std::source_location::current();
    std::ostringstream ErrorMessage;

    ErrorMessage << "[" << Location.file_name() << " at line " << Location.line() 
                << "in function " << Location.function_name() <<"] " 
                << "The AMPL installation driectory is given as ["
                << CLIValues["AMPLDir"].as<std::string>()
                << "] but this directory does not ezist!";

    throw std::invalid_argument( ErrorMessage.str() );
  }

  std::filesystem::path ModelDirectory( CLIValues["ModelDir"].as<std::string>() );

  if( ModelDirectory.empty() || !std::filesystem::exists( ModelDirectory ) )
    ModelDirectory = std::filesystem::temp_directory_path();

  // --------------------------------------------------------------------------
  // AMQ communication
  // --------------------------------------------------------------------------
  //
  // The AMQ communication is managed by the standard communication actors of 
  // the Theron++ Actor framewokr. Thus, it is just a matter of starting the 
  // endpoint actors with the given command line parameters.
  //
  // The network endpoint takes the endpoint name as the first argument, then 
  // the URL for the broker and the port number. The user name and the password
  // are defined in the AMQ Qpid Proton connection options, and the values are
  // therefore set for the connection options.

  proton::connection_options AMQOptions;

  AMQOptions.user( CLIValues["user"].as< std::string >() );
  AMQOptions.password( CLIValues["password"].as< std::string >() );
  
  // Then the network endpoint cna be constructed using the default names for
  // the various network endpoint servers in order to pass the defined 
  // connection options.

  Theron::AMQ::NetworkEndpoint AMQNetWork( 
    CLIValues["endpoint"].as< std::string >(), 
    CLIValues["broker"].as< std::string >(),
    CLIValues["port"].as< unsigned int >(),
    Theron::AMQ::Network::NetworkLayerLabel,
    Theron::AMQ::Network::SessionLayerLabel,
    Theron::AMQ::Network::PresentationLayerLabel,
    AMQOptions
  );

  // --------------------------------------------------------------------------
  // Solver component actors
  // --------------------------------------------------------------------------
  //
  // The solver managager must be started first since its address should be 
  // a parameter to the constructor of the Metric Updater so the latter actor 
  // knows where to send application execution contexts whenever a new solution
  // is requested by the SLO Violation Detector through the Optimzer Controller.

  NebulOuS::SolverManager< NebulOuS::AMPLSolver > 
  WorkloadMabager( "WorkloadManager", 
    std::string( NebulOuS::Solver::Solution::MessageIdentifier ), 
    std::string( NebulOuS::Solver::ApplicationExecutionContext::MessageIdentifier ),
    "AMPLSolver", ampl::Environment( TheAMPLDirectory.native() ), ModelDirectory );

  NebulOuS::MetricUpdater 
  ContextMabager( "MetricUpdater", WorkloadMabager.GetAddress() );

  // --------------------------------------------------------------------------
  // Termination management
  // --------------------------------------------------------------------------
  //
  // The critical part is to wait for the global shut down message from the 
  // Optimiser controller. That message will trigger the network to shut down
  // and the Solver Component may terminate when the actor system has finished.
  // Thus, the actors can still be running for some time after the global shut
  // down message has been received, and it is therefore necessary to also wait
  // for the actors to terminate.

  NebulOuS::ExecutionControl::WaitForTermination();
  Theron::Actor::WaitForGlobalTermination();

  return EXIT_SUCCESS;
}