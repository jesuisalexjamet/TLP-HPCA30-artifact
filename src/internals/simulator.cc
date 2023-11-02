#include <algorithm>
#include <iostream>
#
#include <boost/property_tree/json_parser.hpp>
#
#include <internals/simulator.hh>

using namespace champsim;

// Initializing the unique instance of the simulator.
simulator* simulator::_inst = nullptr;

/**
 * @brief Constructor of the Simulator class.
 */
simulator::simulator()
    : _curr_state(simulator::instanciated), _desc("ChampSim") {
    // Preparing the option parsing utilities.
    this->_init_options_descriptor();
}

/**
 * @brief Destructor of the Simulator class.
 */
simulator::~simulator() {
    for (auto& e : this->_modeled_cpus) {
        delete std::get<0>(e);
        delete std::get<1>(e);
    }
}

/**
 * @brief Parse the arguments through the command line. Based on the provided
 * arguments, the simulator initializes the simulation descriptor responsible of
 * representing the computer system to be simulated.
 * @param argc The size of argv, an array of strings containing the command line
 * arguments.
 * @param argv An array of size argc containing the command line arguments.
 */
void simulator::parse_args(int argc, char** argv) {
    // Parsing the command line arguments.
    po::variables_map vm;

    po::store(po::parse_command_line(argc, argv, this->_desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << this->_desc << std::endl;
        std::exit(0);
    }

    if (!vm.count("config")) {
        throw std::runtime_error("No configuration file provided.");
    }

    if (!vm.count("warmup_instructions")) {
        throw std::runtime_error("No warmup instructions specified.");
    }

    if (!vm.count("simulation_instructions")) {
        throw std::runtime_error("No simulation instructions specified.");
    }

    // Checking traces.
    if (!vm.count("traces")) {
        throw std::runtime_error("No traces specified.");
    } else {
        // Now that we have the traces, we can fill the CPU descriptors.
        for (const auto& e : this->_traces) {
            cpu::cpu_descriptor curr_desc;
            curr_desc.trace_file = e;

            curr_desc.warmup_instructions = this->_sim_desc.warmup_instructions;
            curr_desc.simulation_instructions =
                this->_sim_desc.simulation_instructions;

            curr_desc.fetch_width = FETCH_WIDTH;
            curr_desc.decode_width = DECODE_WIDTH;
            curr_desc.exec_width = EXEC_WIDTH;
            curr_desc.load_queue_width = LQ_WIDTH;
            curr_desc.store_queue_width = SQ_WIDTH;
            curr_desc.retire_width = RETIRE_WIDTH;
            curr_desc.scheduler_size = SCHEDULER_SIZE;
            curr_desc.branch_misprediction_penalty = BRANCH_MISPREDICT_PENALTY;

            this->_sim_desc.cpus.push_back(curr_desc);
        }
    }

    // Switching the current state.
    this->_curr_state = config_obtained;
}

/**
 * @brief Tries to initialize the simulator.
 * @pre The current state of the simulator should be config_obtained.
 */
void simulator::initialize() {
    // Checking the precondition. If the precondition is not met, we should
    // throw an excpetion to signal a problem.
    if (this->_curr_state != simulator::config_obtained) {
        // TODO: Throw an exception.
    }

    this->_initialize(this->_sim_desc);

    // Switching the current state.
    this->_curr_state = initialized;
}

const champsim::computer_descriptor& champsim::simulator::descriptor() const {
    return this->_sim_desc;
}

const std::vector<std::string>& champsim::simulator::traces() const {
    return this->_traces;
}

O3_CPU* champsim::simulator::modeled_cpu(const std::size_t& idx) const {
    return std::get<0>(this->_modeled_cpus.at(idx));
}

void champsim::simulator::start_warmup() {
    // Marking the time of the beginning of the simulation.
    this->_begin_time = std::chrono::system_clock::now();

    // Changing the state of the simulator.
    this->_curr_state = warmup;
}

void champsim::simulator::start_simulation() {
    // Changing the state of the simulator.
    this->_curr_state = simulation;
}

/**
 * @brief Returns whether or not all cores have finished their warmup phases.
 *
 * @return true All cores have finished their warmup phases.
 * @return false Not all cores have finished their warmup phases.
 */
bool champsim::simulator::all_warmup_complete() const {
    return std::all_of(this->_modeled_cpus.cbegin(), this->_modeled_cpus.cend(),
                       [](const simple_cpu_model& e) -> bool {
                           return std::get<0>(e)->warmup_complete();
                       });
}

/**
 * @brief Return whether or not all cores have finished their simulation phases.
 *
 * @return true All cores have finished their simulation phases.
 * @return false Not all cores have finished their simulation phases.
 */
bool champsim::simulator::all_simulation_complete() const {
    return std::all_of(this->_modeled_cpus.cbegin(), this->_modeled_cpus.cend(),
                       [](const simple_cpu_model& e) -> bool {
                           return std::get<0>(e)->simulation_complete();
                       });
}

/**
 * @brief Returns the current state of the simulator.
 *
 * @return const champsim::simulator::inner_states& A constant reference to the
 * current state of the simulator.
 */
const champsim::simulator::inner_states& champsim::simulator::state() const {
    return this->_curr_state;
}

/**
 * @brief Returns the configuration of the Hermes strategy.
 *
 * @return An instance of a structure containing all the Hermes-related knobs.
 */
const champsim::simulator::hermes_configuration&
champsim::simulator::hermes_knobs() const {
    return this->_hermes_knobs;
}

const std::string& champsim::simulator::memory_trace_directory() const {
    return this->_memory_trace_dir;
}

const std::chrono::time_point<std::chrono::system_clock>&
champsim::simulator::begin_time() const {
    return this->_begin_time;
}

/**
 * @brief Returns the unique instance of the simulator class.
 */
simulator* simulator::instance() {
    if (!simulator::_inst) simulator::_inst = new simulator();

    return simulator::_inst;
}

/**
 * @brief Destroys the unique instance of the simulator class.
 */
void simulator::destroy() {
    if (!simulator::_inst) return;

    delete simulator::_inst;
}

/**
 * @brief Initialize the command line options descriptor responsible of
 * describing the options to be parsed.
 */
void simulator::_init_options_descriptor() {
    this->_desc.add_options()("help", "Produce help message and quit.")(
        "config", po::value<std::string>(&this->_config_file),
        "Simulator configuration file")(
        "warmup_instructions",
        po::value<uint32_t>(&this->_sim_desc.warmup_instructions),
        "")("simulation_instructions",
            po::value<uint32_t>(&this->_sim_desc.simulation_instructions), "")(
        "traces", po::value<std::vector<std::string>>(&this->_traces), "");
}

/**
 * @brief Initialize the simulation components based on the given descriptor.
 * @param A descriptor describing the components required for the simulation.
 */
void simulator::_initialize(const computer_descriptor& desc) {
    O3_CPU* curr_cpu = nullptr;
    base_instruction_reader* curr_instr_reader = nullptr;

    // First, let's start by loading the provided config file.
    pt::read_json(this->_config_file, this->_config);

    for (const auto& e : desc.cpus) {
        // Allocating components used to model the CPU.
        curr_cpu = new O3_CPU();

        if (desc.legacy_traces) {
            curr_instr_reader =
                new instruction_reader<cpu::x86_legacy_trace_instruction>(
                    e.trace_file);
        } else {
            curr_instr_reader = new instruction_reader(e.trace_file);
        }

        // Initializing the modeled CPU.
        curr_cpu->initialize_core(e);

        // Inserting the modeled CPU in the array.
        this->_modeled_cpus.push_back(
            simple_cpu_model(curr_cpu, curr_instr_reader));
    }

    // Let's now read into the config file and fill descriptors accordingly.
    this->_sim_desc.llc_config_file =
        this->_config.get<std::string>("llc.config");
    this->_memory_trace_dir =
        this->_config.get<std::string>("dram.memory_trace_directory");

    // Iterating over the core configurations provided.
    pt::ptree cores_subtree = this->_config.get_child("cores");

    for (auto it = cores_subtree.begin(); it != cores_subtree.end(); it++) {
        std::size_t i = std::distance(cores_subtree.begin(), it);
        O3_CPU* curr_cpu = this->modeled_cpu(i);

        this->_sim_desc.cpus[i].l1d_config_file =
            it->second.get<std::string>("l1d.config");
        this->_sim_desc.cpus[i].l1i_config_file =
            it->second.get<std::string>("l1i.config");
        this->_sim_desc.cpus[i].l2c_config_file =
            it->second.get<std::string>("l2c.config");
        this->_sim_desc.cpus[i].sdc_config_file =
            it->second.get<std::string>("sdc.config");
        this->_sim_desc.cpus[i].sdc_enabled =
            it->second.get<bool>("sdc.enabled", false);
        this->_sim_desc.cpus[i].stride_threshold =
            it->second.get<uint64_t>("irregular_predictor.stride_threshold");
        this->_sim_desc.cpus[i].irreg_pred_sets =
            it->second.get<uint64_t>("irregular_predictor.sets");
        this->_sim_desc.cpus[i].irreg_pred_ways =
            it->second.get<uint64_t>("irregular_predictor.ways");
        this->_sim_desc.cpus[i].irreg_pred_stride_bits =
            it->second.get<uint8_t>("irregular_predictor.stride_bits");
        this->_sim_desc.cpus[i].psel_bits =
            it->second.get<uint8_t>("irregular_predictor.psel_bits");
        this->_sim_desc.cpus[i].lp_latency =
            it->second.get<uint8_t>("irregular_predictor.latency");

        this->_sim_desc.cpus[i].metadata_cache_sets =
            it->second.get<uint64_t>("metadata_cache.sets");
        this->_sim_desc.cpus[i].metadata_cache_ways =
            it->second.get<uint64_t>("metadata_cache.ways");

        this->_sim_desc.cpus[i].pld_threshold_1 =
            it->second.get<uint64_t>("popular_level_detector.threshold_1");
        this->_sim_desc.cpus[i].pld_threshold_2 =
            it->second.get<uint64_t>("popular_level_detector.threshold_2");

        // Updating the metadata cache of this core.
        curr_cpu->_mm = std::move(cc::metadata_cache(
            this->_sim_desc.cpus[i].metadata_cache_sets,
            this->_sim_desc.cpus[i].metadata_cache_ways, 512));

        curr_cpu->_mm.pld().thresholds[0] =
            this->_sim_desc.cpus[i].pld_threshold_1;
        curr_cpu->_mm.pld().thresholds[1] =
            this->_sim_desc.cpus[i].pld_threshold_2;

        curr_cpu->_mm.pbp() = cc::metadata_cache::pc_based_predictor(
            it->second.get<std::size_t>(
                "metadata_cache.pc_predictor.counters_bits"),
            it->second.get<std::size_t>("metadata_cache.pc_predictor.size"));

        curr_cpu->_mm.pbp()._threshold =
            it->second.get<int32_t>("metadata_cache.pc_predictor.threshold");
        curr_cpu->_mm.pbp()._high_conf_threshold = it->second.get<int32_t>(
            "metadata_cache.pc_predictor.high_conf_threshold");

        curr_cpu->_mm.set_cpu(curr_cpu);
        curr_cpu->_mm.set_miss_rate_thrshold(
            it->second.get<float>("metadata_cache.miss_rate_threshold"));

        // Updating the irregular accesses predictor of this core.
        curr_cpu->irreg_pred = std::move(
            cc::irreg_access_pred(this->_sim_desc.cpus[i].irreg_pred_sets,
                                  this->_sim_desc.cpus[i].irreg_pred_ways));
        // curr_cpu->irreg_pred.set_threshold
        // (this->_sim_desc.cpus[i].stride_threshold);
        curr_cpu->irreg_pred.set_stride_bits(
            this->_sim_desc.cpus[i].irreg_pred_stride_bits);
        curr_cpu->irreg_pred.set_psel_bits(this->_sim_desc.cpus[i].psel_bits);
        curr_cpu->irreg_pred.latency() = this->_sim_desc.cpus[i].lp_latency;

        // Retrieving the features of prefetch-specific offchip predictor.
        std::vector<uint32_t> pf_features;

        for (auto v_features :
             it->second.get_child("offchip_pred.prefetch.features")) {
            pf_features.push_back(v_features.second.get_value<uint32_t>());
        }

        curr_cpu->offchip_pred->set_pf_pred(
            it->second.get<float>("offchip_pred.prefetch.threshold"),
            pf_features);

        std::vector<uint32_t> features;

        for (auto v_features :
             it->second.get_child("offchip_pred.demand.features")) {
            features.push_back(v_features.second.get_value<uint32_t>());
        }

        curr_cpu->offchip_pred->set_pred(
            it->second.get<float>("offchip_pred.demand.tau_1"),
            it->second.get<float>("offchip_pred.demand.tau_2"), features);

        // Setting the CPU index for the offchip predictor.
        curr_cpu->offchip_pred->set_cpu(i);

        // Setting the PSEL (prefetch enable/disable) for the L1D.
        this->_sim_desc.cpus[i].l1d_prefetching_psel_bits =
            it->second.get<uint64_t>("l1d.psel_bits");
        this->_sim_desc.cpus[i].l1d_prefetching_psel_threshold =
            it->second.get<uint64_t>("l1d.psel_threshold");
    }

    // Loading Hermes-related knobs.
    this->_hermes_knobs.ddrp_request_latency =
        this->_config.get<uint8_t>("hermes.ddrp_request_latency");
}
