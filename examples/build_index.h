#pragma once

#include "sequence.h"
#include "strings/string_basics.h"
#include "group_by.h"
using namespace std;

namespace pbbs {

// an index consists of a sequence of pairs each consisting of
//     a character sequence (the word)
//     an integer sequences (the line numbers it appears in)
using index_type = sequence<pair<sequence<char>,sequence<size_t>>>;

auto build_index(sequence<char> const &str, bool verbose) -> index_type {
  //  timer t("build_index", verbose); // set to true to print times for each step
  auto is_line_break = [&] (char a) {return a == '\n' || a == '\r';};
  auto is_space = [&] (char a) {return a == ' ' || a == '\t';};
  
  // remove punctuation and convert to lower case
  sequence<char> cleanstr = map(str, [&] (char a) -> char {
      return isspace(a) ? a : isalpha(a) ? tolower(a) : ' ';});
  //  t.next("clean");
  
  // split into lines
  auto lines = split(cleanstr, is_line_break);
  //  t.next("split");
  
  // generate sequence of sequences of (token, line_number) pairs
  // tokens are strings separated by spaces.
  auto pairs = tabulate(lines.size(), [&] (size_t i) {
      return dmap(tokens(lines[i], is_space), [=] (sequence<char> s) {
	  return make_pair(s, i);});
      });
  //  t.next("tokens");

  // flatten the sequence
  auto flat_pairs = flatten(pairs);
  //  t.next("flatten");
      
  // group line numbers by tokens
  return group_by(flat_pairs);
}

// converts an index into an ascii character sequence ready for output
sequence<char> index_to_char_seq(index_type const &idx) {

  // print line numbers separated by spaces for a singe word
  auto linelist = [] (auto &A) {
    return flatten(tabulate(2 * A.size(), [&] (size_t i) {
	  if (i & 1) return to_char_seq(A[i/2]);
	  return singleton(' ');
	}));
  };

  // for each entry, print word followed by list of lines it is in
  return flatten(map(idx, [&] (auto& entry) {
	sequence<sequence<char>>&& A = {entry.first,
					linelist(entry.second),
					singleton('\n')};
	return flatten(A);}));
}

}
