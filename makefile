###############################################################################
#
# Solver component
#
# The NebulOuS solver consists of several interacting actors using the AMQ
# interface of the Theron++ framework.
#
# The following packages should be available on Fedora prior to compiling
# the file
#
#	ccache			# for effcient C++ compilations
#	qpid-proton-cpp*	# Qpid Proton Active Message Queue protocol API
#	json-devel		# Niels Lohmann's JSON library
#	coin-or-Couenne		# The solver to be used by AMPL
#
# In addition the problem is formulated using A Mathematical Programming
# Language (AMPL) and so it should be installed from
# https://portal.ampl.com/user/ampl/request/amplce/trial/new
#
# There are source code dependencies that should be cloned to local disk
#
#	Theron++			# https://github.com/GeirHo/TheronPlusPlus.git
#	cxxopts				# https://github.com/jarro2783/cxxopts.git
#
# Author and Copyright: Geir Horn, University of Oslo
# Contact: Geir.Horn@mn.uio.no
# License: MPL2.0 (https://www.mozilla.org/en-US/MPL/2.0/)
###############################################################################

#
# Defining compiler and commands
#

CC = ccache g++
#CC = clang++
RM = rm -f

#------------------------------------------------------------------------------
# Paths
#------------------------------------------------------------------------------
#
# The default values of the paths are given here to be overridden by build
# definitions on the command line for creating the component container.
#
# Location of the Theron++ framework relative to this make file and the code

THERON ?= /home/GHo/Documents/Code/Theron++

# Location of the AMPL API directory

AMPL_INCLUDE ?= /opt/AMPL/amplapi/include

# Location of the library directory

AMPL_LIB ?= /opt/AMPL/amplapi/lib

# The solver component uses the CxxOpts class for parsing the command line
# options since it is header only and lighter than the Options library of
# boost, which seems to have lost the most recent C++ features. The CxxOpts
# library can be cloned from https://github.com/jarro2783/cxxopts

CxxOpts_DIR ?= /home/GHo/Documents/Code/CxxOpts/include

#------------------------------------------------------------------------------
# Options for the compiler and linker
#------------------------------------------------------------------------------
#
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

GENERAL_OPTIONS = -Wall -std=c++26 -ggdb -D_DEBUG
INCLUDE_DIRECTORIES = -I. -I/usr/include -I$(THERON) -I$(AMPL_INCLUDE) \
					  -I$(CxxOpts_DIR)

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
LDFLAGS = -fuse-ld=gold -ggdb -D_DEBUG -pthread $(THERON)/Theron++.a \
		  -lqpid-proton-cpp $(AMPL_LIB)/libampl.so

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
SOLVER_OBJECTS = $(addprefix $(OBJECTS_DIR)/, $(SOLVER_SOURCE:.cpp=.o) )

# Since all source files are in the same directory as the make file and the
# component's objective file, they can be built by a general rule

$(OBJECTS_DIR)/%.o : %.cpp
	$(CC) $(CXXFLAGS) -c $< -o $@ $(INCLUDE_DIRECTORIES)

#------------------------------------------------------------------------------
# Solver component
#------------------------------------------------------------------------------
#

# The only real target is to build the solver component whenever some of
# the object files or the solver actors.

SolverComponent: $(SOLVER_OBJECTS) $(THERON)/Theron++.a
	$(CC) -o SolverComponent $(CXXFLAGS) $(SOLVER_OBJECTS) $(LDFLAGS)

# There is also a standard target to clean the automatically generated build
# files

clean:
	$(RM) $(OBJECTS_DIR)/*.o $(OBJECTS_DIR)/*.d

#------------------------------------------------------------------------------
# Dependencies
#------------------------------------------------------------------------------
#

-include $(SOLVER_OBJECTS:.o=.d)
