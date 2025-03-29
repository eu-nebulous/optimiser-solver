/*==============================================================================
Time Series

The metric values are measured at a number of time points, and the time series
are stored in an ecapsulted map. This template class provides the interface to
the stored data. The data type can be specified as an argument to the class 
template, and is by default set to double. The time point type is set to 
std::chrono::time_point< std::chrono::system_clock >. 

Author and Copyright: Geir Horn, University of Oslo
Contact: Geir.Horn@mn.uio.no
License: MPL2.0 (https://www.mozilla.org/en-US/MPL/2.0/)
==============================================================================*/

#ifndef NEBULOUS_TIME_SERIES
#define NEBULOUS_TIME_SERIES

#include <map>                               // For the time series
#include <chrono>                            // For the time point type
#include <concepts>                          // For testing template arguments
#include <ranges>                            // For range based views
#include <algorithm>                         // Standard algorithms
#include <sstream>                           // For nice error messages
#include <stdexcept>                         // For standard exceptions

namespace NebulOuS
{

using DefaultTimePointType = std::chrono::time_point< std::chrono::system_clock >;

/*==============================================================================

 Time Series

==============================================================================*/

template< typename TimePointType = DefaultTimePointType, 
          typename ValueType = double >
          requires std::is_derived_from< TimePointType, std::chrono::time_point >
class TimeSeries
{
  // --------------------------------------------------------------------------
  // The time series data
  // --------------------------------------------------------------------------
  //
  // The data is kept sorted on the time points.

private:

  std::map< TimePointType, ValueType > Data;

public:

// --------------------------------------------------------------------------
  // Type utilitites
  // --------------------------------------------------------------------------
  //
  // Type definitions allowing the template types to be used ouside the class

  using TimePoint = TimePointType;
  using Value = ValueType;

  // There is a function to cast the time point type to a time_t value. This is
  // used to convert the time point to a C style time_t value. 
  
  static inline std::time_t 
  TimePointToTimeT( const TimePoint & TheTimePoint ) const
  {
    return std::chrono::system_clock::to_time_t( 
      std::chrono::clock_time_conversion< 
        std::chrono::system_clock, TimePoint::clock >( TheTimePoint ) );
  }

  // A function is also needed for the reverse conversion, i.e. from time_t to the
  // time point type. The reason is that if the time point type is not the default
  // type, the conversion will require a clock-time conversion to the right 
  // format for the clock type used by the time point type.

  static inline TimePoint
  TimeTToTimePoint( const std::time_t TheTimePoint ) const
  {
    return TimePoint( 
      std::chrono::clock_time_conversion< 
        TimePoint::clock, std::chrono::system_clock >( 
      std::chrono::system_clock::from_time_t( TheTimePoint ) ) );
  }
  
  // --------------------------------------------------------------------------
  // Managing single events
  // --------------------------------------------------------------------------
  //
  // The first interface function is used to add an event to the time series,
  // and it will overwrite any existing event at the same time point. It comes 
  // in two versions, one taking a proper time point value, and one taking a
  // C style time_t value since this requires conversion to the time point.

  inline void AddEvent( const TimePoint & TheTimePoint, const Value & TheValue )
  {  Data[ TheTimePoint ] = TheValue; }

  inline void AddEvent( const std::time_t TheTimePoint, const Value & TheValue )
  {  Data[ TimeTToTimePoint( TheTimePoint ) ]  = TheValue; }
  
  // One can also check if an event is present at a given time point. This is done
  // by checking if the time point is present in the map. If one has a known time
  // point, one can also get the value at that time point. Note however that asking
  // for a time point that is not present in the map will throw an exception. 

  inline bool HasEvent( const TimePoint & TheTimePoint ) const
  { return Data.contains( TheTimePoint ); }

  inline bool HasEvent( const std::time_t TheTimePoint ) const
  { return Data.contains( TimeTToTimePoint( TheTimePoint ) ); }

  inline Value GetEvent( const TimePoint & TheTimePoint ) const
  { return Data.at( TheTimePoint );  }

  inline Value GetEvent( const std::time_t TheTimePoint ) const
  { return Data.at( TimeTToTimePoint( TheTimePoint ) ); }

  // There are interaces to get the first and last time points in the time series.
  // One should first check if the time series is empty.

  inline bool IsEmpty( void ) const
  { return Data.empty(); }

  inline std::map< TimePoint, Value >::size_type GetSize( void ) const
  { return Data.size(); }

  inline TimePoint FirstTimePoint( void ) const
  { 
    if( Data.empty() )
    {
      std::ostringstream ErrorMessage;

      ErrorMessage << "The time series is empty, no first time point available";

      throw std::length_error( ErrorMessage.str() );
    }
    else
      return Data.begin()->first;
  }

