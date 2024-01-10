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

 Basic interface definitions

==============================================================================*/
//
// Definitions for the terminology to facilitate changing the lables of the 
// various message labels without changing the code. The definitions are 
// compile time constants and as such should not lead to any run-time overhead.
// The JSON attribute names may be found under the "Predicted monitoring 
// metrics" section on the Wiki page [1].

constexpr std::string_view ValueLabel = "metricValue";
constexpr std::string_view TimePoint  = "predictionTime";

// The topic used for receiving the message(s) defining the metrics of the 
// application execution context as published by the Optimiser Controller is 
// defined next.

constexpr std::string_view MetricSubscriptions 
          = "eu.nebulouscloud.monitoring.metric_list";

// The JSON message attribute for the list of metrics is another JSON object
// stored under the following key, see the Event type III defined in 
// https://158.39.75.54/projects/nebulous-collaboration-hub/wiki/slo-severity-based-violation-detector
// where the name of the metric is defined under as sub-key.

constexpr std::string_view MetricList = "metric_list";
constexpr std::string_view MetricName = "name";

// The metric value messages will be published on different topics and to 
// check if an inbound message is from a metric value topic, it is necessary 
// to test against the base string for the metric value topics according to 
// the Wiki-page [1]

constexpr std::string_view MetricValueRootString
          = "eu.nebulouscloud.monitoring.predicted.";

// The SLO violation detector will publish a message when a reconfiguration is 
// deamed necessary for a future time point called "Event type V" on the wiki 
// page [3]. The event contains a probability for at least one of the SLOs 
// being violated at the predicted time point. It is not clear if the assessment
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
//
// However, the Metric updater will get this message from the Optimiser 
// Controller component only if an update must be made. The message must 
// contain a unique identifier, a time point for the solution, and the objective
// function to be maximised.

constexpr std::string_view SLOIdentifier = "Identifier";
constexpr std::string_view ObjectiveFunctionName = "ObjectiveFunction";

// The messages from the Optimizer Controller will be sent on a topic that 
// should follow some standard topic convention.

constexpr std::string_view SLOViolationTopic 
          = "eu.nebulouscloud.optimiser.solver.slo";

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

  std::unordered_map< Theron::AMQ::TopicName, JSON > MetricValues;

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

  const Address TheSolverManager;

  // --------------------------------------------------------------------------
  // Subscribing to metric prediction values
  // --------------------------------------------------------------------------
  //
  // Initially, the Optimiser Controller will pass a message containing all 
  // optimiser metric names and the AMQ topic on which their values will be 
  // published. Essentially, these messages arrives as a JSON message with 
  // one attribute per metric, and where the value is the topic string for 
  // the value publisher.

  class MetricTopic
  : public Theron::AMQ::JSONTopicMessage
  {
  public:

    MetricTopic( void )
    : JSONTopicMessage( std::string( MetricSubscriptions ) )
    {}

    MetricTopic( const MetricTopic & Other )
    : JSONTopicMessage( Other )
    {}

    // MetricTopic( const JSONTopicMessage & Other )
    // : JSONTopicMessage( Other )
    // {}

    virtual ~MetricTopic() = default;
  };

  // The handler for this message will check each attribute value of the 
  // received JSON struct, and those not already existing in the metric 
  // value map be added and a subscription made for the published 
  // prediction values.

  void AddMetricSubscription( const MetricTopic & TheMetrics, 
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
  // SLO violations
  // --------------------------------------------------------------------------
  //
  // The SLO Violation detector publishes an event to indicate that at least 
  // one of the constraints for the application deployment will be violated in 
  // the predicted future, and that the search for a new solution should start.
  // This message is caught by the Optimisation Controller and republished 
  // adding a unique event identifier enabling the Optimisation Controller to
  // match the produced solution with the event and deploy the right 
  // configuration.The message must also contain the name of the objective 
  // function to maximise. This name must match the name in the optimisation
  // model sent to the solver.

  class SLOViolation
  : public Theron::AMQ::JSONTopicMessage
  {
  public:

    SLOViolation( void )
    : JSONTopicMessage( std::string( SLOViolationTopic ) )
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