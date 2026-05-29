// ============================================================================
//  Pipelined RISC-V Simulator  -  USTP C/C++ for Hardware Engineers
//  Group: Waseem Ghulam (25ins-01019), Abid Hussain (25ins-01031),
//         Abdul Samad Abbasi (25ins-01030)
// ============================================================================

#include <iostream>
#include <cstdint>
#include <string>
#include <fstream>
#include <iomanip>
#include <functional>
#include <unordered_map>
#include <vector>
#include <sstream>

using namespace std;

// ============================================================================
// 0.  ABSTRACT BASE CLASS  -  OOP / Inheritance requirement
// ============================================================================

class HardwareComponent {
protected:
    string componentName;
    bool   enabled;

public:
    explicit HardwareComponent(const string& name)
        : componentName(name), enabled(true) {}

    virtual ~HardwareComponent() = default;

    // Pure-virtual interface every hardware block must implement
    virtual void reset()           = 0;
    virtual void printState() const = 0;

    string getName()  const { return componentName; }
    bool   isEnabled() const { return enabled; }
};

// ============================================================================
// 1.  INSTRUCTION RECORD
// ============================================================================

enum class InstrType { R_TYPE, I_TYPE, LOAD, STORE, BRANCH, NOP, UNKNOWN };

struct Instruction {
    uint32_t  rawHex;
    uint8_t   opcode;
    int8_t    rs1, rs2, rd;        // Register indices (-1 = unused)
    uint8_t   funct3, funct7;
    int32_t   imm;                 // Sign-extended immediate
    int32_t   aluResult;           // EX stage output
    int32_t   memData;             // MEM stage read value (LW)
    bool      isNOP;
    bool      memRead;             // LW
    bool      memWrite;            // SW
    bool      regWrite;            // writes to rd
    bool      isBranch;
    bool      branchTaken;
    InstrType type;
    string    mnemonic;

    // Default: NOP bubble
    Instruction()
        : rawHex(0), opcode(0), rs1(-1), rs2(-1), rd(-1),
          funct3(0), funct7(0), imm(0), aluResult(0), memData(0),
          isNOP(true), memRead(false), memWrite(false), regWrite(false),
          isBranch(false), branchTaken(false),
          type(InstrType::NOP), mnemonic("NOP") {}

    // Real instruction constructed from raw hex
    explicit Instruction(uint32_t hex)
        : rawHex(hex), opcode(0), rs1(-1), rs2(-1), rd(-1),
          funct3(0), funct7(0), imm(0), aluResult(0), memData(0),
          isNOP(false), memRead(false), memWrite(false), regWrite(false),
          isBranch(false), branchTaken(false),
          type(InstrType::UNKNOWN), mnemonic("???") {}
};

// ============================================================================
// 2.  INSTRUCTION MEMORY
// ============================================================================

class InstructionMemory : public HardwareComponent {
private:
    uint32_t* memory;   // Heap-allocated ROM  - Day 1: heap/dynamic allocation
    int       capacity;
    int       count;
    int       fetchCount;

public:
    explicit InstructionMemory(int bytes = 4096)
        : HardwareComponent("InstructionMemory") {
        capacity   = bytes / 4;
        count      = 0;
        fetchCount = 0;
        memory     = new uint32_t[capacity]();  // heap allocation
    }

    ~InstructionMemory() override { delete[] memory; }

    void reset() override {
        count      = 0;
        fetchCount = 0;
        for (int i = 0; i < capacity; i++) memory[i] = 0;
    }

    void printState() const override {
        cout << "  [" << componentName << "]  "
             << count << " instructions loaded | "
             << fetchCount << " fetch accesses\n";
    }

    void loadInstruction(uint32_t instr) {
        if (count < capacity) memory[count++] = instr;
        else cerr << "[ERROR] Instruction ROM overflow!\n";
    }

    // File I/O: read one 32-bit hex value per line
    bool loadFromFile(const string& filename) {
        ifstream file(filename);
        if (!file.is_open()) {
            cerr << "[INFO] Could not open '" << filename << "'. Using built-in test program.\n";
            return false;
        }
        uint32_t val;
        while (file >> hex >> val) loadInstruction(val);
        file.close();
        cout << "[INFO] Loaded from '" << filename << "'.\n";
        return true;
    }