  inline TimePoint LastTimePoint( void ) const
  { 
    if( Data.empty() )
    {
      std::ostringstream ErrorMessage;

      ErrorMessage << "The time series is empty, no last time point available";

      throw std::length_error( ErrorMessage.str() );
    }
    else 
      return Data.rbegin()->first; 
  }

  // --------------------------------------------------------------------------
  // Accessing ranges of events
  // --------------------------------------------------------------------------
  //
  // The first interface function is used to get all the time series data 
  // as a constant reference to the internal map. 

  inline const std::map< TimePoint, Value > & GetData( void ) const
  { return Data;}

  // The second interface function is used to get a range of events. The
  // range is defined by two time points, and the function will return a
  // range of time points and values in the time series. The range is
  // exclusive, i.e. the time points at the start and end of the range
  // are only included in the range if they are present in the time series.

  const auto GetRange( const TimePoint & StartTimePoint, 
                       const TimePoint & EndTimePoint ) const
  {
    return std::views::filter( Data,
      [ StartTimePoint, EndTimePoint ]( const auto & TheEvent )
      { return ( TheEvent.first >= StartTimePoint && 
                 TheEvent.first <= EndTimePoint ); } 
      );
  }

  // Again, the range can be defined by two time_t values, and there is an 
  // overloaded function for this. The time_t values are converted to the
  // time point type before the range is defined.

  inline const auto GetRange( const std::time_t StartTimePoint, 
                              const std::time_t EndTimePoint ) const
  {
    return GetRange( TimeTToTimePoint( StartTimePoint ), 
                     TimeTToTimePoint( EndTimePoint   ) );
  }

  // There is an interface function to get the time axis of the time series. 

  const auto GetTimeAxis( void ) const
  { return std::views::keys( Data ); }

  // It is alos possible to get only a range of the time axis. The range is defined
  // by two time points, and the function will return a range of time points in the
  // time series. The range is exclusive, i.e. the time points at the start and end
  // of the range are only included in the range if they are present in the time
  // series.

  inline const auto GetTimeAxis( const TimePoint & StartTimePoint, 
                                 const TimePoint & EndTimePoint ) const
  { return std::views::keys( GetRange( StartTimePoint, EndTimePoint ) ); }

  // Again, the range can be defined by two time_t values, and there is an
  // overloaded function for this. The time_t values are converted to the
  // time point type before the range is defined.
  
  inline const auto GetTimeAxis( const std::time_t StartTimePoint, 
                                 const std::time_t EndTimePoint ) const
  {
    return GetTimeAxis( TimeTToTimePoint( StartTimePoint), 
                        TimeTToTimePoint( EndTimePoint  ) );
  }

  // --------------------------------------------------------------------------
  // Deleting events
  // --------------------------------------------------------------------------
  //
  // The last interface function is used to delete an event from the time series.
  // It comes in two versions, one taking a proper time point value, and one
  // taking a C style time_t value since this requires conversion to the time
  // point. If the time point does not exist in the time series, nothing will be
  // done.

  inline void DeleteEvent( const TimePoint & TheTimePoint )
  { Data.erase( TheTimePoint ); }
  
  inline void DeleteEvent( const std::time_t TheTimePoint )
  { Data.erase( TimeTToTimePoint( TheTimePoint ) ); }

  // A range of events can also be deleted. The range is defined by two time points,
  // and the function will delete all events in the time series that are in the
  // range. The range is exclusive, i.e. the time points at the start and end of
  // the range are only included in the range if they are present in the time
  // series.

  void DeleteRange( const TimePoint & StartTimePoint, 
                    const TimePoint & EndTimePoint )
  {
    auto Range = GetRange( StartTimePoint, EndTimePoint );

    Data.erase( Range.begin(), Range.end() );
  }

  // Again, the range can be defined by two time_t values, and there is an
  // overloaded function for this. The time_t values are converted to the
  // time point type before the range is defined.

  inline void DeleteRange( const std::time_t StartTimePoint, 
                           const std::time_t EndTimePoint )
  {
    DeleteRange( TimeTToTimePoint( StartTimePoint ), 
                 TimeTToTimePoint( EndTimePoint   ) );
  }

  // --------------------------------------------------------------------------
  // Constructor and destructor
  // --------------------------------------------------------------------------
  //
  // The default constructor is used to create an empty time series. 

  TimeSeries( void ) = default;

  // There are also constructors that can copy or move the data from another time
  // series. 

  TimeSeries( const TimeSeries & TheTimeSeries ) = default;
  TimeSeries( TimeSeries && TheTimeSeries ) = default;

  // Destructor
  
  ~TimeSeries( void ) = default;
};

}      // namespace NebulOuS
#endif // NEBULOUS_TIME_SERIES
