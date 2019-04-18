/**
 * @file    codegen_cc.h
 * @date    201810xx
 * @author  Wouter Vlothuizen (wouter.vlothuizen@tno.nl)
 * @brief   code generator backend for the Central Controller
 * @note    here we don't check whether the sequence of calling code generator
 *          functions is correct
 */

// FIXME: this should be the only backend specific code


#ifndef QL_ARCH_CC_CODEGEN_CC_H
#define QL_ARCH_CC_CODEGEN_CC_H

#include <cstddef>  // for size_t etc.

// MS Visual C++ does not know about ssize_t
#ifdef _MSC_VER
  #include <type_traits>
  typedef std::make_signed<size_t>::type ssize_t;
#endif

namespace ql
{
namespace arch
{


class codegen_cc
{
private: // types
    typedef struct {
        int slotIdx;            // index into cc_setup["slots"]. FIXME: outdated comment
        int group;
    } tSignalInfo;

    typedef struct {
        std::string signalValue;
        size_t duration;
        ssize_t readoutCop;     // NB: we use ssize_t iso size_t so we can encode 'unused' (-1)
    } tGroupInfo;

private: // vars
    static const int MAX_SLOTS = 12;
    static const int MAX_GROUPS = 32;                           // enough for VSM

    bool verboseCode = true;                                    // output extra comments in generated code. FIXME: not yet configurable
    bool mapPreloaded = false;

    std::stringstream cccode;                                   // the code generated for the CC

    // codegen state
    std::vector<std::vector<tGroupInfo>> groupInfo;             // matrix[slotIdx][group]
    json codewordTable;                                         // codewords versus signals per instrument group
    json inputLutTable;                                         // input LUT usage per instrument group
    size_t lastStartCycle[MAX_SLOTS];

    // some JSON nodes we need access to. FIXME: use pointers/const json & for efficiency?
    json jsonBackendSettings;
    json jsonInstrumentDefinitions;
    json jsonControlModes;
    json jsonInstruments;
    json jsonSignals;

    const ql::quantum_platform *platform;

public:
    codegen_cc()
    {
    }


    ~codegen_cc()
    {
    }

    /************************************************************************\
    | Generic
    \************************************************************************/

    void init(const ql::quantum_platform &platform)
    {
        // NB: a new eqasm_backend_cc is instantiated per call to compile, and
        // as a result also a codegen_cc, so we don't need to cleanup
        this->platform = &platform;
        load_backend_settings();

        // optionally preload codewordTable
        std::string map_input_file = ql::options::get("backend_cc_map_input_file");
        if(map_input_file != "") {
            DOUT("loading map_input_file='" << map_input_file << "'");
            json map = load_json(map_input_file);
            codewordTable = map["codeword_table"];
            mapPreloaded = true;
        }
    }

    std::string getCode()
    {
        return cccode.str();
    }

    std::string getMap()
    {
        json map;

        map["note"] = "generated by OpenQL CC backend vesrion " CC_BACKEND_VERSION;
        map["codeword_table"] = codewordTable;
    //    map["inputLut_table"] = inputLutTable;
        return SS2S(std::setw(4) << map << std::endl);
    }

    void program_start(std::string prog_name)
    {
        // emit program header
        cccode << std::left;                                // assumed by emit()
        cccode << "# CC_BACKEND_VERSION " << CC_BACKEND_VERSION << std::endl;
        cccode << "# Program: '" << prog_name << "'" << std::endl;
        cccode << "# Note:    generated by OpenQL Central Controller backend" << std::endl;
        cccode << "#" << std::endl;

        latencyCompensation();  // FIXME: does not support measuring yet
    }

    void program_finish()
    {
        emit("", "stop");                                  // NB: cc_light loops whole program indefinitely
    }

    void kernel_start()
    {
        utils::zero(lastStartCycle);       // FIXME: actually, bundle.start_cycle starts counting at 1
    }

    void kernel_finish()
    {
    }

