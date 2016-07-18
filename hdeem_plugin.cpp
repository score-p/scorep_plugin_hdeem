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
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions
 *    and the following disclaimer in the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse
 *    or promote products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define ENV_CONFIG_PREFIX "SCOREP_METRIC_HDEEM_PLUGIN_"
#include <scorep/plugin/plugin.hpp>
#include <scorep/plugin/util/matcher.hpp>

extern "C" {
#include <unistd.h>
#include <climits>
}
#include <ctime>

#include <ostream>
#include <chrono>
#include <ratio>

#include <hdeem_cxx.hpp>

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

using namespace scorep::plugin::policy;

using scorep::plugin::logging;

// Must be system clock for real epoch!
using local_clock = std::chrono::system_clock;

static local_clock::time_point timespec_to_chrono(const struct timespec ts)
{
    auto duration = std::chrono::duration<double>(ts.tv_sec + ts.tv_nsec * 1.0e-9);
    return local_clock::time_point(std::chrono::duration_cast<local_clock::duration>(duration));
}

template <typename T>
static double chrono_to_millis(T duration)
{
    return std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(duration).count();
}

template <typename TP>
class scorep_time_converter
{
    using local_duration_t = typename TP::duration;

public:
    scorep_time_converter(TP local_start, TP local_stop, scorep::chrono::ticks scorep_start,
                          scorep::chrono::ticks scorep_stop)
    : local_start_(local_start), scorep_start_(scorep_start)
    {
        const auto local_duration = local_stop - local_start;
        const auto scorep_duration = scorep_stop - scorep_start;
        tick_rate_ = static_cast<double>(scorep_duration.count()) / local_duration.count();
    }

    template <typename T>
    scorep::chrono::ticks to_ticks(const T duration) const
    {
        return to_ticks(std::chrono::duration_cast<local_duration_t>(duration));
    }

    scorep::chrono::ticks to_ticks(const local_duration_t duration) const
    {
        return scorep::chrono::ticks(duration.count() * tick_rate_);
    }

    scorep::chrono::ticks to_ticks(const TP tp) const
    {
        const auto tp_offset = tp - local_start_;
        return scorep_start_ + to_ticks(tp_offset);
    }

private:
    TP local_start_;
    scorep::chrono::ticks scorep_start_;
    double tick_rate_;
};

struct hdeem_metric
{
    // We need some kind of constructor
    hdeem_metric(const std::string& full_name_, const std::string& name_, hdeem::sensor_id sensor_)
    : full_name(full_name_), name(name_), sensor(sensor_)
    {
    }

    // delete copy constructor to avoid ... copies, needs move and default constructor though!
    hdeem_metric(hdeem_metric&&) = default;
    hdeem_metric(const hdeem_metric&) = delete;
    hdeem_metric& operator=(const hdeem_metric&) = delete;

    std::string full_name;
    std::string name;
    hdeem::sensor_id sensor;
};
/*
template<typename T>
T& operator<<(T&& s, const hdeem_metric& metric)
{
    s << "(" << metric.full_name << ", " << metric.sensor << ")";
    return std::move(s);
}
*/
std::ostream& operator<<(std::ostream& s, const hdeem_metric& metric)
{
    s << "(" << metric.full_name << ", " << metric.sensor << ")";
    return s;
}

template <typename P, typename Policies>
using hdeem_object_id = object_id<hdeem_metric, P, Policies>;

