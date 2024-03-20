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
          = "eu.nebulouscloud.optimiser.controller.metric_list";

// The JSON message attribute for the list of metrics is another JSON object
// stored under the following key, see the Event type III defined in 
// https://158.39.75.54/projects/nebulous-collaboration-hub/wiki/slo-severity-based-violation-detector
// where the name of the metric is defined under as sub-key.

constexpr std::string_view MetricList = "metrics";

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
// The messages from the Optimizer Controller will be sent on a topic that 
// should follow some standard topic convention.

constexpr std::string_view SLOViolationTopic 
          = "eu.nebulouscloud.monitoring.slo.severity_value";

// When a reconfiguration has been enacted by the Optimiser Controller and 
// a new configuration is confirmed to be running on the new platofrm, it will 
// send a message to inform all other components that the reconfiguration 
// has happened on the following topic.

constexpr std::string_view ReconfigurationTopic
          = "eu.nebulouscloud.optimiser.adaptations";

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

  // There is also a flag to indicate when all metric values have received 
  // values since optimising for a application execution context defiend all 
  // metrics requires that at least one value is received for each metric. This
  // condition could be tested before sending the request to find a new 
  // solution, but this means testing all metrics in a linear scan for a 
  // condition that will only happen initially until all metrics have been seen
  // and so it is better for the performance if there is a flag to check for 
  // this condition.

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
  // This will trigger the the publication of the Solver's Application Execution 
  // context message. The context message will contain the current status of the
  // metric values, and trigger a solver to find a new, optimal variable 
  // assignment to be deployed to resolve the identified problem.

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

  // The application execution context (message) will be sent to the
  // Solution Manager actor that will invoke a solver to find the optimal 
  // configuration for this configuration. The Metric Updater must therefore 
  // know the address of the Solver Manager, and this must be passed to 
  // the constructor and stored for for the duration of the execution

  const Address TheSolverManager;

  // After the sending of the application's excution context, one should not 
  // initiate another reconfiguration because the state may the possibly be 
  // inconsistent with the SLO Violation Detector belieivng that the old 
  // configuration is still in effect while the new configuration is being 
  // enacted. It is therefore a flag that will be set by the SLO Violation 
  // handler indicating that a reconfiguration is ongoing.

  bool ReconfigurationInProgress;

  // When the reconfiguration has been done and the Optimizer Controller 
  // confirms that the application is running in a new configuration, it will 
  // send a reconfiguration completed message. This message will just be a 
  // JSON message.

  class ReconfigurationMessage
  : public Theron::AMQ::JSONTopicMessage
  { 
  public:

    ReconfigurationMessage( void )
    : JSONTopicMessage( std::string( ReconfigurationTopic ) )
    {}

    ReconfigurationMessage( const ReconfigurationMessage & Other )
    : JSONTopicMessage( Other )
    {}

    virtual ~ReconfigurationMessage() = default;
  };

  // The handler for this message will actually not use its contents, but only
  // note that the reconfiguration has been completed to reset the 
  // reconfiguration in progress flag allowing future SLO Violation Events to 
  // triger new reconfigurations.

  void ReconfigurationDone( const ReconfigurationMessage & TheReconfiguraton, 
                            const Address TheReconfigurationTopic );

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