    // bundle_start: clear groupInfo, which maintains the work that needs to be performed for bundle
    void bundle_start(std::string cmnt)
    {
        size_t slotsUsed = jsonInstruments.size();   // FIXME: assuming all instruments use a slot
        groupInfo.assign(slotsUsed, std::vector<tGroupInfo>(MAX_GROUPS, {"", 0, -1}));

        comment(cmnt);
    }

    // bundle_finish: generate code for bundle from information collected in groupInfo (which may be empty if no classical gates are present in bundle)
    void bundle_finish(size_t start_cycle, size_t duration_in_cycles, bool isLastBundle)
    {
        if(isLastBundle) {
            comment(SS2S(" # last bundle, will pad outputs to match durations"));
        }

        // iterate over instruments
        for(size_t instrIdx=0; instrIdx<jsonInstruments.size(); instrIdx++) {
            const json &instrument = jsonInstruments[instrIdx];
            std::string instrumentName = instrument["name"];
            int slot = instrument["controller"]["slot"];    // FIXME: assuming controller being cc
            int slotIdx = instrIdx; // FIXME

            // collect info for all groups within slot, i.e. one connected instrument
            // FIXME: the term 'group' is used in a diffused way: 1) index of signal vectors, 2) ...
            bool isSlotUsed = false;
            uint32_t digOut = 0;
            uint32_t digIn = 0;
            uint32_t slotDurationInCycles = 0;                                  // maximum duration over groups that are used
            size_t nrGroups = groupInfo[slotIdx].size();
            for(size_t group=0; group<nrGroups; group++) {                      // iterate over groups used within slot
                if(groupInfo[slotIdx][group].signalValue != "") {
                    isSlotUsed = true;

                    // find control mode & bits
                    // FIXME: check existence of keys below to ease end user debugging on configuration errors
                    std::string controlModeRef = instrument["ref_control_mode"];
                    const json &controlMode = jsonControlModes[controlModeRef];    // the control mode definition for our instrument
                    int nrControlBitsGroups = controlMode["control_bits"].size();

                    int controlModeGroup = -1;
                    if(nrControlBitsGroups == 1) {                  // vector mode: group addresses channel within vector
                        controlModeGroup = 0;
                    } else if(group < nrControlBitsGroups) {        // normal mode: group selects control group
                        controlModeGroup = group;
                    } else {
                        FATAL("instrument '" << instrumentName
                              << "' uses " << nrGroups
                              << " groups, but control mode '" << controlModeRef
                              << "' only defines " << nrControlBitsGroups);
                    }
                    const json &groupControlBits = controlMode["control_bits"][controlModeGroup];

                    // find or create codeword/mask fragment for this group
                    DOUT("instrumentName="  <<
                         ", slot=" << slot <<
                         ", control mode group=" << controlModeGroup <<
                         ", group control bits: " << groupControlBits);
                    size_t nrGroupControlBits = groupControlBits.size();
                    if(nrGroupControlBits == 1) {      // single bit, implying this is a mask (not code word)
                        digOut |= 1<<(int)groupControlBits[0];     // NB: we assume the mask is active high, which is correct for VSM and UHF-QC
                        // FIXME: check controlModeGroup vs group
                    } else {                // > 1 bit, implying code word
                        // FIXME allow single code word for vector of groups. Requires looking at all signals before assigning code word
                        if(group != controlModeGroup) {
                            FATAL("vector mode not yet supported");
                        }

                        // try to find code word
                        uint32_t codeWord = 0;
                        std::string signalValue = groupInfo[slotIdx][group].signalValue;

                        if(JSON_EXISTS(codewordTable, instrumentName) &&                    // instrument exists
                                        codewordTable[instrumentName].size() > group) {     // group exists
                            bool cwFound = false;
                            // try to find signalValue
                            json &myCodewordArray = codewordTable[instrumentName][group];
                            for(codeWord=0; codeWord<myCodewordArray.size() && !cwFound; codeWord++) {   // NB: JSON find() doesn't work for arrays
                                if(myCodewordArray[codeWord] == signalValue) {
                                    DOUT("signal value found at cw=" << codeWord);
                                    cwFound = true;
                                }
                            }
                            if(!cwFound) {
                                std::string msg = SS2S("signal value '" << signalValue
                                        << "' not found in group " << group
                                        << ", which contains " << myCodewordArray);
                                if(mapPreloaded) {
                                    FATAL("mismatch between preloaded 'backend_cc_map_input_file' and program requirements:" << msg)
                                } else {
                                    DOUT(msg);
                                    // NB: codeWord already contains last used value + 1
                                    // FIXME: check that number is available
                                    myCodewordArray[codeWord] = signalValue;                    // NB: structure created on demand
                                }
                            }
                        } else {    // new instrument or group
                            if(mapPreloaded) {
                                FATAL("mismatch between preloaded 'backend_cc_map_input_file' and program requirements: instrument '"
                                      << instrumentName << "', group "
                                      << group
                                      << " not present in file");
                            } else {
                                codeWord = 1;
                                codewordTable[instrumentName][group][0] = "";                   // code word 0 is empty
                                codewordTable[instrumentName][group][codeWord] = signalValue;   // NB: structure created on demand
                            }
                        }

                        // convert codeWord to digOut
                        for(size_t idx=0; idx<nrGroupControlBits; idx++) {
                            int codeWordBit = nrGroupControlBits-1-idx;    // groupControlBits defines MSB..LSB
                            if(codeWord & (1<<codeWordBit)) digOut |= 1<<(int)groupControlBits[idx];
                        }
                    }

                    // add trigger to digOut
                    size_t nrTriggerBits = controlMode["trigger_bits"].size();
                    if(nrTriggerBits == 0) {         // no trigger
                        // do nothing
                    } else if(nrTriggerBits == 1) {  // single trigger for all groups
                        digOut |= 1 << (int)controlMode["trigger_bits"][0];
                    } else {                        // trigger per group
                        digOut |= 1 << (int)controlMode["trigger_bits"][group];
                        // FIXME: check validity of nrTriggerBits
                    }

                    // compute slot duration
                    size_t durationInCycles = platform->time_to_cycles(groupInfo[slotIdx][group].duration);
                    if(durationInCycles > slotDurationInCycles) slotDurationInCycles = durationInCycles;

                    // handle readout
                    // NB: this does not allow for readout without signal generation (by the same instrument), which might be needed in the future
                    // FIXME: move out of this loop
                    // FIXME: test for groupInfo[slotIdx][group].readoutCop >= 0
                    if(JSON_EXISTS(controlMode, "result_bits")) {                               // this instrument mode produces results
                        const json &resultBits = controlMode["result_bits"][group];
                        size_t nrResultBits = resultBits.size();
                        if(nrResultBits == 1) {                     // single bit
                            digIn |= 1<<(int)resultBits[0];         // NB: we assume the result is active high, which is correct for UHF-QC

#if 0   // FIXME: WIP on measurement
                            // FIXME: save groupInfo[slotIdx][group].readoutCop in inputLut
                            if(JSON_EXISTS(inputLutTable, instrumentName) &&                    // instrument exists
                                            inputLutTable[instrumentName].size() > group) {     // group exists
                            } else {    // new instrument or group
                                codeWord = 1;
                                inputLutTable[instrumentName][group][0] = "";                   // code word 0 is empty
                                inputLutTable[instrumentName][group][codeWord] = signalValue;   // NB: structure created on demand
                            }
#endif
                        } else {    // NB: nrResultBits==0 will not arrive at this point
                            std::string controlModeName = controlMode;                          // convert to string
                            FATAL("JSON key '" << controlModeName << "/result_bits' must have 1 bit per group");
                        }
                    }
                } // if signal defined
            } // for group


            // generate code for slot
            if(isSlotUsed) {
                DOUT("bundle_finish(): slot=" << slot <<
                     ", lastStartCycle[slotIdx]=" << lastStartCycle[slotIdx] <<
                     ", start_cycle=" << start_cycle <<
                     ", slotDurationInCycles=" << slotDurationInCycles <<
                     ", instrumentName=" << instrumentName);

                padToCycle(lastStartCycle[slotIdx], start_cycle, slot, instrumentName);

                // emit code for slot
                emit(SS2S("[" << slot << "]").c_str(),      // CCIO selector
                     "seq_out",
                     SS2S("0x" << std::hex << std::setfill('0') << std::setw(8) << digOut << std::dec <<
                          "," << slotDurationInCycles),
                     SS2S("# cycle " << start_cycle << "-" << start_cycle+slotDurationInCycles << ": code word/mask on '" << instrumentName+"'").c_str());

                // update lastStartCycle
                lastStartCycle[slotIdx] = start_cycle + slotDurationInCycles;

                if(digIn) { // FIXME
                    comment(SS2S("# digIn=" << digIn));
                    // get qop
                    // get cop
                    // get/assign LUT
                    // seq_in_sm
                }
            } else {
                // nothing to do, we delay emitting till a slot is used or kernel finishes (i.e. isLastBundle just below)
            }

            // pad end of bundle to align durations
            if(isLastBundle) {
                padToCycle(lastStartCycle[slotIdx], start_cycle+duration_in_cycles, slot, instrumentName);
                // FIXME: also pad latency differences, here or in kernel_finish?
            }
        } // for(slotIdx)

        comment("");    // blank line to separate bundles
    }

