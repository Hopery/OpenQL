# Generated by OpenQL 0.9.0 for program test_cnot_NN_noncommute_DAR
version 1.2

pragma @ql.name("test_cnot_NN_noncommute_DAR")


.aKernel
    cnot q[3], q[5]
    skip 3
    cnot q[0], q[3]
    skip 3
    x45 q[0]
    x q[0]
    x45 q[0]
