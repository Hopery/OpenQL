/**
 * @file   eqasm_backend_cc
 * @date   201809xx
 * @author Wouter Vlothuizen (wouter.vlothuizen@tno.nl)
 * @brief  eqasm backend for the Central Controller
 * @remark based on cc_light_eqasm_compiler.h, commit f34c0d9
 */

/*
    Todo:
    - allow runtime selection of scheduler
    - replace file output with strings
    - output timing diagram (~ tool Nader?) of gates vs qubit
    - idem waveform vs instrument

*/

#ifndef QL_ARCH_CC_EQASM_BACKEND_CC_H
#define QL_ARCH_CC_EQASM_BACKEND_CC_H

// constants:
#define CC_BACKEND_VERSION          "0.2.2"

// options:
#define OPT_CC_SCHEDULE_KERNEL_H    0       // 1=use scheduler from kernel.h iso cclight, overrides next option
#define OPT_CC_SCHEDULE_RC          0       // 1=use resource constraint scheduler

#include <ql/platform.h>
#include <ql/ir.h>
#include <ql/circuit.h>
#include <ql/scheduler.h>
#include <ql/eqasm_compiler.h>
#if OPT_CC_SCHEDULE_RC
 #include <ql/arch/cc_light/cc_light_resource_manager.h>
#endif
#include "codegen_cc.h"

using json = nlohmann::json;


// define classical QASM instructions as generated by classical.h
// FIXME: should be moved to a more sensible location
// FIXME: is "wait" also an instruction, or should we treat is as a scheduling hint only
#define QASM_CLASSICAL_INSTRUCTION_LIST   \
    X(QASM_ADD, "add") \
    X(QASM_SUB, "sub") \
    X(QASM_AND, "and") \
    X(QASM_OR, "or")   \
    X(QASM_XOR, "xor") \
    X(QASM_NOT, "not") \
    X(QASM_NOP, "nop") \
    X(QASM_LDI, "ldi") \
    X(QASM_MOV, "mov") \
    X(QASM_EQ, "eq")   \
    X(QASM_NE, "ne")   \
    X(QASM_LT, "lt")   \
    X(QASM_GT, "gt")   \
    X(QASM_LE, "le")   \
    X(QASM_GE, "ge")

#if 0   // FIXME
// generate enum for instructions
#define X(_enum, _string) _enum
enum eQASM {
    QASM_CLASSICAL_INSTRUCTION_LIST
};
#undef X
#endif


// generate constants for instructions
#define X(_enum, _string) const char *_enum = _string;
QASM_CLASSICAL_INSTRUCTION_LIST
#undef X





namespace ql
{
namespace arch
{

class eqasm_backend_cc : public eqasm_compiler
{
private: // vars
    codegen_cc codegen;
    int bundleIdx;

    // parameters from JSON file:
#if 0   // FIXME: unused
    size_t buffer_matrix[__operation_types_num__][__operation_types_num__];
#endif

public:
    eqasm_backend_cc()
    {
    }

    ~eqasm_backend_cc()
    {
    }

