###############################################################################
#
# Solver component
#
# The NebulOuS solver consists of several interacting actors using the AMQ
# interface of the Theron++ framework. 
#
# Author and Copyright: Geir Horn, University of Oslo
# Contact: Geir.Horn@mn.uio.no
# License: MPL2.0 (https://www.mozilla.org/en-US/MPL/2.0/)
###############################################################################

#
# Defining compiler and commands
#

CC = g++
#CC = clang++
RM = rm -f

# Location of the Theron++ framework relative to this make file and the code

THERON = /home/GHo/Documents/Code/Theron++

# Optimisation -O3 is the highest level of optimisation and should be used 
# with production code. -Og is the code optimising and offering debugging 
# transparency and should be use while the code is under development

OPTIMISATION_FLAG = -Og

# It is useful to let the compiler generate the dependencies for the various 
# files, and the following will produce .d files that can be included at the 
# end. The -MMD flag is equivalent with -MD, but the latter will include system 
# headers in the output (which we do not need here). The -MP includes an 
# empty rule to create the dependencies so that make would not create any errors 
# if the file name changes.

DEPENDENCY_FLAGS = -MMD -MP

# Options 

GENERAL_OPTIONS = -Wall -std=c++23 -ggdb -D_DEBUG
INCLUDE_DIRECTORIES = -I. -I/usr/include -I$(THERON)

CXXFLAGS = $(GENERAL_OPTIONS) $(INCLUDE_DIRECTORIES) $(DEPENDENCY_FLAGS) \
		   $(OPTIMISATION_FLAG)

# Putting it together as the actual options given to the compiler and the 
# linker. Note that pthread is needed on Linux systems since it seems to 
# be the underlying implementation of std::thread. Note that it is 
# necessary to use the "gold" linker as the standard linker requires 
# the object files in the right order, which is hard to ensure with 
# an archive, and the "gold" linker manages this just fine, but it 
# requires the full static path to the custom Theron library.

CFLAGS = $(DEPENDENCY_FLAGS) $(OPTIMISATION_FLAG) $(GENERAL_OPTIONS)
LDFLAGS = -fuse-ld=gold -ggdb -D_DEBUG -pthread -l$(THERON)/Theron++.a \
		  -lqpid-proton-cpp

#------------------------------------------------------------------------------
# Theron library
#------------------------------------------------------------------------------
#
# The Theron++ library must be built first and the following two targets 
# ensures that Make will check if the libray is up-to-date or build it if 
# it is not.

.PHONY: $(THERON)/Theron++.a 

 $(THERON)/Theron++.a:
	 make -C $(THERON) Library

#------------------------------------------------------------------------------
# Solver actors
#------------------------------------------------------------------------------
#
# The compiled object files and their dependencies will be stored in a separate
# directory

OBJECTS_DIR = Bin

# Listing the actors' source files and expected object files

SOLVER_SOURCE = $(wildcard *.cpp)
SOLVER_OBJECTS = $(addprefix $(OBJECTS_DIR)/, $(SOLVER_SOURCE:.cpp=.o)

# Since all source files are in the same directory as the make file and the
# component's objective file, they can be built by a general rule

$(OBJECTS_DIR)/%.o : %.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(INCLUDE_DIRECTORIES)

#------------------------------------------------------------------------------
# Solver component
#------------------------------------------------------------------------------
#
# The solver component uses the CxxOpts class for parsing the command line 
# options since it is header only and lighter than the Options library of 
# boost, which seems to have lost the most recent C++ features. The CxxOpts
# library can be cloned from https://github.com/jarro2783/cxxopts

CxxOpts_DIR = /home/GHo/Documents/Code/CxxOpts/include

# The only real target is to build the solver component whenever some of 
# the object files or the solver actors.

SolverComponent: SolverComponent.cpp $(SOLVER_OBJECTS) $(THERON)/Theron++.a
	$(CXX) SolverComponent.cpp -o SolverComponent $(CXXFLAGS) \
	-I$(CxxOpts_DIR) $(LDFLAGS)

