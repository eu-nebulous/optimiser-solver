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
-E or --endpoint <name> The endpoint name = application identifier 
-M ir --ModelDir <directory> for model and data files
-N or --name The AMQ identity of the solver (see below)
-P or --port <n> the port to use on the AMQ broker URL
-S or --Solver <label> The back-end solver used by AMPL
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
-S couenne
-U admin
-Pw admin

A note on the mandatory endpoint name defining the extension used for the 
solver component when connecting to the AMQ server. Typically the connection 
will be established as "name@endpoint" and so if there are several
solver components running, the endpoint is the only way for the AMQ solvers to 
distinguish the different solver component subscriptions.

Notes on use:

The path to the AMPL API shared libray must be in the LIB path environment 
variable. For instance, the installation of AMPL on the author's machine is in
/opt/AMPL and so the first thing to ensure is that the path to the API library
directory is added to the link library path, e.g.,

  export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/opt/AMPL/amplapi/lib

The AMPL directory also needs to be in the path variable, and the path must
be extended with the AMPL execution file path, e.g.,

  export PATH=$PATH:/opt/AMPL

The parameters to the application are used as described above, and typically the
endpoint is set to some unique identifier of the application for which this 
solver is used, e.g.,

  ./SolverComponent --AMPLDir /opt/AMPL \
   --ModelDir AMPLTest/ --Endpoint f81ee-b42a8-a13d56-e28ec9-2f5578

Debugging after a coredump

  coredumpctl debug SolverComponent

