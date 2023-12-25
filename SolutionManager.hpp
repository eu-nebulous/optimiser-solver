/*==============================================================================
Solution Manager

This class handles the Execution Context mssage containing a time stamp and a 
set of variable value assignments.It manages a time sorted queue and dispatches
the first application execution context to the solver when the solver is ready.
The solution returned for a given execution context will be published together 
with the execution context and the maximal utility value found by the solver.

The solver actor class is given as a template argument to the solution manager,
and at least one solver actor is instantiated at start up. This to allow 
multiple solvers to run in parallel should this be necessary to serve properly
the queue of waiting application execution contexts. If there are multiple 
objects defined, they have to be optimised individualy, and for this purpose 
it would also be useful to have multiple solvers running in parallel working 
on the same problem, but for different objective functions. This will reduce 
the time to find the Pareto front [1] for the multi-objective optimisation 
problem.

The functionality of receiving and maintaining the work queue separately from 
the solver is done to avoid blocking the reception of new execution contexts 
while the solver searches for a solution in separate threads. This is done 
for other entities to use the solver to find the optised configuration, i.e.
feasible value assignments to all propblem variables, maximising the givne 
utiliy for a particular set of independent metric variables, i.e. the 
application execution context. The idea is that other components may use 
the solver in this way to produce training sets for machine learning methods
that aims to estimate the application's performance indicators or even the 
change in utility as a function of the varying the metric values of the 
application execution context. 

References:
[1] https://en.wikipedia.org/wiki/Pareto_front

Author and Copyright: Geir Horn, University of Oslo
Contact: Geir.Horn@mn.uio.no
License: MPL2.0 (https://www.mozilla.org/en-US/MPL/2.0/)
==============================================================================*/

#ifndef NEBULOUS_SOLUTION_MANAGER
#define NEBULOUS_SOLUTION_MANAGER

namespace NebulOuS
{
    
} // namespace NebulOuS
#endif // NEBULOUS_SOLUTION_MANAGER