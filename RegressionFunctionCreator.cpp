/*==============================================================================
Regression Function Creator

This is the base class for the regression function trainers. The trainers are
actors that are created by the Regression Evaluator to train the regression
functions for the various performance indicators. The trainers are implemented
as actors to allow for parallel training of the regression functions. However,
the traners have shared functionality implemented in this base class.

The actors are implemented using the Theron++ Actor ibrary [1].

References:
[1] https://github.com/GeirHo/TheronPlusPlus

Author and Copyright: Geir Horn, University of Oslo
Contact: Geir.Horn@mn.uio.no
License: MPL2.0 (https://www.mozilla.org/en-US/MPL/2.0/)
==============================================================================*/

// NebulOuS headers

#include "RegressionFunctionCreator.hpp"

namespace NebulOuS
{
// --------------------------------------------------------------------------
// GetData
// --------------------------------------------------------------------------
//
// The design matrix and the response vector are returned by reference. The
// design matrix is a matrix where each row is a set of regressor values and
// the response vector is the corresponding response value. It is obtained from 
// the InfluxDB database persisting all metric values.

void RegressionFunctionCreator::GetData( DenseMatrix & TheDesignMatrix, 
                                         DenseVector & TheResponseVector ) const
{
    // The default implementation is to do nothing
}

// --------------------------------------------------------------------------
// SetRetrainTrigger
// --------------------------------------------------------------------------
//
// The trigger for retraining the regression function is stored in the 
// Training Trigger actor, and setting the trigger value locally just means 
// sending a messate to the Training Trigger actor. Derived classes should 
// set the trigger value in the constructor, and change it later as needed 
// to avoide to frequent retraining. It should be reset in the destructor by 
// sending a message with a zero count.

void RegressionFunctionCreator::SetRetrainTrigger( 
    const unsigned long TheTriggerCount ) const
{
    Send( TheTriggerCount, TheTrigger );
}

// --------------------------------------------------------------------------
// RetrainRegression
// --------------------------------------------------------------------------
//
// The retrain regression handler will call the training function with the
// design matrix and send the trained regression function back to the
// Regression Evaluator.

void RegressionFunctionCreator::RetrainRegression( 
    const TrainingTrigger::RetrainRegression & TheTrigger, 
    const Address TheTrainingTrigger )
{
    DenseMatrix TheDesignMatrix;
    DenseVector TheResponseVector;

    GetData( TheDesignMatrix, TheResponseVector );
    
    Send( RegressionEvaluator::NewRegressionFunction( 
          TrainRegressionFunction( TheDesignMatrix, TheResponseVector ), 
          GetAddress().AsString() ), 
          TheEvaluator );
}

// --------------------------------------------------------------------------
// Constructor
// --------------------------------------------------------------------------
//
// The constructor takes the name of the regession function creator actor and
// the regressor names to store.

RegressionFunctionCreator::RegressionFunctionCreator( 
    const std::string & PerformanceIndicatorName, 
    const Address TheTriggerActor, 
    const Address TheEvaluatorActor,
    const std::vector< std::string > & Names )
: Actor( CreatorName ),
    TheTrigger( TheTriggerActor ),
    TheEvaluator( TheEvaluatorActor ),
    RegressorNames( Names )
{
    // Register the handler for the retrain regression message

    RegisterHandler( this, &RegressionFunctionCreator::RetrainRegression );

    // The constructor of the derived classes will set the trigger for retraining
    // the regression function

    SetRetrainTrigger( 0 );
}

}