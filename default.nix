{ pkgs   ? import <nixpkgs> {},
  stdenv ? pkgs.stdenv,
  makeWrapper ? pkgs.makeWrapper,
  gcc ? pkgs.gcc7,
  which ? pkgs.which,
  jemalloc ? pkgs.jemalloc, # use jemalloc, unless this parameter equals null
  dfltSrc ? ./.
}:

stdenv.mkDerivation rec {
  name = "pbbslib";
  
  src = dfltSrc;

  buildInputs =
       [ makeWrapper gcc which ]
     ++ (if jemalloc == null then [] else [ jemalloc]);

  buildPhase =
    let jemallocCfg =
          if jemalloc == null then
            ""
          else
            "export PATH=${jemalloc}/bin:$PATH";
    in
    ''
    ${jemallocCfg}    
    make -j \
      test_schedulers \
      test_alloc \
      time_tests \
      CC=${gcc}/bin/g++
    make -C examples -j all  \
      CC=${gcc}/bin/g++
    make -C strings -j test_suffix_tree \
      CC=${gcc}/bin/g++
    '';

  outputs = [ "out" "examples" "test" ];

  installPhase =
    ''
    mkdir -p $out/
    cp *.h $out
    mkdir -p $out/strings
    cp strings/*.h $out/strings

    mkdir -p $test/
    cp test_scheduler_* test_alloc time_tests $test/
    cp strings/test_suffix_tree $test
    wrapProgram $test/test_suffix_tree \
      --prefix LD_LIBRARY_PATH ":" ${gcc.cc.lib}/lib64

    mkdir -p $examples/
    (cd examples
     cp mcss wc grep build_index primes longest_repeated_substring bw \
       $examples)

    '';
}   
