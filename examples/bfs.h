#pragma once

#include "sequence.h"
#include "ligra.h"

// **************************************************************
//    BFS edge_map structure
// **************************************************************

namespace pbbs {
  
using vertex = ligra::vertex;

struct BFS_F {
  sequence<vertex> Parents;
  vertex n;
  BFS_F(vertex n) : Parents(sequence<vertex>(n, n)), n(n) { }
  inline bool updateAtomic (vertex s, vertex d) {
    return atomic_compare_and_swap(&Parents[d], n , s); }
  inline bool update (long s, long d) {
    Parents[d] = s; return true;}
  inline bool cond (long d) { return (Parents[d] == n); }
};

// **************************************************************
//    Run BFS, returning number of levels, and number of vertices
//    visited
// **************************************************************

std::pair<size_t,size_t> bfs(ligra::graph const &g, vertex start) {
  auto BFS = BFS_F(g.num_vertices());
  BFS.Parents[start] = start;
  ligra::vertex_subset frontier(start); //creates initial frontier
  size_t levels = 0, visited = 0;
  while(!frontier.is_empty()) { //loop until frontier is empty
    visited += frontier.size();
    levels++;
    frontier = ligra::edge_map(g, frontier, BFS);
  }
  return std::make_pair(levels, visited);
}

} // end namespace
