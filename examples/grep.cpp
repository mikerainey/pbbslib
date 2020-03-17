#include "get_time.h"
#include "parse_command_line.h"
#include "grep.h"

using namespace pbbs;

int main (int argc, char *argv[]) {
  std::string infile, pattern;
  //  commandLine P(argc, argv, "[-r <rounds>] search_string infile");
  //int rounds = P.getOptionIntValue("-r", 1);
  int rounds = 1;
  timer t("grep", true);
  sequence<char> out_str, search_str;
  range<char*> str;
  mcsl::launch(argc, (char**)argv,
               [&] {
                 infile = deepsea::cmdline::parse_or_default_string("infile", "grep.txt");
                  pattern = deepsea::cmdline::parse_or_default_string("pattern", "xxy");
                 char* filename = (char*)infile.c_str();
                 search_str = to_sequence(std::string((char*)pattern.c_str()));
                 str = char_range_from_file(filename);
                 t.next("read file");
               },
               [&] {
                 //cout << out_str;
               }, [&] {
                 for (int i=0; i < rounds; i++) {
                   out_str = grep(str, search_str);
                   t.next("do work");
                 }
               });
  return 0;
}

