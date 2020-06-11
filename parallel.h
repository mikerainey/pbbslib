#pragma once

//***************************************
// All the pbbs library uses only four functions for
// accessing parallelism.
// These can be implemented on top of any scheduler.
//***************************************
// number of threads available from OS
//template <>
static int num_workers();

// id of running thread, should be numbered from [0...num-workers)
static int worker_id();

// the granularity of a simple loop (e.g. adding one to each element
// of an array) to reasonably hide cost of scheduler
// #define PAR_GRANULARITY 2000

// parallel loop from start (inclusive) to end (exclusive) running
// function f.
//    f should map long to void.
//    granularity is the number of iterations to run sequentially
//      if 0 (default) then the scheduler will decide
//    conservative uses a safer scheduler
template <typename F>
static void parallel_for(long start, long end, F f,
			 long granularity = 0,
			 bool conservative = false);

// runs the thunks left and right in parallel.
//    both left and write should map void to void
//    conservative uses a safer scheduler
template <typename Lf, typename Rf>
static void par_do(Lf left, Rf right, bool conservative=false);

//***************************************

// cilkplus
#if defined(CILK)
#include <cilk/cilk.h>
#include <cilk/cilk_api.h>
#include <iostream>
#include <sstream>
#define PAR_GRANULARITY 2000

inline int num_workers() {return __cilkrts_get_nworkers();}
inline int worker_id() {return __cilkrts_get_worker_number();}
inline void set_num_workers(int n) {
  __cilkrts_end_cilk();
  std::stringstream ss; ss << n;
  if (0 != __cilkrts_set_param("nworkers", ss.str().c_str())) {
    throw std::runtime_error("failed to set worker count!");
  }
}

template <typename F>
inline void parallel_for(long start, long end, F f,
			 long granularity,
			 bool conservative) {
  if (granularity == 0)
    cilk_for(long i=start; i<end; i++) f(i);
  else if ((end - start) <= granularity)
    for (long i=start; i < end; i++) f(i);
  else {
    long n = end-start;
    long mid = (start + (9*(n+1))/16);
    cilk_spawn parallel_for(start, mid, f, granularity);
    parallel_for(mid, end, f, granularity);
    cilk_sync;
  }
}

template <class F>
inline void mcsl_for(long start, long end, F f,
                     long granularity=0,
                     bool conservative=false) {
  parallel_for(start, end, f, granularity, conservative);
}

template <typename Lf, typename Rf>
inline void par_do(Lf left, Rf right, bool conservative) {
    cilk_spawn right();
    left();
    cilk_sync;
}

template <typename Job>
inline void parallel_run(Job job, int num_threads=0) {
  job();
}

// openmp
#elif defined(OPENMP)
#include <omp.h>
#define PAR_GRANULARITY 200000

inline int num_workers() { return omp_get_max_threads(); }
inline int worker_id() { return omp_get_thread_num(); }
inline void set_num_workers(int n) { omp_set_num_threads(n); }

template <class F>
inline void parallel_for(long start, long end, F f,
			 long granularity,
			 bool conservative) {
  _Pragma("omp parallel for")
    for(long i=start; i<end; i++) f(i);
}

bool in_par_do = false;

template <typename Lf, typename Rf>
inline void par_do(Lf left, Rf right, bool conservative) {
  if (!in_par_do) {
    in_par_do = true;  // at top level start up tasking
#pragma omp parallel
#pragma omp single
#pragma omp task
    left();
#pragma omp task
    right();
#pragma omp taskwait
    in_par_do = false;
  } else {   // already started
#pragma omp task
    left();
#pragma omp task
    right();
#pragma omp taskwait
  }
}

template <typename Job>
inline void parallel_run(Job job, int num_threads=0) {
  job();
}

// Guy's scheduler (ABP)
#elif defined(HOMEGROWN)
#include "scheduler.h"

#ifdef NOTMAIN
extern fork_join_scheduler fj;
#else
fork_join_scheduler fj;
#endif

// Calls fj.destroy() before the program exits
inline void destroy_fj() {
  fj.destroy();
}

struct __atexit {__atexit() {std::atexit(destroy_fj);}};
static __atexit __atexit_var;

#define PAR_GRANULARITY 512

inline int num_workers() {
  return fj.num_workers();
}

inline int worker_id() {
  return fj.worker_id();
}

inline void set_num_workers(int n) {
  fj.set_num_workers(n);
}

template <class F>
inline void parallel_for(long start, long end, F f,
			 long granularity,
			 bool conservative) {
  if (end > start)
    fj.parfor(start, end, f, granularity, conservative);
}

template <class F>
inline void mcsl_for(long start, long end, F f,
                     long granularity=0,
                     bool conservative=false) {
  parallel_for(start, end, f, granularity, conservative);
}

template <typename Lf, typename Rf>
inline void par_do(Lf left, Rf right, bool conservative) {
  return fj.pardo(left, right, conservative);
}

template <typename Job>
inline void parallel_run(Job job, int) {
  job();
}

#elif defined(MCSL)
#include "mcsl_fjnative.hpp"

inline int num_workers() { return 72;} // for now, use conservative value, because it's needed statically by block_allocator
inline int worker_id() { return (int)mcsl::perworker::unique_id::get_my_id();}
inline void set_num_workers(int n) { ; }
#define PAR_GRANULARITY 2000

template <typename Lf, typename Rf>
inline void par_do(Lf left, Rf right, bool conservative) {
  mcsl::fork2(left, right);
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
    par_do([&] () {parfor_(start, mid, f, granularity, conservative);},
           [&] () {parfor_(mid, end, f, granularity, conservative);},
          conservative);
  }
}

template <class F>
inline void parallel_for(long start, long end, F f,
			 long granularity,
			 bool conservative) {
  if (end <= start) return;
  if (granularity == 0) {
    long done = get_granularity(start,end, f);
    granularity = std::max(done, (end-start)/(128*num_workers()));
    parfor_(start+done, end, f, granularity, conservative);
  } else parfor_(start, end, f, granularity, conservative);
}

template <class F>
inline void mcsl_for(long start, long end, F f,
                     long granularity=0,
                     bool conservative=false) {
  if (mcsl::started) {
    parallel_for(start, end, f, granularity, conservative);
    return;
  }
  for (long i=start; i<end; i++) {
    f(i);
  }
}

template <typename Job>
inline void parallel_run(Job job, int num_threads=0) {
  job();
}

// c++
#else

inline int num_workers() { return 1;}
inline int worker_id() { return 0;}
inline void set_num_workers(int n) { ; }
#define PAR_GRANULARITY 1000

template <class F>
inline void parallel_for(long start, long end, F f,
			 long granularity,
			 bool conservative) {
  for (long i=start; i<end; i++) {
    f(i);
  }
}

template <class F>
inline void mcsl_for(long start, long end, F f,
                     long granularity=0,
                     bool conservative=false) {
  parallel_for(start, end, f, granularity, conservative);
}

template <typename Lf, typename Rf>
inline void par_do(Lf left, Rf right, bool conservative) {
  left(); right();
}

template <typename Job>
inline void parallel_run(Job job, int num_threads=0) {
  job();
}

#endif
