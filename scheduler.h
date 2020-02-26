#pragma once

#include <chrono>
#include <thread>
#include <cstdint>
#include <iostream>
#include <functional>
#include <array>
#include <mutex>
#include <functional> 
#include <semaphore.h>
#include <atomic>

// EXAMPLE USE 1:
//
// fork_join_scheduler fj;
//
// long fib(long i) {
//   if (i <= 1) return 1;
//   long l,r;
//   fj.pardo([&] () { l = fib(i-1);},
//            [&] () { r = fib(i-2);});
//   return l + r;
// }
//
// fib(40);
//
// EXAMPLE USE 2:
//
// void init(long* x, size_t n) {
//   parfor(0, n, [&] (int i) {a[i] = i;});
// }
//
// size_t n = 1000000000;
//
// long* a = new long[n];
//
// init(a, n);


// Deque from Arora, Blumofe, and Plaxton (SPAA, 1998).
template <typename Job>
struct Deque {
  using qidx = unsigned int;
  using tag_t = unsigned int;

  // use unit for atomic access
  union age_t {
    struct {
      tag_t tag;
      qidx top;
    } pair;
    size_t unit;
  };

  // align to avoid false sharing
  struct alignas(64) padded_job { Job* job;  };

  static int const q_size = 200;
  age_t age;
  qidx bot;
  padded_job deq[q_size];

  inline bool cas(size_t* ptr, size_t oldv, size_t newv) {
    return __sync_bool_compare_and_swap(ptr, oldv, newv);
  }

  inline void fence() {
    std::atomic_thread_fence(std::memory_order_seq_cst);
  }

  Deque() : bot(0) {
    age.pair.tag = 0;
    age.pair.top = 0;
  }

  void push_bottom(Job* job) {
    qidx local_bot;
    local_bot = bot; // atomic load
    deq[local_bot].job = job; // shared store
    local_bot += 1;
    if (local_bot == q_size) 
      throw std::runtime_error("internal error: scheduler queue overflow");
    bot = local_bot; // shared store
    fence();
  }

  Job* pop_top() {
    age_t old_age, new_age;
    qidx local_bot;
    Job *job, *result;
    old_age.unit = age.unit; // atomic load

    local_bot = bot; // atomic load
    if (local_bot <= old_age.pair.top)
      result = NULL;
    else {
      job = deq[old_age.pair.top].job; // atomic load
      new_age.unit = old_age.unit;
      new_age.pair.top = new_age.pair.top + 1;
      if (cas(&(age.unit), old_age.unit, new_age.unit))  // cas
	      result = job;
      else
	      result = NULL;
    }
    return result;
  }

  Job* pop_bottom() {
    age_t old_age, new_age;
    qidx local_bot;
    Job *job, *result;
    local_bot = bot; // atomic load
    if (local_bot == 0)
      result = NULL;
    else {
      local_bot = local_bot - 1;
      bot = local_bot; // shared store
      fence();
      job = deq[local_bot].job; // atomic load
      old_age.unit = age.unit; // atomic load
      if (local_bot > old_age.pair.top)
	result = job;
      else {
	bot = 0; // shared store
	new_age.pair.top = 0;
	new_age.pair.tag = old_age.pair.tag + 1;
	if ((local_bot == old_age.pair.top) &&
	    cas(&(age.unit), old_age.unit, new_age.unit))
	  result = job;
	else {
	  age.unit = new_age.unit; // shared store
	  result = NULL;
	}
	fence();
      }
    }
  return result;
  }

};

//thread_local int thread_id;

struct alignas(64) perthread_elapsed { double e; };
std::array<perthread_elapsed, 128> time_in_get_job;

template <typename Job>
struct scheduler {

public:
  // see comments under wait(..)
  static bool const conservative = false;
  int num_threads;

  static thread_local int thread_id;

