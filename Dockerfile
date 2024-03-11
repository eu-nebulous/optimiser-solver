# syntax=docker/dockerfile:1.3-labs
FROM fedora:39 AS builder

# To build:
# docker build -t nebulous/solver .

# To run, resulting in a terminal for further experiments:
# docker run -it nebulous/solver

WORKDIR /solver
COPY ./ /solver

RUN <<EOF
    # Development framework
    dnf --assumeyes install gcc-c++ make git-core boost boost-devel ccache qpid-proton-cpp qpid-proton-cpp-devel json-c json-devel json-glib jsoncpp jsoncpp-devel coin-or-Couenne wget
    # Dependencies
    git clone https://github.com/jarro2783/cxxopts.git CxxOpts
    git clone https://github.com/GeirHo/TheronPlusPlus.git Theron++
    mkdir Theron++/Bin

    # Install AMPL library
    wget --no-verbose https://portal.ampl.com/external/?url=https://portal.ampl.com/dl/amplce/ampl.linux64.tgz -O ampl.linux64.tgz
    tar --file=ampl.linux64.tgz --extract
    mv ampl.linux-intel64 AMPL
    rm ampl.linux64.tgz
EOF

# Make AMPL shared libraries findable
ENV LD_LIBRARY_PATH="$LD_LIBRARY_PATH:/solver/AMPL:/solver/AMPL/amplapi/lib"

# Build solver
RUN make SolverComponent -e THERON=./Theron++ AMPL_INCLUDE=./AMPL/amplapi/include AMPL_LIB=./AMPL/amplapi/lib  CxxOpts_DIR=./CxxOpts/include

# ============================================================

FROM fedora:39
WORKDIR /solver
RUN dnf --assumeyes install boost qpid-proton-cpp json-c json-glib jsoncpp coin-or-Couenne
COPY --from=builder /solver /solver
ENV LD_LIBRARY_PATH="$LD_LIBRARY_PATH:/solver/AMPL:/solver/AMPL/amplapi/lib"

# 
# We set constant `--ModelDir` and `--AMPLDir`; the other arguments can be
# given on the command line, like so (note that `--Endpoint` is mandatory):
# 
# docker run nebulous/solver --Broker=https://somewhere.else/ --Endpoint=my-app-id
# 
# For a shell, to diagnose problems etc.:
# docker run --rm -it --entrypoint /bin/bash nebulous/solver
ENTRYPOINT ["/solver/SolverComponent", "--ModelDir=/tmp", "--AMPLDir=/solver/AMPL"]
