#pragma once

#include "sequence.h"
#include "strings/string_basics.h"

namespace pbbs {

auto grep(range<char*> str, sequence<char>& search_str) -> sequence<char> {
  auto is_even = [] (size_t i) { return i & 1; };
  auto is_line_break = [&] (char a) {return a == '\n';};
  auto cr = singleton('\n');
  auto lines = filter(split_range(str, is_line_break), [&] (auto const &s) {
      return search(s, search_str) < s.size();});
  return flatten(tabulate(lines.size()*2, [&] (size_t i) {
      return is_even(i) ? cr : lines[i/2];}));
}

}