    uint32_t fetch(int pc) {
        fetchCount++;
        return (pc >= 0 && pc < count) ? memory[pc] : 0;
    }

    int getProgramSize() const { return count; }
};

// ============================================================================
// 3.  DATA MEMORY  (256 words = 1 KB)
// ============================================================================

class DataMemory : public HardwareComponent {
private:
    int32_t* mem;       // heap-allocated - Day 1: heap usage
    int      words;
    int      readCount, writeCount;

public:
    explicit DataMemory(int numWords = 256)
        : HardwareComponent("DataMemory"),
          words(numWords), readCount(0), writeCount(0) {
        mem = new int32_t[words]();
    }

    ~DataMemory() override { delete[] mem; }

    void reset() override {
        for (int i = 0; i < words; i++) mem[i] = 0;
        readCount = writeCount = 0;
    }

    void printState() const override {
        cout << "  [" << componentName << "]  "
             << readCount << " reads | " << writeCount << " writes\n";
        for (int i = 0; i < words; i++) {
            if (mem[i] != 0)
                cout << "    MEM[" << dec << i*4 << "] = " << mem[i] << "\n";
        }
    }

    int32_t read(int32_t addr) {
        int idx = addr / 4;
        if (idx < 0 || idx >= words) { cerr << "[ERROR] DM read OOB addr=" << addr << "\n"; return 0; }
        readCount++;
        return mem[idx];
    }

    void write(int32_t addr, int32_t data) {
        int idx = addr / 4;
        if (idx < 0 || idx >= words) { cerr << "[ERROR] DM write OOB addr=" << addr << "\n"; return; }
        writeCount++;
        mem[idx] = data;
    }
};

// ============================================================================
// 4.  REGISTER FILE  (x0-x31, x0 is hardwired zero)
// ============================================================================

class RegisterFile : public HardwareComponent {
private:
    int32_t regs[32];
    int     readCount, writeCount;

public:
    RegisterFile()
        : HardwareComponent("RegisterFile"), readCount(0), writeCount(0) {
        for (auto& r : regs) r = 0;
    }

    void reset() override {
        for (auto& r : regs) r = 0;
        readCount = writeCount = 0;
    }

    void printState() const override {
        cout << "\n====================================\n";
        cout << "       FINAL REGISTER STATE         \n";
        cout << "====================================\n";
        bool any = false;
        for (int i = 0; i < 32; i++) {
            if (regs[i] != 0) {
                cout << "  x" << left << setw(2) << dec << i
                     << " = " << setw(10) << regs[i]
                     << "  (0x" << right << hex << uppercase
                     << setw(8) << setfill('0') << (uint32_t)regs[i]
                     << dec << setfill(' ') << ")\n";
                any = true;
            }
        }
        if (!any) cout << "  All registers hold 0.\n";
        cout << "====================================\n";
        cout << "  [RegisterFile] " << readCount << " reads | " << writeCount << " writes\n";
    }

    int32_t read(int idx) {
        if (idx <= 0 || idx > 31) return 0;   // x0 always 0
        readCount++;
        return regs[idx];
    }

    void write(int idx, int32_t data) {
        if (idx <= 0 || idx > 31) return;      // x0 not writable
        writeCount++;
        regs[idx] = data;
    }
};

// ============================================================================
// 5.  DECODER
// ============================================================================

class Decoder : public HardwareComponent {
public:
    Decoder() : HardwareComponent("Decoder") {}
    void reset()           override {}
    void printState() const override { cout << "  [Decoder] Stateless combinational logic.\n"; }

