/*
 * Copyright (c) 2016, Technische Universität Dresden, Germany
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions
 *    and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 * conditions
 *    and the following disclaimer in the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to
 * endorse
 *    or promote products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

// Must be defined before the include!
#define ENV_CONFIG_PREFIX "SCOREP_METRIC_HDEEM_SYNC_PLUGIN_"
#include <scorep/plugin/plugin.hpp>

#include <chrono>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

#ifdef HAVE_MPI
#include <functional>
#include <mpi.h>
#endif

#include <sys/syscall.h>
#include <sys/types.h>

#include <hdeem_cxx.hpp>
#include <scorep/plugin/util/matcher.hpp>

#ifdef HDEEM_BMC_USER
#ifdef HDEEM_BMC_PASS
#define STR_VALUE2(x) #x
#define STR_VALUE(x) STR_VALUE2(x)
#define HDEEM_BMC_USER_STR STR_VALUE(HDEEM_BMC_USER)
#define HDEEM_BMC_PASS_STR STR_VALUE(HDEEM_BMC_PASS)
#define HDEEM_HAS_IPMI 1
#endif
#endif

#ifndef HDEEM_HAS_IPMI
#pragma message("No BMC credentials available, only inband mode enabled.")
#endif

bool global_is_resposible_process = false;
pid_t global_responsible_thread = -1;

namespace spp = scorep::plugin::policy;
using scorep_clock = scorep::chrono::measurement_clock;
using scorep::plugin::logging;

/**
 * Our Hdeem metric handle
 *
 *
 **/
struct hdeem_sync_metric
{
    // We need some kind of constructor
    hdeem_sync_metric(const std::string& full_name_, const std::string& name_,
                      hdeem::sensor_id sensor_, std::string& quantity_)
    : full_name(full_name_), name(name_), sensor(sensor_), quantity(quantity_)
    {
    }

    // delete copy constructor to avoid ... copies, needs move and default constructor though!
    hdeem_sync_metric(hdeem_sync_metric&&) = default;
    hdeem_sync_metric(const hdeem_sync_metric&) = delete;
    hdeem_sync_metric& operator=(const hdeem_sync_metric&) = delete;

    std::string full_name;
    std::string name;
    hdeem::sensor_id sensor;
    std::string quantity;
    bool remesure_reference = false;
    double last_energy_value = 0;
    unsigned long long int last_samples_count = 0;
};

/**
 * operator to print the metric handle
 *
 **/
std::ostream& operator<<(std::ostream& s, const hdeem_sync_metric& metric)
{
    s << "(" << metric.full_name << ", " << metric.sensor << ")";
    return s;
}

template <typename T, typename Policies>
using hdeem_sync_object_id = spp::object_id<hdeem_sync_metric, T, Policies>;

