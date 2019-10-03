#include "get_time.h"
#include "strings/string_basics.h"
#include "parse_command_line.h"
#include "grep.h"

using namespace pbbs;

int main (int argc, char *argv[]) {
  commandLine P(argc, argv, "[-r <rounds>] search_string infile");
  int rounds = P.getOptionIntValue("-r", 1);
  auto search_str = to_sequence(std::string(P.getArgument(1)));
  char* filename = P.getArgument(0);

  timer t("grep", true);
  
  range<char*> str = char_range_from_file(filename);
  t.next("read file");
  sequence<char> out_str;

  for (int i=0; i < rounds; i++) {
    out_str = grep(str, search_str);
    t.next("do work");
  }
  cout << out_str;
}

