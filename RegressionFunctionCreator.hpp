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

  virtual std::shared_ptr< const RegressionEvaluator::RegressionFunction > 
  TrainRegressionFunction( const DenseMatrix & TheDesignMatrix, 
                           const DenseVector & TheResponseVector ) = 0;

  // The trigger will arrive as a message from the Training Trigger actor
  // and the handler will call the training function with the design matrix
  // and send the trained regression function back to the Regression Evaluator.

private:

  void RetrainRegression( const TrainingTrigger::RetrainRegression & TheTrigger, 
                          const Address TheTrainingTrigger );

  // There is a bootstrapping problem since the model may request regression
  // values before there are enough data to train the regression algorithm.
  // In this case, some initial values may be guessed by analysing the 
  // regressor values passed to the regression function. Hence, there is a 
  // function returning an intial regression function that must be defined 
  // by the specific algorithms. It is important that this intial regression
  // function can be returned witout changing any internal state of the 
  // regression function creator actor since it would otherwise violate the 
  // actor model's assumption and could cause rase conditions with the normal
  // operatin of the actor.

public:

  virtual std::shared_ptr< RegressionEvaluator::RegressionFunction >
  BootstrapRegressionFunction( void ) const = 0;

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

/*==============================================================================

Linear Regression

==============================================================================*/
//
// The standard linear regression is implemented as a derived class of the
// regression function creator. The linear regression is a simple linear model
// that is trained by the method of least squares. The regression function is
// a linear function of the regressor values, and the training is done by
// solving the normal equations. The linear regression is a good starting point
// for the regression function, and it is often used as a benchmark for more
// complex models.

class LinearRegression
: virtual public Theron::Actor,
  virtual public Theron::StandardFallbackHandler,
  public RegressionFunctionCreator
{
  // --------------------------------------------------------------------------
  // Training the linear regression function
  // --------------------------------------------------------------------------
  //
  // The only function that needs to be imlemented by the class is the training
  // function. The training function will solve the normal equations to obtain
  // the regression coefficients. The function is called by the base class when
  // the trigger for retraining is received.

protected:

  virtual std::shared_ptr< const RegressionEvaluator::RegressionFunction > 
  TrainRegressionFunction( const DenseMatrix & TheDesignMatrix, 
                           const DenseVector & TheResponseVector ) override;

  // It is also necessary to provide a regression function that can bootstrap
  // the creation process since the regression function creator actor may be 
  // created beore there is sufficient data to train the regression function.

public:

  virtual std::shared_ptr< RegressionEvaluator::RegressionFunction >
  BootstrapRegressionFunction( void ) const override;

  // --------------------------------------------------------------------------
  // Constructor
  // --------------------------------------------------------------------------
  //
  // The constructor takes the name of the performance indicator for which the
  // linear regression is trained. The constructor will set the trigger for
  // retraining to zero, and register the handler for the retrain trigger.

public:

  LinearRegression( const std::string & PerformanceIndicatorName, 
                    const Address TheTriggerActor, 
                    const Address TheEvaluatorActor,
                    const std::vector< std::string > & Names )
  : Actor( PerformanceIndicatorName ),
    StandardFallbackHandler( Actor::GetAddress().AsString() ),
    RegressionFunctionCreator( Actor::GetAddress().AsString(), 
                               TheTriggerActor, 
                               TheEvaluatorActor, 
                               Names )
  {};

  LinearRegression( void ) = delete;
  LinearRegression( const LinearRegression & Other ) = delete;
  virtual ~LinearRegression( void ) = default;

};

/*==============================================================================

Support Vector Regression

==============================================================================*/
//
// The Suppor Vector Regression (SVR) is a non-linear regression model that is
// trained by the method of support vector machines. The SVR is a powerful model
// that can capture complex relationships between the regressor values and the
// performance indicator. The SVR is implemented as a derived class of the
// regression function creator. The SVR is trained by the Sequential Minimal
// Optimization (SMO) algorithm using a kernel method (projection).

class SupportVectorRegression
: virtual public Theron::Actor,
  virtual public Theron::StandardFallbackHandler,
  public RegressionFunctionCreator
{
  // --------------------------------------------------------------------------
  // Training the support vector regression function
  // --------------------------------------------------------------------------
  //
  // The only function that needs to be imlemented by the class is the training
  // function. 

protected:

  virtual std::shared_ptr< const RegressionEvaluator::RegressionFunction > 
  TrainRegressionFunction( const DenseMatrix & TheDesignMatrix, 
                           const DenseVector & TheResponseVector ) override;

  // It is also necessary to provide a regression function that can bootstrap
  // the creation process since the regression function creator actor may be 
  // created beore there is sufficient data to train the regression function.

public:

  virtual std::shared_ptr< RegressionEvaluator::RegressionFunction >
  BootstrapRegressionFunction( void ) const override;

  // --------------------------------------------------------------------------
  // Constructor
  // --------------------------------------------------------------------------

public:

  SupportVectorRegression( const std::string & PerformanceIndicatorName, 
                           const Address TheTriggerActor, 
                           const Address TheEvaluatorActor,
                           const std::vector< std::string > & Names )
  : Actor( PerformanceIndicatorName ),
    StandardFallbackHandler( Actor::GetAddress().AsString() ),
    RegressionFunctionCreator( Actor::GetAddress().AsString(), 
                               TheTriggerActor, 
                               TheEvaluatorActor, 
                               Names )
  {};

  SupportVectorRegression( void ) = delete;
  SupportVectorRegression( const SupportVectorRegression & Other ) = delete;
  virtual ~SupportVectorRegression( void ) = default;
};

/*==============================================================================

Projection Pursuit Regression

==============================================================================*/
//

class ProjectionPursuitRegression
: virtual public Theron::Actor,
  virtual public Theron::StandardFallbackHandler,
  public RegressionFunctionCreator
{
  // --------------------------------------------------------------------------
  // Training the projection pursuit regression function
  // --------------------------------------------------------------------------
  //
  // The only function that needs to be imlemented by the class is the training
  // function.

protected:

  virtual std::shared_ptr< const RegressionEvaluator::RegressionFunction > 
  TrainRegressionFunction( const DenseMatrix & TheDesignMatrix, 
                           const DenseVector & TheResponseVector ) override;

  // It is also necessary to provide a regression function that can bootstrap
  // the creation process since the regression function creator actor may be 
  // created beore there is sufficient data to train the regression function.

public:

  virtual std::shared_ptr< RegressionEvaluator::RegressionFunction >
  BootstrapRegressionFunction( void ) const override;

  // --------------------------------------------------------------------------
  // Constructor
  // --------------------------------------------------------------------------

public:

  ProjectionPursuitRegression( const std::string & PerformanceIndicatorName, 
                               const Address TheTriggerActor, 
                               const Address TheEvaluatorActor,
                               const std::vector< std::string > & Names )
  : Actor( PerformanceIndicatorName ),
    StandardFallbackHandler( Actor::GetAddress().AsString() ),
    RegressionFunctionCreator( Actor::GetAddress().AsString(), 
                               TheTriggerActor, 
                               TheEvaluatorActor, 
                               Names )
  {};

  ProjectionPursuitRegression( void ) = delete;
  ProjectionPursuitRegression( const ProjectionPursuitRegression & Other ) = delete;
  virtual ~ProjectionPursuitRegression( void ) = default;
};

} // End name space NebulOuS
#endif  // NEBULOUS_REGRESSION_FUNCTION_CREATOR