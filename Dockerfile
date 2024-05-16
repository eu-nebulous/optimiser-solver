FROM fedora:39 AS builder

# To build:
# docker build -t nebulous/solver .

#
# For a shell, to diagnose problems etc.:
# docker run --rm -it --entrypoint /bin/bash nebulous/solver

RUN mkdir -p /solver
WORKDIR /solver

# Development framework, dependencies
RUN dnf --assumeyes install gcc-c++-13.2.1-7.fc39 make-1:4.4.1-2.fc39 git-core-2.45.0-1.fc39 boost-devel-1.81.0-8.fc39 ccache-4.8.2-2.fc39 qpid-proton-cpp-devel-0.38.0-4.fc39 json-c-0.17-1.fc39 json-devel-3.11.2-3.fc39 json-glib-1.8.0-1.fc39 jsoncpp-1.9.5-5.fc39 jsoncpp-devel-1.9.5-5.fc39 coin-or-Couenne-0.5.8-12.fc39 wget-1.21.3-7.fc39 && \
    dnf clean all && \
    git clone https://github.com/jarro2783/cxxopts.git CxxOpts && \
    git clone https://github.com/GeirHo/TheronPlusPlus.git Theron++ && \
    mkdir Theron++/Bin

# Install AMPL library
RUN wget --progress=dot:giga https://portal.ampl.com/external/?url=https://portal.ampl.com/dl/amplce/ampl.linux64.tgz -O ampl.linux64.tgz && \
    tar --file=ampl.linux64.tgz --extract && \
    mv ampl.linux-intel64 AMPL && \
    rm ampl.linux64.tgz

# Make AMPL shared libraries findable
ENV LD_LIBRARY_PATH="$LD_LIBRARY_PATH:/solver/AMPL:/solver/AMPL/amplapi/lib"

COPY . /solver

# Build solver
RUN make SolverComponent -e THERON=./Theron++ AMPL_INCLUDE=./AMPL/amplapi/include AMPL_LIB=./AMPL/amplapi/lib  CxxOpts_DIR=./CxxOpts/include && \
    make clean

# ============================================================

FROM fedora:39
WORKDIR /solver
RUN dnf --assumeyes install boost-1.81.0-8.fc39 qpid-proton-cpp-0.38.0-4.fc39 json-c-0.17-1.fc39 json-glib-1.8.0-1.fc39 jsoncpp-1.9.5-5.fc39 coin-or-Couenne-0.5.8-12.fc39 && \
    dnf clean all

COPY --from=builder /solver /solver
ENV LD_LIBRARY_PATH="$LD_LIBRARY_PATH:/solver/AMPL:/solver/AMPL/amplapi/lib"

# The `SolverComponent` arguments `--ModelDir` and `--AMPLDir` are
# constant, the other arguments can be given on the command line or
# via environment variables:
#
#   -b amqbroker (variable ACTIVEMQ_HOST, default localhost)
#   -P amqpport (variable ACTIVEMQ_PORT, default 5672)
#   -u user (variable ACTIVEMQ_USER, default admin)
#   -p amqppassword (variable ACTIVEMQ_PASSWORD)
#   -e appid (variable APPLICATION_ID)
#   -l license (variable AMPL_LICENSE)
#
# The docker can be started with explicit parameters, environment
# variables or a mix of both.  Parameters override variables.
#
#     docker run -e APPLICATION_ID="my_app_id" nebulous/solver -b="https://amqp.example.com/" -p=s3kr1t
#
RUN chmod +x /solver/start-solver.sh

ENTRYPOINT ["/solver/start-solver.sh"]
