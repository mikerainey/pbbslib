// Build_Index.
// Builds an index from a file which maps each word to all the lines
// it appears in.
// It outputs to a file given by "-o <outfile>".  If no file is given,
// it just prints the number of distinct words to stdout.
// The outfile file has one line per word, with the word appearing
// first and then all the line numbers the word appears in.  The words
// are in alphabetical order, and the line numbers are in integer
// order, all in ascii.

#include "get_time.h"
#include "parse_command_line.h"
#include "build_index.h"

int main (int argc, char *argv[]) {
  commandLine P(argc, argv, "[-r <rounds>] [-o <outfile>] infile");
  int rounds = P.getOptionIntValue("-r", 1);
  bool verbose = P.getOption("-v");
  std::string outfile = P.getOptionValue("-o", "");
  char* filename = P.getArgument(0);
  timer idx_timer("build_index", verbose);
  auto str = pbbs::char_range_from_file(filename);
  idx_timer.next("read file");
  index_type idx;

  // resereve 5 x the number of bytes of the string for the memory allocator
  // not needed, but speeds up the first run
  pbbs::allocator_reserve(str.size()*5);

  idx_timer.next("reserve space");

  idx_timer.start();
  for (int i=0; i < rounds ; i++) {
    idx = build_index(str, verbose);
    idx_timer.next("build index");
  }

  cout << (idx[0].second)[2] << endl;

  if (outfile.size() > 0) {
    auto out_str = index_to_char_seq(idx);
    idx_timer.next("generate output string");

    char_seq_to_file(out_str, outfile);
    idx_timer.next("write file");
  } else {
    cout << "number of distinct words: " << idx.size() << endl;
  }
}