    static Instruction decode(uint32_t raw) {
        if (raw == 0) return Instruction();   // treat 0x00000000 as NOP

        Instruction inst(raw);

        // --- Day 3: Bit masking and shift extraction ---
        inst.opcode = raw & 0x7F;              // bits [6:0]
        inst.rd     = (raw >>  7) & 0x1F;     // bits [11:7]
        inst.funct3 = (raw >> 12) & 0x07;     // bits [14:12]
        inst.rs1    = (raw >> 15) & 0x1F;     // bits [19:15]

        switch (inst.opcode) {

            case 0x33: {  // R-Type: ADD, SUB, AND, OR, XOR, SLT
                inst.type     = InstrType::R_TYPE;
                inst.rs2      = (raw >> 20) & 0x1F;
                inst.funct7   = (raw >> 25) & 0x7F;
                inst.regWrite = true;
                inst.mnemonic = rMnemonic(inst.funct3, inst.funct7);
                break;
            }

            case 0x13: {  // I-Type ALU: ADDI, ANDI, ORI, XORI, SLTI
                inst.type     = InstrType::I_TYPE;
                inst.rs2      = -1;
                inst.imm      = static_cast<int32_t>(raw) >> 20;  // arithmetic right-shift = sign-extend
                inst.regWrite = true;
                inst.mnemonic = iMnemonic(inst.funct3) + "I";
                break;
            }

            case 0x03: {  // Load: LW (funct3=010)
                inst.type     = InstrType::LOAD;
                inst.rs2      = -1;
                inst.imm      = static_cast<int32_t>(raw) >> 20;
                inst.memRead  = true;
                inst.regWrite = true;
                inst.mnemonic = "LW";
                break;
            }

            case 0x23: {  // Store: SW (funct3=010)
                inst.type     = InstrType::STORE;
                inst.rs2      = (raw >> 20) & 0x1F;
                // S-type: imm[11:5] | imm[4:0]
                inst.imm      = ((static_cast<int32_t>(raw) >> 20) & ~0x1F)
                              | ((raw >> 7) & 0x1F);
                inst.memWrite = true;
                inst.rd       = -1;
                inst.mnemonic = "SW";
                break;
            }

            case 0x63: {  // Branch: BEQ (f3=000), BNE (f3=001)
                inst.type     = InstrType::BRANCH;
                inst.rs2      = (raw >> 20) & 0x1F;
                inst.rd       = -1;
                inst.isBranch = true;
                // B-type immediate reconstruction
                int32_t imm12   = (raw >> 31) & 1;
                int32_t imm11   = (raw >>  7) & 1;
                int32_t imm10_5 = (raw >> 25) & 0x3F;
                int32_t imm4_1  = (raw >>  8) & 0x0F;
                inst.imm = (imm12 << 12) | (imm11 << 11) | (imm10_5 << 5) | (imm4_1 << 1);
                if (imm12) inst.imm |= ~0x1FFF;   // sign-extend
                inst.mnemonic = (inst.funct3 == 0) ? "BEQ" : "BNE";
                break;
            }

            default:
                inst.type     = InstrType::UNKNOWN;
                inst.mnemonic = "???";
                break;
        }
        return inst;
    }

private:
    static string rMnemonic(uint8_t f3, uint8_t f7) {
        if (f7 == 0x20 && f3 == 0x0) return "SUB";
        switch (f3) {
            case 0x0: return "ADD"; case 0x7: return "AND";
            case 0x6: return "OR";  case 0x4: return "XOR";
            case 0x2: return "SLT"; default:  return "R??";
        }
    }
    static string iMnemonic(uint8_t f3) {
        switch (f3) {
            case 0x0: return "ADD"; case 0x7: return "AND";
            case 0x6: return "OR";  case 0x4: return "XOR";
            case 0x2: return "SLT"; default:  return "???";
        }
    }
};

// ============================================================================
// 6.  ALU - lambda-based operation dispatch  (Day 6-7: lambdas requirement)
// ============================================================================

class ALU : public HardwareComponent {
public:
    using AluOp = function<int32_t(int32_t, int32_t)>;

    ALU() : HardwareComponent("ALU") {
        // Each operation is a named lambda stored in an unordered_map
        ops["ADD"] = [](int32_t a, int32_t b) -> int32_t { return a + b; };
        ops["SUB"] = [](int32_t a, int32_t b) -> int32_t { return a - b; };
        ops["AND"] = [](int32_t a, int32_t b) -> int32_t { return a & b; };
        ops["OR"]  = [](int32_t a, int32_t b) -> int32_t { return a | b; };
        ops["XOR"] = [](int32_t a, int32_t b) -> int32_t { return a ^ b; };
        ops["SLT"] = [](int32_t a, int32_t b) -> int32_t { return (a < b) ? 1 : 0; };
        ops["SEQ"] = [](int32_t a, int32_t b) -> int32_t { return (a == b) ? 1 : 0; };  // for BEQ
        ops["SNE"] = [](int32_t a, int32_t b) -> int32_t { return (a != b) ? 1 : 0; };  // for BNE
    }

