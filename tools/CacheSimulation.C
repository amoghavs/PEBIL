#include <CacheSimulation.h>

#include <BasicBlock.h>
#include <Function.h>
#include <Instrumentation.h>
#include <Instruction.h>
#include <InstructionGenerator.h>
#include <LineInformation.h>
#include <Loop.h>
#include <TextSection.h>

#define SIM_FUNCTION "MetaSim_simulFuncCall_Simu"
#define EXIT_FUNCTION "MetaSim_endFuncCall_Simu"
#define INST_LIB_NAME "libsimulator.so"
#define INST_SUFFIX "siminst"
#define BUFFER_ENTRIES 0x00010000
#define Size__BufferEntry 16

CacheSimulation::CacheSimulation(ElfFile* elf, char* inputFuncList, char* inputFileList)
    : InstrumentationTool(elf, inputFuncList, inputFileList)
{
    instSuffix = new char[__MAX_STRING_SIZE];
    sprintf(instSuffix,"%s\0", INST_SUFFIX);

    simFunc = NULL;
    exitFunc = NULL;
}

void CacheSimulation::declare(){
    
    // declare any shared library that will contain instrumentation functions
    declareLibrary(INST_LIB_NAME);

    // declare any instrumentation functions that will be used
    simFunc = declareFunction(SIM_FUNCTION);
    ASSERT(simFunc && "Cannot find memory print function, are you sure it was declared?");
    exitFunc = declareFunction(EXIT_FUNCTION);
    ASSERT(exitFunc && "Cannot find exit function, are you sure it was declared?");
}

void CacheSimulation::instrument(){
    uint32_t temp32;
    
    LineInfoFinder* lineInfoFinder = NULL;
    if (hasLineInformation()){
        lineInfoFinder = getLineInfoFinder();
    } else {
        PRINT_ERROR("This executable does not have any line information");
    }

    ASSERT(isPowerOfTwo(Size__BufferEntry));
    uint64_t bufferStore  = reserveDataOffset(BUFFER_ENTRIES * Size__BufferEntry);
    uint32_t startValue = 1;
    initializeReservedData(getInstDataAddress() + bufferStore, sizeof(uint32_t), &startValue);
    uint64_t buffPtrStore = reserveDataOffset(sizeof(uint64_t));


    char* appName = getElfFile()->getFileName();
    char* extension = "siminst";
    uint32_t phaseId = 0;
    uint32_t dumpCode = 0;
    uint32_t commentSize = strlen(appName) + sizeof(uint32_t) + strlen(extension) + sizeof(uint32_t) + sizeof(uint32_t) + 4;
    uint64_t commentStore = reserveDataOffset(commentSize);
    char* comment = (char*)malloc(commentSize);
    sprintf(comment, "%s %u %s %u %u", appName, phaseId, extension, getNumberOfExposedBasicBlocks(), dumpCode);
    initializeReservedData(getInstDataAddress() + commentStore, commentSize, comment);

    simFunc->addArgument(bufferStore);
    simFunc->addArgument(buffPtrStore);
    simFunc->addArgument(commentStore);

    exitFunc->addArgument(bufferStore);
    exitFunc->addArgument(buffPtrStore);
    exitFunc->addArgument(commentStore);

    InstrumentationPoint* p = addInstrumentationPoint(getProgramExitBlock(), exitFunc, InstrumentationMode_tramp);
    ASSERT(p);
    if (!p->getInstBaseAddress()){
        PRINT_ERROR("Cannot find an instrumentation point at the exit function");
    }

    Vector<BasicBlock*>* allBlocks = new Vector<BasicBlock*>();
    Vector<LineInfo*>* allLineInfos = new Vector<LineInfo*>();

    uint32_t blockId = 0;
    uint32_t memopId = 0;
    for (uint32_t i = 0; i < getNumberOfExposedBasicBlocks(); i++){
        BasicBlock* bb = getExposedBasicBlock(i);
        (*allBlocks).append(bb);
        (*allLineInfos).append(lineInfoFinder->lookupLineInfo(bb));

        for (uint32_t j = 0; j < bb->getNumberOfInstructions(); j++){
            Instruction* memop = bb->getInstruction(j);
            if (memop->isMemoryOperation()){            
                InstrumentationPoint* pt = addInstrumentationPoint(memop, simFunc, InstrumentationMode_trampinline);
                
                Vector<Instruction*>* addressCalcInstructions = generateBufferedAddressCalculation(memop, bufferStore, buffPtrStore, blockId, memopId, BUFFER_ENTRIES, FlagsProtectionMethod_full);
                ASSERT(addressCalcInstructions);
                while ((*addressCalcInstructions).size()){
                    pt->addPrecursorInstruction((*addressCalcInstructions).remove(0));
                }
                delete addressCalcInstructions;
                memopId++;
            }
        }
        blockId++;
    }
    ASSERT(memopId == getNumberOfExposedMemOps());

    printStaticFile(allBlocks, allLineInfos);

    delete allBlocks;
    delete allLineInfos;

    ASSERT(currentPhase == ElfInstPhase_user_reserve && "Instrumentation phase order must be observed"); 
}

