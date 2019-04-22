#pragma once

#ifdef USEMALLOC

#include <malloc.h>

namespace pbbs {
  struct __mallopt {
    __mallopt() {
      mallopt(M_MMAP_MAX,0);
      mallopt(M_TRIM_THRESHOLD,-1);
    }
  };

  __mallopt __mallopt_var;

  inline void* my_alloc(size_t i) {return malloc(i);}
  inline void my_free(void* p) {free(p);}
  void allocator_clear() {}
}

#else

#include <atomic>
#include <vector>
#include "utilities.h"
#include "concurrent_stack.h"
#include "utilities.h"
#include "block_allocator.h"
#include "memory_size.h"

namespace pbbs {

#if defined(__APPLE__) // a little behind the times
  void* aligned_alloc(size_t a, size_t n) {return malloc(n);}
#endif


  // ****************************************
  //    pool_allocator
  // ****************************************

  // Allocates headerless blocks from pools of different sizes.
  // A vector of pool sizes is given to the constructor.
  // Sizes must be at least 8, and must increase.
  // For pools of small blocks (below large_threshold) each thread keeps a
  //   thread local list of elements from each pool using the
  //   block_allocator.
  // For pools of large blocks there is only one shared pool for each.
  struct pool_allocator {

  private:
    static const size_t large_align = 64;
    static const size_t large_threshold = (1 << 16);
    size_t num_buckets;
    size_t num_small;
    size_t max_small;
    size_t max_size;
    std::atomic<long> large_allocated{0};
  
    concurrent_stack<void*>* large_buckets;
    struct block_allocator *small_allocators;
    std::vector<size_t> sizes;

    void* allocate_large(size_t n) {
    
      if (n <= max_size) {
	size_t bucket = num_small;
	while (n > sizes[bucket]) bucket++;
	maybe<void*> r = large_buckets[bucket-num_small].pop();
	if (r) return *r;
      }

      void* a = (void*) aligned_alloc(large_align, n);
      large_allocated += n;
      if (a == NULL) std::cout << "alloc failed on size: " << n << std::endl;
      // a hack to make sure pages are touched in parallel
      // not the right choice if you want processor local allocations
      size_t stride = (1 << 21); // 2 Mbytes in a huge page
      parallel_for(0, n/stride, [&] (size_t i) {
	  ((bool*) a)[i*stride] = 0;});
      return a;
    }

    void deallocate_large(void* ptr, size_t n) {
      if (n > max_size) { 
	free(ptr);
	large_allocated -= n;
      } else {
	size_t bucket = num_small;
	while (n > sizes[bucket]) bucket++;
	large_buckets[bucket-num_small].push(ptr);
      }
    }

  public:
    ~pool_allocator() {
      for (size_t i=0; i < num_small; i++)
	small_allocators[i].~block_allocator();
      free(small_allocators);
      clear();
      delete[] large_buckets;
    }

    pool_allocator() {}
  
    pool_allocator(std::vector<size_t> const &sizes) : sizes(sizes) {
      num_buckets = sizes.size();
      max_size = sizes[num_buckets-1];
      num_small = 0;
      while (sizes[num_small] < large_threshold && num_small < num_buckets)
	num_small++;
      max_small = (num_small > 0) ? sizes[num_small - 1] : 0;

      large_buckets = new concurrent_stack<void*>[num_buckets-num_small];

      small_allocators = (struct block_allocator*)
	malloc(num_buckets * sizeof(struct block_allocator));
      size_t prev_bucket_size = 0;
    
      for (size_t i = 0; i < num_small; i++) {
	size_t bucket_size = sizes[i];
	if (bucket_size < 8)
	  cout << "for small_allocator, bucket sizes must be at least 8" << endl;
	if (!(bucket_size > prev_bucket_size))
	  cout << "for small_allocator, bucket sizes must increase" << endl;
	prev_bucket_size = bucket_size;
	new (static_cast<void*>(std::addressof(small_allocators[i]))) 
	  block_allocator(bucket_size); 
      }
    }

    void* allocate(size_t n) {
      if (n > max_small) return allocate_large(n);
      size_t bucket = 0;
      while (n > sizes[bucket]) bucket++;
      return small_allocators[bucket].alloc();
    }

    void deallocate(void* ptr, size_t n) {
      if (n > max_small) deallocate_large(ptr, n);
      else {
	size_t bucket = 0;
	while (n > sizes[bucket]) bucket++;
	small_allocators[bucket].free(ptr);
      }
    }

    // void reserve(int n, size_t count) {
    //   int bucket = 0;
    //   while (n > sizes[bucket]) bucket++;
    //   return small_allocators[bucket].reserve(count);
    // }

