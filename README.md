#Score-P HDEEM Energy Measurement Plugin

##Compilation and Installation

###Prerequisites

To compile this plugin, you need:

* C++11 compiler

* Score-P with SCOREP_METRIC_PLUGIN_VERSION 1
    Currently this is the branch TRY_RTSCHUETER_plugins_sync_callback

* The HDEEM library and header files by BULL/ATOS

###Building and installation

1. Invoke CMake

        mkdir BUILD && cd BUILD
        cmake ..

The following settings are important:

* HDEEM_BMC_USER HDEEM_BMC_PASS   required for out of band access
* HDEEM_INCLUDE_DIRS              directory where hdeem.h is located
* HDEEM_LIBRARIES                 the full path of libhdeem.so, including the file name,
                                  e.g. /usr/local/hdeem/libhdeem.so.1
* SCOREPPLUGINCXX_INCLUDE_DIRS    directory for the ScoreP C++ abstraction headers
* SCOREP_CONFIG                   path to the scorep-config tool including the file name
* CMAKE_INSTALL_PREFIX            directory where the resulting plugin will be installed
                                  (lib/ suffix will be added)
* ENABLE_MPI                      [just for the sync plugin] Enables MPI communication for
                                  the sync plugin, and allows to run more than one MPI
                                  process per node.

2. Invoke make

        make

> *Note:*

> If you have `scorep-config` in your `PATH`, it should be found by CMake.

3. Invoke make

        make install

> *Note:*

> Make sure to add the subfolder `lib` to your `LD_LIBRARY_PATH`.

##Usage

To add a dataheap metric to your trace, you have to add `hdeem_plugin` to the environment
variable `SCOREP_METRIC_PLUGINS`.

To add the synchrone metric, add `hdeem_sync_plugin` to the environment
variable `SCOREP_METRIC_PLUGINS`.


You have to add the list of the metric channel you are interested in to the environment
variable `SCOREP_METRIC_HDEEM_PLUGIN` or `SCOREP_METRIC_HDEEM_SYNC_PLUGIN`.

Note: Score-P does not support per-host post-mortem plugins with profiling. If you want to
use the post-mortem (hdeem_plugin) plugin, you should enable tracing and disable profiling
by using:

        export SCOREP_ENABLE_PROFILING="false"
        export SCOREP_ENABLE_TRACING="true"
        
The sync plugin (hdeem_sync_plugin) works with profiling and tracing.
        
Note: The sync plugin is implemented as a stricly syncronus plugin. Therefor, it measures
per thread. As HDEEM can just be used nodewide, the plguin does some thread and if enabled
MPI operations in order to obtain the responsible thread and process. The trace file might
hold some traces that are reported as 0. The profile might report wrong profiling
statistics. Moreover, the plugin reports mJ and mW as interfer values. This operations
are necesarry to be compatible with PTF (http://periscope.in.tum.de/)

###Environment variables

* `SCOREP_METRIC_HDEEM_PLUGIN` 

    Comma-separated list of sensors, can also contain wildcards, e.g. `Blade,CPU*`
    [Just for hdeem_plugin]
    
* `SCOREP_METRIC_HDEEM_SYNC_PLUGIN`
    
    Comma-separated list of sensors, and physical quantity. Can also contain wildcards,
    e.g. `BLADE/P,*/E` 
    [Just for hdeem_sync_plugin]
    

* `SCOREP_METRIC_HDEEM_PLUGIN_CONNECTION` or `SCOREP_METRIC_HDEEM_SYNC_PLUGIN_CONNECTION` 

    Can be set to either `INBAND` for inband connection to the BMC via PCIe/GPIO (default)
    or `OOB` for out-of-band connection via out-of-band / side-band IPMI.
    It is recommended to use `INBAND` as it is faster and has better time synchronization.

* `SCOREP_METRIC_HDEEM_PLUGIN_VERBOSE` or `SCOREP_METRIC_HDEEM_SYNC_PLUGIN_VERBOSE` 

    Controls the output verbosity of the plugin. Possible values are:
    `FATAL`, `ERROR`, `WARN` (default), `INFO`, `DEBUG`, `TRACE` (`TRACE` currently not used)
    If set to any other value, DEBUG is used. Case in-sensitive.

* `SCOREP_METRIC_HDEEM_PLUGIN_TIMER`

    Can be set to either `BMC` (default) for using the timestamps from the BMC
    or `NODE` to use the timestamps of the node. `BMC` is usually much better.
    [Just for hdeem_plugin]
    
* `SCOREP_METRIC_HDEEM_SYNC_PLUGIN_STATS_TIMEOUT_MS`

    Can be set to any integer variable. Defines the time in milliseconds after a
    hdeem_get_stats become invalid. The measurement of energy or power values might be at
    any time point since the event to this time. Default is 10 ms.
    [Just for hdeem_sync_plugin]


###If anything fails:

1. Check whether the plugin library can be loaded from the `LD_LIBRARY_PATH`.

2. Check whether your HDEEM functionality is working properly by using the HDEEM command line tools.

3. Write a mail to the author.

### About hdeem_cxx.hpp

The hdeem_cxx.hpp contains a convenient wrapper around hdeem for access with C++.
See the example/hdeem_cxx.cpp on how to use it or run doxygen in the include directory.

##Authors

* Thomas Ilsche (thomas.ilsche at tu-dresden dot de)
* Mario Bielert (mario.bielert at tu-dresden dot de)
* Andreas Gocht (andreas.gocht at tu-dresden dot de)
