# Score-P HDEEM Energy Measurement Plugin

## Compilation and Installation

### Prerequisites

To compile this plugin, you need:

* A C++ 14 compiler

* Score-P Version 2+ (`SCOREP_METRIC_PLUGIN_VERSION` >= 1)

* The hdeem library and header files by BULL/ATOS

### Building and installation

1. Create a build directory

        mkdir build
        cd build

2. Invoke CMake

        cmake ..

    The following settings can be customized:

    * `HDEEM_BMC_USER HDEEM_BMC_PASS`

        Required for out of band access

    * `HDEEM_INCLUDE_DIRS`

        Directory where `hdeem.h` is located

    * `HDEEM_LIBRARIES`

        The full path of `libhdeem.so`, including the file name, e.g. `/usr/local/hdeem/libhdeem.so.1`

    * `SCOREP_CONFIG`

        Path to the `scorep-config` tool including the file name

        > *Note:*

        > If you have `scorep-config` in your `PATH`, it should be found by CMake.

    * `CMAKE_INSTALL_PREFIX`

        Directory where the resulting plugin will be installed (`lib/` suffix will be added)

    * `ENABLE_MPI` (only applicable to the `hdeem_sync_plugin`)

        Enables MPI communication for the sync plugin, and allows to run more than one MPI process
        per node.

3. Invoke make

        make

4. Install the files (optional)

        make install

> *Note:*

> Make sure to add the subfolder `lib` to your `LD_LIBRARY_PATH`.

## Usage

To add a hdeem metric to your trace, you have to add `hdeem_plugin` to the environment variable
`SCOREP_METRIC_PLUGINS`. This is the preferred version for tracing. It preserves the best possible
time resolution of the measurements.

To add the synchronous metric, add `hdeem_sync_plugin` to the environment variable
`SCOREP_METRIC_PLUGINS`. This can be useful for profiling. Measurements are only taken at events,
e.g. enter / leave of regions.

You have to add the list of the metric channel you are interested in to the environment variable
`SCOREP_METRIC_HDEEM_PLUGIN` or `SCOREP_METRIC_HDEEM_SYNC_PLUGIN`.

Note: Score-P does not support per-host post-mortem plugins with profiling. If you want to use the
post-mortem (`hdeem_plugin`) plugin, you should enable tracing and disable profiling by using:

    export SCOREP_ENABLE_PROFILING="false"
    export SCOREP_ENABLE_TRACING="true"

The sync plugin (`hdeem_sync_plugin`) works with profiling and tracing.

Note: The sync plugin is implemented as a strictly synchronous plugin. Therefor, it measures per
thread. As hdeem can just be used node-wide, the plugin does some thread and if enabled MPI
operations in order to obtain the responsible thread and process. The trace file might hold some
traces that are reported as 0. The profile might report wrong profiling statistics. Moreover, the
plugin reports mJ and mW as interfer values. This operations are necessary to be compatible with PTF
(http://periscope.in.tum.de/)

### Environment variables

* `SCOREP_METRIC_HDEEM_PLUGIN` (only applicable to the `hdeem_plugin`)

    Comma-separated list of sensors, can also contain wildcards, e.g. `Blade,CPU*`

* `SCOREP_METRIC_HDEEM_SYNC_PLUGIN` (only applicable to the `hdeem_sync_plugin`)

    Comma-separated list of sensors, and physical quantity. Can also contain wildcards, e.g.
    `BLADE/P,*/E`

* `SCOREP_METRIC_HDEEM_PLUGIN_CONNECTION` or `SCOREP_METRIC_HDEEM_SYNC_PLUGIN_CONNECTION` (default
    `BMC`)

    Can be set to either `INBAND` for inband connection to the BMC via PCIe/GPIO (default) or `OOB`
    for out-of-band connection via out-of-band / side-band IPMI.

    It is recommended to use `INBAND` as it is faster and has better time synchronization.

* `SCOREP_METRIC_HDEEM_PLUGIN_VERBOSE` or `SCOREP_METRIC_HDEEM_SYNC_PLUGIN_VERBOSE` (default `WARN`)

    Controls the output verbosity of the plugin. Possible values are: `FATAL`, `ERROR`, `WARN`,
    `INFO`, `DEBUG`, `TRACE` (`TRACE` currently not used). If set to any other value, `DEBUG` is
    used. Case in-sensitive.

* `SCOREP_METRIC_HDEEM_PLUGIN_TIMER` (only applicable to the `hdeem_plugin`, default `BMC`)

    Can be set to either `BMC` for using the timestamps from the BMC or `NODE` to use the timestamps
    of the node. `BMC` is usually much better.

* `SCOREP_METRIC_HDEEM_SYNC_PLUGIN_STATS_TIMEOUT_MS` (only applicable to the `hdeem_sync_plugin`,
    default `10`)

    Can be set to any integer variable. Defines the time in milliseconds after a `hdeem_get_stats`
    becomes invalid. The measurement of energy or power values might be at any time point since the
    event to this time.

* `GET_NEW_STATS` (only applicable to the `hdeem_sync_plugin`, default `10`)

    Can be set to any integer variable. Defines the time in milliseconds after a new measurement is
    taken. Events that triger the HDEEM plugin in between, will deliver the last value. This is 
    supposed to reduce the measurement overhead of HDEEM (i.e. 5 ms).



###If anything fails:

1. Check whether the plugin library can be loaded from the `LD_LIBRARY_PATH`.

2. Check whether your hdeem functionality is working properly by using the hdeem command line tools.

3. Write a mail to the authors.

##Authors

* Thomas Ilsche (thomas.ilsche at tu-dresden dot de)
* Mario Bielert (mario.bielert at tu-dresden dot de)
* Andreas Gocht (andreas.gocht at tu-dresden dot de)