    // compile for Central Controller (CCCODE)
    // NB: a new eqasm_backend_cc is instantiated per call to compile, so we don't need to cleanup
    void compile(std::string prog_name, std::vector<quantum_kernel> kernels, const ql::quantum_platform &platform)
    {
#if 1   // FIXME: patch for issue #164, should be moved to caller
        if(kernels.size() == 0) {
            FATAL("Trying to compile empty kernel");
        }
#endif
        DOUT("Compiling " << kernels.size() << " kernels to generate CCCODE ... ");

        // init
        load_hw_settings(platform);
        codegen.init(platform);
        bundleIdx = 0;

        // generate program header
        codegen.program_start(prog_name);

        // generate code for all kernels
        for(auto &kernel : kernels) {
            IOUT("Compiling kernel: " << kernel.name);
            codegen_kernel_prologue(kernel);

#if OPT_CC_SCHEDULE_KERNEL_H    // FIXME: WIP
            // FIXME: try kernel.h::schedule()
            std::string kernel_sched_qasm;
            std::string kernel_sched_dot;
            kernel.schedule(platform, kernel_sched_qasm, kernel_sched_dot);
#else
            ql::circuit& ckt = kernel.c;
            if (!ckt.empty()) {
                auto creg_count = kernel.creg_count;     // FIXME: there is no platform.creg_count

#if OPT_CC_SCHEDULE_RC
                // schedule with platform resource constraints
                ql::ir::bundles_t bundles = cc_light_schedule_rc(ckt, platform, platform.qubit_number, creg_count);
#else
                // schedule without resource constraints
                /* FIXME: we use the "CC-light" scheduler, which actually has little platform specifics apart from
                 * requiring us to define a field "cc_light_instr" for every instruction in the JSON configuration file.
                 * That function could and should be generalized.
                 */
                ql::ir::bundles_t bundles = cc_light_schedule(ckt, platform, platform.qubit_number, creg_count);
#endif
#endif


#if 0   // FIXME: from CClight
                // write RC scheduled bundles with parallelism as simple QASM file
                std::stringstream sched_qasm;
                sched_qasm <<"qubits " << num_qubits << "\n\n"
                           << ".fused_kernels";
                string fname( ql::options::get("output_dir") + "/" + prog_name + "_scheduled_rc.qasm");
                IOUT("Writing Recourse-contraint scheduled CC-Light QASM to " << fname);
                sched_qasm << ql::ir::qasm(bundles);
                ql::utils::write_file(fname, sched_qasm.str());
#endif

                codegen_bundles(bundles, platform);
            } else {
                DOUT("Empty kernel: " << kernel.name);                      // NB: normal situation for kernels with classical control
            }

            codegen_kernel_epilogue(kernel);
        }

        codegen.program_finish();

        // write CCCODE to file
        std::string file_name(ql::options::get("output_dir") + "/" + prog_name + ".cccode");
        IOUT("Writing CCCODE to " << file_name);
        ql::utils::write_file(file_name, codegen.getCode());

        // write instrument map to file (unless we were using input file)
        std::string map_input_file = ql::options::get("backend_cc_map_input_file");
        if(map_input_file != "") {
            std::string file_name_map(ql::options::get("output_dir") + "/" + prog_name + ".map");
            IOUT("Writing instrument map to " << file_name_map);
            ql::utils::write_file(file_name_map, codegen.getMap());
        }

        DOUT("Compiling CCCODE [Done]");
    }


    void compile(std::string prog_name, ql::circuit& ckt, ql::quantum_platform& platform)
    {
        FATAL("Circuit compilation not implemented, because it does not support classical kernel operations");
    }

private:
    // based on cc_light_eqasm_compiler.h::classical_instruction2qisa/decompose_instructions
    // NB: input instructions defined in classical.h::classical
    void codegen_classical_instruction(ql::gate *classical_ins)
    {
        auto &iname =  classical_ins->name;
        auto &iopers = classical_ins->operands;
        int iopers_count = iopers.size();

        if(  (iname == QASM_ADD) || (iname == QASM_SUB) ||
             (iname == QASM_AND) || (iname == QASM_OR) || (iname == QASM_NOT) || (iname == QASM_XOR) ||
             (iname == QASM_LDI) || (iname == QASM_MOV) ||
             (iname == QASM_NOP)
          )

        {
            FATAL("Classical instruction not implemented: " << iname);

#if 0   // FIXME: adapt for CC, this is still CC-light
            ret << iname;
            for(int i=0; i<iopers_count; ++i)
            {
                if(i==iopers_count-1)
                    ret << " r" <<  iopers[i];
                else
                    ret << " r" << iopers[i] << ",";
            }
            if(iname == QASM_LDI)
            {
//                ret << ", " + std::to_string(classical_ins->imm_value);
            }
#endif
        }

        // inserted from decompose_instructions
        else if( (iname == QASM_EQ) || (iname == QASM_NE) || (iname == QASM_LT) ||
                 (iname == QASM_GT) || (iname == QASM_LE) || (iname == QASM_GE)
               )
        {
            FATAL("Classical instruction not implemented: " << iname);
        }
        else
        {
            FATAL("Unknown classical operation'" << iname << "' with'" << iopers_count << "' operands!");
        }
    }


