# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(tell) begin
(tell) 10 is the proper position and was 10
(tell) end
tell: exit(0)
EOF
pass;
