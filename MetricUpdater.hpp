/*==============================================================================
Metric Updater

This is an actor that gets the metrics and the corresponding AMQ topics where 
predictions for the metrics will be published, and subscribes to updates for 
these metric predictions. When a new prediction is provided it is initially 
just recorded. When the SLO Violation Detector decides that a new application 
configuration is necessary to maintain the application within the feasible 
region, it will issue a message to indicate that a new solution is necessary. 
The Metric Updater will respond to this message by generating a data file 
containing the parameters and their values and publish this data file. This 
will then be used by the solver to find the optimal configuration for the 
the given application execution context defined by the recorded set of metric
value predictions.

The metrics of the application execution context can either be sent as one 
JSON message where the attributes are the metric names to be used in the 
optimisation data file and the values are the metric topic paths, or as a 
sequence of messages, one per metric.

The metric values are sent using messages as defined by the Event Management 
System (EMS) [1], and the format of the data file generated for the solver
follows the AMPL data file format [2]. The message from the SLO Violation 
Detector is supposed to be a message for Event V since all the predicted 
metric values are already collected [3]. In future versions it may be possible
to also use the computed erro bounds for the metric predictions of the other 
SLO Violation events calculated.

References:
[1] https://openproject.nebulouscloud.eu/projects/nebulous-collaboration-hub/wiki/monitoringdata-interface
[2] https://ampl.com/wp-content/uploads/Chapter-9-Specifying-Data-AMPL-Book.pdf 
[3] https://openproject.nebulouscloud.eu/projects/nebulous-collaboration-hub/wiki/slo-severity-based-violation-detector 

Author and Copyright: Geir Horn, University of Oslo
Contact: Geir.Horn@mn.uio.no
License: MPL2.0 (https://www.mozilla.org/en-US/MPL/2.0/)
==============================================================================*/

#ifndef NEBULOUS_METRIC_UPDATE
#define NEBULOUS_METRIC_UPDATE

// Standard headers

#include <string_view>                          // Constant strings
#include <unordered_map>                        // To store metric-value maps

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