  scheduler() {
    init_num_workers();
    num_deques = 2 * num_threads;
    deques     = new Deque<Job>[num_deques];
    attempts   = new attempt[num_deques];
    finished_flag = 0;

    // Spawn num_workers many threads on startup
    spawned_threads = new std::thread[num_threads-1];
    std::function<bool()> finished = [&] () {  return finished_flag == 1; };
    thread_id = 0; // thread-local write
    for (int i=1; i<num_threads; i++) {
      spawned_threads[i-1] = std::thread([&, i, finished] () {
        thread_id = i; // thread-local write
        start(finished);
      });
    }
  }

  ~scheduler() {
    finished_flag = 1;
    for (int i=1; i<num_threads; i++) {
      spawned_threads[i-1].join();
    }
    delete[] spawned_threads;
    delete[] deques;
    delete[] attempts;
  }

  // Push onto local stack.
  void spawn(Job* job) {
    int id = worker_id();
    deques[id].push_bottom(job);
  }

  // Wait for condition: finished().
  template <typename F>
  void wait(F finished, bool conservative=false) {
    // Conservative avoids deadlock if scheduler is used in conjunction
    // with user locks enclosing a wait.
    if (conservative)
      while (!finished())
    	std::this_thread::yield();
    // If not conservative, schedule within the wait.
    // Can deadlock if a stolen job uses same lock as encloses the wait.
    else start(finished);
  }

  // All scheduler threads quit after this is called.
  void finish() {finished_flag = 1;}

  // Pop from local stack.
  Job* try_pop() {
    int id = worker_id();
    return deques[id].pop_bottom();
  }

  void init_num_workers() {
    if (const char* env_p = std::getenv("NUM_THREADS")) {
      num_threads = std::stoi(env_p);
    } else {
      num_threads = std::thread::hardware_concurrency();
    }
  }

  int num_workers() {
    return num_threads;
  }
  int worker_id() {
    return thread_id;
  }
  void set_num_workers(int n) {
    std::cout << "Unsupported" << std::endl; exit(-1);
  }

private:

  // Align to avoid false sharing.
  struct alignas(128) attempt { size_t val; };

  int num_deques;
  Deque<Job>* deques;
  attempt* attempts;
  std::thread* spawned_threads;
  int finished_flag;

  // Start an individual scheduler task.  Runs until finished().
  template <typename F>
  void start(F finished) {
    while (1) {
      Job* job = get_job(finished);
      if (!job) return;
      (*job)();
    }
  }

  Job* try_steal(size_t id) {
    // use hashing to get "random" target
    size_t target = (hash(id) + hash(attempts[id].val)) % num_deques;
    attempts[id].val++;
    return deques[target].pop_top();
  }

  // Find a job, first trying local stack, then random steals.
  template <typename F>
  Job* get_job(F finished) {
    Job* res = NULL;
    struct timezone tzp({0,0});
    auto double_of_tv = [] (struct timeval tv) {
      return ((double) tv.tv_sec) + ((double) tv.tv_usec)/1000000.;
    };
    
    if (finished()) return NULL;
    Job* job = try_pop();
    if (job) return job;
    size_t id = worker_id();
    while (1) {
      timeval now;
      auto s = gettimeofday(&now, &tzp);
      // By coupon collector's problem, this should touch all.
      for (int i=0; i <= num_deques * 100; i++) {
	if (finished()) {
	  time_in_get_job[thread_id].e += gettimeofday(&now, &tzp)-s;
	  return NULL;
	}
	job = try_steal(id);
	if (job) {
	  time_in_get_job[thread_id].e += gettimeofday(&now, &tzp)-s;
	  return job;
	}
      }
      time_in_get_job[thread_id].e += gettimeofday(&now, &tzp)-s;
      // If haven't found anything, take a breather.
      std::this_thread::sleep_for(std::chrono::nanoseconds(num_deques*100));
    }
  }

  uint64_t hash(uint64_t x) {
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    x = x ^ (x >> 31);
    return x;
  }
};

class ConcurrentRandomSet {
  int num_threads;
  struct alignas(64) flag { bool val; };

  // Take in the worker id roll's its dice
  typedef std::function<uint64_t(size_t)> RngFunc;
  RngFunc rng;

  flag* flags; // One flag for each potential element

public:
  ConcurrentRandomSet() 
    : num_threads(0),
      flags(nullptr),
      rng([](size_t i){return UINT64_C(0);})
    {}

