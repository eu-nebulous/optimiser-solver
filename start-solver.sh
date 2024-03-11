#!/usr/bin/bash

# call bash directly since we know we're on Fedora
set -euf -o pipefail

amqpbroker=${ACTIVEMQ_HOST:-localhost}
amqpport=${ACTIVEMQ_PORT:-5672}
amqpuser=${ACTIVEMQ_USER:-admin}
amqppassword=${ACTIVEMQ_PASSWORD:-}
appid=${APPLICATION_ID:-}
license=${AMPL_LICENSE:-}

while getopts b:P:u:p:e:l: name
do
    case $name in
        b) amqpbroker="$OPTARG";;
        p) amqppassword="$OPTARG";;
        u) amqpuser="$OPTARG";;
        P) amqpport="$OPTARG";;
        e) appid="$OPTARG";;
        l) license="$OPTARG";;
        ?) printf "Usage: %s [-b amqbroker] [-P amqpport] [-u user] [-p amqppassword] [-e appid] [-l license]\n -b amqpbroker   overrides ACTIVEMQ_HOST (default localhost)\n -P amqpport     overrides ACTIVEMQ_PORT (default 5672)\n -u user         overrides ACTIVEMQ_USER (default admin)\n -p amqppassword overrides ACTIVEMQ_PASSWORD\n -e appid        overrides APPLICATION_ID\n -l license      overrides AMPL_LICENSE\n" "$0"
        exit 2;;
    esac
done
if [ -z "${appid}" ]; then
    printf "Missing Application ID, unable to start the solver"
    exit 1
fi

if [ -n "${license}" ] ; then
    echo "creating license file /solver/AMPL/ampl.lic"
    echo "$license" > /solver/AMPL/ampl.lic
fi

echo "Starting SolverComponent: app id='${appid}' broker='${amqpbroker}:${amqpport}'" && sync

exec ./SolverComponent --AMPLDir=/solver/AMPL --ModelDir=/tmp --Broker="$amqpbroker" --Port="$amqpport" --User="$amqpuser" --Pw="$amqppassword" --Endpoint="$appid"
