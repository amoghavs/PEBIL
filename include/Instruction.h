#ifndef _Instruction_h_
#define _Instruction_h_

#include <Base.h>
#include <BinaryFile.h>

class ElfFile;

#define MAX_DISASM_STR_LENGTH 80
#define INVALID_OPCODE_INDEX 0xffffffff
#define MAX_OPERANDS 3
#define JUMP_TARGET_OPERAND 2

enum x86_insn_format {
    x86_insn_format_unknown = 0,
    x86_insn_format_onebyte,
    x86_insn_format_twobyte,
    x86_insn_format_groups,
    x86_insn_format_prefix_user_table,
    x86_insn_format_x86_64,
    x86_insn_format_float_mem,
    x86_insn_format_float_reg,
    x86_insn_format_float_groups,
    x86_insn_format_Total
};

enum x86_insn_type {
    x86_insn_type_unknown = 0,
    x86_insn_type_cond_branch,
    x86_insn_type_branch,
    x86_insn_type_int,
    x86_insn_type_float,
    x86_insn_type_simd,
    x86_insn_type_io,
    x86_insn_type_prefetch,
    x86_insn_type_syscall,
    x86_insn_type_hwcount,
    x86_insn_type_Total
};

enum x86_operand_type {
    x86_operand_type_unused = 0,
    x86_operand_type_immrel,
    x86_operand_type_reg,
    x86_operand_type_imreg,
    x86_operand_type_imm,
    x86_operand_type_mem,
    x86_operand_type_Total
};


class Operand {
protected:
    uint32_t type;
    uint64_t value;
public:
    Operand(uint32_t type, uint64_t value);
    ~Operand(){}

    uint32_t getType() { return type; }
    uint64_t getValue() { return value; }

    uint64_t setValue(uint64_t val);
    uint32_t setType(uint32_t typ);
};

class Instruction {
protected:
    uint32_t instructionLength;
    char* rawBytes;
    bool isLocalBytes;
    uint64_t virtualAddress;
    uint64_t nextAddress;
    uint32_t type;
    char disassembledString[MAX_DISASM_STR_LENGTH];
    Operand* operands[MAX_OPERANDS];    

public:
    Instruction();
    ~Instruction();

    uint32_t read(BinaryInputFile* b);
    void print();

    char* charStream() { return rawBytes; }

    uint64_t getNextAddress();
    uint64_t getAddress();
    uint32_t getLength();
    char* getBytes();
    Operand* getOperand(uint32_t idx);

    uint64_t setNextAddress();
    uint64_t setAddress(uint64_t addr);
    uint32_t setLength(uint32_t len);
    char* setBytes(char* bytes, bool islocal);
    uint64_t setOperandValue(uint32_t idx, uint64_t val);
    uint32_t setOperandType(uint32_t idx, uint32_t typ);

    char* setDisassembledString(char* disStr);

    uint32_t setOpcodeType(uint32_t formatType, uint32_t idx1, uint32_t idx2);

private:
    uint32_t computeOpcodeTypeOneByte(uint32_t idx);
    uint32_t computeOpcodeTypeTwoByte(uint32_t idx);
    uint32_t computeOpcodeTypeGroups(uint32_t idx1, uint32_t idx2);
    uint32_t computeOpcodeTypePrefixUser(uint32_t idx1, uint32_t idx2);
    uint32_t computeOpcodeTypeX8664(uint32_t idx1, uint32_t idx2);

};
#endif /* _Instruction_h_ */