#include <utility>
#
#include <instrumentations/block_usage_descriptor.hh>

block_usage_descriptor::block_usage_descriptor () :
  _stack_distance (0x0), _reuses (0x0) {

}

block_usage_descriptor::block_usage_descriptor (const block_usage_descriptor& o) :
  _cache_hit (o._cache_hit),
  _paddr (o._paddr),
  _vaddr (o._vaddr),
  _ip (o._ip),
  _stack_distance (o._stack_distance) {

}

block_usage_descriptor::block_usage_descriptor (block_usage_descriptor&& o) :
  _cache_hit (std::move (o._cache_hit)),
  _paddr (std::move (o._paddr)),
  _vaddr (std::move (o._vaddr)),
  _ip (std::move (o._ip)),
  _stack_distance (std::move (o._stack_distance)) {

}

block_usage_descriptor& block_usage_descriptor::operator= (const block_usage_descriptor& o) {
  this->_cache_hit = o._cache_hit;
  this->_paddr = o._paddr;
  this->_vaddr = o._vaddr;
  this->_ip = o._ip;
  this->_stack_distance = o._stack_distance;

  return *this;
}

block_usage_descriptor& block_usage_descriptor::operator= (block_usage_descriptor&& o) {
  this->_cache_hit = std::move (o._cache_hit);
  this->_paddr = std::move (o._paddr);
  this->_vaddr = std::move (o._vaddr);
  this->_ip = std::move (o._ip);
  this->_stack_distance = std::move (o._stack_distance);

  return *this;
}

bool& block_usage_descriptor::cache_hit () {
  return this->_cache_hit;
}

const bool& block_usage_descriptor::cache_hit () const {
  return this->_cache_hit;
}

uint32_t& block_usage_descriptor::reuses () {
    return this->_reuses;
}

const uint32_t& block_usage_descriptor::reuses () const {
    return this->_reuses;
}

uint64_t& block_usage_descriptor::paddr () {
  return this->_paddr;
}

const uint64_t& block_usage_descriptor::paddr () const {
  return this->_paddr;
}

uint64_t& block_usage_descriptor::vaddr () {
    return this->_vaddr;
}

const uint64_t& block_usage_descriptor::vaddr () const {
    return this->_vaddr;
}

uint64_t& block_usage_descriptor::ip () {
  return this->_ip;
}

const uint64_t& block_usage_descriptor::ip () const {
  return this->_ip;
}

uint64_t& block_usage_descriptor::stack_distance () {
  return this->_stack_distance;
}

const uint64_t& block_usage_descriptor::stack_distance () const {
  return this->_stack_distance;
}

bool block_usage_descriptor::operator== (const uint64_t& o) const {
  return (this->_paddr == o);
}

bool block_usage_descriptor::operator== (const block_usage_descriptor& o) const {
  return (this->_paddr == o._paddr);
}

bool block_usage_descriptor::operator!= (const block_usage_descriptor& o) const {
  return !(*this == o);
}

std::fstream& operator<< (std::fstream &s, const block_usage_descriptor &b) {
	s << b.stack_distance ()
		<< ";"
		<< b.cache_hit ();

	return s;
}
