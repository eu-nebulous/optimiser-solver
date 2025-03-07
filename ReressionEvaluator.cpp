/*==============================================================================
Regression Evaluator

The Regression Evaluator stores the trained regression functions and provides
intrfaces for the AMPL model to evaluate regression functions for the current
set of metric values for a proposed set of variable values. It is an actor 
that receives the trained functions as messages from the Regression Function
actors created by this class for each performance indicator. The actors are 
implemented using the Theron++ Actor ibrary [1].

References:
[1] https://github.com/GeirHo/TheronPlusPlus

Author and Copyright: Geir Horn, University of Oslo
Contact: Geir.Horn@mn.uio.no
License: MPL2.0 (https://www.mozilla.org/en-US/MPL/2.0/)
==============================================================================*/

// Standard headars

#include <sstream>						// Formatted error messages
#include <stdexcept>					// Standard exceptions
#include <source_location>              // Informative error messages

// NebulOuS specific headers

#include "RegressionEvaluator.hpp"

// AMLP specific headers

#include "amplp.hpp"					// AMPL interface

namespace NebulOuS
{
// --------------------------------------------------------------------------
// Utility functions
// --------------------------------------------------------------------------
//
// The first utility function converts a string to an algorithm type. This is
// used when the performance indicators are defined from the AMPL model. Both
// the full name and the abbreviation are accepted.

RegressionEvaluator::Algorithm 
RegressionEvaluator::String2Algorithm ( const std::string AlgorithmName )
{
  static std::map< std::string, Algorithm > 
	RegressionAlgorithms{
	    {"Linear Regression", Algorithm::LinearRegression},
		{"LR", Algorithm::LinearRegression},
      	{"Support Vector Regression", Algorithm::SupportVectorRegression},
		{"SVR", Algorithm::SupportVectorRegression},
      	{"Projection Pursuit Regression", Algorithm::ProjectionPursuitRegression},
		{"PPR", Algorithm::ProjectionPursuitRegression}
    };

    return RegressionAlgorithms.at( AlgorithmName );
  }

// --------------------------------------------------------------------------
// Interface functions
// --------------------------------------------------------------------------
//
// The function to set the regressor nammes can only be called once at the 
// beginning to ensure that the definitions of the involved variables and 
// metric names stays the same for all regression functions for this 
// particular problem 

void RegressionEvaluator::SetRegressorNames( 
		 const std::vector< std::string > & TheNames )
{
	if ( RegressorNames.empty() )
		RegressorNames.assign( TheNames.begin(), TheNames.end() );
	else
	{
		std::source_location Location = std::source_location::current();
    std::ostringstream ErrorMessage;

    ErrorMessage << "[" << Location.file_name() << " at line " << Location.line() 
                << " in function " << Location.function_name() <<"] " 
                << "Regressor names can only be given once at start up!";

    throw std::invalid_argument( ErrorMessage.str() );
	}
}

// The performance indicators are declared one-by-one, and it is possible to 
// re-declare a performance indicator changing its regression type. However it 
// is not possible to declare the performance indicator before the regressor 
// names have been given as this will lead to an exception.

void RegressionEvaluator::NewPerformanceIndicator( 
		 const std::string IndicatorName, Algorithm RegressionType )
{
	// First assert that new performance indicators can be defined

	if ( RegressorNames.empty() )
	{
		std::source_location Location = std::source_location::current();
    std::ostringstream ErrorMessage;

    ErrorMessage << "[" << Location.file_name() << " at line " << Location.line() 
                << " in function " << Location.function_name() <<"] " 
                << "The performance indicator " << IndicatorName 
								<< " is declared before the names of the regression variables"
								<< " have been declared";

    throw std::invalid_argument( ErrorMessage.str() );
	}

	// If there is already a performance indicator with the same name it will be
	// forgotten and its training actor closed.

	if ( PerformanceIndicators.contains( IndicatorName ) )
		PerformanceIndicators.erase( IndicatorName );

	// Then one can construct the performance indicator with the given name and 
	// type. The actual work is done by the performance indicator constructor

	PerformanceIndicators.emplace( IndicatorName, RegressionType );
}

// --------------------------------------------------------------------------
// Message handlers
// --------------------------------------------------------------------------
//
// When an updated regression function is received, it is stored in the
// performance indicator map. 

void RegressionEvaluator::StoreRegressionFunction( 
			const NewRegressionFunction & TheFunction, 	const Address RegressionTrainer ) 
{ PerformanceIndicators.at( TheFunction.IndicatorName )
											 .UpdateFunction( TheFunction.TheFunction ); }

// The regressor names are set by the AMPL solver actor, and this is done by
// sending a message with the names.

void RegressionEvaluator::StoreRegressorNames( 
			const std::vector< std::string > & TheNames, const Address TheAMPLSolver )
{ SetRegressorNames( TheNames ); }

// The performance indicators are set by the AMPL solver actor, and this is done
// by sending a message with the names and types of the performance indicators.

void RegressionEvaluator::StorePerformanceIndicators( 
			const std::unordered_map< std::string, Algorithm > & TheIndicators, 
			const Address TheAMPLSolver )
{
	for ( const auto & [ IndicatorName, RegressionType ] : TheIndicators )
		NewPerformanceIndicator( IndicatorName, RegressionType );
}

} // End name space NebulOuS

