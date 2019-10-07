// Word Count.
// Prints the number of lines, number of space separated words, and number of
// characters to stdout.

#include "wc.h"
#include "parse_command_line.h"
#include "get_time.h"

int main (int argc, char *argv[]) {
  commandLine P(argc, argv, "[-r <rounds>] infile");
  int rounds = P.getOptionIntValue("-r", 1);
  char* filename = P.getArgument(0);
  timer t("word counts", true);

  auto str = pbbs::char_range_from_file(filename);
  t.next("read file");

  size_t lines, words, bytes;
  for (int i=0; i < rounds; i++) {
    std::tie(lines, words, bytes) = wc(str);
    t.next("calculate counts");
  }
  
  cout << "  " << lines << "  " << words << " "
       << bytes << " " << filename << endl;
}

