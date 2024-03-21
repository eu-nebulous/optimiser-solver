/*==============================================================================
Metric Updater

This file implements the methods of the Metric Updater class subscribing to 
the relevant metric values of the application and publishes a data file with 
the metric values when a new solution is requested.

Author and Copyright: Geir Horn, University of Oslo
Contact: Geir.Horn@mn.uio.no
License: MPL2.0 (https://www.mozilla.org/en-US/MPL/2.0/)
==============================================================================*/

#include <ranges>                                  // Better containers
#include <source_location>                         // Informative error messages
#include <sstream>                                 // To format error messages
#include <stdexcept>                               // standard exceptions
#include <iterator>                                // Iterator support
#include <ranges>                                  // Container ranges
#include <algorithm>                               // Algorithms

#include "Utility/ConsolePrint.hpp"                // For logging
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
//
// The message is just considered if the version number of the message is larger
// than the version of the current set of metrics. The complicating factor is 
// to deal with metrics that have changed in the case the metric version is 
// increased. Then new metrics must be subscribed, deleted metrics must be 
// unsubscribed, and values for kept metrics must be kept.

void MetricUpdater::AddMetricSubscription( const MetricTopic & TheMetrics,
                                           const Address OptimiserController )
{
  if( TheMetrics.is_object() && 
      TheMetrics.at( NebulOuS::MetricList ).is_array() )
  {
    if( MetricsVersion < TheMetrics.at( MetricVersionCounter ).get<long int>() )
    {
      // The first step is to try inserting the metrics into the metric value 
      // map and if this is successful, a subscription is created for the 
      // publisherof this metric value. The metric names are recorded since 
      // some of them may correspond to known metrics, some of them may 
      // correspond to metrics that are new.

      std::set< std::string > TheMetricNames;

      for (auto & MetricRecord : TheMetrics.at( NebulOuS::MetricList ) )
      {
        auto [ MetricRecordPointer, MetricAdded ] = MetricValues.try_emplace( 
              MetricRecord.at( NebulOuS::MetricName ).get<std::string>(), JSON() );

        TheMetricNames.insert( MetricRecordPointer->first );

        // If a new metric was added, a subscription will be set up for this 
        // new metric, and the flag indicating that values have been received 
        // for all metrics will be reset.

        if( MetricAdded )
        {
          Send( Theron::AMQ::NetworkLayer::TopicSubscription( 
            Theron::AMQ::NetworkLayer::TopicSubscription::Action::Subscription,
            std::string( MetricValueRootString ) + MetricRecordPointer->first ), 
            GetSessionLayerAddress() );

          AllMetricValuesSet = false;
        }
      }

      // There could be some metric value records that were defined by the
      // previous metrics defined, but missing from the new metric set. If 
      // this is the case, the metric value records for the missing metrics
      // should be unsubcribed and their metric records removed.

      for( const auto & TheMetric : std::views::keys( MetricValues ) )
        if( !TheMetricNames.contains( TheMetric ) )
        {
          Send( Theron::AMQ::NetworkLayer::TopicSubscription( 
            Theron::AMQ::NetworkLayer::TopicSubscription::Action::CloseSubscription,
            std::string( MetricValueRootString ) + TheMetric ), 
            GetSessionLayerAddress() );

          MetricValues.erase( TheMetric );
        }
    }
  }
  else
  {
    std::source_location Location = std::source_location::current();
    std::ostringstream ErrorMessage;

    ErrorMessage << "[" << Location.file_name() << " at line " << Location.line() 
                << " in function " << Location.function_name() <<"] " 
                << "The message to define a new metric subscription is given as "
                << std::endl << TheMetrics.dump(2) << std::endl
                << "this is not as expected!";

    throw std::invalid_argument( ErrorMessage.str() );
  }
}

// The metric update value is received whenever any of subscribed forecasters
// has a new value for its metric. The format of the message is described in
// the project wiki page [1], with an example message given as
// {
//     "metricValue": 12.34,
//     "level": 1,
//     "timestamp": 163532341,
//     "probability": 0.98,
//     "confidence_interval " : [8,15]
//     "predictionTime": 163532342,
// } 
//
// Currently only the metric value and the timestamp will be used from this 
// record. It would be interesting in the future to explore ways to use the 
// confidence interval in some Bayesian resoning about the true value.
//
// The sender address will contain the metric topic, but this will contain the
// generic metric prediction root string, and this string must be removed 
// before the metric name can be updated. 
//
// Note that the map's [] operator cannot be used to look up the topic in the
// current map because it assumes the implicit creation of non-existing keys,
// which means that an empty metric value record should be constructed first
// and then used. To modify the existing record, the 'at' function must be 
// used.

