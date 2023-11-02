#include <iostream>
#
#include <internals/instruction.h>

using namespace champsim::cpu;

std::fstream& operator>> (std::fstream& is, trace_header& th) {
	std::size_t pairs = 0;

	is.read (reinterpret_cast<char *> (&pairs), sizeof (std::size_t));

	for (std::size_t i = 0; i < pairs; i++) {
		trace_header::irreg_array_boundaries p;

		// Reading elements of the pair.
		is.read (reinterpret_cast<char *> (&p.first), sizeof (trace_header::irreg_array_boundaries::first_type));
		is.read (reinterpret_cast<char *> (&p.second), sizeof (trace_header::irreg_array_boundaries::second_type));

		th.irreg_arrays.push_back (p);
	}

	return is;
}
std::fstream& operator<< (std::fstream& os, const trace_header& th) {
	// Writing the whole thing in the provided file stream.
	os.write ((const char*) (&th), sizeof (trace_header));

	return os;
}

std::fstream& operator<< (std::fstream& os, const x86_trace_instruction& cti) {
	// Writing the whole thing in the provided file stream.
	os.write ((const char*) (&cti), sizeof (x86_trace_instruction));

	return os;
}

std::fstream& operator>> (std::fstream& is, x86_trace_instruction& ti) {
	is.read ((char*) (&ti), sizeof (x86_trace_instruction));

	return is;
}
