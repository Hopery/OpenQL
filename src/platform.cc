/**
 * @file   platform.cc
 * @date   11/2016
 * @author Nader Khammassi
 * @brief  platform header for target-specific compilation
 */

#include "platform.h"

namespace ql {

// FIXME: constructed object is not usable
quantum_platform::quantum_platform() : name("default") {
}

quantum_platform::quantum_platform(
    const std::string &name,
    const std::string &configuration_file_name
) :
    name(name),
    configuration_file_name(configuration_file_name)
{
    ql::hardware_configuration hwc(configuration_file_name);
    hwc.load(instruction_map, instruction_settings, hardware_settings, resources, topology, aliases);
    eqasm_compiler_name = hwc.eqasm_compiler_name;
    QL_DOUT("eqasm_compiler_name= " << eqasm_compiler_name);

    if (hardware_settings.count("qubit_number") <= 0) {
        QL_FATAL("qubit number of the platform is not specified in the configuration file !");
    } else {
        qubit_number = hardware_settings["qubit_number"];
    }

    // FIXME: add creg_count to JSNN file and platform

    if (hardware_settings.count("cycle_time") <= 0) {
        QL_FATAL("cycle time of the platform is not specified in the configuration file !");
    } else {
        cycle_time = hardware_settings["cycle_time"];
    }
}

/**
 * display information about the platform
 */
void quantum_platform::print_info() const {
    QL_PRINTLN("[+] platform name      : " << name);
    QL_PRINTLN("[+] qubit number       : " << qubit_number);
    QL_PRINTLN("[+] eqasm compiler     : " << eqasm_compiler_name);
    QL_PRINTLN("[+] configuration file : " << configuration_file_name);
    QL_PRINTLN("[+] supported instructions:");
    for (const auto &i : instruction_map) {
        QL_PRINTLN("  |-- " << i.first);
    }
}

size_t quantum_platform::get_qubit_number() const {
    return qubit_number;
}

// find settings for custom gate, preventing JSON exceptions
const utils::Json &quantum_platform::find_instruction(const std::string &iname) const {
    // search the JSON defined instructions, to prevent JSON exception if key does not exist
    if (!QL_JSON_EXISTS(instruction_settings, iname)) {
        QL_FATAL("JSON file: instruction not found: '" << iname << "'");
    }
    return instruction_settings[iname];
}


// find instruction type for custom gate
std::string quantum_platform::find_instruction_type(const std::string &iname) const {
    const utils::Json &instruction = find_instruction(iname);
    if (!QL_JSON_EXISTS(instruction, "type")) {
        QL_FATAL("JSON file: field 'type' not defined for instruction '" << iname << "'");
    }
    return instruction["type"];
}

size_t quantum_platform::time_to_cycles(float time_ns) const {
    return std::ceil(time_ns / cycle_time);
}

} // namespace ql
