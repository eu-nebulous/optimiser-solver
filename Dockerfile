FROM fedora:42 AS builder

# To build:
# docker build -t nebulous/solver .

#
# For a shell, to diagnose problems etc.:
# docker run --rm -it --entrypoint /bin/bash nebulous/solver

RUN mkdir -p /solver
WORKDIR /solver

# Development framework, dependencies
RUN dnf --assumeyes update && dnf --assumeyes install \
      gcc-c++ \
      binutils \
      binutils-gold \
      make \
      git-core \
      boost-devel \
      ccache \
      qpid-proton-cpp-devel \
      json-c \
      json-devel \
      json-glib \
      jsoncpp \
      jsoncpp-devel \
      wget \
      && \
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

FROM fedora:42
WORKDIR /solver
RUN dnf --assumeyes update && dnf --assumeyes install \
      boost \
      qpid-proton-cpp \
      cyrus-sasl \
      cyrus-sasl-plain \
      json-c \
      json-glib \
      jsoncpp \
      wget \
      && \
    dnf clean all

# Install Couenne

RUN wget https://kojipkgs.fedoraproject.org//packages/coin-or-Couenne/0.5.8/19.fc41/x86_64/coin-or-Couenne-0.5.8-19.fc41.x86_64.rpm \
    && dnf --assumeyes install coin-or-Couenne-0.5.8-19.fc41.x86_64.rpm \
    && rm coin-or-Couenne-0.5.8-19.fc41.x86_64.rpm

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
# docker run -e APPLICATION_ID="my_app_id" nebulous/solver -b="https://amqp.example.com/" -p=s3kr1t
# docker run --network=host -e APPLICATION_ID="my_app_id" -e ACTIVEMQ_HOST="amqp://localhost" -e ACTIVEMQ_PORT=61616 -e ACTIVEMQ_USER=admin -e ACTIVEMQ_PASSWORD=test solver
RUN chmod +x /solver/start-solver.sh

ENTRYPOINT ["/solver/start-solver.sh"]