    void reset()           override {}
    void printState() const override {
        cout << "  [ALU] Lambda-based dispatch, " << ops.size() << " operations.\n";
    }

    int32_t execute(const string& op, int32_t a, int32_t b) {
        auto it = ops.find(op);
        if (it != ops.end()) return it->second(a, b);
        cerr << "[ALU] Unknown op: " << op << "\n";
        return 0;
    }

    // Determine which ALU operation an instruction needs
    string selectOp(const Instruction& inst) const {
        switch (inst.opcode) {
            case 0x33:  // R-Type
                if (inst.funct7 == 0x20 && inst.funct3 == 0) return "SUB";
                switch (inst.funct3) {
                    case 0x0: return "ADD"; case 0x7: return "AND";
                    case 0x6: return "OR";  case 0x4: return "XOR";
                    case 0x2: return "SLT"; default:  return "ADD";
                }
            case 0x13:  // I-Type ALU
                switch (inst.funct3) {
                    case 0x0: return "ADD"; case 0x7: return "AND";
                    case 0x6: return "OR";  case 0x4: return "XOR";
                    case 0x2: return "SLT"; default:  return "ADD";
                }
            case 0x03:  // LW - base + offset
            case 0x23:  // SW - base + offset
                return "ADD";
            case 0x63:  // Branch
                return (inst.funct3 == 0) ? "SEQ" : "SNE";
            default:
                return "ADD";
        }
    }

private:
    unordered_map<string, AluOp> ops;
};

// ============================================================================
// 7.  SIMULATION STATISTICS
// ============================================================================

struct SimStats {
    int cycles              = 0;
    int stallCycles         = 0;
    int hazardsDetected     = 0;
    int instructionsDone    = 0;
    int lwCount             = 0;
    int swCount             = 0;
    int branchTotal         = 0;
    int branchTaken         = 0;

    void print() const {
        cout << "\n====================================\n";
        cout << "      SIMULATION  STATISTICS        \n";
        cout << "====================================\n";
        cout << "  Total cycles          : " << cycles           << "\n";
        cout << "  Instructions retired  : " << instructionsDone << "\n";
        cout << "  Stall cycles          : " << stallCycles      << "\n";
        cout << "  Hazards detected      : " << hazardsDetected  << "\n";
        cout << "  LW executed           : " << lwCount          << "\n";
        cout << "  SW executed           : " << swCount          << "\n";
        cout << "  Branches (total/taken): " << branchTotal
             << " / " << branchTaken << "\n";
        if (instructionsDone > 0) {
            double cpi = static_cast<double>(cycles) / instructionsDone;
            cout << fixed << setprecision(2)
                 << "  CPI                   : " << cpi << "\n";
        }
        cout << "====================================\n";
    }
};

// ============================================================================
// 8.  FIVE-STAGE PIPELINE SIMULATOR
//     Stages: IF(0)  ID(1)  EX(2)  MEM(3)  WB(4)
// ============================================================================

class PipelineSimulator {
private:
    InstructionMemory& instrMem;
    RegisterFile&      regFile;
    DataMemory&        dataMem;
    ALU&               aluUnit;

    Instruction pipeline[5];   // Days 4-5: array as pipeline queue
    int         pc;
    SimStats    stats;

public:
    PipelineSimulator(InstructionMemory& im, RegisterFile& rf,
                      DataMemory& dm, ALU& alu)
        : instrMem(im), regFile(rf), dataMem(dm), aluUnit(alu), pc(0) {}