  ConcurrentRandomSet(int num_threads, RngFunc rng, bool init=true)
    : num_threads(num_threads), 
      rng(rng), 
      flags(nullptr) 
      {
        flags = new flag[num_threads];
        for (int i = 0; i < num_threads; ++i) {
          flags[i].val = init;
        }
      }

  void add(uint64_t i) {
    this->flags[i].val = true;
  }

  void remove(uint64_t i) {
    this->flags[i].val = false;
  }

  bool exists(uint64_t i) {
    return this->flags[i].val;
  }

  uint64_t sample(size_t id) {
    uint64_t rand;
    do {
      rand = rng(id) % num_threads;
    } while (!this->flags[rand].val);
    return rand;
  }

  ~ConcurrentRandomSet() {
    delete[] this->flags;
  }
};

template <typename Job>
struct elasticws_scheduler {
public:
  // see comments under wait(..)
  static bool const conservative = false;
  int num_threads;
  ConcurrentRandomSet crs;

  static thread_local int thread_id;

  elasticws_scheduler() {
    init_num_workers();
    num_deques = 2 * num_threads;
    deques = new Deque<Job>[num_deques];
    // attempts = new attempt[num_deques];
    data = new procData[num_threads];
    finished_flag = 0;

    /* Initialize all the data fields: */
    for (int i = 0; i < num_threads; ++i) {
      data[i].seed = hash(i) + 1;
      data[i].status.clear(data[i].seed, i);
      sem_init(&data[i].sem, 0, 0);
    }

    // Initialize CRS
    auto rng = [data = this->data](size_t i) {
      return hash(&data[i].seed);
    };
    crs = ConcurrentRandomSet(this->num_threads, rng); // Initialize CRS

    // Spawn num_workers many threads on startup
    spawned_threads = new std::thread[ num_threads - 1];
    std::function<bool()> finished = [&] () {  return finished_flag == 1; };
    thread_id = 0; // thread-local write
    for (int i=1; i<num_threads; i++) {
      spawned_threads[i-1] = std::thread([&, i, finished] () {
        thread_id = i; // thread-local write
        start(finished);
      });
    }
  }

  ~elasticws_scheduler() {
    finished_flag = 1;
    for (int i=1; i<num_threads; i++) {
      spawned_threads[i-1].join();
    }
    delete[] spawned_threads;
    delete[] deques;
    for (int i = 0; i < num_threads; ++i) {
      sem_destroy(&data[i].sem);
    }
    delete[] data;
  }

  // Push onto local stack.
  void spawn(Job* job) {
    int id = worker_id();
    deques[id].push_bottom(job);
  }

  // Wait for condition: finished().
  template <typename F>
  void wait(F finished, bool conservative=false) {
    // Conservative avoids deadlock if scheduler is used in conjunction
    // with user locks enclosing a wait.
    if (conservative)
      while (!finished())
    	std::this_thread::yield();
    // If not conservative, schedule within the wait.
    // Can deadlock if a stolen job uses same lock as encloses the wait.
    else start(finished);
  }

  // All scheduler threads quit after this is called.
  void finish() {finished_flag = 1;}

  // Pop from local stack.
  Job* try_pop() {
    int id = worker_id();
    return deques[id].pop_bottom();
  }

  void init_num_workers() {
    if (const char* env_p = std::getenv("NUM_THREADS")) {
      num_threads = std::stoi(env_p);
    } else {
      num_threads = std::thread::hardware_concurrency();
    }
  }

  int num_workers() {
    return num_threads;
  }
  int worker_id() {
    return thread_id;
  }

  void set_num_workers(int n) {
    std::cout << "Unsupported" << std::endl; exit(-1);
  }

private:

  int num_deques;
  Deque<Job>* deques;
  std::thread* spawned_threads;
  int finished_flag;

  // Data structure for each processor:

  // Status word. 64-bits wide
  union status_word {
    uint64_t asUint64; // The order of fields is significant 
                       // Always initializes the first member
    struct {
      uint8_t  busybit  : 1 ;
      uint64_t priority : 56;
      uint8_t  head     : 7 ;  // Supports at most 128 processors
    } bits; 
  };

