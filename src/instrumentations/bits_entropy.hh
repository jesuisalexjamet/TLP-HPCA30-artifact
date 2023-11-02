#ifndef __CHAMPSIM_INSTRUMENTATIONS_BITS_ENTROPY_HH__
#define __CHAMPSIM_INSTRUMENTATIONS_BITS_ENTROPY_HH__

#include <cstdint>
#
#include <cmath>
#include <numeric>
#
#include <array>
#include <list>
#include <vector>

namespace champsim {
	namespace instrumentations {
		template <typename T>
		struct bits_entropy {
		public:
			static constexpr std::size_t states = 2;
			static constexpr std::size_t byte_size = 8;
			static constexpr std::size_t length = sizeof(T) * byte_size;

		private:
			std::size_t _begin;
			std::size_t _end;

		public:
			bits_entropy (const std::size_t& begin, const std::size_t& end) :
			 	_begin (begin), _end (end) {

			};

			const std::vector<float> operator() (const std::list<T>& addr_dir) const {
				std::vector<float> r (this->_end - this->_begin, 0.0f);

				for (std::size_t i = this->_begin; i < this->_end; i++) {
					std::list<bool> bits;
					uint64_t mask = (1ULL << i);

					// Building the bits array for entropy of the i-th bit.
					for (const T& e: addr_dir) {
						bool v = ((e & mask) >> i);
						bits.push_back (static_cast<bool> (v));
					}

					r[i - this->_begin] = _single_bit_entropy (bits);
				}

				return r;
			}

		private:
			float _single_bit_entropy (const std::list<bool>& bits) const {
				float h = 0.0f, total = 0.0f;
				std::array<float, states> freq_table = { 0.0f };

				// Computing frequency table.
				for (const bool& e: bits) {
					freq_table[static_cast<int> (e)]++;
					total++;
				}

				// Computing frequency-based probabilities.
				for (std::size_t i = 0; i < states; i++) {
					freq_table[i] /= total;
				}

				h = std::accumulate (freq_table.begin (), freq_table.end (), h,
									 [] (const float&a, const float& b) -> float {
										 return (b != 0.0f ? (a + (b * std::log2 (b))) : a);
									 });

				return -h;
			}
		};
	}
}

#endif // __CHAMPSIM_INSTRUMENTATIONS_BITS_ENTROPY_HH__
