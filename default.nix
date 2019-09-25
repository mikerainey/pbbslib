{ pkgs   ? import <nixpkgs> {},
  stdenv ? pkgs.stdenv,
  makeWrapper ? pkgs.makeWrapper,
  gcc ? pkgs.gcc7,
  which ? pkgs.which,
  jemalloc ? pkgs.jemalloc,
  useJemalloc ? false,
  dfltSrc ? ./.,
  headerFilesOnly ? true
}:

stdenv.mkDerivation rec {
  name = "pbbslib";
  
  src = dfltSrc;

  buildInputs =
    if headerFilesOnly then
      [ ]
    else
      [ makeWrapper gcc which ]
      ++ (if useJemalloc then [ jemalloc ] else []);
  
  buildPhase =
    if headerFilesOnly then
      "mkdir _tmp; rmdir _tmp"
    else
      ''
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

  installPhase =
    let buildBinaries =
          if headerFilesOnly then
            ""
          else
            ''
            mkdir -p $out/strings/
            cp strings/*.h $out/strings/
            mkdir -p $out/test/
            mv test_scheduler_* test_alloc time_tests $out/test/
            mv strings/test_suffix_tree $out/test
            wrapProgram $out/test/test_suffix_tree \
              --prefix LD_LIBRARY_PATH ":" ${gcc.cc.lib}/lib64
            mkdir -p $out/examples/
            (cd examples
             mv mcss wc grep build_index primes longest_repeated_substring bw \
               $out/examples)
            '';
    in
    ''
    mkdir -p $out
    cp *.h $out/
    ${buildBinaries}
    '';
}   