  // A status word that can be operated on atomically
  // 1) clear() will always success in bounded number of steps.
  // 2) setBusyBit() uses atomic fetch_and_AND. It is guaranteed to
  //    succeed in bounded number of steps.
  // 3) updateHead() may fail. It's upto the caller to verify that the
  //    operations succeeded. This is to ensure that the operation completes
  //    in bounded number of steps.
  class AtomicStatusWord {
    std::atomic<uint64_t> statusWord;

  public:
    // Since no processor can be a child of itself, the thread_id of the 
    // processor itself can be used as the nullary value of the head

    AtomicStatusWord() : statusWord(UINT64_C(0)) {}

    AtomicStatusWord(uint64_t prio, uint8_t nullaryHead) {
      clear(prio, nullaryHead);
    }

    // 1) Unsets the busy bit
    // 2) Hashes and obtain a new priority
    // 3) Resets the head value
    void clear(uint64_t prio, uint8_t nullaryHead) {
      status_word word = {UINT64_C(0)};
      word.bits.busybit  = 0u;   // Not busy
      word.bits.priority = prio; 
      word.bits.head     = nullaryHead;
      statusWord.store(word.asUint64);
    }

    // Sets busy bit and returns the old status word
    status_word setBusyBit() {
      status_word word = {UINT64_C(0)};
      word.bits.busybit = 1u; // I'm going to be busy
      word = {statusWord.fetch_or(word.asUint64)};
      return word;
    }

    // Update the head field while preserving all other fields
    bool casHead(status_word word, uint8_t newHead) {
      uint64_t expected = word.asUint64;
      word.bits.head = newHead; // Update only the head field
      return statusWord.compare_exchange_weak(expected, word.asUint64);
    }

    status_word load() {
      return status_word{statusWord.load()};
    }
  };

  // Align to avoid false sharing
  // Contains all private fields. Fields are arranged in a compact way.
  // Designed for best cache behavior.
  struct alignas(128) procData { 
    uint64_t         seed;        // Seed for RNG
    AtomicStatusWord status;      // Status word
    sem_t            sem;         // Semaphore for sleep/wake-up
    unsigned char    children[0]; // All remaining bytes are used as linked list for children 
  };

  procData* data;

  // Start an individual scheduler task.  Runs until finished().
  template <typename F>
  void start(F finished) {
    while (1) {
      Job* job = get_job(finished);
      if (!job) return;
      (*job)();
    }
  }

  // Returns 
  // 1) Id of the target
  // 2) The job. NULL if none.
  std::tuple<size_t, Job*> try_steal(size_t id) {
    // use hashing to get "random" target
    size_t target = (hash(id) + hash(&data[id].seed)) % num_deques;
    bool is_empty;
    Job* job = deques[target].pop_top();
    return std::make_tuple(target, job);
  }

  // Find a job, first trying local stack, then random steals.
  template <typename F>
  Job* get_job(F finished) {
    Job* res = NULL;
    // struct timezone tzp({0,0});
    // auto double_of_tv = [] (struct timeval tv) {
    //   return ((double) tv.tv_sec) + ((double) tv.tv_usec)/1000000.;
    // };
    
    if (finished()) return NULL;
    Job* job = try_pop();
    if (job) return job;
    size_t id = worker_id();
    /* Transition into stealing: */
    // Clear the status word with new priority and my own id
    data[id].status.clear(hash(&data[id].seed), id);
    while (1) {
      // By coupon collector's problem, this should touch all.
      if (finished()) return NULL;
      size_t target;
      std::tie(target, job) = try_steal(id);
      // Cannot attach a lifeline to myself. Stealing from myself will not succeed
      if (target == id) continue;
      if (job) {
        // steal is successful, we need to wake up all of our children
        // Set the busy bit and get all children
        auto status = data[id].status.setBusyBit();
        size_t idx = status.bits.head;
        while (idx != id) {
          sem_post(&data[idx].sem); // Wake it up.
          idx = data[id].children[idx];
        }
        return job;
      } else {
        // It is possible that we are in this branch because the steal failed
        // due to contention instead of empty queue. However we are still safe 
        // because of the busy bit.
        auto target_status = data[target].status.load();
        auto my_status     = data[id].status.load();
        if ((!target_status.bits.busybit) && 
            target_status.bits.priority > my_status.bits.priority) {
          data[target].children[id] = target_status.bits.head;
          // It's safe to just leave it in the array even if the following
          // CAS fails because it will never be referenced in case of failure.
          if (data[target].status.casHead(target_status, id)) {
            crs.add(id);
            sem_wait(&data[id].sem);
            crs.remove(id);
          } else {
            continue; // We give up
          }
        }
        continue; // If it fails, then nothing happens
      }
      //std::this_thread::sleep_for(std::chrono::nanoseconds(num_deques*100));
    }
  }

