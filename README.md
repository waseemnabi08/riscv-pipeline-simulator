## RISC-V Pipelined Simulator

5-stage (IF/ID/EX/MEM/WB) software simulator for a pipelined RISC-V processor.

**Features:** RAW hazard detection & stall insertion, RV32I instruction subset  
(ADD/SUB/AND/OR/XOR/SLT + immediate variants, LW, SW, BEQ, BNE),  
OOP design with abstract HardwareComponent base class, lambda-based ALU dispatch.

**Build:** `g++ -std=c++17 -o simulator main.cpp`  
**Run:** `./simulator` (uses built-in test program, or provide `program.hex`)

*USTP C/C++ for Hardware Engineers - Module 1, June 2026*