    void run() {
        bool halted = false;
        cout << "\n=== 5-STAGE PIPELINE SIMULATION STARTED ===\n";
        cout << "  IF  ->  ID  ->  EX  ->  MEM  ->  WB\n\n";

        while (!halted) {
            stats.cycles++;
            cout << "+-- CYCLE " << stats.cycles << " -----------------------------+\n";

            // Back-to-front evaluation avoids read-after-write within the same cycle
            stageWB();
            stageMEM();

            if (detectHazard()) {
                cout << "  [!]  RAW hazard -> stalling IF/ID, injecting NOP bubble\n";
                stats.stallCycles++;
                stats.hazardsDetected++;
                stageEX();           // EX->MEM advances normally
                pipeline[2] = Instruction();   // bubble replaces EX input
                // pipeline[1] and pipeline[0] held (stalled)
            } else {
                stageEX();
                pipeline[2] = pipeline[1];   // ID -> EX
                pipeline[1] = pipeline[0];   // IF -> ID
                stageIF();
            }

            printPipeline();

            // Halt when PC is past program and all stages are drained
            bool allNop = true;
            for (int i = 0; i < 5; i++) if (!pipeline[i].isNOP) { allNop = false; break; }
            if (pc >= instrMem.getProgramSize() && allNop) halted = true;
            if (stats.cycles > 300) {
                cout << "\n[WARN] Safety cycle limit hit.\n";
                halted = true;
            }
        }
        cout << "\n=== SIMULATION COMPLETE (" << stats.cycles << " cycles) ===\n";
    }

    const SimStats& getStats() const { return stats; }

private:
    // --- Stage handlers ---

    void stageIF() {
        if (pc < instrMem.getProgramSize()) {
            pipeline[0] = Decoder::decode(instrMem.fetch(pc++));
        } else {
            pipeline[0] = Instruction();
        }
    }

    void stageEX() {
        pipeline[3] = pipeline[2];     // EX -> MEM slot
        Instruction& inst = pipeline[3];
        if (inst.isNOP) return;

        int32_t val1 = regFile.read(inst.rs1);
        int32_t val2 = (inst.rs2 >= 0 && !inst.memRead && !inst.isBranch)
                           ? regFile.read(inst.rs2)
                           : inst.imm;

        // For branches, always compare the two source registers
        if (inst.isBranch) {
            val2 = regFile.read(inst.rs2);
        }

        string op = aluUnit.selectOp(inst);
        inst.aluResult = aluUnit.execute(op, val1, val2);

        if (inst.isBranch) {
            inst.branchTaken = (inst.aluResult != 0);
            stats.branchTotal++;
            if (inst.branchTaken) {
                stats.branchTaken++;
                cout << "  EX : " << inst.mnemonic << " -> branch "
                     << (inst.branchTaken ? "TAKEN" : "NOT TAKEN") << "\n";
            }
        }
    }

    void stageMEM() {
        pipeline[4] = pipeline[3];
        Instruction& inst = pipeline[4];
        if (inst.isNOP) return;

        if (inst.memRead) {          // LW
            inst.memData = dataMem.read(inst.aluResult);
            stats.lwCount++;
            cout << "  MEM: LW  addr=" << inst.aluResult
                 << "  data=" << inst.memData << "\n";
        } else if (inst.memWrite) {  // SW
            int32_t storeVal = regFile.read(inst.rs2);
            dataMem.write(inst.aluResult, storeVal);
            stats.swCount++;
            cout << "  MEM: SW  addr=" << inst.aluResult
                 << "  data=" << storeVal << "\n";
        }
    }

    void stageWB() {
        Instruction& inst = pipeline[4];
        if (inst.isNOP) return;

        if (inst.regWrite && inst.rd > 0) {
            int32_t wdata = inst.memRead ? inst.memData : inst.aluResult;
            regFile.write(inst.rd, wdata);
            cout << "  WB : x" << (int)inst.rd << " <- " << wdata << "\n";
            stats.instructionsDone++;
        } else if (!inst.regWrite && !inst.isNOP) {
            stats.instructionsDone++;  // SW, BEQ/BNE still count
        }
    }

    // --- Hazard detection (RAW, no forwarding) ---
    bool detectHazard() {
        if (pipeline[1].isNOP) return false;
        int rs1 = pipeline[1].rs1;
        int rs2 = pipeline[1].rs2;

        // Lambda used for conflict checking - course requirement also fulfilled here
        auto conflict = [](int rd, int rs) -> bool {
            return rd > 0 && rs > 0 && rd == rs;
        };

        // Check EX stage (pipeline[2]) - 1-cycle distance
        if (!pipeline[2].isNOP && pipeline[2].regWrite) {
            if (conflict(pipeline[2].rd, rs1) || conflict(pipeline[2].rd, rs2))
                return true;
        }
        // Check MEM stage (pipeline[3]) - 2-cycle distance
        if (!pipeline[3].isNOP && pipeline[3].regWrite) {
            if (conflict(pipeline[3].rd, rs1) || conflict(pipeline[3].rd, rs2))
                return true;
        }
        return false;
    }