  static inline uint64_t hash(uint64_t x) {
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    x = x ^ (x >> 31);
    return x;
  }

  static inline uint64_t hash(uint64_t *seed) {
    uint64_t x = hash(*seed);
    *seed = x;
    return x;
  }

};

template<typename T>
thread_local int scheduler<T>::thread_id = 0;

template<typename T>
thread_local int elasticws_scheduler<T>::thread_id = 0;

struct fork_join_scheduler {

public:
  // Jobs are thunks -- i.e., functions that take no arguments
  // and return nothing.   Could be a lambda, e.g. [] () {}.
  using Job = std::function<void()>;

  elasticws_scheduler<Job>* sched;

  fork_join_scheduler() {
    sched = new elasticws_scheduler<Job>;
  }

  ~fork_join_scheduler() {
    if (sched) {
      delete sched;
      sched = nullptr;
    }
  }

  // Must be called using std::atexit(..) to free resources
  void destroy() {
    if (sched) {
      delete sched;
      sched = nullptr;
    }
  }

  int num_workers() { return sched->num_workers(); }
  int worker_id() { return sched->worker_id(); }
  void set_num_workers(int n) { sched->set_num_workers(n); }

  // Fork two thunks and wait until they both finish.
  template <typename L, typename R>
  void pardo(L left, R right, bool conservative=false) {
    bool right_done = false;
    Job right_job = [&] () {
      right(); right_done = true;};
    sched->spawn(&right_job);
    left();
    if (sched->try_pop() != NULL) right();
    else {
      auto finished = [&] () {return right_done;};
      sched->wait(finished, conservative);
    }
  }

  template <typename F>
  int get_granularity(size_t start, size_t end, F f) {
    size_t done = 0;
    size_t size = 1;
    int ticks;
    do {
      size = std::min(size,end-(start+done));
      auto tstart = std::chrono::high_resolution_clock::now();
      for (size_t i=0; i < size; i++) f(start+done+i);
      auto tstop = std::chrono::high_resolution_clock::now();
      ticks = (tstop-tstart).count();
      done += size;
      size *= 2;
    } while (ticks < 1000 && done < (end-start));
    return done;
  }

  template <typename F>
  void parfor(size_t start, size_t end, F f,
	      size_t granularity = 0,
	      bool conservative = false) {
    if (end <= start) return;
    if (granularity == 0) {
      size_t done = get_granularity(start,end, f);
      granularity = std::max(done, (end-start)/(128*sched->num_threads));
      parfor_(start+done, end, f, granularity, conservative);
    } else parfor_(start, end, f, granularity, conservative);
  }

private:

  template <typename F>
  void parfor_(size_t start, size_t end, F f,
	       size_t granularity,
	       bool conservative) {
    if ((end - start) <= granularity)
      for (size_t i=start; i < end; i++) f(i);
    else {
      size_t n = end-start;
      // Not in middle to avoid clashes on set-associative caches
      // on powers of 2.
      size_t mid = (start + (9*(n+1))/16);
      pardo([&] () {parfor_(start, mid, f, granularity, conservative);},
	    [&] () {parfor_(mid, end, f, granularity, conservative);},
	    conservative);
    }
  }

};