    void comment(std::string c)
    {
        if(verboseCode) emit(c.c_str());
    }

    /************************************************************************\
    | Quantum instructions
    \************************************************************************/

    // single/two/N qubit gate, including readout
    void custom_gate(std::string iname, std::vector<size_t> qops, std::vector<size_t> cops, size_t duration, double angle)
    {
#if 1   // FIXME: test for angle parameter
        if(angle != 0.0) {
            DOUT("iname=" << iname << ", angle=" << angle);
        }
#endif
        bool isReadout = false;

        if("readout" == platform->find_instruction_type(iname))          // handle readout
        /* FIXME: we only use the "readout" instruction_type and don't care about the rest because the terms "mw" and "flux" don't fully
         * cover gate functionality. It would be nice if custom gates could mimic ql::gate_type_t
         * We could also infer readout from cops/qops.size()
        */
        {
            isReadout = true;

            if(cops.size() != 1) {
                FATAL("Readout instruction requires exactly 1 classical operand, not " << cops.size());
                // FIXME: CClight also seems to support nCoperands=0
            }
            if(qops.size() != 1) {
                FATAL("Readout instruction requires exactly 1 qubit operand, not " << qops.size());
            }

            comment(SS2S(" # READOUT: " << iname << "(c" << cops[0] << ",q" << qops[0] << ")"));
        } else { // handle all other instruction types
            // generate comment. NB: we don't have a particular limit for the number of operands
            std::stringstream cmnt;
            cmnt << " # gate '" << iname << " ";
            for(size_t i=0; i<qops.size(); i++) {
                cmnt << qops[i];
                if(i<qops.size()-1) cmnt << ",";
            }
            cmnt << "'";
            comment(cmnt.str());
        }

        // find signal definition for iname
        const json &instruction = platform->find_instruction(iname);
        const json *tmp;
        if(JSON_EXISTS(instruction["cc"], "signal_ref")) {
            std::string signalRef = instruction["cc"]["signal_ref"];
            tmp = &jsonSignals[signalRef];  // poor man's JSON pointer
            if(tmp->size() == 0) {
                FATAL("Error in JSON definition of instruction '" << iname <<
                      "': signal_ref '" << signalRef << "' does not resolve");
            }
        } else {
            tmp = &instruction["cc"]["signal"];
            DOUT("signal for '" << instruction << "': " << *tmp);
        }
        const json &signal = *tmp;

        // iterate over signals defined in instruction
        for(size_t s=0; s<signal.size(); s++) {
            // get the qubit to work on
            size_t operandIdx = signal[s]["operand_idx"];
            if(operandIdx >= qops.size()) {
                FATAL("Error in JSON definition of instruction '" << iname <<
                      "': illegal operand number " << operandIdx <<
                      "' exceeds expected maximum of " << qops.size()-1)
            }
            size_t qubit = qops[operandIdx];

            // get the instrument and group that generates the signal
            std::string instructionSignalType = signal[s]["type"];
            const json &instructionSignalValue = signal[s]["value"];
            tSignalInfo si = findSignalInfoForQubit(instructionSignalType, qubit);
            const json &instrument = jsonInstruments[si.slotIdx];
            std::string instrumentName = instrument["name"];
            int slot = instrument["controller"]["slot"];

            // expand macros in signalValue
            std::string signalValueString = SS2S(instructionSignalValue);   // serialize instructionSignalValue into std::string
            utils::replace(signalValueString, std::string("{gateName}"), iname);
            utils::replace(signalValueString, std::string("{instrumentName}"), instrumentName);
            utils::replace(signalValueString, std::string("{instrumentGroup}"), std::to_string(si.group));
            // FIXME: allow using all qubits involved (in same signalType?, or refer to signal: qubitOfSignal[n]), e.g. qubit[0], qubit[1], qubit[2]
            utils::replace(signalValueString, std::string("{qubit}"), std::to_string(qubit));

            comment(SS2S("  # slot=" << slot <<
                         ", group=" << si.group <<
                         ", instrument='" << instrumentName <<
                         "', signal='" << signalValueString << "'"));

            // check and store signal value
            if(groupInfo[si.slotIdx][si.group].signalValue == "") {                         // signal not yet used
                groupInfo[si.slotIdx][si.group].signalValue = signalValueString;
            } else if(groupInfo[si.slotIdx][si.group].signalValue == signalValueString) {   // signal unchanged
                // do nothing
            } else {
                EOUT("Code so far:\n" << cccode.str());                    // FIXME: provide context to help finding reason
                FATAL("Signal conflict on instrument='" << instrumentName <<
                      "', group=" << si.group <<
                      ", between '" << groupInfo[si.slotIdx][si.group].signalValue <<
                      "' and '" << signalValueString << "'");
            }

            if(isReadout) {
                // remind the classical operand used
                groupInfo[si.slotIdx][si.group].readoutCop = cops[0];
            }

            groupInfo[si.slotIdx][si.group].duration = duration;

            DOUT("custom_gate(): iname='" << iname <<
                 "', duration=" << duration <<
                 "[ns], si.slotIdx=" << si.slotIdx <<
                 ", si.group=" << si.group);

            // NB: code is generated in bundle_finish()
        }   // for(signal)
    }