    // --- Formatted pipeline view ---
    void printPipeline() {
        const char* labels[] = { "IF ", "ID ", "EX ", "MEM", "WB " };
        for (int i = 0; i < 5; i++) {
            const Instruction& inst = pipeline[i];
            cout << "  " << labels[i] << " | ";
            if (inst.isNOP) {
                cout << "[   NOP   ]\n";
            } else {
                cout << left << setw(5) << inst.mnemonic;
                if (inst.rd  >= 0) cout << " x" << (int)inst.rd;
                if (inst.rs1 >= 0) cout << ", x" << (int)inst.rs1;
                if (inst.rs2 >= 0) cout << ", x" << (int)inst.rs2;
                if (inst.type == InstrType::I_TYPE ||
                    inst.type == InstrType::LOAD   ||
                    inst.type == InstrType::STORE)
                    cout << ", #" << inst.imm;
                cout << "\n";
            }
        }
        cout << "+------------------------------------+\n";
    }
};

// ============================================================================
// 9.  MAIN
// ============================================================================

int main() {
    cout << "============================================================\n";
    cout << "   PIPELINED RISC-V SIMULATOR  -  v2.0\n";
    cout << "   USTP · C/C++ for Hardware Engineers · Module 1\n";
    cout << "   Waseem Ghulam | Abid Hussain | Abdul Samad Abbasi\n";
    cout << "============================================================\n\n";

    // --- Instantiate hardware components ---
    InstructionMemory instrMem(4096);
    DataMemory        dataMem(256);
    RegisterFile      regFile;
    ALU               aluUnit;

    // Polymorphic array - demonstrates inheritance (HardwareComponent*)
    HardwareComponent* hw[] = { &instrMem, &dataMem, &regFile, &aluUnit };
    cout << "Hardware blocks online:\n";
    for (auto* c : hw) cout << "  [OK] " << c->getName() << "\n";
    cout << "\n";

    // --- Load program ---
    if (!instrMem.loadFromFile("program.hex")) {
        // Built-in test program (standard RISC-V encoding)
        cout << "Built-in test program:\n";
        cout << "  ADDI x10, x0,  10      (x10 = 10)\n";
        cout << "  ADDI x11, x0,  20      (x11 = 20)\n";
        cout << "  ADD  x12, x10, x11     (x12 = 30)  [RAW hazard]\n";
        cout << "  AND  x13, x10, x11     (x13 = 0)   [RAW hazard]\n";
        cout << "  OR   x14, x10, x11     (x14 = 30)\n";
        cout << "  SUB  x15, x11, x10     (x15 = 10)\n";
        cout << "  XOR  x16, x10, x11     (x16 = 30)\n";
        cout << "  SLT  x17, x10, x11     (x17 = 1)\n";

        instrMem.loadInstruction(0x00A00513); // ADDI x10, x0,  10
        instrMem.loadInstruction(0x01400593); // ADDI x11, x0,  20
        instrMem.loadInstruction(0x00B50633); // ADD  x12, x10, x11
        instrMem.loadInstruction(0x00B576B3); // AND  x13, x10, x11
        instrMem.loadInstruction(0x00B56733); // OR   x14, x10, x11
        instrMem.loadInstruction(0x40A587B3); // SUB  x15, x11, x10
        instrMem.loadInstruction(0x00B54833); // XOR  x16, x10, x11
        instrMem.loadInstruction(0x00B528B3); // SLT  x17, x10, x11
    }

    cout << "\n" << instrMem.getProgramSize() << " instructions loaded into ROM.\n";

    // --- Run simulation ---
    PipelineSimulator sim(instrMem, regFile, dataMem, aluUnit);
    sim.run();

    // --- Print final state ---
    regFile.printState();
    dataMem.printState();
    sim.getStats().print();

    return 0;
}