namespace NebulOuS 
{
/*==============================================================================

 Metric Updater

==============================================================================*/
//
// The Metric Updater actor is an Networking Actor supporting the AMQ message
// exchange.

class MetricUpdater
: virtual public Theron::Actor,
  virtual public Theron::StandardFallbackHandler,
  virtual public Theron::NetworkingActor< 
    typename Theron::AMQ::Message::PayloadType >
{
private:

   using ProtocolPayload = Theron::PolymorphicMessage< 
          typename Theron::AMQ::Message::PayloadType >::PayloadType;

  // --------------------------------------------------------------------------
  // Metric value registry
  // --------------------------------------------------------------------------
  //
  // The metric values are stored essentially as a JSON values where the 
  // attributes are the metric names and the values are JSON values. It is 
  // assumed that same metric name is used both for the optimisation model 
  // and for the metric topic.

  Solver::MetricValueType MetricValues;

  // The metric values should ideally be forecasted for the same future time
  // point, but this may not be assured, and as such a zero-order hold is 
  // assumed for all metric values. This means that the last value received 
  // for a metric is taken to be valid until the next update. The implication 
  // is that the whole vector of metric values is valid for the largest time 
  // point of any of the predictions. Hence, the largest prediction time point 
  // must be stored for being able to associate a time point of validity to 
  // the retruned metric vector.

  Solver::TimePointType ValidityTime;

  // When an SLO violation message is received the current vector of metric 
  // values should be sent as an application execution context (message) to the
  // Solution Manager actor that will invoke a solver to find the optimal 
  // configuration for this configuration. The Metric Updater must therefore 
  // know the address of the Soler Manager, and this must be passed to 
  // the constructor.

  bool AllMetricValuesSet;

  // --------------------------------------------------------------------------
  // Subscribing to metric prediction values
  // --------------------------------------------------------------------------
  //
  // Initially, the Optimiser Controller will pass a message containing all 
  // optimiser metric names that are used in the optimisation and therefore 
  // constitutes the application's execution context. This message is a simple
  // JSON map containing an array since the Optimiser Controller is not able
  // to send just an array.

  class MetricTopic
  : public Theron::AMQ::JSONTopicMessage
  {
  public:

    // The topic used for receiving the message(s) defining the metrics of the 
    // application execution context as published by the Optimiser Controller 
    // is the following

    static constexpr std::string_view AMQTopic 
                     = "eu.nebulouscloud.optimiser.controller.metric_list";

    // The EXN middleware sending the AMQP messages for the other component 
    // of the NebulOuS project only sends JSON objects, meaning that the list 
    // of metric names to subscribe is sent as a JSON array, but it must be 
    // embedded in a map with a single key, see the message format described
    // in the Wiki page at
    // https://openproject.nebulouscloud.eu/projects/nebulous-collaboration-hub/wiki/1-optimiser-controller#controller-to-metric-updater-and-ems-metric-list

    struct Keys
    {
      static constexpr std::string_view MetricList = "metrics";
    };
 
    // Constructors

    MetricTopic( void )
    : JSONTopicMessage( AMQTopic )
    {}

    MetricTopic( const MetricTopic & Other )
    : JSONTopicMessage( Other )
    {}

    virtual ~MetricTopic() = default;
  };

  // The handler for this message will check each attribute value of the 
  // received JSON struct, and those not already existing in the metric 
  // value map be added and a subscription made for the published 
  // prediction values.

  void AddMetricSubscription( const MetricTopic & MetricDefinitions, 
                              const Address OptimiserController );

  // --------------------------------------------------------------------------
  // Metric values
  // --------------------------------------------------------------------------
  //
  // The metric value message is defined as a topic message where the message 
  // identifier is the root of the metric value topic name string. This is 
  // identical to a wildcard operation matching all topics whose name start
  // with this string. 
  
  class MetricValueUpdate
  : public Theron::AMQ::JSONWildcardMessage
  {
  public:

    // The metric value messages will be published on different topics and to 
    // check if an inbound message is from a metric value topic, it is 
    // necessary to test against the base string for the metric value topics 
    // according to the Wiki-page describing the Type II message:
    // https://openproject.nebulouscloud.eu/projects/nebulous-collaboration-hub/wiki/monitoringdata-interface#type-ii-messages-predicted-monitoring-metrics

    static constexpr std::string_view MetricValueRootString
                      = "eu.nebulouscloud.monitoring.predicted.";

    // Only two of the fields in this message will be looked up and stored
    // in the current application context map.

    struct Keys
    {
      static constexpr std::string_view 
                ValueLabel = "metricValue",
                TimePoint  = "predictionTime";
    };

    MetricValueUpdate( void )
    : JSONWildcardMessage( std::string( MetricValueRootString ) )
    {}
    
    MetricValueUpdate( const MetricValueUpdate & Other )
    : JSONWildcardMessage( Other )
    {}

    virtual ~MetricValueUpdate() = default;
  };

  // The handler function will update the value of the subscribed metric  
  // based on the given topic name. If there is no such metric known, then the
  // message will just be discarded.

  void UpdateMetricValue( const MetricValueUpdate & TheMetricValue, 
                          const Address TheMetricTopic );

  // --------------------------------------------------------------------------
  // Application lifecycle
  // --------------------------------------------------------------------------
  //
  // There is a message from the Optimiser Controller when the status of the 
  // application changes. The state communicated in this message shows the 
  // current state of the application and decides how the Solver will act to
  // SLO Violations detected.

  class ApplicationLifecycle
  : public Theron::AMQ::JSONTopicMessage
  { 
  public:

    // The topic for the reconfiguration finished messages is defined by the 
    // optimiser as the sender.

    static constexpr std::string_view AMQTopic
                     = "eu.nebulouscloud.optimiser.controller.app_state";

    // The state of the application goes from the the initial creation of 
    // the cluster to deployments covering reconfigurations. Note that there is
    // no state indicating that the application has terminated.

    enum class State
    {
      New,        // Waiting for the utility evaluator
      Ready,      // The application is ready for deployment
      Deploying,  // The application is being deployed or redeployed
      Running,    // The application is running
      Failed     // The application is in an invalid state
    };

    // An arriving lifecycle message indicates a change in state and it is 
    // therefore a way to set a state variable directly from the message by
    // a cast operator

    operator State() const;

    // Constructors and destructor

    ApplicationLifecycle( void )
    : JSONTopicMessage( AMQTopic )
    {}

    ApplicationLifecycle( const ApplicationLifecycle & Other )
    : JSONTopicMessage( Other )
    {}

    virtual ~ApplicationLifecycle() = default;
  };

  // After starting a reconfiguration with an SLO Violation, one should not 
  // initiate another reconfiguration because the state may the possibly be 
  // inconsistent with the SLO Violation Detector belieivng that the old 
  // configuration is still in effect while the new configuration is being 
  // enacted. The application lifecycle state must therefore be marked as 
  // running before the another SLO Violation will trigger the next 
  // reconfiguration

  ApplicationLifecycle::State ApplicationState;

  // The handler for the lifecycle message simply updates this variable by 
  // setting it to the state received in the lifecycle message.

  void LifecycleHandler( const ApplicationLifecycle & TheState, 
                         const Address TheLifecycleTopic );

  // --------------------------------------------------------------------------
  // SLO violations
  // --------------------------------------------------------------------------
  //
  // The SLO violation detector will publish a message when a reconfiguration is 
  // deamed necessary for a future time point called "Event type VI" on the wiki 
  // page: https://openproject.nebulouscloud.eu/projects/nebulous-collaboration-hub/wiki/slo-severity-based-violation-detector#output-event-type-vi 
  // The event contains a probability for at least one of the SLOs  being 
  // violated at the predicted time point. It is not clear if the assessment
  // is being made by the SLO violation detector at every new metric prediction,
  // or if this event is only signalled when the probability is above some 
  // internal threshold of the SLO violation detector. 
  //
  // The current implementation assumes that the latter is the case, and hence 
  // just receiving the message indicates that a new application configuration 
  // should be found given the application execution context as predicted by the 
  // metric values recorded by the Metric Updater.  Should this assumption be 
  // wrong, the probability must be compared with some  user set threshold for 
  // each message, and to cater for this the probability field will always be 
  // compared to a threshold, currently set to zero to ensure that every event 
  // message will trigger a reconfiguration.
  
  class SLOViolation
  : public Theron::AMQ::JSONTopicMessage
  {
  public:

    // The messages from the SLO Violation Detector will be sent on a topic that 
    // should follow some standard topic convention.

    static constexpr std::string_view AMQTopic 
                      = "eu.nebulouscloud.monitoring.slo.severity_value";

    // The only information taken from this detction message is the prediction 
    // time which will be used as the time for the application's execution 
    // context when this is forwarded to the solvers for processing.

    struct Keys
    {
      static constexpr std::string_view TimePoint  = "predictionTime";
    };
    
    // Constructors

    SLOViolation( void )
    : JSONTopicMessage( AMQTopic )
    {}

    SLOViolation( const SLOViolation & Other )
    : JSONTopicMessage( Other )
    {}

    virtual ~SLOViolation() = default;
  };

  // The handler for this message will generate an Application Execution 
  // Context message to the Solution Manager passing the values of all 
  // the metrics currently kept by the Metric Updater. 

  void SLOViolationHandler( const SLOViolation & SeverityMessage, 
                            const Address TheSLOTopic );

  // The application execution context (message) will be sent to the
  // Solution Manager actor that will invoke a solver to find the optimal 
  // configuration for this configuration. The Metric Updater must therefore 
  // know the address of the Solver Manager, and this must be passed to 
  // the constructor and stored for for the duration of the execution

  const Address TheSolverManager;

  // --------------------------------------------------------------------------
  // Constructor and destructor
  // --------------------------------------------------------------------------
  //
  // The constructor requires the name of the Metric Updater Actor, and the 
  // actor address of the Solution Manager Actor. It registers the handlers
  // for all the message types

public:

  MetricUpdater( const std::string UpdaterName, 
                 const Address ManagerOfSolvers );

  // The destructor will unsubscribe from the control channels for the 
  // message defining metrics, and the channel for receiving SLO violation
  // events.
  
  virtual ~MetricUpdater();

};      // Class Metric Updater
}       // Name space NebulOuS
#endif  // NEBULOUS_METRIC_UPDATE