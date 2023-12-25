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

namespace NebulOuS 
{
// Definitions for the terminology to facilitate changing the lables of the 
// various message labels without changing the code. The definitions are 
// compile time constants and as such should not lead to any run-time overhead.

constexpr std::string_view ValueLabel{ "metricValue" };
constexpr std::string_view TimePoint { "predictionTime" };

// The topic used for receiving the message(s) defining the metrics of the 
// application execution context as published by the Optimiser Controller is 
// defined next.

constexpr std::string_view MetricSubscriptions{ "ApplicationContext" };

// The metric value messages will be published on different topics and to 
// check if an inbound message is from a metric value topic, it is necessary 
// to test against the base string for the metric value topics according to 
// the Wiki-page at
// https://openproject.nebulouscloud.eu/projects/nebulous-collaboration-hub/wiki/monitoringdata-interface

constexpr std::string_view MetricValueRootString{
  "eu.nebulouscloud.monitoring.predicted"
};

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
  // attributes are the metric names and the values are JSON values because 
  // they are polymorphic with respect to different variable types, and as 
  // they arrive as JSON values this avoids converting the values on input and
  // output. The metric optimisation name is just a string.

private:

  class MetricValueRecord
  {
  public:
    const std::string OptimisationName;
    JSON              Value;

    MetricValueRecord( const std::string & TheName, JSON InitialValue )
    : OptimisationName( TheName ), Value( InitialValue )
    {}

    MetricValueRecord( const MetricValueRecord & Other )
    : OptimisationName( Other.OptimisationName ), Value( Other.Value )
    {}

    MetricValueRecord()  = delete;
    ~MetricValueRecord() = default;
  };

  // This value record is used in the map where the subscribed topic name is 
  // the key so that values can quickly be updated when messages arrives.

  std::unordered_map< Theron::AMQ::TopicName, MetricValueRecord > MetricValues;

  // --------------------------------------------------------------------------
  // JSON messages: Type by topic
  // --------------------------------------------------------------------------
  //
  // The JSON message initialiser assumes that the content_type field of 
  // the message contains an unique label for the JSON message type to 
  // cover the situation where an actor may subscribe to multiple different
  // messages all encoded as JSON messages. However, for this actor the type 
  // of the message will be decided by the topic on which the message is 
  // received. It is therefore necessary to set the message content type equal 
  // to the AMQ sender prior to decoding the AMQ message to the correct JSON
  // object.
  //
  // The issue with the metric subscriptions is that the same type of message
  // can come from any of the topics publishing metric values, and as such any 
  // topic name not being from the metric subscription command topic or the 
  // SLO Violation Event topic will be understood as a metric value update 
  // event The initialiser will check if the sender (topic) starts with the 
  // message identifier. This will allow the wildcard matching for metric 
  // values as well as an exact match for topic whose reply to address 
  // equals the message identifer.
  
  class TypeByTopic
  : public Theron::AMQ::JSONMessage
  {
  protected:

    virtual bool 
    Initialize( const ProtocolPayload & ThePayload ) noexcept override
    {
      if( ThePayload->reply_to().starts_with( GetMessageIdentifier() ) )
      {
        ThePayload->content_type( GetMessageIdentifier() );
        return JSONMessage::Initialize( ThePayload );
      }
      else return false;
    }
  
  public:

    TypeByTopic( const std::string & TopicIdentifier )
    : JSONMessage( TopicIdentifier )
    {}
    
    TypeByTopic( const TypeByTopic & Other )
    : JSONMessage( Other.GetMessageIdentifier(), Other )
    {}
    
    virtual ~TypeByTopic() = default;
  };

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
  : public TypeByTopic
  {
  public:

    MetricTopic( void )
    : TypeByTopic( MetricSubscriptions.data() )
    {}

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
  
  class MetricValueUpdate
  : public TypeByTopic
  {
  public:

    MetricValueUpdate( void )
    : TypeByTopic( MetricValueRootString.data() )
    {}
    
  };

  // The handler function will check the sender address against the subscribed
  // topics and if a match is found it will update the value of the metric. 
  // if no subscribed metric corresponds to the received message, the message
  // will just be discarded.

  void UpdateMetricValue( const MetricValueUpdate & TheMetricValue, 
                          const Address TheMetricTopic );

};      // Class Metric Updater
}       // Name space NebulOuS
#endif  // NEBULOUS_METRIC_UPDATE