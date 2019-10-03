#pragma once

#include "sequence.h"
#include "group_by.h"
#include "strings/string_basics.h"

namespace pbbs {

auto grep(range<char*> str, sequence<char>& search_str) -> sequence<char> {
  auto is_line_break = [&] (char a) {return a == '\n';};
  auto cr = singleton('\n');
  auto lines = filter(split(str, is_line_break), [&] (auto const &s) {
      return search(s, search_str) < s.size();});
  auto out_str = flatten(tabulate(lines.size()*2, [&] (size_t i) {
      return (i & 1) ? cr : std::move(lines[i/2]);}));
  return out_str;
}

}