void MetricUpdater::UpdateMetricValue( 
     const MetricValueUpdate & TheMetricValue, const Address TheMetricTopic)
{
  Theron::AMQ::TopicName TheTopic 
          = TheMetricTopic.AsString().erase( 0, 
                                      NebulOuS::MetricValueRootString.size() );

  if( MetricValues.contains( TheTopic ) )
  {
    MetricValues.at( TheTopic ) = TheMetricValue.at( NebulOuS::ValueLabel );
    
    ValidityTime = std::max( ValidityTime, 
      TheMetricValue.at( NebulOuS::TimePoint ).get< Solver::TimePointType >() );
  }
}

// --------------------------------------------------------------------------
// SLO Violation Events
// --------------------------------------------------------------------------
//
// When an SLO Violation is predicted a message is received from the SLO 
// violation detector and this will trigger the definition of a new 
// application execution context and a request to the Solution Manager to 
// generate a new solution for this context. 
// 
// Note that the identifier of the application execution context is defined
// based on the time point of the severity message. The Optimiser controller
// must look for this identifier type on the solutions in order to decide 
// which solutions to deploy.
//
// The message will be ignored if not all metric values have been received 
// or if there are no metric values defined. In both cases the SLO violation 
// message will just be ignored. In order to avoid the scan over all metrics
// to see if they are set, a boolean flag will be used and set once all metrics
// have values. Then future scans will be avoided.

void MetricUpdater::SLOViolationHandler( 
     const SLOViolation & SeverityMessage, const Address TheSLOTopic )
{
  Theron::ConsoleOutput Output;
  Output << "Metric Updater: SLO violation received " << std::endl
         << SeverityMessage.dump(2) << std::endl;

  if( AllMetricValuesSet || 
      (!MetricValues.empty() &&
        std::ranges::none_of( std::views::values( MetricValues ), 
        [](const auto & MetricValue){ return MetricValue.is_null(); }  )))
  {
    Send( Solver::ApplicationExecutionContext(
      SeverityMessage.at( NebulOuS::TimePoint ).get< Solver::TimePointType >(),
      MetricValues, true
    ), TheSolverManager );

    AllMetricValuesSet = true;
  }
  else
    Output << "... failed to forward the application execution context (size: " 
           << MetricValues.size() << ")" << std::endl;
}

// --------------------------------------------------------------------------
// Constructor and destructor
// --------------------------------------------------------------------------
//
// The constructor initialises the base classes and sets the validity time
// to zero so that it will be initialised by the first metric values received.
// The message handlers are registered, and the the updater will then subscribe
// to the two topics published by the Optimisation Controller: One for the 
// initial message defining the metrics and the associated topics to subscribe
// to for their values, and the second for receiving the SLO violation message.

MetricUpdater::MetricUpdater( const std::string UpdaterName, 
                              const Address ManagerOfSolvers )
: Actor( UpdaterName ),
  StandardFallbackHandler( Actor::GetAddress().AsString() ),
  NetworkingActor( Actor::GetAddress().AsString() ),
  MetricValues(), ValidityTime(0), AllMetricValuesSet(false),
  TheSolverManager( ManagerOfSolvers ),
  MetricsVersion(-1)
{
  RegisterHandler( this, &MetricUpdater::AddMetricSubscription );
  RegisterHandler( this, &MetricUpdater::UpdateMetricValue     );
  RegisterHandler( this, &MetricUpdater::SLOViolationHandler   );

  Send( Theron::AMQ::NetworkLayer::TopicSubscription(
    Theron::AMQ::NetworkLayer::TopicSubscription::Action::Subscription,
    std::string( NebulOuS::MetricSubscriptions ) ), 
    GetSessionLayerAddress() );

  Send( Theron::AMQ::NetworkLayer::TopicSubscription(
    Theron::AMQ::NetworkLayer::TopicSubscription::Action::Subscription,
    std::string( NebulOuS::SLOViolationTopic ) ), 
    GetSessionLayerAddress() ); 
}

// The destructor is closing the established subscription if the network is 
// still running. If this is called when the application is closing the network
// connection should be stopped, and in that case all subscriptions will be 
// automatically cancelled. 

MetricUpdater::~MetricUpdater()
{
  if( HasNetwork() )
  {
    Send( Theron::AMQ::NetworkLayer::TopicSubscription(
      Theron::AMQ::NetworkLayer::TopicSubscription::Action::CloseSubscription,
      std::string( NebulOuS::MetricSubscriptions ) ), 
      GetSessionLayerAddress() );

    Send( Theron::AMQ::NetworkLayer::TopicSubscription(
      Theron::AMQ::NetworkLayer::TopicSubscription::Action::CloseSubscription,
      std::string( NebulOuS::SLOViolationTopic ) ), 
      GetSessionLayerAddress() );  

    std::ranges::for_each( std::views::keys( MetricValues ),
    [this]( const Theron::AMQ::TopicName & TheMetricTopic ){
      Send( Theron::AMQ::NetworkLayer::TopicSubscription(
        Theron::AMQ::NetworkLayer::TopicSubscription::Action::CloseSubscription,
        std::string( MetricValueRootString ) + TheMetricTopic ), 
        GetSessionLayerAddress() );
    });
  }
}

} // End name space NebulOuS