class hdeem_sync_plugin
    : public scorep::plugin::base<hdeem_sync_plugin, spp::per_thread, spp::sync_strict,
                                  spp::scorep_clock, spp::synchronize, hdeem_sync_object_id>
{
public:
    hdeem_bmc_data_t bmc;
    bool init = false;

    std::unique_ptr<hdeem::connection> hdeem;
    std::map<std::string, bool> already_saved_metrics;

private:
    const std::string prefix_ = "hdeem/";
    bool got_remesure_reference_ = false;
    hdeem::sensor_stats last_stats_;
    std::string hostname;
    bool is_resposible = false;

    pid_t responsible_thread = -1;

    /*
     * timeout for hdeem->get_stats() call. If more Time is expired result of call is
     * dismissed
     */

    std::chrono::milliseconds stats_timeout_ms;
    std::chrono::milliseconds get_new_stats;
    bool invalid_result = false;
    long int invalid_result_count = 0;
    long int valid_result_count = 0;

    std::chrono::steady_clock::time_point plugin_start_time;

public:
    /**
     * Initialization of the plugin.
     *
     * obtaining hostname.
     *
     **/
    hdeem_sync_plugin()
    {

        char c_hostname[HOST_NAME_MAX + 1];
        if (gethostname(c_hostname, HOST_NAME_MAX + 1))
        {

            int errsv = errno;
            switch (errsv)
            {
            case EFAULT:
                throw std::runtime_error("Failed to get local hostname. EFAULT");
                break;
            case EINVAL:
                throw std::runtime_error("Failed to get local hostname. EINVAL");
                break;
            case ENAMETOOLONG:
                throw std::runtime_error("Failed to get local hostname. ENAMETOOLONG");
                break;
            case EPERM:
                throw std::runtime_error("Failed to get local hostname. EPERM");
                break;
            default:
                throw std::runtime_error(std::string("Failed to get local hostname. ERRNO: ") +
                                         std::to_string(errsv));
                break;
            }
        }
        this->hostname = std::string(c_hostname);

        auto connection = scorep::environment_variable::get("CONNECTION", "INBAND");
        if (connection == "INBAND")
        {
            hdeem.reset(new hdeem::connection());
            logging::info() << "using In Band";
        }
        else
        {
#ifdef HDEEM_HAS_IPMI
            std::string bmc_hostname = this->hostname + std::string("-bmc");
            hdeem.reset(
                new hdeem::connection(bmc_hostname, HDEEM_BMC_USER_STR, HDEEM_BMC_PASS_STR));
            logging::info() << "using Out of Band";
#else
            throw std::logic_error("This build of hdeem_plugin does not support out of band.");
#endif
        }

        stats_timeout_ms = std::chrono::milliseconds(
            stoi(scorep::environment_variable::get("STATS_TIMEOUT_MS", "10")));
        get_new_stats = std::chrono::milliseconds(
            stoi(scorep::environment_variable::get("GET_NEW_STATS", "10")));
        logging::debug() << "set get_stats timeout to " << stats_timeout_ms.count() << " ms";
        logging::debug() << "set get_new_stats to " << get_new_stats.count() << " ms";

        /*
         * this part restores informations saved in the global variables.
         * This is necessary if the plugin gets reinitalisized and the
         * synchronise funciton is not called, for example if PTF is used
         */
        this->responsible_thread = global_responsible_thread;
        this->is_resposible = global_is_resposible_process;
        logging::info() << "restore local responsibility from global";

        start_hdeem();
        logging::debug() << "hdeem_init successful";
    }

    /**
     * Destructor
     *
     * Stopping Hdeem
     */
    ~hdeem_sync_plugin()
    {
        pid_t ptid = syscall(SYS_gettid);
        if (this->is_resposible && (this->responsible_thread == ptid))
        {
            try
            {
                hdeem->stop();
            }
            catch (std::exception& e)
            {
                logging::error() << "Error occoured: " << e.what();
            }

            if (invalid_result_count > 0)
            {
                logging::warn() << "got " << invalid_result_count
                                << " invalid results at host: " << this->hostname;
                logging::warn() << "and " << valid_result_count
                                << " valid results at host: " << this->hostname;
            }
        }
        logging::debug() << "plugin sucessfull finalized";
    }

    /**
     * You don't have to do anything in this method, but it tells the plugin that this metric will
     * indeed be used
     */
    void add_metric(hdeem_sync_metric& m)
    {
        logging::info() << "adding hdeem metric " << m.name;
        logging::debug() << "adding counter for ptid:" << syscall(SYS_gettid);
    }

    /** Will be called for every event in by the measurement environment.
     * You may or may or may not give it a value here.
     *
     * @param m contains the sored metric informations
     * @param proxy get and save the reuslts
     *
     * NOTE: In this implemenation we use a few assumptions:
     * * scorep calls at every event all metrics
     * * this metrics are called everytime in the same order
     *
     * NOTE: sometimes a get_stats() of hdeem takes very long, witch leeds to incorrect results
     * for the function. A workaround is to check the length of the call, and just return vaules
     * if the call is shorter then stats_timeout_ms witch is set
     * using SCOREP_METRIC_HDEEM_SYNC_PLUGIN_STATS_TIMEOUT_MS.
     */
    template <typename P>
    void get_current_value(hdeem_sync_metric& m, P& proxy)
    {
        // retrun 0 if not responsible. Simulate PER_HOST.
        pid_t ptid = syscall(SYS_gettid);
        if (!this->is_resposible || (this->responsible_thread != ptid))
        {
            proxy.store((int64_t)0);
            return;
        }

        // get the the first metric
        if (!got_remesure_reference_)
        {
            got_remesure_reference_ = true;
            m.remesure_reference = true;
        }

        // check if we are the first metric. If yes get new data from hdeem (all other metrics
        // already got their data).
        if (m.remesure_reference)
        {
            invalid_result = false;

            auto start = std::chrono::steady_clock::now();

            try
            {
                last_stats_ = hdeem->get_stats();
            }
            catch (std::exception& e)
            {
                logging::error() << "Error occoured: " << e.what();
                invalid_result = true;
            }

            auto end = std::chrono::steady_clock::now();
            auto diff = end - start;

#ifdef HDEEM_GET_STATS_TRACE
            auto diff_ms = std::chrono::duration_cast<std::chrono::microseconds>(diff);

            auto plugin_time = std::chrono::steady_clock::now();
            auto plugin_runtime = plugin_time - plugin_start_time;
            auto plugin_runtime_s =
                std::chrono::duration_cast<std::chrono::duration<double>>(plugin_runtime);

            logging::trace() << "response time for hdeem->get_stats();" << plugin_runtime_s.count()
                             << ";" << diff_ms.count();
#endif

            // if no error in hdeem occured, check if the measurment was to long
            if ((!invalid_result) && (diff > stats_timeout_ms))
            {
                invalid_result = true;
            }

            // cout the valid and invalid results from hdeem calls
            if (invalid_result)
            {
                invalid_result_count++;
            }
            else
            {
                valid_result_count++;
            }
        }

        if (!invalid_result)
        {
            if (m.quantity == "E")
            {
#ifdef HDEEM_GET_STATS_TRACE
                logging::trace() << "storing value (int): "
                                 << (int64_t)(last_stats_.energy(m.sensor) * 1000)
                                 << ", for: " << m.full_name;
#endif
                proxy.store((int64_t)(last_stats_.energy(m.sensor) * 1000));
            }
            else if (m.quantity == "P")
            {

                // check if the last measurement has been valid.
                if ((m.last_energy_value >= 0) && (m.last_samples_count >= 0))
                {
                    double energy = last_stats_.energy(m.sensor);
                    auto samples_count = last_stats_.samples_count(m.sensor);

                    double energy_diff = energy - m.last_energy_value;
                    auto samples_diff = samples_count - m.last_samples_count;

                    if (samples_diff > 0)
                    {
                        double time = samples_diff / hdeem->sensor_sampling_rate(m.sensor);
                        double result = energy_diff / time;

                        proxy.store((int64_t)(result * 1000));
                    }
                }

                // we need to store this values in any way for the next measurement.
                m.last_energy_value = last_stats_.energy(m.sensor);
                m.last_samples_count = last_stats_.samples_count(m.sensor);
            }
        }
        else
        {
#ifdef HDEEM_GET_STATS_TRACE
            logging::trace() << "storing no value (invalid result)";
#endif

            // wo do not know when the values have been recorded. So we need to invalidate them.
            proxy.store((int64_t)0);
            m.last_energy_value = -1;
            m.last_samples_count = -1;
        }
    }

    /** function to determine the responsible process for HDEEM
     *
     * If there is no MPI communication, the HDEEM communication is PER_PROCESS, so Score-P
     * cares about everything.
     * If there is MPI communication and the plugin is build with -DHAVE_MPI,
     * we are grouping all MPI_Processes according to their hostname hash.
     * Then we select rank 0 to be the responsible rank for MPI communication.
     *
     * @param is_responsible the Score-P responsibility
     * @param sync_mode sync mode, i.e. SCOREP_METRIC_SYNCHRONIZATION_MODE_BEGIN for non MPI
     *              programs and SCOREP_METRIC_SYNCHRONIZATION_MODE_BEGIN_MPP for MPI program.
     *              Does not deal with SCOREP_METRIC_SYNCHRONIZATION_MODE_END
     *
     */
    void synchronize(bool is_responsible, SCOREP_MetricSynchronizationMode sync_mode)
    {
        logging::debug() << "Synchronize " << is_responsible << ", " << sync_mode;
        if (is_responsible)
        {
            switch (sync_mode)
            {
            case SCOREP_METRIC_SYNCHRONIZATION_MODE_BEGIN:
            {
                logging::debug() << "SCOREP_METRIC_SYNCHRONIZATION_MODE_BEGIN";

                this->is_resposible = true;
                this->responsible_thread = syscall(SYS_gettid);
                logging::debug() << "got responsible ptid:" << this->responsible_thread;
                start_hdeem();

                break;
            }
            case SCOREP_METRIC_SYNCHRONIZATION_MODE_BEGIN_MPP:
            {
                logging::debug() << "SCOREP_METRIC_SYNCHRONIZATION_MODE_BEGIN_MPP";

#ifdef HAVE_MPI
                std::hash<std::string> mpi_color_hash;
                MPI_Comm node_local_comm;
                int myrank;
                int new_myrank;

                // TODO
                // we are assuming, that this is uniqun enought .... wee probably need a rework here
                int hash = abs(mpi_color_hash(this->hostname));

                logging::debug() << "hash value: " << hash;

                PMPI_Comm_rank(MPI_COMM_WORLD, &myrank);
                PMPI_Comm_split(MPI_COMM_WORLD, hash, myrank, &node_local_comm);
                PMPI_Comm_rank(node_local_comm, &new_myrank);

                if (new_myrank == 0)
                {
                    this->is_resposible = true;
                    logging::debug() << "got responsible process for host: " << this->hostname;
                }
                else
                {
                    this->is_resposible = false;
                }

                this->responsible_thread = syscall(SYS_gettid);
                logging::debug() << "got responsible ptid:" << this->responsible_thread;

                PMPI_Comm_free(&node_local_comm);
#else
                logging::warn() << "You are using the non MPI version of this plugin. This might "
                                   "lead to trouble if there is more than one MPI rank per node.";
                this->is_resposible = true;
                this->responsible_thread = syscall(SYS_gettid);
                logging::debug() << "got responsible ptid:" << this->responsible_thread;
#endif
                start_hdeem();

                break;
            }
            case SCOREP_METRIC_SYNCHRONIZATION_MODE_END:
            {
                break;
            }
            case SCOREP_METRIC_SYNCHRONIZATION_MODE_MAX:
            {
                break;
            }
            }
        }

        global_is_resposible_process = this->is_resposible;
        global_responsible_thread = this->responsible_thread;
        logging::debug() << "setting global responsible ptid to:" << this->responsible_thread;
    }

    /**
     * Convert a named metric (may contain wildcards or so) to a vector of actual metrics (may have
     * a different name)
     *
     * NOTE: Adds the metrics. Currently available metrics are (according to hdeem doku):
     *
     *  * BLADE
     *  * CPU0
     *  * CPU1
     *  * DDR_AB
     *  * DDR_CD
     *  * DDR_EF
     *  * DDR_GH
     *
     *  Wildcards are allowed.
     *
     *
     */
    std::vector<scorep::plugin::metric_property> get_metric_properties(const std::string& name)
    {

        std::string metric_name = name;
        logging::debug() << "get_event_info called";
        std::vector<scorep::plugin::metric_property> properties;

        // Allow the user to prefix the metric with hdeem/ or not.
        // In the trace it will always be full_name with hdeem/[blade,vr]
        auto pos_prefix = metric_name.find(prefix_);
        if (pos_prefix == 0)
        {
            metric_name = metric_name.substr(prefix_.length());
        }

        auto pos_qunatity = metric_name.find("/");
        std::string qunatity;
        if (pos_qunatity == std::string::npos)
        {
            logging::warn() << "no physical quantity found using Power";
            qunatity = "P";
        }
        else
        {
            qunatity = metric_name.substr(pos_qunatity + 1);
            metric_name.erase(pos_qunatity);
        }

        scorep::plugin::util::matcher match(metric_name);
        for (auto sensor : hdeem->sensors())
        {
            const auto& sensor_name = hdeem->sensor_name(sensor);
            if (match(sensor_name))
            {
                properties.push_back(add_metric_property(sensor_name, sensor, qunatity));
            }
        }
        if (properties.empty())
        {
            logging::fatal() << "No metrics added. Check your metrics!";
        }

        logging::debug() << "get_event_info(" << metric_name << ") Quantity: " << qunatity
                         << " returning " << properties.size() << " properties";

        return properties;
    }

private:
    scorep::plugin::metric_property
    add_metric_property(const std::string& name, hdeem::sensor_id sensor, std::string qunatity)
    {
        const std::string full_name = prefix_ + name + std::string("/") + qunatity;
        std::string description;

        already_saved_metrics.insert(std::pair<std::string, bool>(full_name, true));

        auto& handle = make_handle(full_name, full_name, name, sensor, qunatity);
        logging::trace() << "registered handle: " << handle;
        if (qunatity == "E")
        {
            return scorep::plugin::metric_property(full_name, " Energy Consumption", "mJ")
                .accumulated_last()
                .value_int();
        }
        else if (qunatity == "P")
        {
            return scorep::plugin::metric_property(full_name, " Power Consumption", "mW")
                .absolute_last()
                .value_int();
        }
        else
        {
            return scorep::plugin::metric_property(full_name, " Unknown quantity", qunatity)
                .absolute_point()
                .value_int();
        }
    }

    /** Initalisation of hdeem. Called after synchronize.
     *
     * if "is_resposible" is true does:
     *
     * Connection to hdeem
     * Starting of hdeem
     * Getting a few environment variables
     *
     *
     */
    void start_hdeem()
    {
        pid_t ptid = syscall(SYS_gettid);
        if (this->is_resposible && (this->responsible_thread == ptid))
        {
            hdeem->start();

            logging::info() << "hdeem started";

            plugin_start_time = std::chrono::steady_clock::now();
        }
        else
        {
            logging::debug() << "not responsible";
        }
    }
};

SCOREP_METRIC_PLUGIN_CLASS(hdeem_sync_plugin, "hdeem_sync")
