#include <utility>
#
#include <instrumentations/cache_utilization.hh>

using namespace champsim::instrumentations;

usage_tracker::usage_tracker (std::size_t sets, std::size_t ways) :
	_usage_bitmap (sets, set_usage_bitmap (ways)) {

}

usage_tracker::usage_tracker (const usage_tracker& o) :
	_usage_bitmap (o._usage_bitmap),
	_counters (o._counters) {

}

usage_tracker::usage_tracker (usage_tracker&& o) :
	_usage_bitmap (std::move (o._usage_bitmap)),
	_counters (std::move (o._counters)) {

}

usage_tracker::~usage_tracker () {

}

usage_tracker& usage_tracker::operator= (const usage_tracker& o) {
	this->_usage_bitmap = o._usage_bitmap;
	this->_counters = o._counters;

	return *this;
}

usage_tracker& usage_tracker::operator= (usage_tracker&& o) {
	this->_usage_bitmap = std::move (o._usage_bitmap);
	this->_counters = std::move (o._counters);

	return *this;
}

usage_tracker::usage_bitmap& usage_tracker::bitmap (const std::tuple<std::size_t, std::size_t>& coords) {
	return this->_usage_bitmap[std::get<0> (coords)][std::get<1> (coords)];
}

void usage_tracker::set (const std::tuple<std::size_t, std::size_t, std::size_t>& coords,
						 const std::size_t &size) {
	std::size_t set 	= std::get<0> (coords),
				way 	= std::get<1> (coords),
				offset 	= std::get<2> (coords),
				classes	= 0x0;

	for (std::size_t i = 0, curr_offset = offset; i < size; i++) {
		// Computing the appropirate class.
		classes = (usage_tracker::classes == BLOCK_SIZE ? curr_offset : (curr_offset / usage_tracker::word_granularity));

		// Setting the appropriate bits in the bitmap.
		this->bitmap({set, way}).set (classes);

		// Updating the current offset.
		curr_offset = ((curr_offset + 1) % BLOCK_SIZE);
	}
}

const std::map<uint8_t, uint64_t>& usage_tracker::counters () const {
	return this->_counters;
}

std::map<uint8_t, uint64_t>& usage_tracker::counters () {
	return this->_counters;
}
