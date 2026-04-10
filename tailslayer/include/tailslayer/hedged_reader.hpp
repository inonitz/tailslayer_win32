#ifndef TAILSLAYER_HEDGED_READER_HPP
#define TAILSLAYER_HEDGED_READER_HPP
#include <util2/C/platform.h>
#include <util2/C/macro.h>
#include <util2/C/sleep.h>
#include <util2/C/thread_sleep.h>
#include <util2/C/print.h>
#include <array>
#include <thread>
#include <cstdint>
#include <cassert>
#include <cstring>





namespace tailslayer {


inline constexpr int kDEFAULT_CHANNEL_OFFSET = 256; 
inline constexpr int kDEFAULT_CHANNEL_BIT = 8; 
inline constexpr int kDEFAULT_NUM_CHANNELS = 2;
inline constexpr size_t kDEFAULT_NUM_REPLICAS = 2;
inline constexpr size_t kHUGEPAGE_SIZE = 1 << 30;

inline constexpr int kCORE_MEAS_A = 11;
inline constexpr int kCORE_MEAS_B = 12;
inline constexpr int kCORE_MAIN   = 14;







static inline int pin_to_core(int core_id) {
    return utilities::pin_to_core(core_id);
}


// This lets the caller pass arguments to their worker functions
template <auto... Vals>
struct ArgList {};


/*
    Template parameters:
    1. The type of the values to insert and read
    2. The "timer" function that waits for an independent signal and returns the target index to read
    3. The function that gets executed with the value once it's been read
*/
template<
    typename    T, 
    auto        wait_work, 
    auto        final_work, 
    typename    WaitArgs = ArgList<>, 
    typename    WorkArgs = ArgList<>, 
    size_t      N        = kDEFAULT_NUM_REPLICAS
> class HedgedReader;

template <
    typename    T, 
    auto        wait_work, 
    auto        final_work, 
    auto...     WaitArgs, 
    auto...     WorkArgs, 
    size_t N
>
class HedgedReader<T, wait_work, final_work, ArgList<WaitArgs...>, ArgList<WorkArgs...>, N> 
{
public:
    HedgedReader(
        int    channel_offset = kDEFAULT_CHANNEL_OFFSET, 
        int    channel_bit    = kDEFAULT_CHANNEL_BIT,
        size_t num_channels   = kDEFAULT_NUM_CHANNELS
    ) :    
        m_channel_offset{channel_offset},
        m_channel_bit{channel_bit},
        m_num_channels{num_channels},
        m_logical_index{0},
        m_replica_page{nullptr}
    {
        assert(m_channel_offset % sizeof(T) == 0 && "Channel offset must be a multiple of sizeof(T)");
        assert(N <= m_num_channels && "Can't have more replicas than memory channels");

        size_t elements_per_chunk = m_channel_offset / sizeof(T);
        size_t stride_bytes       = m_num_channels * m_channel_offset;
        size_t max_strides        = kHUGEPAGE_SIZE / stride_bytes;

        // Precompute all these so they're not getting computed in the hot path
        m_capacity           = max_strides * elements_per_chunk;
        m_chunk_shift        = __builtin_ctzll(elements_per_chunk); // counts trailing zeros to get the shift amount
        m_chunk_mask         = elements_per_chunk - 1;
        m_stride_in_elements = (m_num_channels * m_channel_offset) / sizeof(T);


        setup_replica_cores();
        assert(setup_memory() && "HedgedReader Couldn't Setup Memory Region\n");
        return;
    }

    ~HedgedReader() {
        for (auto& worker : m_workers) {
            if (worker.joinable()) { worker.join(); }
        }
        utilities::freeHugePages(m_replica_page, kHUGEPAGE_SIZE);
        m_replica_page = nullptr;
        return;
    }



    size_t size()     const noexcept { return m_logical_index; }
    size_t capacity() const noexcept { return m_capacity; }
    

    void insert(T val) {
        assert(utilities::enabledLockMemoryPrivileges && "Memory Must Not be swapped out to disk\n");
        assert(m_replica_page != nullptr && "Memory Allocation Must be successful\n");
        assert(m_logical_index + 1 < m_capacity && "Tried to insert out of bounds");


        for (size_t i = 0; i < N; ++i) {
            // We have to keep accounting for making sure we're on different channels
            //  especially when we exceed the channel offset size
            T* target_addr = get_next_logical_index_address(i, m_logical_index);
            //std::cout << "Storing value: " << +val << " at address: " << (void*) target_addr << "\n";
            *target_addr = val;
        }
        ++m_logical_index;
    }


    void start_workers() {
        for (size_t i = 0; i < N; ++i) {
            m_workers[i] = std::thread(&HedgedReader::worker_func, this, i);
        }
        /* 
            10ms delay to make sure the workers are started
            If you don't do this, it freezes because the workers can't get to their cores 
        */
        microsleep(10000);
        return;
    }


private:
    int    m_channel_bit;
    int    m_channel_offset;
    size_t m_num_channels;
    void*  m_replica_page;
    size_t m_logical_index; // Internal counter to figure out next place to insert the next value
    size_t m_capacity;
    size_t m_chunk_shift;
    size_t m_chunk_mask;
    size_t m_stride_in_elements;
    std::array<T*, N>          m_replicas{};
    std::array<int, N>         m_cores{};
    std::array<std::thread, N> m_workers{};


    void worker_func(size_t worker_idx) {
        pin_to_core(m_cores[worker_idx]);

        size_t read_index = wait_work(WaitArgs...);

        // Only for quick sanity check benchmark counting cycles
        // T* flush_addr = get_next_logical_index_address(worker_idx, read_index);
        // detail::clflush_addr(flush_addr);
        // detail::mfence_inst();
        // std::uint64_t t0 = detail::rdtsc_lfence();

        T* target_addr = get_next_logical_index_address(worker_idx, read_index);

        // The actual read of the data
        // Passed directly to the inline worker function for processing
        final_work(*target_addr, WorkArgs...);

        // std::uint64_t t1 = detail::rdtscp_lfence();
        // std::cout << "\nRunning time: " << t1 - t0 << " cycles\n";
    }


    __force_inline inline T* get_next_logical_index_address(
        size_t replica_idx,
        size_t logical_index
    ) const {
        size_t chunk_idx = logical_index >> m_chunk_shift; 
        size_t offset_in_chunk = logical_index & m_chunk_mask;
        size_t element_offset = (chunk_idx * m_stride_in_elements) + offset_in_chunk;
        
        return m_replicas[replica_idx] + element_offset;
    }


    bool setup_memory() {
        m_replica_page = utilities::allocateHugePages(kHUGEPAGE_SIZE);
        if (m_replica_page == nullptr) {
            return false;
        }
        
        std::memset(m_replica_page, 0x42, kHUGEPAGE_SIZE);
        // if(!utilities::LockMemoryRegion(m_replica_page, kHUGEPAGE_SIZE)) {
        //     return false;
        // }


        char* base = static_cast<char*>(m_replica_page);
        for (size_t i = 0; i < N; ++i) {
            m_replicas[i] = reinterpret_cast<T*>(base + (i * m_channel_offset));
        }
        return true;
    }


    void setup_replica_cores() {
        m_cores[0] = kCORE_MEAS_A;
        if (m_num_channels > 1 && N > 1) {
            m_cores[1] = kCORE_MEAS_B;
        }

        for (size_t i = 2; i < N; ++i) { 
            m_cores[i] = kCORE_MEAS_B + i - 1; 
        }
        return;
    }
};


} // namespace tailslayer


#endif // TAILSLAYER_HEDGED_READER_HPP
