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

#ifndef NEBULOUS_REGRESSION_FUNCTION_CREATOR
#define NEBULOUS_REGRESSION_FUNCTION_CREATOR    

// Standard headers

#include <vector>                               // To store regressor names

// Utility headers

#include <armadillo>                            // Linear algebra library

// Theron++ headers

#include "Actor.hpp"                            // Actor base class
#include "Utility/StandardFallbackHandler.hpp"  // Exception unhanded messages 

// NebulOuS headers

#include "RegressionEvaluator.hpp"              // Regression Evaluator actor
#include "TrainingTrigger.hpp"                  // Training Trigger actor

namespace NebulOuS
{
/*==============================================================================

Regression Function Creator

==============================================================================*/

class RegressionFunctionCreator
: virtual public Theron::Actor,
  virtual public Theron::StandardFallbackHandler
{
  // --------------------------------------------------------------------------
  // Storing regressor names 
  // --------------------------------------------------------------------------
  //
  // To train the regression function, the values of all the regressors must be
  // collected from the time series database. The names of the regressors are
  // stored in a vector to be used to extract the values from the database. The
  // regressor names are passed to the constructor

private:
  
  const std::vector< std::string > RegressorNames;

  // --------------------------------------------------------------------------
  // Database connection function
  // --------------------------------------------------------------------------
  //
  // There is a support function to obtain the regressor values from the time 
  // series database. The values are returned as a 'design matrix' with one 
  // row for each regressor and one column for each time point where there is 
  // a metric observation. Note that all regressors are not measured at each
  // time point, and it is assumed that the value of a regressor is constant 
  // between observations. In other words, the values are copied to subsequent
  // time points until a new value is observed. Since the design matrix can
  // be large, it is returned a filled Armadillo matrix.
  //
  // The data function also fills the response vector with the metric values 
  // of the performance indicator in the same way as the values are kept 
  // constant to the last observation if there are no observations 
  // corresponding to a time point.

  using DenseMatrix = arma::Mat< double >;
  using DenseVector = arma::Col< double >;

  void GetData( DenseMatrix & TheDesignMatrix, 
                DenseVector & TheResponseVector ) const;

  // --------------------------------------------------------------------------
  // Retrain the regression function
  // --------------------------------------------------------------------------
  //
  // The regression function trainer must keep the addresses of the trigger 
  // actor and the regression evaluator actor.

  const Address TheTrigger, TheEvaluator;

  // A function is provided to set the trigger for the regression function 
  // re-training as a function of the numeber of regressor observations 
  // before the training is beneficial. The function is called by the 
  // constructor of the derived classes, and may be called conditionally 
  // during runtime. 

protected:

  void SetRetrainTrigger( const unsigned long TheTriggerCount ) const;

  // The actual training of the regression function is done by the derived
  // classes, by retruning the trained regression function in response to
  // a design matrix.

  virtual std::unique_ptr< const RegressionEvaluator::RegressionFunction > 
  TrainRegressionFunction( const DenseMatrix & TheDesignMatrix, 
                           const DenseVector & TheResponseVector ) = 0;

  // The trigger will arrive as a message from the Training Trigger actor
  // and the handler will call the training function with the design matrix
  // and send the trained regression function back to the Regression Evaluator.

private:

  void RetrainRegression( const TrainingTrigger::RetrainRegression & TheTrigger, 
                          const Address TheTrainingTrigger );

  // --------------------------------------------------------------------------
  // Constructor and destructor
  // --------------------------------------------------------------------------
  //
  // The constructor takes the name of the performance indcator for which 
  // this actor trains the regression function. It is important that this is 
  // the same name as recognised by the Regression Evaluator and the AMPL 
  // model. This name is forwarded both to the InfluxDB to obtain the metric
  // values and to the Regression Evaluator to store the trained regression.
 
public:

  RegressionFunctionCreator( const std::string & PerformanceIndicatorName, 
                             const Address TheTriggerActor, 
                             const Address TheEvaluatorActor,
                             const std::vector< std::string > & Names );

  RegressionFunctionCreator( void ) = delete;
  RegressionFunctionCreator( const RegressionFunctionCreator & Other ) = delete;
  virtual ~RegressionFunctionCreator() = default;
};

} // End name space NebulOuS
#endif  // NEBULOUS_REGRESSION_FUNCTION_CREATOR