    void nop_gate()
    {
        comment("# NOP gate");
        FATAL("FIXME: NOP gate not implemented");
    }

    /************************************************************************\
    | Classical operations on kernels
    \************************************************************************/

    void if_start(size_t op0, std::string opName, size_t op1)
    {
        comment(SS2S("# IF_START(R" << op0 << " " << opName << " R" << op1 << ")"));
        FATAL("FIXME: not implemented");
    }

    void else_start(size_t op0, std::string opName, size_t op1)
    {
        comment(SS2S("# ELSE_START(R" << op0 << " " << opName << " R" << op1 << ")"));
        FATAL("FIXME: not implemented");
    }

    void for_start(std::string label, int iterations)
    {
        comment(SS2S("# FOR_START(" << iterations << ")"));
        // FIXME: reserve register
        emit((label+":").c_str(), "move", SS2S(iterations << ",R63"), "# R63 is the 'for loop counter'");        // FIXME: fixed reg, no nested loops
    }

    void for_end(std::string label)
    {
        comment("# FOR_END");
        // FIXME: free register
        emit("", "loop", SS2S("R63,@" << label), "# R63 is the 'for loop counter'");        // FIXME: fixed reg, no nested loops
    }

    void do_while_start(std::string label)
    {
        comment("# DO_WHILE_START");
        emit((label+":").c_str(), "", SS2S(""), "# ");        // FIXME: just a label
    }

