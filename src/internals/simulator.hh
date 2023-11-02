#ifndef __CHAMPSIM_INTERNALS_SIMULATOR_HPP__
#define __CHAMPSIM_INTERNALS_SIMULATOR_HPP__

#include <list>
#include <string>
#include <tuple>
#include <vector>
#
#include <chrono>
#
#include <boost/program_options.hpp>
#
#include <boost/property_tree/ptree.hpp>
#
#include <internals/ooo_cpu.h>

#include <internals/instruction_reader.hh>

namespace po = boost::program_options;
namespace pt = boost::property_tree;
namespace cpu = champsim::cpu;

namespace champsim {
/**
 * @brief This class provides a complete description of the simulated
 * system including the different CPUs along with the memory sub-system.
 */
struct computer_descriptor {
   public:
    std::vector<cpu::cpu_descriptor> cpus;

    std::string llc_config_file;

    bool legacy_traces;

    uint32_t warmup_instructions, simulation_instructions;
};

class simulator {
   public:
    enum inner_states {
        instanciated = 0x0,
        config_obtained = 0x1,
        initialized = 0x2,
        warmup = 0x3,
        simulation = 0x4,
    };

    struct hermes_configuration {
       public:
        uint8_t ddrp_request_latency;

       public:
        hermes_configuration() : ddrp_request_latency(0) {}
    };

   private:
    inner_states _curr_state;
    hermes_configuration _hermes_knobs;

    // Here are the options describing the simlation setup.
    std::string _config_file, _memory_trace_dir;
    pt::ptree _config;
    po::options_description _desc;

    // Here is the whole simulation description.
    computer_descriptor _sim_desc;
    std::vector<std::string> _traces;

    // Here are all the utilities needed to describe the modeled CPUs and theirs
    // inputs.
    using simple_cpu_model = std::tuple<O3_CPU*, base_instruction_reader*>;
    std::vector<simple_cpu_model> _modeled_cpus;

    // Timing info.
    std::chrono::time_point<std::chrono::system_clock> _begin_time;

    static simulator* _inst;

   public:
    simulator();
    ~simulator();

    void parse_args(int argc, char** argv);

    void initialize();

    const computer_descriptor& descriptor() const;
    const std::vector<std::string>& traces() const;

    O3_CPU* modeled_cpu(const std::size_t& idx) const;

    void start_warmup();
    void start_simulation();

    bool all_warmup_complete() const;
    bool all_simulation_complete() const;

    const inner_states& state() const;

    const hermes_configuration& hermes_knobs() const;

    const std::string& memory_trace_directory() const;

    // Accessing timing info.
    const std::chrono::time_point<std::chrono::system_clock>& begin_time()
        const;

    static simulator* instance();
    static void destroy();

   private:
    simulator(const simulator&) = delete;
    simulator(simulator&&) = delete;

    simulator& operator=(const simulator&) = delete;
    simulator& operator=(simulator&&) = delete;

    void _init_options_descriptor();

    void _initialize(const computer_descriptor& desc);
};
}  // namespace champsim

#endif  // __CHAMPSIM_INTERNALS_SIMULATOR_HPP__
