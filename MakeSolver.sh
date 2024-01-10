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

# Installing the development framework for the distribution

dnf --assumeyes group 'Development Tools' 
dnf --assumeyes install ccache qpid-proton-cpp* json-devel coin-or-Couenne

# Cloning the open source dependencies

mkdir Externals
cd Externals
git clone https://github.com/jarro2783/cxxopts.git CxxOpts
git clone https://github.com/GeirHo/TheronPlusPlus.git Theron++
cd

# Installing the AMPL library

wget https://portal.ampl.com/external/?url=\
https://portal.ampl.com/dl/amplce/ampl.linux64.tgz
tar --file=ampl.linux64.tgz --extract --directory=Externals/AMPL
cp ampl.lic Externals/AMPL

# Building the solver component

make SolverComponent -e THERON=Externals/Theron++ \
AMPL_INCLUDE=Externals/AMPL/amplapi/include AMPL_LIB=Externals/AMPL/amplapi/lib\
CxxOpts_DIR=Externals/CxxOpts/include