    void do_while_end(std::string label, size_t op0, std::string opName, size_t op1)
    {
        comment(SS2S("# DO_WHILE_END(R" << op0 << " " << opName << " R" << op1 << ")"));
        emit("", "jmp", SS2S("@" << label), "# endless loop'");        // FIXME: just endless loop
    }

    /************************************************************************\
    | Classical arithmetic instructions
    \************************************************************************/

    void add() {
        FATAL("FIXME: not implemented");
    }

    // FIXME: etc

    /************************************************************************\
    | Some helpers to ease nice assembly formatting
    \************************************************************************/

    void emit(const char *labelOrComment, const char *instr="")
    {
        if(!labelOrComment || strlen(labelOrComment)==0) {  // no label
            cccode << "        " << instr << std::endl;
        } else if(strlen(labelOrComment)<8) {               // label fits before instr
            cccode << std::setw(8) << labelOrComment << instr << std::endl;
        } else if(strlen(instr)==0) {                       // no instr
            cccode << labelOrComment << std::endl;
        } else {
            cccode << labelOrComment << std::endl << "        " << instr << std::endl;
        }
    }

    // @param   label       must include trailing ":"
    // @param   comment     must include leading "#"
    void emit(const char *label, const char *instr, std::string qops, const char *comment="")
    {
        cccode << std::setw(8) << label << std::setw(8) << instr << std::setw(24) << qops << comment << std::endl;
    }
    // FIXME: also provide the above with std::string parameters

