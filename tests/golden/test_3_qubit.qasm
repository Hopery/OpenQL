# Generated by OpenQL 0.9.0 for program 3_qubit_program
version 1.2

pragma @ql.name("3_qubit_program")


.aKernel
    prepz q[0]
    prepz q[1]
    prepz q[2]
    toffoli q[0], q[1], q[2]
    measure q[2]
