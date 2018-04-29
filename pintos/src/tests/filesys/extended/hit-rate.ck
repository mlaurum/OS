# -*- perl -*-
use strict;
use warnings;
use tests::tests;
use tests::random;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(hit-rate) begin
(hit-rate) open "cache-test.txt"
(hit-rate) read "cache-test.txt"
(hit-rate) second open "cache-test.txt"
(hit-rate) second read "cache-test.txt"
(hit-rate) New hits should be greater than initial hits: 1, expected 1
(hit-rate) end
EOF
pass;
