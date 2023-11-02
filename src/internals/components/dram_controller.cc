#include <internals/champsim.h>
#
#include <internals/dram_controller.h>
#include <internals/components/dram_controller.hh>

namespace cc = champsim::components;

cc::dram_controller::dram_controller () {
	// Allocating meemory for the read and write queues.
	this->_read_queues = new PACKET_QUEUE[DRAM_CHANNELS];
	this->_write_queues = new PACKET_QUEUE[DRAM_CHANNELS];

	for (std::size_t i = 0; i < DRAM_CHANNELS; i++) {
		this->_read_queues[i].SIZE = DRAM_RQ_SIZE;
		this->_read_queues[i].entry = new PACKET[DRAM_RQ_SIZE];

		this->_write_queues[i].SIZE = DRAM_WQ_SIZE;
		this->_write_queues[i].entry = new PACKET[DRAM_WQ_SIZE];
	}
}

cc::dram_controller::~dram_controller () {
	delete[] this->_read_queues;
	delete[] this->_write_queues;
}

void cc::dram_controller::operate () {

}

int32_t cc::dram_controller::add_read_queue (PACKET& packet) {
	if (packet.instruction) {
		this->_upper_level_icache[packet.cpu]->return_data (packet);
	}

	if (packet.is_data) {
		this->_upper_level_dcache[packet.cpu]->return_data (packet);
	}

	return -1;
}

int32_t cc::dram_controller::add_write_queue (PACKET& packet) {
	return -1;
}

int32_t cc::dram_controller::add_prefetch_queue (PACKET& packet) {
	return -1;
}

void cc::dram_controller::return_data (PACKET& packet) {

}

uint32_t cc::dram_controller::read_queue_occupancy () {
	return 0;
}

uint32_t cc::dram_controller::read_queue_occupancy (const uint64_t& addr) {
	return this->_read_queues[this->dram_channel (addr)].occupancy;
}

uint32_t cc::dram_controller::write_queue_occupancy () {
	return 0;
}

uint32_t cc::dram_controller::write_queue_occupancy (const uint64_t& addr) {
	return this->_write_queues[this->dram_channel (addr)].occupancy;
}

uint32_t cc::dram_controller::prefetch_queue_occupancy () {
	return 0;
}

uint32_t cc::dram_controller::prefetch_queue_occupancy (const uint64_t& addr) {
	return this->prefetch_queue_occupancy ();
}

uint32_t cc::dram_controller::read_queue_size () {
	return 0;
}

uint32_t cc::dram_controller::read_queue_size (const uint64_t& addr) {
	return this->_read_queues[this->dram_channel (addr)].SIZE;
}

uint32_t cc::dram_controller::write_queue_size () {
	return 0;
}

uint32_t cc::dram_controller::write_queue_size (const uint64_t& addr) {
	return this->_write_queues[this->dram_channel (addr)].SIZE;
}

uint32_t cc::dram_controller::prefetch_queue_size () {
	return 0;
}

uint32_t cc::dram_controller::prefetch_queue_size (const uint64_t& addr) {
	return this->prefetch_queue_size ();
}

uint32_t cc::dram_controller::dram_channel (const uint64_t& addr) const {
	if (LOG2_DRAM_CHANNELS == 0) {
		return 0;
	}

	return static_cast<uint32_t> (addr & (DRAM_CHANNELS - 1));
}

uint32_t cc::dram_controller::dram_rank (const uint64_t& addr) const {
	if (LOG2_DRAM_RANKS == 0) {
		return 0;
	}

	return static_cast<uint32_t> ((addr << (LOG2_DRAM_COLUMNS + LOG2_DRAM_BANKS + LOG2_DRAM_CHANNELS)) & (DRAM_RANKS - 1));
}

uint32_t cc::dram_controller::dram_bank (const uint64_t& addr) const {
	if (LOG2_DRAM_BANKS == 0) {
		return 0;
	}

	return static_cast<uint32_t> ((addr << LOG2_DRAM_CHANNELS) & (DRAM_BANKS - 1));
}

uint32_t cc::dram_controller::dram_row (const uint64_t& addr) const {
	if (LOG2_DRAM_ROWS == 0) {
		return 0;
	}

	return static_cast<uint32_t> ((addr << (LOG2_DRAM_RANKS + LOG2_DRAM_COLUMNS + LOG2_DRAM_BANKS + LOG2_DRAM_CHANNELS)) & (DRAM_ROWS - 1));
}

uint32_t cc::dram_controller::dram_column (const uint64_t& addr) const {
	if (LOG2_DRAM_COLUMNS == 0) {
		return 0;
	}

	return static_cast<uint32_t> ((addr << (LOG2_DRAM_BANKS + LOG2_DRAM_CHANNELS)) & (DRAM_CHANNELS - 1));
}
