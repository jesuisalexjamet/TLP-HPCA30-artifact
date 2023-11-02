#ifndef __ANALYSIS_REUSE_TRACKER_HH__
#define __ANALYSIS_REUSE_TRACKER_HH__

#include <list>
#include <map>
#include <unordered_map>
#
#include <fstream>
#
#include <internals/ooo_cpu.h>
#include <internals/components/cache.hh>
#
#include <instrumentations/block_usage_descriptor.hh>

class reuse_tracker {
  public:
    using descriptor_list = std::list<block_usage_descriptor>;

    using iterator = descriptor_list::iterator;
    using const_iterator = descriptor_list::const_iterator;
    using reverse_iterator = descriptor_list::reverse_iterator;
    using const_reverse_iterator = descriptor_list::const_reverse_iterator;

    using cache_reuse_heat_map = std::unordered_map<uint32_t, int32_t>;

  public:
    struct accuracy_metrics {
    public:
      uint64_t never_reused;
      uint64_t true_positives;
      uint64_t true_negatives;
      uint64_t false_positives;
      uint64_t false_negatives;


      uint64_t cache_friendly;
      uint64_t cache_averse;
      uint64_t prediction_friendly;
      uint64_t prediction_averse;

      uint64_t minority_wrongs;
      uint64_t wrong_features;
      std::map<std::uint8_t, uint64_t> wrong_features_mass_func; /*<! Associate the number of wrong features to occurences. */
      float avg_wrong_features;
      float minory_wrong_rate;

      uint64_t minority_corrects;
      uint64_t correct_features;
      std::map<std::uint8_t, uint64_t> correct_features_mass_func; /*!< Associate the number of correct features to occurences. */
      float avg_correct_features;
      float minority_correct_rate;

      float sensitivity;
      float specificity;

      cache_reuse_heat_map reuse_heat_map;

    public:
      accuracy_metrics ();
    };

    using accuracy_metrics_map = std::map<uint64_t, accuracy_metrics>;

  public:
    reuse_tracker (const std::size_t& distance_limit, champsim::components::cache* cache);
    ~reuse_tracker ();

    accuracy_metrics& metrics ();
    const accuracy_metrics& metrics () const;

    accuracy_metrics_map& per_entry_metrics ();

    void add_usage (uint64_t vaddr, uint64_t paddr, uint64_t ip, bool cache_hit);

    std::size_t size () const;

    // Iterators.
    iterator begin () noexcept;
    const_iterator begin () const noexcept;
    const_iterator cbegin () const noexcept;

    iterator end () noexcept;
    const_iterator end () const noexcept;
    const_iterator cend () const noexcept;

    reverse_iterator rbegin () noexcept;
    const_reverse_iterator rbegin () const noexcept;
    const_reverse_iterator crbegin () const noexcept;

    reverse_iterator rend () noexcept;
    const_reverse_iterator rend () const noexcept;
    const_reverse_iterator crend () const noexcept;

		void wrap ();

    void update_metrics (block_usage_descriptor &);
    void update_metrics (block_usage_descriptor &, accuracy_metrics &);

  public:
    static uint64_t usages;

  private:
    descriptor_list _usages;
    accuracy_metrics _metrics;
    accuracy_metrics_map _per_entry_metrics;

    std::size_t _distance_limit;

    std::ofstream _report_file;
};

std::fstream& operator<< (std::fstream&, const reuse_tracker&);

#endif // __ANALYSIS_REUSE_TRACKER_HH__
