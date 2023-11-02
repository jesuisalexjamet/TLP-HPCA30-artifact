#include <cstdio>
#
#include <iostream>
#include <fstream>
#include <string>
#
#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#
#include <internals/instruction.h>
#
#include "trace_utils.hh"

namespace po = boost::program_options;
using boost::format;

static po::options_description prog_opt;
static std::string champsim_trace,
				   topt_trace;
static irreg_arrays_t irreg_arrays;
static FILE* in_trace = NULL;
static std::ofstream out_trace;

void initialize_program_options (po::options_description& desc) {
	desc.add_options ()
		("help", "Produce an help message and quit.")
		("in_trace", po::value<std::string> (&champsim_trace), "The ChampSim trace to go through.")
		("out_trace", po::value<std::string> (&topt_trace), "The output trace that will be used by the T-OPT replacement policy.");
}

void parse_program_options (const po::options_description& desc, int argc, const char** argv) {
	po::variables_map vm;

	po::store (po::parse_command_line (argc, argv, desc), vm);
	po::notify (vm);

	if (vm.count ("help")) {
		std::cout << desc << std::endl;
		std::exit (0);
	}

	if (!vm.count ("in_trace")) {
		throw std::runtime_error ("[ERROR] No input trace provided.");
	}

	if (!vm.count("out_trace")) {
		throw std::runtime_error ("[ERROR] No output trace provided.");
	}

	// Now that we have obtained the program options, we can runsome checks in order to know if we can run the tracer.
	boost::filesystem::path in_trace_path (champsim_trace),
							ext (in_trace_path.extension ());

	if (!boost::filesystem::exists (in_trace_path)) {
		throw std::runtime_error ("[ERROR] ChampSim trace file not found.");
	}

	if (ext != ".xz") {
		throw std::runtime_error ("[ERROR] T-OPT tracer only supports .xz trace files.");
	}
}

void inittialize_champsim_trace (const std::string& trace_path, irreg_arrays_t& arrays, FILE** fs) {
	std::size_t pairs = 0;
	champsim::cpu::trace_header head;
	std::string cmd = (format ("xz -dc %1s") % trace_path).str ();

	// Opening the trace file using the command defined above.
	if ((*fs = popen (cmd.c_str (), "r")) == NULL) {
		throw std::runtime_error ("[ERROR] Unexpected error on trace opening.");
	}

	// Now that the trace has been openned, let's look into it for irreg array boundaries.
	fread (reinterpret_cast<char *> (&pairs), sizeof (std::size_t), 1, *fs);

	std::cout << format ("Found %1d irregular arrays available in the trace.") % pairs << std::endl;

	for (std::size_t i = 0; i < pairs; i++) {
		uint64_t curr_pair[2] = { 0ULL, 0ULL };

		fread (reinterpret_cast<char *> (curr_pair), sizeof (uint64_t), 2, *fs);

		arrays.push_back (std::make_pair (curr_pair[0], curr_pair[1]));

		std::cout << format ("Boundaries on pair %1d are %#2x to %#3x.") % i % curr_pair[0] % curr_pair[1] << std::endl;
	}
}

void initialize_topt_trace (const std::string& trace_path, std::ofstream& ofs) {
	// First we check if there is already such a file. If yes, we must fail.
	boost::filesystem::path out_trace_path (trace_path);

	if (boost::filesystem::exists (out_trace_path)) {
		throw std::runtime_error ((format ("[ERROR] Cannot override %1s") % trace_path).str ());
	}

	// The file doesn't exist so we can open it and write in it safely.
	ofs.open (trace_path, std::ios::out | std::ios::binary);

	if (ofs.fail ()) {
		throw std::runtime_error ("[ERROR] An error occured while opening the output trace.");
	}
}

int main (int argc, const char** argv) {
	// Initializing program opptions descriptor.
	initialize_program_options (prog_opt);

	try {
		parse_program_options (prog_opt, argc, argv);

		inittialize_champsim_trace (champsim_trace, irreg_arrays, &in_trace);
		initialize_topt_trace (topt_trace, out_trace);

		traverse_trace (in_trace, out_trace, irreg_arrays);
	} catch (const std::runtime_error& e) {
		std::cerr << e.what () << std::endl;
		std::exit (1);
	}

	return 0;
}
