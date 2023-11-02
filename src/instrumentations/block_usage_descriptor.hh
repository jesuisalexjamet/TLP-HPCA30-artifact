#ifndef __ANALYSIS_BLOCK_USAGE_DESCRIPTOR_HH__
#define __ANALYSIS_BLOCK_USAGE_DESCRIPTOR_HH__

#include <cstdint>
#include <fstream>

/**
 * @brief
 */
class block_usage_descriptor {
  public:
    block_usage_descriptor ();
    block_usage_descriptor (const block_usage_descriptor&);
    block_usage_descriptor (block_usage_descriptor&&);

    block_usage_descriptor& operator= (const block_usage_descriptor&);
    block_usage_descriptor& operator= (block_usage_descriptor&&);

    bool& cache_hit ();
    const bool& cache_hit () const;

    uint32_t& reuses ();
    const uint32_t& reuses () const;

    uint64_t& paddr ();
    const uint64_t& paddr () const;

    uint64_t& vaddr ();
    const uint64_t& vaddr () const;

    uint64_t& ip ();
    const uint64_t& ip () const;

    uint64_t& stack_distance ();
    const uint64_t& stack_distance () const;

    bool operator== (const uint64_t&) const;
    bool operator== (const block_usage_descriptor&) const;
    bool operator!= (const block_usage_descriptor&) const;

  private:
    bool                  _cache_hit;       /*!< Did that usage of the block led to a cache hit? */
    uint32_t              _reuses;
    uint64_t              _paddr;           /*!< What was physical address of the block? */
    uint64_t              _vaddr;           /*!< What was the virtual address? */
    uint64_t              _ip;              /*!< The PC used for this access. */
    uint64_t              _stack_distance;  /*!< What is the current stack distance of this block? */
};

std::fstream& operator<< (std::fstream &, const block_usage_descriptor &);

#endif // __ANALYSIS_BLOCK_USAGE_DESCRIPTOR_HH__
