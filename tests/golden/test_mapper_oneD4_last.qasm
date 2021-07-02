# Generated by OpenQL 0.9.0 for program test_mapper_oneD4
version 1.2

pragma @ql.name("test_mapper_oneD4")


.kernel_oneD4
    y q[4]
    { # start at cycle 1
        x q[2]
        ym90 q[0]
        ym90 q[1]
    }
    { # start at cycle 2
        cz q[2], q[0]
        cz q[4], q[1]
    }
    skip 1
    { # start at cycle 4
        y90 q[0]
        ym90 q[2]
        y90 q[1]
        ym90 q[4]
    }
    { # start at cycle 5
        cz q[0], q[2]
        cz q[1], q[4]
    }
    skip 1
    { # start at cycle 7
        y90 q[2]
        ym90 q[0]
        y90 q[4]
        ym90 q[1]
    }
    { # start at cycle 8
        cz q[2], q[0]
        cz q[4], q[1]
    }
    skip 1
    { # start at cycle 10
        y90 q[1]
        ym90 q[3]
    }
    cz q[1], q[3]
    skip 1
    { # start at cycle 13
        y90 q[3]
        ym90 q[1]
    }
    cz q[3], q[1]
    skip 1
    { # start at cycle 16
        ym90 q[3]
        y90 q[0]
    }
    cz q[0], q[3]
    y90 q[1]
    cz q[1], q[3]
    skip 1
    { # start at cycle 21
        x q[0]
        ym90 q[3]
    }