class hdeem_plugin
    : public scorep::plugin::base<hdeem_plugin, async, per_host, post_mortem, scorep_clock, hdeem_object_id>
{
public:

    hdeem_plugin()
    {
        auto connection = scorep::environment_variable::get("CONNECTION", "INBAND");
        if (connection == "INBAND")
        {
            hdeem.reset(new hdeem::connection());
        }
        else
        {
#ifdef HDEEM_HAS_IPMI
            char c_hostname[HOST_NAME_MAX + 1];
            if (gethostname(c_hostname, HOST_NAME_MAX + 1))
            {
                throw std::runtime_error("Failed to get local hostname");
            }
            std::string bmc_hostname = c_hostname;
            bmc_hostname = bmc_hostname + "-bmc";
            hdeem.reset(new hdeem::connection(bmc_hostname, HDEEM_BMC_USER_STR, HDEEM_BMC_PASS_STR));
#else
            throw std::logic_error("This build of hdeem_plugin does not support out of band.");
#endif
        }

        auto timer_str = scorep::environment_variable::get("TIMER", "BMC");
        timer = timer_t::NODE;
        if (timer_str == "NODE")
        {
            timer = timer_t::NODE;
            logging::info() << "Using NODE (Score-P) timer";
        }
        else if (timer_str == "BMC")
        {
            timer = timer_t::BMC;
            logging::info() << "Using remote BMC timer";
        }
        else
        {
            logging::warn() << "Selected timer " << timer_str << " currently not implemented";
        }

        logging::info() << "hdeem_init successful";
    }

public:
    void add_metric(hdeem_metric& handle)
    {
        // We actually don't need to do a thing! :-)
    }

    void start()
    {
        assert(!stopped);
        if (started)
        {
            return;
        }

        local_start = local_clock::now();
        scorep_start = scorep::chrono::measurement_clock::now();
        hdeem->start();

        started = true;
        logging::info() << "Successfully started hdeem measurement.";
    }

    void stop()
    {
        if (!started)
        {
            logging::warn() << "Trying to stop without being started.";
            return;
        }
        if (stopped)
        {
            return;
        }

        // TODO Figure out where to take the stop time
        local_stop = local_clock::now();
        scorep_stop = scorep::chrono::measurement_clock::now();
        stopped = true;
        try
        {
            hdeem->stop();
        }
        catch (hdeem::overflow_error& oe)
        {
            logging::error() << "HDEEM overflow. Sensor data for this node will not be available.";
            return;
        }
        catch (std::exception& ex)
        {
            logging::error() << "HDEEM could not stop: " <<  ex.what();
        }
        auto tp_after_stop = local_clock::now();
        auto status = hdeem->get_status();
        readings = std::unique_ptr<hdeem::sensor_data>(new hdeem::sensor_data(hdeem->get_global()));

        logging::info() << "Successfully stopped hdeem measurement and retrieved "
                        << readings->size(hdeem::sensor_id::blade(0)) << " blade values, "
                        << readings->size(hdeem::sensor_id::vr(0)) << " vr values"
                        << " in " << chrono_to_millis(local_clock::now() - tp_after_stop) << " ms.";
        logging::debug() << "scorep(ticks) start: " << scorep_start.count() << ", stop: " << scorep_stop.count();
        logging::debug() << "local(sys,us) start: " << local_start.time_since_epoch().count()
                         << ", stop: " << local_stop.time_since_epoch().count();
        logging::debug() << "bmc stats(s)  start: " << status.start_time_blade.tv_sec << "." << status.start_time_blade.tv_nsec
                         << ", stop: " << status.stop_time_blade.tv_sec << "." << status.stop_time_blade.tv_nsec;
    }

    void synchronize(bool is_responsible, SCOREP_MetricSynchronizationMode sync_mode)
    {
        logging::debug() << "Synchronize " << is_responsible << ", " << sync_mode;
    }

    template <typename C>
    void get_all_values(hdeem_metric& handle, C& cursor)
    {
        stop(); // Should be already called, but just to be sure. Will also get the hdeem_readings.
        auto tp_start = local_clock::now();
        if (!stopped)
        {
            throw std::runtime_error("Could not stop hdeem measurement to get values.");
        }
        if (!readings)
        {
            logging::error() << "No hdeem readings available.";
            return;
        }

        std::chrono::duration<double> filter_offset;
        auto sensor_data = readings->get_single_sensor_data(handle.sensor);

        if (handle.sensor.is_blade())
        {
            filter_offset = time_offset_blade;
        }
        else
        {
            filter_offset = time_offset_vr;
        }

        auto converter =
            scorep_time_converter<local_clock::time_point>(local_start, local_stop, scorep_start, scorep_stop);
        local_clock::duration duration_actual;
        scorep::chrono::ticks scorep_start_actual;
        if (timer == timer_t::NODE)
        {
            duration_actual = local_stop - local_start;
            scorep_start_actual = scorep_start + converter.to_ticks(filter_offset);
        }
        else if (timer == timer_t::BMC)
        {
            auto status = hdeem->get_status(false);
            auto hdeem_duration =
                std::chrono::nanoseconds(1000000000 * (status.stop_time_blade.tv_sec - status.start_time_blade.tv_sec) +
                                         (status.stop_time_blade.tv_nsec - status.start_time_blade.tv_nsec));
            duration_actual = std::chrono::duration_cast<local_clock::duration>(hdeem_duration);
            // Need to convert from timespec to scorep ... going through local as intermediate step
            const auto scorep_start_bmc = converter.to_ticks(timespec_to_chrono(status.start_time_blade));
            logging::trace() << "BMC timer duration(sysclock usually us) " << duration_actual.count() << " instead of "
                             << (local_stop - local_start).count() << ", start(ticks) " << scorep_start_bmc.count()
                             << " instead of " << scorep_start.count();
            scorep_start_actual = scorep_start_bmc + converter.to_ticks(filter_offset);
        }
        else
        {
            throw std::logic_error("unimplemented timer model.");
        }

        // We must use double here to avoid too much precision loss e.g. from integers in microseconds
        const double sampling_period = static_cast<double>(duration_actual.count()) / sensor_data.size();

        logging::trace() << "Using effective sampling period of " << sampling_period << " sysclock ticks (usually us)";

        const auto index_to_scorep_ticks = [sampling_period, scorep_start_actual, converter](size_t index)
        {
            // TODO Add offset, needs further time computation
            return scorep_start_actual +
                   converter.to_ticks(local_clock::duration(static_cast<int64_t>(sampling_period * index)));
        };

        logging::trace() << "Reading " << sensor_data.size() << " values from sensor " << handle.sensor;
        cursor.resize(sensor_data.size());
        for (auto elem : sensor_data)
        {
            auto index = elem.first;
            auto value = elem.second;
            cursor.store(index_to_scorep_ticks(index), value);
        }
        if (cursor.size() == 0)
        {
            logging::warn() << "no valid measurements are in the time range. Total measurements: "
                            << sensor_data.size();
        }
        logging::debug() << "get_all_values wrote " << sensor_data.size() << " values (out of which " << cursor.size()
                         << " are in the valid time range) in "
                         << chrono_to_millis(local_clock::now() - tp_start) << " ms.";
    }

    std::vector<scorep::plugin::metric_property> get_metric_properties(std::string metric_name)
    {
        std::vector<scorep::plugin::metric_property> properties;

        // Allow the user to prefix the metric with hdeem/ or not.
        // Internally we use the short version without the prefix.
        // In the trace it will always be full_name with hdeem/[blade,vr]
        if (metric_name.find(prefix) == 0)
        {
            metric_name = metric_name.substr(prefix.length());
        }

        scorep::plugin::util::matcher match(metric_name);
        for (auto sensor : hdeem->sensors()) {
            const auto& sensor_name = hdeem->sensor_name(sensor);
            if (match(sensor_name)) {
                properties.push_back(add_metric_property(sensor_name, sensor));
            }
        }

        logging::debug() << "get_event_info(" << metric_name << ") returning " << properties.size() << " properties";
        return properties;
    }

private:
    const std::string prefix = "hdeem/";
    scorep::plugin::metric_property add_metric_property(const std::string& name, hdeem::sensor_id sensor)
    {
        const std::string full_name = prefix + name;

        auto& handle = make_handle(full_name, full_name, name, sensor);
        logging::trace() << "registered handle: " << handle;
        return scorep::plugin::metric_property(full_name, "power consumption", "W").absolute_point().value_double();
    }

    scorep::chrono::ticks scorep_start, scorep_stop;
    std::chrono::time_point<local_clock> local_start, local_stop;

    enum class timer_t
    {
        NODE,
        BMC,
        SYNC
    };
    timer_t timer;

    std::unique_ptr<hdeem::connection> hdeem;
    bool started = false;
    bool stopped = false;
    std::unique_ptr<hdeem::sensor_data> readings;

    // Time is calibrated by Blade start/stop time, so we need to shift vr time a little bit...
    // TODO implement separate vr time scaling
    std::chrono::duration<double> time_offset_blade{ 0 * 1.0e-6 };
    std::chrono::duration<double> time_offset_vr{ -(35000 - 5600) * 1.0e-6 };
};

SCOREP_METRIC_PLUGIN_CLASS(hdeem_plugin, "hdeem")