    // get label from kernel name: FIXME: the label is the program name
    // FIXME: the kernel name has a structure (e.g. "sp1_for1_start" or "sp1_for1_start") which we use here. This should be made explicit
    // FIXME: looks very inefficient
    // extracted from get_epilogue
    std::string kernelLabel(ql::quantum_kernel &k)
    {
        std::string kname(k.name);
        std::replace(kname.begin(), kname.end(), '_', ' ');
        std::istringstream iss(kname);
        std::vector<std::string> tokens{ std::istream_iterator<std::string>{iss},
                                         std::istream_iterator<std::string>{} };
        return tokens[0];
    }

    // handle kernel conditionality at beginning of kernel
    // based on cc_light_eqasm_compiler.h::get_prologue
    void codegen_kernel_prologue(ql::quantum_kernel &k)
    {
        codegen.comment(SS2S("### Kernel: '" << k.name << "'"));

        // FIXME: insert waits to compensate latencies.

        switch(k.type) {
            case kernel_type_t::IF_START:
            {
                auto op0 = k.br_condition.operands[0]->id;
                auto op1 = k.br_condition.operands[1]->id;
                auto opName = k.br_condition.operation_name;
                codegen.if_start(op0, opName, op1);
                break;
            }

            case kernel_type_t::ELSE_START:
            {
                auto op0 = k.br_condition.operands[0]->id;
                auto op1 = k.br_condition.operands[1]->id;
                auto opName = k.br_condition.operation_name;
                codegen.else_start(op0, opName, op1);
                break;
            }

            case kernel_type_t::FOR_START:
            {
                std::string label = kernelLabel(k);
                codegen.for_start(label, k.iterations);
                break;
            }

            case kernel_type_t::DO_WHILE_START:
            {
                std::string label = kernelLabel(k);
                codegen.do_while_start(label);
                break;
            }

            case kernel_type_t::STATIC:
            case kernel_type_t::FOR_END:
            case kernel_type_t::DO_WHILE_END:
            case kernel_type_t::IF_END:
            case kernel_type_t::ELSE_END:
                // do nothing
                break;

            default:
                FATAL("inconsistency detected: unhandled kernel type");
                break;
        }
    }


    // handle kernel conditionality at end of kernel
    // based on cc_light_eqasm_compiler.h::get_epilogue
    void codegen_kernel_epilogue(ql::quantum_kernel &k)
    {
        // FIXME: insert waits to align kernel duration (in presence of latency compensation)

        switch(k.type) {
            case kernel_type_t::FOR_END:
            {
                std::string label = kernelLabel(k);
                codegen.for_end(label);
                break;
            }

            case kernel_type_t::DO_WHILE_END:
            {
                auto op0 = k.br_condition.operands[0]->id;
                auto op1 = k.br_condition.operands[1]->id;
                auto opName = k.br_condition.operation_name;
                std::string label = kernelLabel(k);
                codegen.do_while_end(label, op0, opName, op1);
                break;
            }

            case kernel_type_t::IF_END:
                // do nothing? FIXME
                break;

            case kernel_type_t::ELSE_END:
                // do nothing? FIXME
                break;

            case kernel_type_t::STATIC:
            case kernel_type_t::IF_START:
            case kernel_type_t::ELSE_START:
            case kernel_type_t::FOR_START:
            case kernel_type_t::DO_WHILE_START:
                // do nothing
                break;

            default:
                FATAL("inconsistency detected: unhandled kernel type");
                break;
        }
    }