Author and Copyright: Geir Horn, University of Oslo
Contact: Geir.Horn@mn.uio.no
License: MPL2.0 (https://www.mozilla.org/en-US/MPL/2.0/)
==============================================================================*/

// Standard headers

#include <string>           // For standard strings
#include <source_location>  // Making informative error messages
#include <sstream>          // To format error messages
#include <stdexcept>        // standard exceptions
#include <filesystem>       // Access to the file system
#include <map>              // For extended AMQ properties

// Theron++ headers

#include "Actor.hpp"
#include "Utility/StandardFallbackHandler.hpp"
#include "Utility/ConsolePrint.hpp"

#include "Communication/PolymorphicMessage.hpp"
#include "Communication/NetworkingActor.hpp"

// AMQ protocol related headers

#include "proton/symbol.hpp"                    // AMQ symbols
#include "proton/connection_options.hpp"        // Options for the Broker
#include "proton/message.hpp"                   // AMQ messages definitions
#include "proton/source_options.hpp"            // App ID filters
#include "proton/source.hpp"                    // The filter map
#include "proton/types.hpp"                     // Type definitions
#include "Communication/AMQ/AMQMessage.hpp"     // The AMQP messages
#include "Communication/AMQ/AMQEndpoint.hpp"    // The AMP endpoint
#include "Communication/AMQ/AMQjson.hpp"        // Transparent JSON-AMQP

// The cxxopts command line options parser that can be cloned from 
// https://github.com/jarro2783/cxxopts

#include "cxxopts.hpp"

// AMPL Application Programmer Interface (API)

#include "ampl/ampl.h"

// NegulOuS related headers

#include "MetricUpdater.hpp"
#include "SolverManager.hpp"
#include "AMPLSolver.hpp"

/*==============================================================================

 Main

==============================================================================*/

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
    ("B,Broker", "The URL of the AMQ broker", 
        cxxopts::value<std::string>()->default_value("localhost") )
    ("E,Endpoint", "The endpoint name", cxxopts::value<std::string>() )
    ("M,ModelDir", "Directory to store the model and its data",
        cxxopts::value<std::string>()->default_value("") )
    ("N,Name", "The name of the Solver Component",
        cxxopts::value<std::string>()->default_value("NebulOuS::Solver") )
    ("P,Port", "TCP port on  AMQ Broker", 
        cxxopts::value<unsigned int>()->default_value("5672") )
    ("S,Solver", "Solver to use, default Couenne",
        cxxopts::value<std::string>()->default_value("couenne") )
    ("U,User", "The user name used for the AMQ Broker connection", 
        cxxopts::value<std::string>()->default_value("admin") )
    ("Pw,Password", "The password for the AMQ Broker connection", 
        cxxopts::value<std::string>()->default_value("admin") )
    ("h,help", "Print help information");

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

  if( ModelDirectory.empty() ) 
    ModelDirectory = std::filesystem::temp_directory_path();
  else if ( !std::filesystem::exists( ModelDirectory ) )
  {
    if( !std::filesystem::create_directory( ModelDirectory ) )
    {
      std::source_location Location = std::source_location::current();
      std::ostringstream ErrorMessage;

      ErrorMessage  << "[" << Location.file_name() << " at line " 
                    << Location.line() << "in function " 
                    << Location.function_name() <<"] " 
                    << "The requested model directory " << ModelDirectory
                    << " does not exist and cannot be created";

      throw std::runtime_error( ErrorMessage.str() );
    }
  }

// --------------------------------------------------------------------------
  // AMQ options
  // --------------------------------------------------------------------------
  //
  // In order to be general and flexible, the various AMQ options must be 
  // provided as a user specified class to allow the user full fexibility in 
  // deciding on the connection properties. This class should keep the user 
  // name, the password, and the application identifier, which is identical 
  // to the endpoint.

  class AMQOptions
  : public Theron::AMQ::NetworkLayer::AMQProperties
  {
  private:

    const std::string User, Password, ApplicationID;

  protected:

    // The connection options just sets the user and the password to be used 
    // when the first connection is established with the AMQ broker

    virtual proton::connection_options ConnectionOptions(void) const override
    {
      proton::connection_options Options( 
              Theron::AMQ::NetworkLayer::AMQProperties::ConnectionOptions() );

      // Set credentials - ensure they are not empty
      if (!User.empty() && !Password.empty()) {
        std::cout << "Credentials provided User: " << User << " Password: *********" << std::endl;
        Options.user( User );
        Options.password( Password );
      }else{
        std::cout << "No credentials provided" << std::endl;
      }

      std::string MECHANISM = "PLAIN";
      Options.sasl_allowed_mechs(MECHANISM);
      Options.sasl_allow_insecure_mechs(true);
      Options.sasl_enabled(true);
      return Options;
    };

    // Setting the application filter is slightly more complicated as it 
    // involves setting the filter map for the sender. However, this is not 
    // well documented and the current implmenentation is based on the 
    // example for an earlier Proton version (0.32.0) and the example at
    // https://qpid.apache.org/releases/qpid-proton-0.32.0/proton/cpp/examples/selected_recv.cpp.html

    virtual proton::receiver_options ReceiverOptions( void ) const override
    {
      proton::source::filter_map TheFilter;
      proton::source_options     TheSourceOptions;
      proton::symbol             FilterKey("selector");
      proton::value              FilterValue;
      proton::codec::encoder     EncodedFilter( FilterValue );
      proton::receiver_options   TheOptions( AMQProperties::ReceiverOptions() );

      std::ostringstream SelectorString;

      SelectorString << "application = '" << ApplicationID << "'";

      EncodedFilter << proton::codec::start::described()
                    << proton::symbol("apache.org:selector-filter:string")
                    << SelectorString.str()
                    << proton::codec::finish();

      TheFilter.put( FilterKey, FilterValue );
      TheSourceOptions.filters( TheFilter );
      TheOptions.source( TheSourceOptions );

      return TheOptions;
    }

    // The application identifier must also be provided in every message to 
    // allow other receivers to filter on this. First will the default 
    // properties from the base class be set before the new application 
    // identifier property will be added.

    virtual std::map<std::string, proton::scalar> MessageProperties( 
      const proton::message::property_map & CurrentProperties 
          = proton::message::property_map() ) const override
    {
      std::map<std::string, proton::scalar> 
      TheProperties( AMQProperties::MessageProperties( CurrentProperties ) );

      TheProperties["application"] = ApplicationID;

      return TheProperties;
    }

  public:

    AMQOptions( const std::string & TheUser, const std::string & ThePassword,
                const std::string & TheAppID )
    : User( TheUser ), Password( ThePassword ), ApplicationID( TheAppID )
    {}

    AMQOptions( const AMQOptions & Other )
    : User( Other.User ), Password( Other.Password ), 
      ApplicationID( Other.ApplicationID )
    {}

    virtual ~AMQOptions() = default;
  };

  // --------------------------------------------------------------------------
  // AMQ communication
  // --------------------------------------------------------------------------
  //
  // The AMQ communication is managed by the standard communication actors of 
  // the Theron++ Actor framework. Thus, it is just a matter of starting the 
  // endpoint actors with the given command line parameters.
  //
  // The network endpoint takes the endpoint name as the first argument, then 
  // the URL for the broker and the port number. Then the network endpoint can
  // be constructed using the default names for the Session Layer and the 
  // Presentation layer servers, but calling the endpoint for "Solver" to make
  // it more visible at the AMQ broker listing of subscribers. The endpoint 
  // will be a unique application identifier. The server names are followed
  // by the defined AMQ options.

  Theron::AMQ::NetworkEndpoint AMQNetWork( 
    CLIValues["Endpoint"].as< std::string >(), 
    CLIValues["Broker"].as< std::string >(),
    CLIValues["Port"].as< unsigned int >(),
    CLIValues["Name"].as< std::string >(),
    Theron::AMQ::Network::SessionLayerLabel,
    Theron::AMQ::Network::PresentationLayerLabel,
    std::make_shared< AMQOptions >(
      CLIValues["User"].as< std::string >(),
      CLIValues["Password"].as< std::string >(),
      CLIValues["Endpoint"].as< std::string >()
    )
  );

  // --------------------------------------------------------------------------
  // Solver component actors
  // --------------------------------------------------------------------------
  //
  // The solver managager must be started first since its address should be 
  // a parameter to the constructor of the Metric Updater so the latter actor 
  // knows where to send application execution contexts whenever a new solution
  // is requested by the SLO Violation Detector through the Optimzer Controller.
  // Then follows the number of solvers to use in the solver pool and the root 
  // name of the solvers. This root name string will be extended with _n where n
  // where n is a sequence number from 1.As all solvers are of the same type 
  // given by the template parameter (here AMPLSolver), they are assumed to need
  // the same set of constructor arguments and the constructor arguments follow
  // the root solver name.

  NebulOuS::SolverManager< NebulOuS::AMPLSolver > 
  WorkloadMabager( CLIValues["Name"].as<std::string>(), 
    NebulOuS::Solver::Solution::AMQTopic, 
    NebulOuS::Solver::ApplicationExecutionContext::AMQTopic,
    1, "AMPLSolver", 
    ampl::Environment( TheAMPLDirectory.native() ), ModelDirectory, 
    CLIValues["Solver"].as<std::string>() );

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