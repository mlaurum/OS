# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(seek-past-end) begin
(seek-past-end) should be zero and was 0
(seek-past-end) end
seek-past-end: exit(0)
EOF
pass;