    void print_stats() {
      size_t total_a = 0;
      size_t total_u = 0;
      for (size_t i = 0; i < num_small; i++) {
	size_t bucket_size = sizes[i];
	size_t allocated = small_allocators[i].num_allocated_blocks();
	size_t used = small_allocators[i].num_used_blocks();
	total_a += allocated * bucket_size;
	total_u += used * bucket_size;
	cout << "size = " << bucket_size << ", allocated = " << allocated
	     << ", used = " << used << endl;
      }
      cout << "Large allocated = " << large_allocated << endl;
      cout << "Total bytes allocated = " << total_a + large_allocated << endl;
      cout << "Total bytes used = " << total_u << endl;
    }

    void clear() {
      for (size_t i = num_small; i < num_buckets; i++) {
	maybe<void*> r = large_buckets[i-num_small].pop();
	while (r) {
	  large_allocated -= sizes[i];
	  free(*r);
	  r = large_buckets[i-num_small].pop();
	}
      }
    }
  };

  // ****************************************
  //    default_allocator (uses powers of two as pool sizes)
  // ****************************************

  // these are bucket sizes used by the default allocator.
  std::vector<size_t> default_sizes() {
    size_t log_min_size = 4;
    size_t log_max_size = pbbs::log2_up(getMemorySize()/64);

    std::vector<size_t> sizes;
    for (size_t i = log_min_size; i <= log_max_size; i++)
      sizes.push_back(1 << i);
    return sizes;
  }

  pool_allocator default_allocator(default_sizes());

  // Matches the c++ Allocator specification (minimally)
  // https://en.cppreference.com/w/cpp/named_req/Allocator
  // Can therefore be used for containers, e.g.:
  // std::vector
  template <typename T>
  struct allocator {
    using value_type = T;
    T* allocate(size_t n) {
      return (T*) default_allocator.allocate(n * sizeof(T));
    }
    T* deallocate(T* ptr, size_t n) {
      default_allocator.deallocate((void*) ptr, n * sizeof(T));
    }

    allocator() = default;
    template <class U> constexpr allocator(const allocator<U>&) {}
  };

  template <class T, class U>
  bool operator==(const allocator<T>&, const allocator<U>&) { return true; }
  template <class T, class U>
  bool operator!=(const allocator<T>&, const allocator<U>&) { return false; }


  // ****************************************
  //    my_alloc and my_free (add size tags)
  // ****************************************

  inline size_t header_size(size_t n) {
    return (n >= 1024) ? 64 : (n & 15) ? 8 : (n & 63) ? 16 : 64;
  }

  // allocates and tags with a header (8, 16 or 64 bytes) that contains the size
  void* my_alloc(size_t n) {
    size_t hsize = header_size(n);
    void* ptr = default_allocator.allocate(n + hsize);
    void* r = (void*) (((char*) ptr) + hsize);
    *(((size_t*) r)-1) = n; // puts size in previous word
    return r;
  }

  // reads the size, offsets the header and frees
  void my_free(void *ptr) {
    size_t n = *(((size_t*) ptr)-1);
    size_t hsize = header_size(n);
    default_allocator.deallocate((void*) (((char*) ptr) - hsize), n + hsize);
  }

  void allocator_clear() {
    default_allocator.clear();
  }

  // Does not initialize the array
  template<typename E>
  E* new_array_no_init(size_t n) {
    size_t bytes = n * sizeof(E);
    E* r = (E*) my_alloc(bytes);
    if (r == NULL) {fprintf(stderr, "Cannot allocate space: %lu bytes", bytes); exit(1);}
    return r;
  }

  // Initializes in parallel
  template<typename E>
  E* new_array(size_t n) {
    E* r = new_array_no_init<E>(n);
    if (!std::is_trivially_default_constructible<E>::value) {
      //if (!std::is_default_constructible<E>::value) {
      if (n > 2048) 
	parallel_for(0, n, [&] (size_t i) {
	    new ((void*) (r+i)) E;});
      else
	for (size_t i = 0; i < n; i++)
	  new ((void*) (r+i)) E;
    }
    return r;
  }

  inline void free_array(void* a) {
    my_free(a);
  }

  // Destructs in parallel
  template<typename E>
  void delete_array(E* A, size_t n) {
    // C++14 -- suppored by gnu C++11
    if (!std::is_trivially_destructible<E>::value) {
      if (n > 2048) 
	parallel_for(0, n, [&] (size_t i) {
	    A[i].~E();});
      else for (size_t i = 0; i < n; i++)
	     A[i].~E();
    }
    my_free(A);
  }
}
#endif

