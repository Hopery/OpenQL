# Generated by OpenQL 0.9.0 for program test_fast_feedback
version 1.2

pragma @ql.name("test_fast_feedback")


.aKernel
    { # start at cycle 0
        prepz q[0]
        cprepz q[1]
    }
    skip 1
    { # start at cycle 2
        measure q[0]
        measure q[1]
    }
    skip 1
