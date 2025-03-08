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

#ifndef NEBULOUS_TRAINING_TRIGGER
#define NEBULOUS_TRAINING_TRIGGER

// Standard headers

#include <map>                                  // To store trigger subscribers

// Theron++ headers

#include "Actor.hpp"                            // Actor base class
#include "Utility/StandardFallbackHandler.hpp"  // Exception unhanded messages

// NebulOuS specific headers

#include "MetricUpdater.hpp"                    // The Metric Updater actor

// Specific headers

#include <boost/multiprecision/gmp.hpp>         // Arbitrary precision arithmetic

namespace NebulOuS
{
/*==============================================================================

Training Trigger

==============================================================================*/

class TrainingTrigger
: virtual public Theron::Actor,
  virtual public Theron::StandardFallbackHandler
  public NebulOuS::MetricUpdater
{
  // --------------------------------------------------------------------------
  // Trigger subsciption 
  // --------------------------------------------------------------------------
  //
  // A remote trainer class sends an integer indicating how many metric updates
  // are needed before the training is triggered. The remote trainer is a
  // separate actor that is created the performance indicator evaluator 
  // actor when a new regression function is defined. The subscribing trainers
  // are stored in a map with the actor name as the key.

private:

  std::map< Address, unsigned long > TriggerSubscribers;

  // The handler simply stores the subscription in the map. If a new subscription
  // is made for an existing trainer, the old subscription is replaced. This 
  // may happen if the trainer algorithm adapts to more data available. To cancel 
  // the subscription, a trainer sends a message with a zero count.

  void NewSubscription( const unsigned long & TheTriggerCount, 
                        const Address TheTrainer );

  // --------------------------------------------------------------------------
  // Metric update counter
  // --------------------------------------------------------------------------
  //
  // There is a counter for the number of received metric values and since this
  // is never re-initialised, it must support arbitrary precision arithmetic.

  boost::multiprecision::mpz_int MetricCounter;

  // When a metric results in a trigger to be sent, a retrain regression message
  // is sent back to the trainer containing the current value of the counter.

public:

  class RetrainRegression
  {
    public:

      const boost::multiprecision::mpz_int MetricCount;

      RetrainRegression( const boost::multiprecision::mpz_int & Count )
      : MetricCount( Count ) {};
  };

  // The counter is updated and possible triggers sent when a new metric 
  // arrives and this is detected by overloading the the metric update handler.

protected:

  virtual void UpdateMetricValue( const MetricValueUpdate & TheMetricValue, 
                                  const Address TheMetricTopic ) override;

  // --------------------------------------------------------------------------
  // Constructor
  // --------------------------------------------------------------------------
  //
  // The constructor will set the counter to zero and register the handler for 
  // the new subscriptions.

public:

  TrainingTrigger( const std::string UpdaterName, const Address ManagerOfSolvers );
  TrainingTrigger(void) = delete;

  // --------------------------------------------------------------------------
  // Destructor
  // --------------------------------------------------------------------------
  //
  // The destructor will unregister the handler for the new subscriptions.

  virtual ~TrainingTrigger( void );
};

}      // End name space NebulOuS
#endif // NEBULOUS_TRAINING_TRIGGER