#include <algorithm>
#include <iterator>
#
#include <instrumentations/memory_region.hh>

using namespace champsim::instrumentations;

memory_region::memory_region () :
	_begin (UINT64_MAX), _end (0x0), _entropy_comp (19, 47) {

}

void memory_region::record_access (const uint64_t& paddr) {
	this->_begin = std::min (this->_begin, paddr);
	this->_end = std::max (this->_end, paddr);

	// Adding the provided address to the recording directory.
	this->_addr_dir.push_back (paddr);
}

uint64_t& memory_region::begin () {
	return this->_begin;
}

const uint64_t& memory_region::begin () const {
	return this->_begin;
}

uint64_t& memory_region::end () {
	return this->_end;
}

const uint64_t& memory_region::end () const {
	return this->_end;
}

uint64_t memory_region::mask () const {
	uint64_t r = *this->_addr_dir.begin ();

	for (auto it = std::next (this->_addr_dir.begin ());
		 it != this->_addr_dir.end ();
		 std::advance (it, 1)) {
		r |= *it;
	}

	return r;
}

const std::vector<float> memory_region::entropy () const {
	return this->_entropy_comp (this->_addr_dir);
}

uint64_t memory_region::size () const {
	return (this->_end - this->_begin);
}
