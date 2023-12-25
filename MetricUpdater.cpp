/*==============================================================================
Metric Updater

This file implements the methods of the Metric Updater class subscribing to 
the relevant metric values of the application and publishes a data file with 
the metric values when a new solution is requested.

Author and Copyright: Geir Horn, University of Oslo
Contact: Geir.Horn@mn.uio.no
License: MPL2.0 (https://www.mozilla.org/en-US/MPL/2.0/)
==============================================================================*/

#include "ranges"           // Better containers
#include <source_location>  // Making informative error messages
#include <sstream>          // To format error messages
#include <stdexcept>        // standard exceptions


#include "Communication/AMQ/AMQEndpoint.hpp"       // For Topic subscriptions

#include "MetricUpdater.hpp"

namespace NebulOuS
{

// --------------------------------------------------------------------------
// Subscribing to metric prediction values
// --------------------------------------------------------------------------
//
// The received message must be a JSON object with metric names as 
// attribute (keys) and the topic name as the value. Multiple metrics maby be
// included in the same message and and the andler will iterate and set up a 
// subcription for each of the provided metrics. It should be noted that 
// initially the metric has no value, and it is a prerequisite that all 
// metric values must be updated before the complete set of metrics will be 
// used for finding a better configuration for the application's execution 
// context given by the metric values.

void MetricUpdater::AddMetricSubscription( const MetricTopic & TheMetrics,
                                           const Address OptimiserController )
{
    if( TheMetrics.is_object() )
      for( const auto & [MetricName, TopicName] : TheMetrics.items() )
      {
        auto [ MetricRecord, NewMetric ] = MetricValues.try_emplace( TopicName, 
                                                        MetricName, JSON() );
                                                        
        if( NewMetric )
          Send( Theron::AMQ::NetworkLayer::TopicSubscription( 
                Theron::AMQ::NetworkLayer::TopicSubscription::Action::Subscription,
                TopicName ), 
                Theron::AMQ::Network::GetAddress( Theron::Network::Layer::Session) );
      }
    else
    {
      std::source_location Location = std::source_location::current();
      std::ostringstream ErrorMessage;

      ErrorMessage << "[" << Location.file_name() << " at line " << Location.line() 
                  << "in function " << Location.function_name() <<"] " 
                  << "The message to define a new metric subscription is given as "
                  << std::endl << TheMetrics.dump(2) << std::endl
                  << "this is not a JSON object!";

      throw std::invalid_argument( ErrorMessage.str() );
    }
}

} // End name space NebulOuS