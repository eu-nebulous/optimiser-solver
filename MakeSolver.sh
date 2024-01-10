#!/usr/bin/bash
# ==============================================================================
#
# Solver Component
#
# This script will build the Solver Component on an 'empty' machine with 
# minimal installation of the latest Fedora version. The intent is to load 
# all dependencies in a constructive way.
#
# Author and Copyright: Geir Horn, University of Oslo
# Contact: Geir.Horn@mn.uio.no
# License: MPL2.0 (https://www.mozilla.org/en-US/MPL/2.0/)
# ==============================================================================

# Installing the development framework for the distribution. Must be run 
# as root on the machine - put 'sudo' in front if the build user is not root.

dnf --assumeyes install gcc-c++ make git boost boost-devel ccache \
qpid-proton-cpp* jsoncpp-devel coin-or-Couenne wget

# Cloning the open source dependencies

git clone https://github.com/jarro2783/cxxopts.git CxxOpts
git clone https://github.com/GeirHo/TheronPlusPlus.git Theron++
mkdir Theron++/Bin

# Clone the solver component 

git clone https://opendev.org/nebulous/optimiser-solver.git Solver

# Installing the AMPL library

wget https://portal.ampl.com/external/?url=\
https://portal.ampl.com/dl/amplce/ampl.linux64.tgz -O ampl.linux64.tgz
tar --file=ampl.linux64.tgz --extract
mv ampl.linux-intel64 AMPL
rm ampl.linux64.tgz
#cp ampl.lic AMPL

# Building the solver component

make -C Solver SolverComponent -e THERON=../Theron++ \
AMPL_INCLUDE=../AMPL/amplapi/include AMPL_LIB=../AMPL/amplapi/lib \
CxxOpts_DIR=../CxxOpts/include