    void latencyCompensation()
    {
        std::map<int, int> slotLatencies;   // maps slot to latency

        // get latencies per slot, iterating over instruments
        for(size_t instrIdx=0; instrIdx<jsonInstruments.size(); instrIdx++) {
            const json &instrument = jsonInstruments[instrIdx];
            std::string instrumentName = instrument["name"];
            std::string instrumentRef = instrument["ref_instrument_definition"];
            int slot = instrument["controller"]["slot"];    // FIXME: assuming controller being cc

            // find latency
            const json &id = findInstrumentDefinition(instrumentRef);
            int latency = id["latency"];
            DOUT("latency of '" << instrumentRef << "' in slot " << slot << " is " << latency);
            slotLatencies.insert(std::make_pair(slot, latency));
        }

        // find max latency
        int maxLatency = 0;
        for(auto it=slotLatencies.begin(); it!=slotLatencies.end(); it++) {
            int latency = it->second;
            if(latency > maxLatency) maxLatency = latency;
        }
        DOUT("maxLatency = " << maxLatency);

        // align latencies
        comment("# synchronous start and latency compensation");
        for(auto it=slotLatencies.begin(); it!=slotLatencies.end(); it++) {
            int slot = it->first;
            int latency = it->second;
            const int minDelay = 1;     // min value for seq_bar
            int delayInCycles = minDelay + platform->time_to_cycles(maxLatency-latency);

            emit(SS2S("[" << slot << "]").c_str(),      // CCIO selector
                "seq_bar",
                SS2S(delayInCycles),
                SS2S("# latency compensation").c_str());    // FIXME: add instrumentName/instrumentRef/latency
        }
    }

    void padToCycle(size_t lastStartCycle, size_t start_cycle, int slot, std::string instrumentName)
    {
        // compute prePadding: time to bridge to align timing
        ssize_t prePadding = start_cycle - lastStartCycle;
        if(prePadding < 0) {
            EOUT(getCode());        // show what we made
            FATAL("Inconsistency detected in bundle contents: time travel not yet possible in this version: prePadding=" << prePadding <<
                  ", start_cycle=" << start_cycle <<
                  ", lastStartCycle=" << lastStartCycle);
        }

        if(prePadding > 0) {     // we need to align
            emit(SS2S("[" << slot << "]").c_str(),      // CCIO selector
                "seq_out",
                SS2S("0x00000000," << prePadding),
                SS2S("# cycle " << lastStartCycle << "-" << start_cycle << ": padding on '" << instrumentName+"'").c_str());
        }
    }

