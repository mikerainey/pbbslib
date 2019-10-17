#include "bfs.h"
#include "parse_command_line.h"
#include "get_time.h"

using namespace pbbs;

// **************************************************************
//    Main
// **************************************************************

int main (int argc, char *argv[]) {
  commandLine P(argc, argv,
     "[-r <rounds>] [-t <sparse_dense_ratio>] [-s <source>] filename");
  int rounds = P.getOptionIntValue("-r", 1);
  ligra::sparse_dense_ratio = P.getOptionIntValue("-t", 10);
  int start = P.getOptionIntValue("-s", 0);
  char* filename = P.getArgument(0);
  timer t("BFS");
  auto g = ligra::read_graph(filename);
  t.next("read and parse graph");

  size_t levels, visited;
  for (int i=0; i < rounds; i++) {
    std::tie(levels, visited) = bfs(g, start);
    t.next("calculate bfs");
  }
  cout << levels << " levels in BFS, "
       << visited << " vertices visited" << endl;
}