// base and index regs are saved and restored by the caller
Vector<Instruction*>* CacheSimulation::generateBufferedAddressCalculation(Instruction* instruction, uint64_t bufferStore, uint64_t bufferPtrStore, uint32_t blockId, uint32_t memopId, uint32_t bufferSize, FlagsProtectionMethods method){
    if (getElfFile()->is64Bit()){
        return generateBufferedAddressCalculation64(instruction, bufferStore, bufferPtrStore, blockId, memopId, bufferSize, method);
    } else {
        return generateBufferedAddressCalculation32(instruction, bufferStore, bufferPtrStore, blockId, memopId, bufferSize, method);
    }
    __SHOULD_NOT_ARRIVE;
    return NULL;
}

Vector<Instruction*>* CacheSimulation::generateBufferedAddressCalculation64(Instruction* instruction, uint64_t bufferStore, uint64_t bufferPtrStore, uint32_t blockId, uint32_t memopId, uint32_t bufferSize, FlagsProtectionMethods method){
    Vector<Instruction*>* addressCalc = new Vector<Instruction*>();
    uint64_t dataAddr = getInstDataAddress();

    MemoryOperand* memerand = NULL;
    Operand* operand = NULL;
    if (instruction->isExplicitMemoryOperation()){
        memerand = new MemoryOperand(instruction->getMemoryOperand(), this);
        operand = memerand->getOperand();
    }


    // find 3 temp registers to use in the calculation
    BitSet<uint32_t>* availableRegs = new BitSet<uint32_t>(X86_64BIT_GPRS);
    availableRegs->insert(X86_REG_SP);
    if (operand){
        operand->getInstruction()->touchedRegisters(availableRegs);
    }

    ~(*availableRegs);

    uint32_t tempReg1 = X86_64BIT_GPRS;
    uint32_t tempReg2 = X86_64BIT_GPRS;
    uint32_t tempReg3 = X86_64BIT_GPRS;

    for (int32_t i = 0; i < availableRegs->size(); i++){
        uint32_t idx = X86_64BIT_GPRS - i;
        if (availableRegs->contains(idx)){
            if (tempReg1 == X86_64BIT_GPRS){
                tempReg1 = idx;
            } else if (tempReg2 == X86_64BIT_GPRS){
                tempReg2 = idx;
            } else if (tempReg3 == X86_64BIT_GPRS){
                tempReg3 = idx;
            }
        }
    }
    ASSERT(tempReg1 != X86_64BIT_GPRS && tempReg2 != X86_64BIT_GPRS && tempReg3 != X86_64BIT_GPRS);
    delete availableRegs;


    uint8_t baseReg = 0;
    uint64_t lValue = 0;

    if (operand){
        lValue = operand->getValue();
        if (operand->GET(base)){
            if (!IS_64BIT_GPR(operand->GET(base)) && !IS_PC_REG(operand->GET(base))){
                PRINT_ERROR("bad operand value %d -- %s", operand->GET(base), ud_reg_tab[operand->GET(base)-1]);
            }
            if (IS_64BIT_GPR(operand->GET(base))){
                baseReg = operand->GET(base) - UD_R_RAX;
            } else {
                baseReg = UD_R_RAX - UD_R_RAX;
            }
        } else {
            if (!lValue && !operand->GET(index)){
                PRINT_WARN(3, "Operand requesting memory address 0?");
            }
            if (!operand->GET(index)){
                if (lValue < MIN_CONST_MEMADDR){
                    PRINT_WARN(6, "Const memory address probably isn't valid %#llx, zeroing", lValue);
                    lValue = 0;
                }
            }
        }
    }

    uint8_t indexReg = 0;
    if (operand){
        if (operand->GET(index)){
            ASSERT(operand->GET(index) >= UD_R_RAX && operand->GET(index) <= UD_R_R15);
            indexReg = operand->GET(index) - UD_R_RAX;
        } else {
            ASSERT(!operand->GET(scale));
        }
    }

    //operand->getInstruction()->print();
    //PRINT_INFOR("Using tmp1/tmp2/base/index/value/scale/baddr %hhd/%hhd/%hhd/%hhd/%#llx/%d/%#llx", tempReg1, tempReg2, baseReg, indexReg, lValue, operand->GET(scale), operand->getInstruction()->getProgramAddress());

    // save a few temp regs
    (*addressCalc).append(InstructionGenerator64::generateMoveRegToMem(tempReg1, dataAddr + getRegStorageOffset() + 2*(sizeof(uint64_t))));
    (*addressCalc).append(InstructionGenerator64::generateMoveRegToMem(tempReg2, dataAddr + getRegStorageOffset() + 3*(sizeof(uint64_t))));
    (*addressCalc).append(InstructionGenerator64::generateMoveRegToMem(tempReg3, dataAddr + getRegStorageOffset() + 4*(sizeof(uint64_t))));
 
    if (operand){
        if (operand->GET(base)){
            (*addressCalc).append(InstructionGenerator64::generateMoveRegToReg(baseReg, tempReg1));
#ifndef NO_LAHF_SAHF
            // AX contains the flags values and the legitimate value of AX is in regStorage when LAHF/SAHF are in place
            if (baseReg == X86_REG_AX && method == FlagsProtectionMethod_light){
                (*addressCalc).append(InstructionGenerator64::generateMoveMemToReg(dataAddr + getRegStorageOffset(), tempReg1));
            }
#endif
        }
    } else {
        (*addressCalc).append(InstructionGenerator64::generateMoveRegToReg(X86_REG_SP, tempReg1));
    }

    if (operand){
        if (operand->GET(index)){
            (*addressCalc).append(InstructionGenerator64::generateMoveRegToReg(indexReg, tempReg2));
#ifndef NO_LAHF_SAHF
            // AX contains the flags values and the legitimate value of AX is in regStorage when LAHF/SAHF are in place
            if (indexReg == X86_REG_AX && method == FlagsProtectionMethod_light){
                (*addressCalc).append(InstructionGenerator64::generateMoveMemToReg(dataAddr + getRegStorageOffset(), tempReg2));
            }
#endif
        }

        if (IS_PC_REG(operand->GET(base))){
            (*addressCalc).append(InstructionGenerator64::generateMoveImmToReg(instruction->getProgramAddress(), tempReg1));
            (*addressCalc).append(InstructionGenerator64::generateRegAddImm(tempReg1, instruction->getSizeInBytes()));
        }
        
        if (operand->GET(base)){
            (*addressCalc).append(InstructionGenerator64::generateRegAddImm(tempReg1, lValue)); 
        } else {
            (*addressCalc).append(InstructionGenerator64::generateMoveImmToReg(lValue, tempReg1));
        }

        if (operand->GET(index)){
            uint8_t scale = operand->GET(scale);
            if (!scale){
                scale++;
            }
            (*addressCalc).append(InstructionGenerator64::generateRegImmMultReg(tempReg2, scale, tempReg2));
            (*addressCalc).append(InstructionGenerator64::generateRegAddReg2OpForm(tempReg2, tempReg1));
        }
    }

    (*addressCalc).append(InstructionGenerator64::generateMoveImmToReg(dataAddr + bufferStore, tempReg2));
    (*addressCalc).append(InstructionGenerator64::generateMoveMemToReg(dataAddr + bufferPtrStore, tempReg3));

    // compute the address of the buffer entry
    (*addressCalc).append(InstructionGenerator64::generateShiftLeftLogical(logBase2(Size__BufferEntry), tempReg3));
    (*addressCalc).append(InstructionGenerator64::generateRegAddReg2OpForm(tempReg3, tempReg2));
    (*addressCalc).append(InstructionGenerator64::generateShiftRightLogical(logBase2(Size__BufferEntry), tempReg3));

    // fill the buffer entry
    (*addressCalc).append(InstructionGenerator64::generateMoveRegToRegaddrImm(tempReg1, tempReg2, 2*sizeof(uint32_t), true));
    (*addressCalc).append(InstructionGenerator64::generateMoveImmToReg(blockId, tempReg1));
    (*addressCalc).append(InstructionGenerator64::generateMoveRegToRegaddrImm(tempReg1, tempReg2, 0, false));
    (*addressCalc).append(InstructionGenerator64::generateMoveImmToReg(memopId, tempReg1));
    (*addressCalc).append(InstructionGenerator64::generateMoveRegToRegaddrImm(tempReg1, tempReg2, sizeof(uint32_t), false));

    // inc the buffer pointer and see if the buffer is full
    (*addressCalc).append(InstructionGenerator64::generateRegAddImm(tempReg3, 1));
    (*addressCalc).append(InstructionGenerator64::generateMoveRegToMem(tempReg3, dataAddr + bufferPtrStore));
    (*addressCalc).append(InstructionGenerator64::generateCompareImmReg(bufferSize, tempReg3));
    
    // restore regs
    (*addressCalc).append(InstructionGenerator64::generateMoveMemToReg(dataAddr + getRegStorageOffset() + 4*(sizeof(uint64_t)), tempReg3));
    (*addressCalc).append(InstructionGenerator64::generateMoveMemToReg(dataAddr + getRegStorageOffset() + 3*(sizeof(uint64_t)), tempReg2));
    (*addressCalc).append(InstructionGenerator64::generateMoveMemToReg(dataAddr + getRegStorageOffset() + 2*(sizeof(uint64_t)), tempReg1));

    (*addressCalc).append(InstructionGenerator::generateBranchJL(Size__64_bit_inst_function_call_support));

    return addressCalc;
}