    /************************************************************************\
    | Functions processing JSON
    \************************************************************************/

    void load_backend_settings()
    {
        // remind some main JSON areas
        jsonBackendSettings = platform->hardware_settings["eqasm_backend_cc"];
        jsonInstrumentDefinitions = jsonBackendSettings["instrument_definitions"];
        jsonControlModes = jsonBackendSettings["control_modes"];
        jsonInstruments = jsonBackendSettings["instruments"];
        jsonSignals = jsonBackendSettings["signals"];


#if 0   // FIXME: print some info, which also helps detecting errors early on
        // read instrument definitions
        // FIXME: the following requires json>v3.1.0:  for(auto& id : jsonInstrumentDefinitions.items()) {
        for(size_t i=0; i<jsonInstrumentDefinitions.size(); i++) {
            std::string idName = jsonInstrumentDefinitions[i];        // NB: uses type conversion to get node value
            DOUT("found instrument definition: '" << idName <<"'");
        }

        // read control modes
        for(size_t i=0; i<jsonControlModes.size(); i++)
        {
            const json &name = jsonControlModes[i]["name"];
            DOUT("found control mode '" << name <<"'");
        }

        // read instruments
        const json &ccSetupType = jsonCcSetup["type"];

        // CC specific
        const json &ccSetupSlots = jsonCcSetup["slots"];                      // FIXME: check against jsonInstrumentDefinitions
        for(size_t slot=0; slot<ccSetupSlots.size(); slot++) {
            const json &instrument = ccSetupSlots[slot]["instrument"];
            std::string instrumentName = instrument["name"];
            std::string signalType = instrument["ref_signals_type"];

            DOUT("found instrument: name='" << instrumentName << "', signal type='" << signalType << "'");
        }
#endif
    }


    const json &findInstrumentDefinition(const std::string &name)
    {
        if JSON_EXISTS(jsonInstrumentDefinitions, name) {
            return jsonInstrumentDefinitions[name];
        } else {
            FATAL("Could not find key 'name'=" << name << "in JSON section 'instrument_definitions'");
        }
    }


    // find instrument/group providing instructionSignalType for qubit
    tSignalInfo findSignalInfoForQubit(std::string instructionSignalType, size_t qubit)
    {
        tSignalInfo ret = {-1, -1};
        bool signalTypeFound = false;
        bool qubitFound = false;

        // iterate over instruments
        for(size_t instrIdx=0; instrIdx<jsonInstruments.size(); instrIdx++) {
            const json &instrument = jsonInstruments[instrIdx];
            std::string instrumentSignalType = instrument["ref_signals_type"];
            if(instrumentSignalType == instructionSignalType) {
                signalTypeFound = true;
                std::string instrumentName = instrument["name"];
                const json &qubits = instrument["qubits"];
                // FIXME: verify group size: qubits vs. control mode
                // FIXME: verify signal dimensions

                // anyone connected to qubit?
                for(size_t group=0; group<qubits.size() && !qubitFound; group++) {
                    for(size_t idx=0; idx<qubits[group].size() && !qubitFound; idx++) {
                        if(qubits[group][idx] == qubit) {
                            qubitFound = true;

                            DOUT("qubit " << qubit
                                 << " signal type '" << instructionSignalType
                                 << "' driven by instrument '" << instrumentName
                                 << "' group " << group
//                                 << " in CC slot " << ccSetupSlot["slot"]
                                 );

                            ret.slotIdx = instrIdx;     // FIXME: rename struct var
                            ret.group = group;
                            // FIXME: stop searching?
                        }
                    }
                }
            }
        }
        if(!signalTypeFound) {
            FATAL("No instruments found providing signal type '" << instructionSignalType << "'");     // FIXME: clarify for user
        }
        if(!qubitFound) {
            FATAL("No instruments found driving qubit " << qubit << " for signal type '" << instructionSignalType << "'");     // FIXME: clarify for user
        }

        return ret;
    }
}; // class

} // arch
} // ql


#endif  // ndef QL_ARCH_CC_CODEGEN_CC_H