    // based on cc_light_eqasm_compiler.h::bundles2qisa()
    void codegen_bundles(ql::ir::bundles_t &bundles, const ql::quantum_platform &platform)
    {
        IOUT("Generating CCCODE for bundles");

        codegen.kernel_start();
        for(ql::ir::bundle_t &bundle : bundles) {
            // generate bundle header
            codegen.bundle_start(SS2S("## Bundle " << bundleIdx++ <<
                                      ", start_cycle=" << bundle.start_cycle <<
                                      ", duration_in_cycles=" << bundle.duration_in_cycles << "):"));

            // generate code for this bundle
            for(auto section = bundle.parallel_sections.begin(); section != bundle.parallel_sections.end(); ++section ) {
                // check whether section defines classical gate
                ql::gate *firstInstr = *section->begin();
                auto firstInstrType = firstInstr->type();
                if(firstInstrType == __classical_gate__) {
                    if(section->size() != 1) {
                        FATAL("Inconsistency detected in bundle contents: classical gate with parallel sections");
                    }
                    codegen_classical_instruction(firstInstr);
                } else {
                    /* iterate over all instructions in section.
                     * NB: our strategy differs from cc_light_eqasm_compiler, we have no special treatment of first instruction
                     * and don't require all instructions to be identical
                     */
                    for(auto insIt = section->begin(); insIt != section->end(); ++insIt) {
                        ql::gate *instr = *insIt;
                        ql::gate_type_t itype = instr->type();
                        std::string iname = instr->name;

                        switch(itype) {
                            case __nop_gate__:       // a quantum "nop", see gate.h
                                codegen.nop_gate();
                                break;

                            case __classical_gate__:
                                FATAL("Inconsistency detected in bundle contents: classical gate found after first section (which itself was non-classical)");
                                break;

                            case __custom_gate__:
                                codegen.custom_gate(iname, instr->operands, instr->creg_operands, instr->duration, instr->angle);
                                break;

                            case __display__:
                                FATAL("Gate type __display__ not supported");           // QX specific, according to openql.pdf
                                break;

                            case __measure_gate__:
                                FATAL("Gate type __measure_gate__ not supported");      // no use, because there is no way to define CC-specifics
                                break;

                            default:
                                FATAL("Unsupported gate type: " << itype);
                        }   // switch(itype)
                    } // for(section...)
                }
            }

            // generate bundle trailer, and code for classical gates
            bool isLastBundle = &bundle==&bundles.back();
            codegen.bundle_finish(bundle.start_cycle, bundle.duration_in_cycles, isLastBundle);
        }   // for(bundles)
        codegen.kernel_finish();

        IOUT("Generating CCCODE for bundles [Done]");
    }


    // based on: cc_light_eqasm_compiler.h::load_hw_settings
    void load_hw_settings(const ql::quantum_platform& platform)
    {
#if 0   // NB: Visual Studio does not like empty array
        const struct {
            size_t  *var;
            std::string name;
        } hw_settings[] = {
#if 0   // FIXME: unused. Convert to cycle
            { &mw_mw_buffer,            "mw_mw_buffer" },
            { &mw_flux_buffer,          "mw_flux_buffer" },
            { &mw_readout_buffer,       "mw_readout_buffer" },
            { &flux_mw_buffer,          "flux_mw_buffer" },
            { &flux_flux_buffer,        "flux_flux_buffer" },
            { &flux_readout_buffer,     "flux_readout_buffer" },
            { &readout_mw_buffer,       "readout_mw_buffer" },
            { &readout_flux_buffer,     "readout_flux_buffer" },
            { &readout_readout_buffer,  "readout_readout_buffer" }
#endif
        };

        DOUT("Loading hardware settings ...");
        size_t i=0;
        try
        {
            for(i=0; i<ELEM_CNT(hw_settings); i++) {
                size_t val = platform.hardware_settings[hw_settings[i].name];
                *hw_settings[i].var = val;
            }
        }
        catch (json::exception &e)
        {
            throw ql::exception(
                "[x] error : ql::eqasm_compiler::compile() : error while reading hardware settings : parameter '"
                + hw_settings[i].name
                + "'\n\t"
                + std::string(e.what()), false);
        }
    }
#endif
}; // class

} // arch
} // ql

#endif // QL_ARCH_CC_EQASM_BACKEND_CC_H

