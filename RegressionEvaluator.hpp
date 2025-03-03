/*==============================================================================
Regression Evaluator

The Regression Evaluator stores the trained regression functions and provides
intrfaces for the AMPL model to evaluate regression functions for the current
set of metric values for a proposed set of variable values. It is an actor 
that receives the trained functions as messages from the Regression Function
actors created by this class for each performance indicator. The actors are 
implemented using the Theron++ Actor ibrary [1].

06:45 - 08:30
10:45 - 

References:
[1] https://github.com/GeirHo/TheronPlusPlus

Author and Copyright: Geir Horn, University of Oslo
Contact: Geir.Horn@mn.uio.no
License: MPL2.0 (https://www.mozilla.org/en-US/MPL/2.0/)
==============================================================================*/

#ifndef NEBULOUS_REGRESSION_EVALUATOR
#define NEBULOUS_REGRESSION_EVALUATOR

// Standard headers

#include <string_view>                          // Constant strings
#include <string>                               // Normal strings
#include <unordered_map>                        // To store performance indices
#include <vector>                               // To store regressor values
#include <functional>                           // The regression function
#include <memory>                               // Managed pointers

// Theron++ headers

#include "Actor.hpp"                            // Actor base class
#include "Utility/StandardFallbackHandler.hpp"  // Exception unhanded messages

namespace NebulOuS
{
/*==============================================================================

 Regression Evaluator Actor

==============================================================================*/

class RegressionEvaluator
: virtual public Theron::Actor,
  virtual public Theron::StandardFallbackHandler
{
public:

  // --------------------------------------------------------------------------
  // Regression function
  // --------------------------------------------------------------------------
  //
  // The regression function is a standard functon that takes a vector of 
  // double values as argument and returns a double. The arguments are in the 
  // order of definition when the function is defined (see below).

  using RegressionFunction = std::function< double( std::vector<double> & ) >;

  // --------------------------------------------------------------------------
  // Regression algorithms
  // --------------------------------------------------------------------------
  //
  // The algorithms must correspond to actors implementing trainers for the 
  // various regression functions.
  
  enum class Algorithms
  {
    LinearRegression,
    SupportVectorRegression,
    ProjectionPursuitRegression
  };

private:

  // --------------------------------------------------------------------------
  // Performance indicators
  // --------------------------------------------------------------------------
  //
  // Each performance indicator has an actor to train the regression function
  // and the regression function itself.

  class PerformanceIndicator
  {
    private:

      std::shared_ptr< RegressionFunction > ValueFunction;
      std::shared_ptr< Theron::Actor >      FunctionTrainer;

    public:

      inline void UpdateFunction( std::shared_ptr< RegressionFunction > NewFunction )
      { ValueFunction = NewFunction; }

      inline double Value( std::vector< double > & RegressorValues )
      { return ValueFunction->operator()( RegressorValues ); }
      
      PerformanceIndicator( void ) = delete;
      ~PerformanceIndicator() = default;

      PerformanceIndicator( const std::string InidcatorName, 
                            Algorithms RegressionType );
  };

  // The performance indicators are stored in an unordered map where the name of the
  // indicator is the key.

  std::unordered_map< std::string, PerformanceIndicator > PerformanceIndicators;

  // --------------------------------------------------------------------------
  // Regressor names
  // --------------------------------------------------------------------------
  //
  // The regressor names are stored so that they can defined once and then be 
  // passed on to the regression function trainers. The variable names should 
  // be given first, and then the names of the used metrics.

  std::vector< std::string > RegressorNames;

public:

  // --------------------------------------------------------------------------
  // Interface functions
  // --------------------------------------------------------------------------
  //
  // These functions are called from the AMPL solver library when the model 
  // has been established. The first function copies the names of the variables
  // and metrics involved in the problem to the name store. The definitions 
  // can only be posted once and an exception will be thrown if the regression 
  // names are not empty when this function is called. 

  void SetRegressorNames( std::vector< std::string > & TheNames );

  // There is a function to define a new performance indicator. This will create
  // the trainer for the regression function of the right type. Note that this
  // function requires that the regressor names are defined first and will 
  // through an exception if the regrssor names are not given.

  void NewPerformanceIndicator( const std::string IndicatorName, 
                                Algorithms RegressionType  );

  // When the regression function has been defined for a performance indicator
  // the value can be found by calling the value function with a given set of 
  // regressor values. It will throw an exception if the indicator name cannot 
  // be found.

  inline double Value( const std::string IndicatorName, 
                       std::vector< double > & RegressorValues )
  { return PerformanceIndicators.at( IndicatorName ).Value( RegressorValues ); } 

  // --------------------------------------------------------------------------
  // Message handlers
  // --------------------------------------------------------------------------
  //
  // The regression trainers are triggered by the Metric Updater when a 
  // sufficient number of metric values have been received. The trainers will
  // then send the trained regression functions to the Regression Evaluator
  // which will store them in the performance indicator map.

  class NewRegressionFunction
  {
    public:

      std::string IndicatorName;
      std::shared_ptr< RegressionFunction > TheFunction;

      NewRegressionFunction( const std::string Name, 
                             std::shared_ptr< RegressionFunction > Function )
      : IndicatorName( Name ), TheFunction( Function ) {};

      ~NewRegressionFunction() = default;
  };

  // The message handler for the new regression function will store the function
  // in the performance indicator map.

  void StoreRegressionFunction( const NewRegressionFunction & TheFunction, 
                                const Address RegressionTrainer ) 
  { PerformanceIndicators.at( TheFunction.IndicatorName ).UpdateFunction( TheFunction.TheFunction ); }
  
  // --------------------------------------------------------------------------
  // Constructor and destructor
  // --------------------------------------------------------------------------
  //
  // The constructor will register the message handler for the new regression
  // function and the destructor will unregister the handler.

  RegressionEvaluator( const std::string EvaluatorName )
  : Actor( EvaluatorName ),
    StandardFallbackHandler( Actor::GetAddress().AsString() ),
    PerformanceIndicators(), RegressorNames()
  { RegisterHandler( this, &RegressionEvaluator::StoreRegressionFunction ); }

  ~RegressionEvaluator( void )
  { DeregisterHandler( this, &RegressionEvaluator::StoreRegressionFunction ); }
};

}
#endif