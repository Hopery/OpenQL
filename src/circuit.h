/**
 * @file   circuit.h
 * @date   11/2016
 * @author Nader Khammassi
 * @brief  circuit (i.e. gate container) implementation
 */

#pragma once

#include "utils/str.h"
#include "utils/vec.h"
#include "gate.h"

namespace ql {

typedef utils::Vec<gate*> circuit;

void print(const circuit &c);

/**
 * generate qasm for a given circuit
 */
utils::Str qasm(const circuit &c);

utils::Vec<circuit*> split_circuit(circuit &x);

/**
 * detect measurements and qubit preparations
 */
bool contains_measurements(const circuit &x);

} // namespace ql
