# -*- perl -*-
use strict;
use warnings;
use tests::tests;
use tests::random;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(rw-counts) begin
(rw-counts) open "empty-file.txt"
(rw-counts) There should only be one inode metadata read: 1, expected 1
(rw-counts) end
EOF
pass;
