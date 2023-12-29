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
        auto [ MetricRecord, NewMetric ] = MetricValues.try_emplace( 
                                           TopicName, MetricName, JSON() );

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

void MetricUpdater::UpdateMetricValue( 
     const MetricValueUpdate & TheMetricValue, const Address TheMetricTopic)
{
  Theron::AMQ::TopicName TheTopic 
          = TheMetricTopic.AsString().erase(0, MetricValueRootString.size() );
        
  if( MetricValues.contains( TheTopic ) )
  {
    MetricValues[ TheTopic ].Value = TheMetricValue[ NebulOuS::ValueLabel ];

    ValidityTime = std::max( ValidityTime, 
      TheMetricValue[ NebulOuS::TimePoint ].get< Solver::TimePointType >() );
  }
}

// When an SLO Violation is predicted a message is received from the SLO 
// violation detector and this will trigger the definition of a new 
// application execution context and a request to the Solution Manager to 
// generate a new solution for this context. 
// 
// Note that the identifier of the application execution context is defined
// based on the time point of the severity message. The Optimiser controller
// must look for this identifier type on the solutions in order to decide 
// which solutions to deploy.

void MetricUpdater::SLOViolationHandler( 
     const SLOViolation & SeverityMessage, const Address TheSLOTopic )
{
  // The application execution context is constructed first 
  // as it represents the name and the current values of the recorded
  // metrics. Note the construction has to be done conditionally based
  // on whether the standard library containers supports the range based
  // constructors defined for C++23

  #ifdef __cpp_lib_containers_ranges
  #pragma message("C++23: Range inserters available! Rewrite MetricUpdater.hpp!")

  Solver::MetricValueType TheApplicationExecutionContext(
    std::views::transform( MetricValues, [](const auto & MetricRecord){
      return std::make_pair( MetricRecord.second.OptimisationName, 
                             MetricRecord.second.Value );
    }) );
  #else

  Solver::MetricValueType TheApplicationExecutionContext;

  for( const auto & [_, MetricRecord ] : MetricValues )
    TheApplicationExecutionContext.emplace( MetricRecord.OptimisationName, 
                                            MetricRecord.Value );

  #endif

  // The application context can then be sent to the solution manager 
  // using the corresponding message, and the time stamp of the severity 
  // message,

  Send( Solver::ApplicationExecutionContext(
    SeverityMessage[ NebulOuS::SLOIdentifier ],
    SeverityMessage[ NebulOuS::TimePoint ].get< Solver::TimePointType >(),
    SeverityMessage[ NebulOuS::ObjectiveFunctionName ],
    TheApplicationExecutionContext
  ), TheSolutionManger );
}

// The constructor initialises the base classes and sets the validity time
// to zero so that it will be initialised by the first metric values received.
// The message handlers are registered, and the the updater will then subscribe
// to the two topics published by the Optimisation Controller: One for the 
// initial message defining the metrics and the associated topics to subscribe
// to for their values, and the second for receiving the SLO violation message.

MetricUpdater::MetricUpdater( const std::string UpdaterName, 
                              const Address ManagerForSolutions )
: Actor( UpdaterName ),
  StandardFallbackHandler( Actor::GetAddress().AsString() ),
  NetworkingActor( Actor::GetAddress().AsString() ),
  MetricValues(), ValidityTime(0), TheSolutionManger( ManagerForSolutions )
{
  RegisterHandler( this, &MetricUpdater::AddMetricSubscription );
  RegisterHandler( this, &MetricUpdater::UpdateMetricValue     );
  RegisterHandler( this, &MetricUpdater::SLOViolationHandler   );

  Send( Theron::AMQ::NetworkLayer::TopicSubscription(
    Theron::AMQ::NetworkLayer::TopicSubscription::Action::Subscription,
    std::string( MetricSubscriptions ) ), 
    Theron::Network::GetAddress( Theron::Network::Layer::Session ) );

  Send( Theron::AMQ::NetworkLayer::TopicSubscription(
    Theron::AMQ::NetworkLayer::TopicSubscription::Action::Subscription,
    std::string( SLOViolationTopic ) ), 
    Theron::Network::GetAddress( Theron::Network::Layer::Session ) ); 
}

} // End name space NebulOuS