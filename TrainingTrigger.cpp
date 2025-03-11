/*==============================================================================
Training Trigger

The performance indicators are regression functions where the regressors are 
the metric values and the application configuration variables. Each metric 
observation may therefore result in a new regression function. The training of
the regression functions is triggered by the arrival of a sufficient number of
metric observations. The training trigger extends the Metric Updater actor 
providing a counter to count the number of metric observations. When the counter
reaches a predefined limit set individually for the various performance 
indicator trainers, the training trigger will send a message to the trainer
actor to start the training process. 

The actors are implemented using the Theron++ Actor ibrary [1].

References:
[1] https://github.com/GeirHo/TheronPlusPlus

Author and Copyright: Geir Horn, University of Oslo
Contact: Geir.Horn@mn.uio.no
License: MPL2.0 (https://www.mozilla.org/en-US/MPL/2.0/)
==============================================================================*/

// Standard headers

#include <algorithm>                        // Standard algorithms

// NebulOuS specific headers

#include "TrainingTrigger.hpp"

namespace NebulOuS
{
// --------------------------------------------------------------------------
// New subscription
// --------------------------------------------------------------------------
//
// The new subscription handler will store the subscription in the map. If a new
// subscription is made for an existing trainer, the old subscription is replaced.
// This may happen if the trainer algorithm adapts to more data available. To cancel
// the subscription, a trainer sends a message with a zero count.

void TrainingTrigger::NewSubscription( const unsigned long & TheTriggerCount, 
                                       const Address TheTrainer )
{
  // If the count is zero, the subscription is cancelled

  if ( TheTriggerCount == 0 )
      TriggerSubscribers.erase( TheTrainer );
  else
  {
      TriggerSubscribers[ TheTrainer ] = TheTriggerCount;

      if ( MetricCounter > TheTriggerCount )
          Send( RetrainRegression( MetricCounter ), TheTrainer );
  }
}

// --------------------------------------------------------------------------
// Update metric value
// --------------------------------------------------------------------------
//
// The counter is updated and possible triggers sent when a new metric arrives and
// this is detected by overloading the the metric update handler.

void TrainingTrigger::UpdateMetricValue( 
    const MetricValueUpdate & TheMetricValue, const Address TheMetricTopic )
{
  // The standard processing of the metric value update is done first.

  MetricUpdater::UpdateMetricValue( TheMetricValue, TheMetricTopic );
  
  // The counter is updated

  MetricCounter += 1;

  // The counter is checked against the trigger counts for the various trainers

  std::ranges::for_each( TriggerSubscribers, 
  [ this ]( const auto & [ Trainer, TriggerCount ] )
  {
      if ( MetricCounter % TriggerCount == 0 )
        Send( RetrainRegression( MetricCounter ), Trainer );
  } );
}

// --------------------------------------------------------------------------
// Constructor and destructor
// --------------------------------------------------------------------------

TrainingTrigger( const std::string UpdaterName, const Address ManagerOfSolvers )
: Actor( UpdaterName ), 
  StandardFallbackHandler( Actor::GetAddress().AsString() ),
  MetricUpdater( Actor::GetAddress().AsString(), ManagerOfSolvers ),
  TriggerSubscribers(), MetricCounter( 0 )
{
  RegisterHandler( this, &TrainingTrigger::NewSubscription );
}

~TrainingTrigger( void )
{
  DeregisterHandler( this, &TrainingTrigger::NewSubscription );
}

} // End name space NebulOuS