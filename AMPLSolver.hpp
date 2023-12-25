/*==============================================================================
AMPL Solver

This instantiates the purely virtual Solver providing an interface to the 
mathematical domain specific language "A Mathematical Programming Language" 
(AMPL) [1] allowing to use the wide range of solvers, free or commercial, that 
support AMPL descriptions of the optimisation problem.

The AMPL problem description and associated data files are received by handlers
and stored locally as proper files to ensure that the problem is always solved 
for the lates problem descriptions received. When the actor receives an 
Application Execution Context message, the AMPL description and data files will
be loaded and the appropriate solver called from the AMPL library. When the 
solution is returned from AMPL, the solution message is returned to the sender 
of the application execution context message, typically the Solution Manager 
actor for publishing the solution to external subscribers.

References:
[1] https://ampl.com/

Author and Copyright: Geir Horn, University of Oslo
Contact: Geir.Horn@mn.uio.no
License: MPL2.0 (https://www.mozilla.org/en-US/MPL/2.0/)
==============================================================================*/

#ifndef NEBULOUS_AMPL_SOLVER
#define NEBULOUS_AMPL_SOLVER

namespace NebulOuS
{

} // namespace NebulOuS
#endif // NEBULOUS_AMPL_SOLVER