/*==============================================================================

 AMPL Interface

==============================================================================*/
//
// The AMPL interface is a set of functions that are called from the AMPL
// model to set up the regression functions and to evaluate the regression
// functions for a given set of regressor values. The functions encapsulate
// the methods on the RegressionEvaluator class, and it is therefore necessary
// to have an instance of the RegressionEvaluator class to call these functions.

NebulOuS::RegressionEvaluator TheRegressionEvaluator( "RegressionEvaluator" );

// The various functions called from AMPL are violating the Actor model, because
// they will directly call the interface functions of the RegressionEvaluator
// class. This is possible since the operations are "read-only" and will not
// change the state of the actor. 

extern "C"
{

// The first function is used to compute the value of a performance indicator
// for a given set of regressor values. 

double Value( amplp::arglist * args )
{
	// The first argument is the name of the performance indicator

	std::string IndicatorName;

	// The indicator name must be a valid name

	if ( args->sa == nullptr )
	{
		std::source_location Location = std::source_location::current();
		std::ostringstream ErrorMessage;

		ErrorMessage << "[" << Location.file_name() << " at line " 
					 << Location.line() << " in function " 
					 << Location.function_name() <<"] " 
					 << "The performance indicator name is missing";

		throw std::invalid_argument( ErrorMessage.str() );
	}
	else
	{
		IndicatorName = *(args->sa);

		if ( !TheRegressionEvaluator.HasPerformanceIndicator( IndicatorName ) )
		{
			std::source_location Location = std::source_location::current();
			std::ostringstream ErrorMessage;

			ErrorMessage << "[" << Location.file_name() << " at line " 
						 << Location.line() << " in function " 
						 << Location.function_name() <<"] " 
						 << "The performance indicator " << IndicatorName 
						 << " is not defined";

			throw std::invalid_argument( ErrorMessage.str() );
		}
	}

	// The argument is valid if the right number of regressor values.

	if ( args->nr != TheRegressionEvaluator.GetNumberOfRegressors() )
	{
		std::source_location Location = std::source_location::current();
		std::ostringstream ErrorMessage;

		ErrorMessage << "[" << Location.file_name() << " at line " 
					 << Location.line() << " in function " 
					 << Location.function_name() <<"] " 
					 << "The number of regressor values does not match "
					 << "the number of registered regressors";

		throw std::invalid_argument( ErrorMessage.str() );
	}

	// The regressor values are copied to a vector to be passed to the value
	// function of the performance indicator.

	std::vector< double > RegressorValues( args->ra, args->ra + args->nr );

	// The value is found by calling the value function of the performance
	// indicator with the given regressor values.

	return TheRegressionEvaluator.Value( IndicatorName, RegressorValues );
}

// The performance indices can be defined one by one from the AMPL model

void NewPerformanceIndicator( amplp::arglist * args )
{
	// The function must be called with two string (symbolic) arguments

	if ( args->nsin != 2 )
	{
		std::source_location Location = std::source_location::current();
		std::ostringstream ErrorMessage;

		ErrorMessage << "[" << Location.file_name() << " at line " 
					 << Location.line() << " in function " 
					 << Location.function_name() <<"] " 
					 << "The function New Performance Indicator must be "
					 << "called with two arguments";

		throw std::invalid_argument( ErrorMessage.str() );
	}

	// The first argument is the name of the performance indicator 

	std::string IndicatorName( *(args->sa) );

	// The second argument is the type of regression function

	RegressionEvaluator::Algorithm 
	RegressionType( RegressionEvaluator::String2Algorithm ( *(args->sa+1) ) );

	// The performance indicator is defined by calling the NewPerformanceIndicator
	// function of the RegressionEvaluator class.

	TheRegressionEvaluator.NewPerformanceIndicator( IndicatorName, RegressionType );
}

// The regressor names can also be defined from the AMPL model directly

void SetRegressorNames( amplp::arglist * args )
{
	// The function must be called with at least one string (symbolic) argument

	if ( args->nsin < 1 )
	{
		std::source_location Location = std::source_location::current();
		std::ostringstream ErrorMessage;

		ErrorMessage << "[" << Location.file_name() << " at line " 
					 << Location.line() << " in function " 
					 << Location.function_name() <<"] " 
					 << "The function Set Regressor Names must be called with "
					 << "at least one argument";

		throw std::invalid_argument( ErrorMessage.str() );
	}

	// The regressor names are copied to a vector to be passed to the 
	// SetRegressorNames function of the RegressionEvaluator class.

	std::vector< std::string > RegressorNames( args->sa, args->sa + args->nsin );

	TheRegressionEvaluator.SetRegressorNames( RegressorNames );
}

// Finally, the global functions are registered with the AMPL solver interface 
// so that they can be called from the AMPL model.

void funcadd( amplp::AmplExports *ae )
{
	// Arg 3, called type, must satisfy 0 <= type <= 6:
 	// type&1 == 0:	0,2,4,6	==> force all arguments to be numeric.
 	// type&1 == 1:	1,3,5	==> pass both symbolic and numeric arguments.
 	// type&6 == 0:	0,1	==> the function is real valued.
 	// type&6 == 2:	2,3	==> the function is char * valued; static storage
	//			    suffices: AMPL copies the return value.
 	// type&6 == 4:	4,5	==> the function is random (real valued).
 	// type&6 == 6: 6	==> random, real valued, pass nargs real args,
 	//				0 <= nargs <= 2.
 	//
 	//  Arg 4, called nargs, is interpretted as follows:
 	//	>=  0 ==> the function has exactly nargs arguments
 	//	<= -1 ==> the function has >= -(nargs+1) arguments.
 	//
 	// Arg 5, called funcinfo, is copied without change to the arglist
 	//	structure passed to the function; funcinfo is for the
 	//	function to use or ignore as it sees fit.

	ae->Addfunc("Value", Value, amplp::FUNCADD_double_VALUED | amplp::FUNCADD_STRING_ARGS,
				-1, 0, ae);
	ae->Addfunc("NewPerformanceIndicator", (amplp::rfunc) NewPerformanceIndicator,
				amplp::FUNCADD_STRING_ARGS, 2, 0, ae);
	ae->Addfunc("SetRegressorNames", (amplp::rfunc) SetRegressorNames,
				amplp::FUNCADD_STRING_ARGS, -1, 0, ae);
}

} // End extern "C"
