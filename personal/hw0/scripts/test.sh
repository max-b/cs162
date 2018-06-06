#!/bin/bash
patt='([[:digit:]]+)[[:space:]]+([[:digit:]]+)[[:space:]]+([[:digit:]]+)';

success=1;

for file in ./*.c;
do
  valid_out=$(wc $file);
  test_out=$(./build/wc $file);

  [[ $valid_out =~ $patt ]]
  valid_lines=${BASH_REMATCH[1]};
  valid_words=${BASH_REMATCH[2]};
  valid_characters=${BASH_REMATCH[3]};

  [[ $test_out =~ $patt ]]
  test_lines=${BASH_REMATCH[1]};
  test_words=${BASH_REMATCH[2]};
  test_characters=${BASH_REMATCH[3]};


  if [[ $test_lines -ne $valid_lines || $test_words -ne $valid_words || $test_characters -ne $valid_characters ]]; then
    echo "Failed on $file";
    echo "Valid: $valid_out";
    echo "lines: $valid_lines, words: $valid_words, characters: $valid_characters";
    echo "Test: $test_out";
    echo "lines: $test_lines, words: $test_words, characters: $test_characters";
    success=0;
  fi

done

if [[ success -eq 1 ]]; then
  echo "Passed";
fi