Vector<Instruction*>* CacheSimulation::generateBufferedAddressCalculation32(Instruction* instruction, uint64_t bufferStore, uint64_t bufferPtrStore, uint32_t blockId, uint32_t memopId, uint32_t bufferSize, FlagsProtectionMethods method){
    Vector<Instruction*>* addressCalc = new Vector<Instruction*>();
    uint64_t dataAddr = getInstDataAddress();

    MemoryOperand* memerand = NULL;
    Operand* operand = NULL;
    if (instruction->isExplicitMemoryOperation()){
        memerand = new MemoryOperand(instruction->getMemoryOperand(), this);
        operand = memerand->getOperand();
    }

    // find 3 temp registers
    BitSet<uint32_t>* availableRegs = new BitSet<uint32_t>(X86_64BIT_GPRS);
    availableRegs->insert(X86_REG_SP);
    if (operand){
        operand->getInstruction()->touchedRegisters(availableRegs);
    }

    ~(*availableRegs);

    uint32_t tempReg1 = X86_32BIT_GPRS;
    uint32_t tempReg2 = X86_32BIT_GPRS;
    uint32_t tempReg3 = X86_32BIT_GPRS;

    for (int32_t i = 0; i < availableRegs->size(); i++){
        uint32_t idx = X86_32BIT_GPRS - i;
        if (availableRegs->contains(idx)){
            if (tempReg1 == X86_32BIT_GPRS){
                tempReg1 = idx;
            } else if (tempReg2 == X86_32BIT_GPRS){
                tempReg2 = idx;
            } else if (tempReg3 == X86_32BIT_GPRS){
                tempReg3 = idx;
            }
        }
    }
    ASSERT(tempReg1 != X86_32BIT_GPRS && tempReg2 != X86_32BIT_GPRS && tempReg3 != X86_32BIT_GPRS);
    delete availableRegs;

    uint8_t baseReg = 0;
    if (operand){
        if (operand->GET(base)){
            if (!IS_32BIT_GPR(operand->GET(base))){
                PRINT_ERROR("bad operand value %d -- %s", operand->GET(base), ud_reg_tab[operand->GET(base)-1]);
            }
            if (IS_32BIT_GPR(operand->GET(base))){
                baseReg = operand->GET(base) - UD_R_EAX;
            } else {
                baseReg = UD_R_EAX - UD_R_EAX;
            }
        } else {
            ASSERT(operand->getValue() || operand->GET(index));
        }
    }

    uint8_t indexReg = 0;
    if (operand){
        if (operand->GET(index)){
            ASSERT(operand->GET(index) >= UD_R_EAX && operand->GET(index) <= UD_R_EDI);
            indexReg = operand->GET(index) - UD_R_EAX;
        } else {
            ASSERT(!operand->GET(scale));
        }
    }

    //    PRINT_INFOR("Using tmp1/tmp2/base/index/value/scale/baddr %hhd/%hhd/%hhd/%hhd/%#llx/%d/%#llx", tempReg1, tempReg2, baseReg, indexReg, operand->getValue(), operand->GET(scale), operand->getInstruction()->getProgramAddress());
    
    (*addressCalc).append(InstructionGenerator32::generateMoveRegToMem(tempReg1, dataAddr + getRegStorageOffset() + 2*(sizeof(uint64_t))));
    (*addressCalc).append(InstructionGenerator32::generateMoveRegToMem(tempReg2, dataAddr + getRegStorageOffset() + 3*(sizeof(uint64_t))));
    (*addressCalc).append(InstructionGenerator32::generateMoveRegToMem(tempReg3, dataAddr + getRegStorageOffset() + 4*(sizeof(uint64_t))));
 
    if (operand){
        if (operand->GET(base)){
            (*addressCalc).append(InstructionGenerator32::generateMoveRegToReg(baseReg, tempReg1));
#ifndef NO_LAHF_SAHF
            // AX contains the flags values and the legitimate value of AX is in regStorage when LAHF/SAHF are in place
            if (baseReg == X86_REG_AX && method == FlagsProtectionMethod_light){
                //(*addressCalc).append(InstructionGenerator64::generateMoveMemToReg(dataAddr + getRegStorageOffset(), tempReg1));
            }
#endif
        }
        if (operand->GET(index)){
            (*addressCalc).append(InstructionGenerator32::generateMoveRegToReg(indexReg, tempReg2));
#ifndef NO_LAHF_SAHF
            // AX contains the flags values and the legitimate value of AX is in regStorage when LAHF/SAHF are in place
            if (indexReg == X86_REG_AX && method == FlagsProtectionMethod_light){
                //(*addressCalc).append(InstructionGenerator64::generateMoveMemToReg(dataAddr + getRegStorageOffset(), tempReg2));
            }
#endif
        }

        if (operand->GET(base)){
            (*addressCalc).append(InstructionGenerator32::generateRegAddImm(tempReg1, operand->getValue())); 
        } else {
            (*addressCalc).append(InstructionGenerator32::generateMoveImmToReg(operand->getValue(), tempReg1));
        }

        if (operand->GET(index)){
            uint8_t scale = operand->GET(scale);
            if (!scale){
                scale++;
            }
            (*addressCalc).append(InstructionGenerator32::generateRegImm1ByteMultReg(tempReg2, scale, tempReg2));
            (*addressCalc).append(InstructionGenerator32::generateRegAddReg2OpForm(tempReg2, tempReg1));
        }
    }

    (*addressCalc).append(InstructionGenerator32::generateMoveImmToReg(dataAddr + bufferStore, tempReg2));
    (*addressCalc).append(InstructionGenerator32::generateMoveMemToReg(dataAddr + bufferPtrStore, tempReg3));

    (*addressCalc).append(InstructionGenerator32::generateShiftLeftLogical(logBase2(Size__BufferEntry), tempReg3));
    (*addressCalc).append(InstructionGenerator32::generateRegAddReg2OpForm(tempReg3, tempReg2));
    (*addressCalc).append(InstructionGenerator32::generateShiftRightLogical(logBase2(Size__BufferEntry), tempReg3));
    (*addressCalc).append(InstructionGenerator32::generateMoveRegToRegaddrImm(tempReg1, tempReg2, 0));

    (*addressCalc).append(InstructionGenerator32::generateRegAddImm(tempReg3, 1));
    (*addressCalc).append(InstructionGenerator32::generateMoveRegToMem(tempReg3, dataAddr + bufferPtrStore));
    (*addressCalc).append(InstructionGenerator32::generateCompareImmReg(bufferSize, tempReg3));

    (*addressCalc).append(InstructionGenerator32::generateMoveMemToReg(dataAddr + getRegStorageOffset() + 4*(sizeof(uint64_t)), tempReg3));
    (*addressCalc).append(InstructionGenerator32::generateMoveMemToReg(dataAddr + getRegStorageOffset() + 3*(sizeof(uint64_t)), tempReg2));
    (*addressCalc).append(InstructionGenerator32::generateMoveMemToReg(dataAddr + getRegStorageOffset() + 2*(sizeof(uint64_t)), tempReg1));

    (*addressCalc).append(InstructionGenerator::generateBranchJL(Size__32_bit_inst_function_call_support));
    return addressCalc;
}
