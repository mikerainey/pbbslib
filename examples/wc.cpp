// Word Count.
// Prints the number of lines, number of space separated words, and number of
// characters to stdout.

#include "wc.h"
#include "parse_command_line.h"
#include "get_time.h"

int main (int argc, char *argv[]) {
  mcsl::basic_stats::on_enter_launch();

  auto f_body = mcsl::new_fjnative_of_function([&] {
                                                 started = true;
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
  });
  auto nb_workers = 1;
  f_body->release();
  using scheduler_type = mcsl::chase_lev_work_stealing_scheduler<mcsl::basic_scheduler_configuration, mcsl::fiber, mcsl::basic_stats>;
  auto start_time = std::chrono::system_clock::now();
  scheduler_type::launch(nb_workers);
  auto end_time = std::chrono::system_clock::now();
  
  mcsl::basic_stats::on_exit_launch();
  {
    std::chrono::duration<double> elapsed = end_time - start_time;
    printf("exectime %.3f\n", elapsed.count());
  }
  mcsl::basic_stats::report();

  return 0;
}

