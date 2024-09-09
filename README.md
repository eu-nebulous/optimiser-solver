# NebulOuS Solver

## Synopsis

The [NebulOuS](https://nebulouscloud.eu/) optimizer is reconfiguring a running application to the deployment that maximises the application utility for the current application execution context defined by the metrics used in the utility function and the related constraints. 

The Solver component is interacting with messages from the other NebulOuS modules, and provides the solution to the configuration variables of the application model. 

## Architecture

The solver component consists of three concurrent [Actors](https://en.wikipedia.org/wiki/Actor_model):
* The **Metric Updater** tasked with receiving updates of metric values representing the application's execution context that are first defined. When an event happens in the application updating one or more metric values defined as being a part of the execution context, the most recent value will be cached. When a Service Level Objective (SLO) violation is detected for the system, the current vector of metric values will be forwarded to the Solver Manager.
* The **Solver Manager** implements the execution control of the Solver Component. Its main task is to maintain a set of mathematical solvers and dispatch incoming application execution contexts to a free solver. When the solver finishes the search for the configuration optimising the utility function, it will forward the corresponding solution to the NebulOuS Optimizer Controller in charge of deploying the configuration according to the found optimal application configuration. Not all solutions are automatically deployed because the maximal utility may also be used to train various performance indicators for hypothetical application execution contexts. Hence, there can be many solutions requested than those corresponding to SLO violations triggering the Metric Updater.
* The **AMPL Solver** implementing the mathematical solver finding the configuration that maximises the utility function for the provided application execution context. A Mathematical Programming Language ([AMPL](https://ampl.com/)) is used to formulate the constraint mathematical programming problem calling a back end mathematical solver. Many different solvers can be used, both commercial and open source, and the current implementation uses the open source solver [Couenne](https://github.com/coin-or/Couenne) from the [Computational Infrastructure for Operations Research (COIN-OR)](https://www.coin-or.org/).

A Class-Actor-Communication diagram is shwon below. The green components are other parts of the NebulOuS Optimizer module, and the gray components are other NebulOuS componts intaracting with the Solver Component. 

![Architecture diagram](https://github.com/eu-nebulous/optimiser-solver/blob/main/Optimizer%20architecture-Solver.drawio.png)


## Messages

The various components of the NebulOuS platform exchange messages in [JavaScript Object Notation ](https://en.wikipedia.org/wiki/JSON) (JSON) format. The external messages are all sent using the [Active Message Queue Protocol](https://www.amqp.org/) (AMQ). The exchange mechanism is many-to-many on publish-subscribe topic indicated beow for the supported messages. It shoud be noted that only key-value maps are supported at the top level leading sometimes to unnecessary redirection.

### Initial messages

#### Metric List
**AMQ Topic:** eu.nebulouscloud.optimiser.controller.metric_list

This message is sent from the Optimizer Controller component when a new application is created. It contains the names of the metric that jointly constitutes the application execution context. The Metric Updater actor will subscribe to the predicted versions of all of these metrics and cache the last value received for each of these metrics.
```
{
  "metrics": [
    "metric_1",
    "metric_2",
    ...
  ]
}
```

#### Optimization Problem
**AMQ Topic:** eu.nebulouscloud.optimiser.controller.model

The definition of the constraint optimisation problem is sent as an AMPL file from the Optimizer Controller component to all the running AMPL Solver actors. No optimisation will take place before this message has been received by the solving actors. The file name is provided in the message together with the AMPL file as a serialised text string. The data file provided contains the initial values for performance indicator regression function coefficients. The AMPL file format supports multiple objective functions to be defined, but only one can be used as the optimisation target for each run. It is therefore necessary to convey also the name of the objective function to use for the utility function initially. 

Finally, there is a set of "constants".  These are cached variables representing properties of the currently deployed configuration so that potential improvements of a new configuration can be computed relative to the current configuration.


```
{
    "ModelFileName" : "<AMPL Model File Name>"
    "ModelFileContent" : "<AMPL model file content as a string>"
    "DataFileName" : "<AMPL Data File Name>"
    "DataFileContent" : "<AMPL data file content as a string>"
    "ObjectiveFunction" : "<Name of the objective function in the AMPL file to use by default>"
    "Constants" : {
        "<Constant Name 1>" : {
            "Variable" : "<AMPL Variable Name >",
            "Value"    : "<value from variable domain used for initial application deployment >"
        },
        "<Constant name 2 >" : {...}
        ...
    }
}
```

#### Life cycle: Solver Status
**AMQ Topic**: eu.nebulouscloud.solver.state

Two state messages are posted by the solver component when it starts up. The Solver Component will post the state as "starting" when the code starts running, and "ready" when it is ready to receive the messages from external components. When the solver is shut down it will send the "stopped" message.


```
{
"when":"2023-01-01 00:00:00.000"
"state": "starting"  | "ready" | "stopped"
}
```

### Repeated messages

#### SLO Violation
**AMQ Topic**: eu.nebulouscloud.monitoring.slo.severity_value

The SLO Violation Detector component will send this message when it thinks that there will be a future SLO violation within the prediction horizon. This message will trigger the reconfiguration cycle starting with the Metric Updater sending the current application execution context, i.e. the cached metric values to the Solver Manager, which forwards the application context to the first available Solver actor. SLO Violation messages will only be accepted again once the application is indicated as running normally. 

```
{
  "severity": 0.9064,
  "predictionTime": 1626181860,
  "probability": 0.92246521
}
```

#### Application status
**AMQ Topic**: eu.nebulouscloud.optimiser.controller.app_state

The Optimiser Controller component is sending the application status message to indicate the status of the managed application. The Metric Updater will ignore SLO violation messages unless the application status indicate that the application is running.

```
{
  "when": "2024-04-17T07:54:00.169580700Z",
  "state":  "NEW" | "RUNNING" | "DEPLOYING" | "RUNNING" | "FAILED" | "DELETED"
}
```

#### Metric value update
**AMQ Topic**: eu.nebulouscloud.monitoring.predicted.{METRIC_NAME}

Every time there is an event happening in the application creating a new measurement the value of the metric will be predicted for a future time point corresponding to the estimated application reconfiguration delay. 

```
{
    "metricValue": 12.34,
    "level": 1,
    "timestamp": 163532341,
    "probability": 0.98,
    "confidence_interval" : [8,15]
    "predictionTime": 163532342,
}
```
#### Application Execution Context
**AMQ Topic**: eu.nebulouscloud.optimiser.solver.context

This message convey the cached metric values by the Metric Updater to the Solver Manager when an SLO Violation message is received from the SLO Violation Detector. It contains the time stamp of the latest metric recorded and the map of all metric names and their values.

There is a small detail in the flag to indicate if the solution should be deployed or not. In order to train various regression functions and the SLO Violation Detector various hypothetical application execution context can be submitted allowing the same solver to calculate the optimal configuration for these hypothetical execution contexts. These training evaluations shall not lead to a reconfiguration of the running application, and therefore the found configuration will only be deployed if the deployment flag is set to true. The Metric Updater will always set the deployment flag to true when submitting the application execution context in response to the SLO Violation detected, and all other training context submitted must have this value set to false.

The message also carries an optional name of the utility function to maximise since there can be multiple utility _dimensions_ that may be separately optimised if one searches for the [Pareto front](https://en.wikipedia.org/wiki/Pareto_front) of the multi-objective optimisation problem. 

```
{
    "Timestamp" : <Time of the most recent metric update>,
    "ObjectiveFunction" : <Name of the objective function to use>,
    "ExecutionContext" : {
	<metric name 1> : <metric value>,
        <metric name 2> : <metric value>,
        ...
     },
     "DeploySolution" : "true"| "false"
}
```



### Solution
**AMQ Topic**: eu.nebulouscloud.optimiser.solver.solution

The solution message is sent by the Execution Manger when the solver working on a particular application execution context returns the new configuration with values assigned to all variables of the problem. The time stamp is the unique key for matching the application execution with the produced solution. The flag to indicate whether the Optimiser Controller will deploy this solution or not is copied from the application execution context message.

The solution will be optimal with respect to the utility function provided as the objective function of the application execution context message, and if no objective function was explicitly indicated the default objective function will be used for the optimisation. The value of the other objective functions present in the optimisation problem will just be evaluated for the solution found for the requested or default objective function.

```
{
  "Timestamp" : <Timestamp taken from the application execution context message>,
  "ObjectiveFunction" : <Name of the objective function optimized>,
  "ObjectiveValues" : {
      <Objective function 1> : <Value>,
      <Objective function 2> : <Value>,
      ...
  },
  "VariableValues" : {
      <Variable 1> : <Value>,
      <Variable 2> : <Value>,
      ...
  },
  "DeploySolution" : true | false
}
```

### Data File
**AMQ Topic**: eu.nebulouscloud.optimiser.solver.data

The data file message contains a file name and the content of a problem specific data file to be loaded by the solver to set temporal parameters for the optimisation problem. Typically these are regression function coefficients or parameters allowing the solver to compute performance indicators to predict the performance aspects of a solution candidate. Based on the assessment of the current situation, some external component like the performance module may estimate these coefficients, and communicate them in a data message to be used by the solver.

```
{
  "FileName" : <The file name>,
  "FileContent" : <Content of the data file>
}
```



## Implementation

The three concurrent actors are implemented as  [Theron++](https://github.com/GeirHo/TheronPlusPlus) Actors each running in their own thread. In addtion, the Theron++ communication library is used which also uses four additional actors to ensure that outbound and inbound messages can be handled as concurrently as possible. The AMQ protocol interface implemented by Theron++ is based on the [Qpid Proton library.](https://qpid.apache.org/proton/) There is a separate thread to handle the AMQ interface. 

The Solver Component actors and the Theron++ library uses features of the latest version of the [C++ standard](https://isocpp.org/) and its standard template library, now C++23. It should therefore be possible to compile the Solver Component with any recent compatible compiler.

The AMPL Solver actor uses the [AMPL C++ Application Programming Interface (API)](https://ampl.com/api/latest/cpp/) to parse and interpret the constraint optimisation problem file, and to call the back-end mathematical programme solvers

##  License
The software is copyleft open source provided under the [Mozilla Public License version 2.0](https://www.mozilla.org/en-US/MPL/2.0/).
