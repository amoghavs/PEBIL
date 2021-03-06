/* 
 * This file is part of the pebil project.
 * 
 * Copyright (c) 2010, University of California Regents
 * All rights reserved.
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <TextSection.h>

#include <BasicBlock.h>
#include <ElfFile.h>
#include <Function.h>
#include <X86Instruction.h>
#include <SectionHeader.h>
#include <SymbolTable.h>

//#define GENERATE_BLACKLIST

void TextSection::wedge(uint32_t shamt){
    for (uint32_t i = 0; i < sortedTextObjects.size(); i++){
        sortedTextObjects[i]->wedge(shamt);
    }
}

void FreeText::wedge(uint32_t shamt){
    for (uint32_t i = 0; i < blocks.size(); i++){
        blocks[i]->setBaseAddress(blocks[i]->getBaseAddress() + shamt);
    }
    setBaseAddress(getBaseAddress() + shamt);
}

uint32_t FreeText::getNumberOfInstructions(){
    uint32_t numberOfInstructions = 0;
    for (uint32_t i = 0; i < blocks.size(); i++){
        if (blocks[i]->getType() == PebilClassType_BasicBlock ||
            blocks[i]->getType() == PebilClassType_CodeBlock){
            numberOfInstructions += ((CodeBlock*)blocks[i])->getNumberOfInstructions();
        }
    }
    return numberOfInstructions;
}

char* TextObject::getName(){
    if (symbol){
        return symbol->getSymbolName();
    }
    return symbol_without_name;
}

uint32_t TextSection::printDisassembly(bool instructionDetail){
    ASSERT(elfFile && "Text section should be linked to its corresponding ElfFile object");

    fprintf(stdout, "Disassembly of section %s\n\n", getSectionHeader()->getSectionNamePtr());

    for (uint32_t i = 0; i < sortedTextObjects.size(); i++){
        sortedTextObjects[i]->printDisassembly(instructionDetail);
        fprintf(stdout, "\n");
    }
}

void FreeText::printDisassembly(bool instructionDetail){
    fprintf(stdout, "%llx <free -- %s>:\n", getBaseAddress(), getName());

    for (uint32_t i = 0; i < blocks.size(); i++){
        blocks[i]->printDisassembly(instructionDetail);
    }
}

uint32_t FreeText::getAllInstructions(X86Instruction** allinsts, uint32_t nexti){
    uint32_t instructionCount = 0;

    for (uint32_t i = 0; i < blocks.size(); i++){
        if (blocks[i]->getType() == PebilClassType_BasicBlock){
            BasicBlock* bb = (BasicBlock*)blocks[i];
            bb->getAllInstructions(allinsts, nexti + instructionCount);
            instructionCount += bb->getNumberOfInstructions();
        } else if (blocks[i]->getType() == PebilClassType_CodeBlock){
            CodeBlock* cb = (CodeBlock*)blocks[i];
            cb->getAllInstructions(allinsts, nexti + instructionCount);
            instructionCount += cb->getNumberOfInstructions();
        }
    }
    return instructionCount;
}

uint32_t TextSection::getAllInstructions(X86Instruction** allinsts, uint32_t nexti){
    uint32_t instructionCount = 0;
    for (uint32_t i = 0; i < sortedTextObjects.size(); i++){
        instructionCount += sortedTextObjects[i]->getAllInstructions(allinsts, instructionCount+nexti);
    }
    ASSERT(instructionCount == getNumberOfInstructions());
    return instructionCount;
}

Function* TextSection::replaceFunction(uint32_t idx, Function* replacementFunction){
    ASSERT(idx < sortedTextObjects.size() && "function index out of bounds");
    ASSERT(sortedTextObjects[idx]->isFunction() && "function index is not a function");

    Function* toReplace = (Function*)sortedTextObjects[idx];
    ASSERT(toReplace->getNumberOfBytes() == replacementFunction->getNumberOfBytes());

    sortedTextObjects.assign(replacementFunction,idx);
    return toReplace;
}

uint32_t TextSection::getNumberOfBasicBlocks(){
    uint32_t numberOfBasicBlocks = 0;
    for (uint32_t i = 0; i < sortedTextObjects.size(); i++){
        if (sortedTextObjects[i]->isFunction()){
            numberOfBasicBlocks += ((Function*)sortedTextObjects[i])->getFlowGraph()->getNumberOfBasicBlocks();
        }
    }
    return numberOfBasicBlocks;
}

uint32_t TextSection::getNumberOfInstructions(){
    uint32_t numberOfInstructions = 0;
    for (uint32_t i = 0; i < sortedTextObjects.size(); i++){
        numberOfInstructions += sortedTextObjects[i]->getNumberOfInstructions();
    }
    return numberOfInstructions;
}

uint32_t TextSection::getNumberOfMemoryOps(){
    uint32_t numberOfMemoryOps = 0;
    for (uint32_t i = 0; i < sortedTextObjects.size(); i++){
        if (sortedTextObjects[i]->isFunction()){
            numberOfMemoryOps += ((Function*)sortedTextObjects[i])->getFlowGraph()->getNumberOfMemoryOps();
        }
    }
    return numberOfMemoryOps;
}

uint32_t TextSection::getNumberOfFloatOps(){
    uint32_t numberOfFloatOps = 0;
    for (uint32_t i = 0; i < sortedTextObjects.size(); i++){
        if (sortedTextObjects[i]->isFunction()){
            numberOfFloatOps += ((Function*)sortedTextObjects[i])->getFlowGraph()->getNumberOfFloatOps();
        }
    }
    return numberOfFloatOps;
}

ByteSources TextSection::getByteSource(){
    return source;
}

uint32_t TextSection::buildLoops(){
    uint32_t numberOfLoops = 0;
    for (uint32_t i = 0; i < sortedTextObjects.size(); i++){
        if (sortedTextObjects[i]->isFunction()){
            numberOfLoops += ((Function*)sortedTextObjects[i])->getFlowGraph()->buildLoops();
        }
    }
    return numberOfLoops;
}

void FreeText::print(){
    PRINT_INFOR("Free Text area at address %#llx", baseAddress);
}

void TextSection::printLoops(){
    for (uint32_t i = 0; i < sortedTextObjects.size(); i++){
        if (sortedTextObjects[i]->isFunction()){
            ((Function*)sortedTextObjects[i])->getFlowGraph()->printLoops();
        }
    }
}

bool TextObject::isFunction(){
    return (getType() == PebilClassType_Function);
}


Vector<Symbol*> TextSection::discoverTextObjects(){
    Vector<Symbol*> functionSymbols;

    ASSERT(!functionSymbols.size() && "This array should be empty since it is loaded by this function");

    // count the number of symbols for this text section
    uint32_t numberOfSymbols = 0;
    for (uint32_t i = 0; i < elfFile->getNumberOfSymbolTables(); i++){
        SymbolTable* symbolTable = elfFile->getSymbolTable(i);
        if (!symbolTable->isDynamic()){
            for (uint32_t j = 0; j < symbolTable->getNumberOfSymbols(); j++){
                Symbol* symbol = symbolTable->getSymbol(j);
                if (symbol->isFunctionSymbol(this)){
                    functionSymbols.append(symbol);
                } else if (symbol->isTextObjectSymbol(this)){
                    functionSymbols.append(symbol);
                }
            }
        }
    }

    // sort text symbols in decreasing order
    qsort(&functionSymbols,functionSymbols.size(),sizeof(Symbol*),compareSymbolValue);

    // delete symbol values that have duplicate values
    functionSymbols.reverse();
    if (functionSymbols.size()){
        for (uint32_t i = 0; i < functionSymbols.size()-1; i++){
            while (functionSymbols.size() > i+1 && functionSymbols[i+1]->GET(st_value) == functionSymbols[i]->GET(st_value)){
                functionSymbols.remove(i+1);
            }
        }
    }
    functionSymbols.reverse();

    return functionSymbols;
}

char* TextObject::charStream(){
    ASSERT(textSection);
    uint64_t functionOffset = getBaseAddress() -
        textSection->getElfFile()->getSectionHeader(textSection->getSectionIndex())->GET(sh_addr);
    return (char*)(textSection->getFilePointer() + functionOffset);
}

Vector<X86Instruction*>* TextObject::digestLinear(){
    Vector<X86Instruction*>* allInstructions = new Vector<X86Instruction*>();

    uint32_t currByte = 0;
    uint32_t instructionLength = 0;
    uint64_t instructionAddress;

    PRINT_DEBUG_CFG("Digesting textobject linearly");

    uint32_t numberOfInstructions = 0;
    while (currByte < sizeInBytes){

        instructionAddress = (uint64_t)((uint64_t)charStream() + currByte);
        X86Instruction* newInstruction = new X86Instruction(this, getBaseAddress() + currByte, charStream() + currByte, ByteSource_Application_FreeText, numberOfInstructions++);
        PRINT_DEBUG_CFG("linear cfg: instruction at %#llx with %d bytes", newInstruction->getBaseAddress(), newInstruction->getSizeInBytes());

        (*allInstructions).append(newInstruction);
        currByte += newInstruction->getSizeInBytes();
    }

    // in case the disassembler found an instruction that exceeds the function boundary, we will
    // reduce the size of the last instruction accordingly so that the extra bytes will not be
    // used
    if (currByte > sizeInBytes){
        uint32_t extraBytes = currByte-sizeInBytes;
        (*allInstructions).back()->setSizeInBytes((*allInstructions).back()->getSizeInBytes()-extraBytes);
        currByte -= extraBytes;

        char oType[9];
        if (getType() == PebilClassType_FreeText){
            sprintf(oType, "%s", "FreeText\0");
        } else if (getType() == PebilClassType_Function){
            sprintf(oType, "%s", "Function\0");
        }

        PRINT_WARN(3,"Found instructions that lexceed the %s boundary in %.24s by %d bytes", oType, getName(), extraBytes);
    }

    ASSERT(currByte == sizeInBytes && "Number of bytes read does not match object size");

    return allInstructions;   
}

uint32_t FreeText::digest(Vector<AddressAnchor*>* addressAnchors){
    ASSERT(!blocks.size());
    if (usesInstructions){
        PRINT_DEBUG_CFG("\tdigesting freetext instructions at %#llx", getBaseAddress());
        Vector<X86Instruction*>* allInstructions = digestLinear();
        ASSERT(allInstructions);
        (*allInstructions).sort(compareBaseAddress);

        CodeBlock* cb = new CodeBlock(0, NULL);
        cb->setBaseAddress(getBaseAddress());
        for (uint32_t i = 0; i < (*allInstructions).size(); i++){
            cb->addInstruction((*allInstructions)[i]);
        }
        blocks.append(cb);
        delete allInstructions;
    } else {
        PRINT_DEBUG_CFG("\tdigesting freetext unknown area at %#llx", getBaseAddress());
        RawBlock* ub = new RawBlock(0, NULL, textSection->getStreamAtAddress(getBaseAddress()),
                                            sizeInBytes, getBaseAddress());
        blocks.append(ub);
    }
    return sizeInBytes;
}

void FreeText::dump(BinaryOutputFile* binaryOutputFile, uint32_t offset){
    uint32_t currByte = 0;

    for (uint32_t i = 0; i < blocks.size(); i++){
        blocks[i]->dump(binaryOutputFile,offset + currByte);
        currByte += blocks[i]->getNumberOfBytes();
    }
    ASSERT(currByte == sizeInBytes && "Size dumped does not match object size");
}

FreeText::FreeText(TextSection* text, uint32_t idx, Symbol* sym, uint64_t addr, uint32_t sz, bool usesI)
    : TextObject(PebilClassType_FreeText, text, idx, sym, addr, sz)
{
    usesInstructions = usesI;
}

FreeText::~FreeText(){
    for (uint32_t i = 0; i < blocks.size(); i++){
        delete blocks[i];
    }
}

bool TextObject::inRange(uint64_t addr){
    if (addr >= baseAddress && addr < baseAddress + sizeInBytes){
        return true;
    }
    return false;
}

TextObject::TextObject(PebilClassTypes typ, TextSection* text, uint32_t idx, Symbol* sym, uint64_t addr, uint32_t sz) :
    Base(typ)
{
    symbol = sym;
    textSection = text;
    index = idx;
    baseAddress = addr;
    sizeInBytes = sz;
}


uint64_t TextSection::getBaseAddress() { 
    return elfFile->getSectionHeader(sectionIndex)->GET(sh_addr); 
}

bool TextSection::inRange(uint64_t addr) { 
    return elfFile->getSectionHeader(sectionIndex)->inRange(addr); 
}

TextSection::TextSection(char* filePtr, uint64_t size, uint16_t scnIdx, uint32_t idx, ElfFile* elf, ByteSources src) :
    RawSection(PebilClassType_TextSection,filePtr,size,scnIdx,elf)
{
    index = idx;
    source = src;
}

uint32_t TextSection::disassemble(BinaryInputFile* binaryInputFile){
    SectionHeader* sectionHeader = elfFile->getSectionHeader(getSectionIndex());

    Vector<Symbol*> textSymbols = discoverTextObjects();

    if (textSymbols.size()){
        uint32_t i;

        for (i = 0; i < textSymbols.size()-1; i++){

            // use the max of: the size listed in the symbol table and the size between this function and the next
            uint32_t size = textSymbols[i+1]->GET(st_value) - textSymbols[i]->GET(st_value);
            if (textSymbols[i]->GET(st_size) > size && textSymbols[i]->GET(st_size) < sectionHeader->GET(sh_size)){
                size = textSymbols[i]->GET(st_size);
            }

            if (textSymbols[i]->isFunctionSymbol(this)){
                sortedTextObjects.append(new Function(this, i, textSymbols[i], size));
                ASSERT(sortedTextObjects.back()->isFunction());
#ifdef GENERATE_BLACKLIST
                fprintf(stdout, "pebil_function_list %s\n", ((Function*)sortedTextObjects.back())->getName());
#endif
            } else if (textSymbols[i]->isTextObjectSymbol(this)){
                sortedTextObjects.append(new FreeText(this, i, textSymbols[i], textSymbols[i]->GET(st_value), size, false));
                ASSERT(!sortedTextObjects.back()->isFunction());
            } else {
                PRINT_ERROR("Unknown symbol type found to be associated with text section");
            }
        }

        // the last function ends at the end of the section
        uint32_t size = sectionHeader->GET(sh_addr) + sectionHeader->GET(sh_size) - textSymbols.back()->GET(st_value);
        if (textSymbols[i]->GET(st_size) > size){
            size = textSymbols[i]->GET(st_size);
        }
        if (textSymbols.back()->isFunctionSymbol(this)){
            sortedTextObjects.append(new Function(this, i, textSymbols.back(), size));
        } else {
            sortedTextObjects.append(new FreeText(this, i, textSymbols.back(), textSymbols.back()->GET(st_value), size, false));
        }
    }

    // this is a text section with no functions (probably the .plt section), so we will put everything into a single textobject
    else{
        sortedTextObjects.append(new FreeText(this, 0, NULL, sectionHeader->GET(sh_addr), sectionHeader->GET(sh_size), true));
    }

    verify();

    return sortedTextObjects.size();
}

uint32_t TextSection::generateCFGs(Vector<AddressAnchor*>* addressAnchors){
    for (uint32_t i = 0; i < sortedTextObjects.size(); i++){
        if (sortedTextObjects[i]->isFunction()){
            PRINT_DEBUG_CFG("Digesting function object at %#llx", sortedTextObjects[i]->getBaseAddress());
        } else {
            PRINT_DEBUG_CFG("Digesting gentext object at %#llx", sortedTextObjects[i]->getBaseAddress());
        }
        sortedTextObjects[i]->digest(addressAnchors);
    }

    verify();
}

uint32_t TextSection::read(BinaryInputFile* binaryInputFile){
    return 0;
}


uint64_t TextSection::findInstrumentationPoint(uint64_t addr, uint32_t size, InstLocations loc){
    //    ASSERT((loc == InstLocation_dont_care || loc == InstLocation_exact) && "Unsupported inst location being used in TextSection");
    ASSERT(inRange(addr) && "Instrumentation address should fall within TextSection bounds");

    for (uint32_t i = 0; i < sortedTextObjects.size(); i++){
        if (sortedTextObjects[i]->getType() == PebilClassType_Function){
            Function* f = (Function*)sortedTextObjects[i];
            if (f->inRange(addr)){
                return f->findInstrumentationPoint(addr, size, loc);
            }
            /*
              else { // loc == InstLocation_dont_care
                uint64_t instAddress = f->findInstrumentationPoint(addr, size, loc);
                if (instAddress){
                    return instAddress;
                }
            }
            */
        }
    }
    PRINT_ERROR("No instrumentation point found in (text) section %d", getSectionIndex());
    __SHOULD_NOT_ARRIVE;
    return 0;
}


Vector<X86Instruction*>* TextSection::swapInstructions(uint64_t addr, Vector<X86Instruction*>* replacements){
    for (uint32_t i = 0; i < sortedTextObjects.size(); i++){
        if (sortedTextObjects[i]->getType() == PebilClassType_Function){
            Function* f = (Function*)sortedTextObjects[i];
            //            PRINT_INFOR("Searching function %s at range [%#llx,%#llx)", f->getName(), f->getBaseAddress(), f->getBaseAddress()+f->getSizeInBytes());
            if (f->inRange(addr)){
                return f->swapInstructions(addr, replacements);
                for (uint32_t j = 0; j < f->getNumberOfBasicBlocks(); j++){
                    if (f->getBasicBlock(j)->inRange(addr)){
                        return f->getBasicBlock(j)->swapInstructions(addr,replacements);
                    }
                }
            }
        }
    }
    PRINT_ERROR("Cannot find instructions at address 0x%llx to replace", addr);
    return 0;
}


void TextSection::printInstructions(){
    PRINT_INFOR("Printing Instructions for (text) section %d", getSectionIndex());
    for (uint32_t i = 0; i < sortedTextObjects.size(); i++){
        if (sortedTextObjects[i]->getType() == PebilClassType_Function){
            ((Function*)sortedTextObjects[i])->printInstructions();
        }
    }
}


X86Instruction* TextSection::getInstructionAtAddress(uint64_t addr){
    SectionHeader* sectionHeader = elfFile->getSectionHeader(getSectionIndex());
    if (!sectionHeader->inRange(addr)){
        return NULL;
    }

    for (uint32_t i = 0; i < sortedTextObjects.size(); i++){
        if (sortedTextObjects[i]->getType() == PebilClassType_Function){
            Function* f = (Function*)sortedTextObjects[i];
            if (f->inRange(addr)){
                return f->getInstructionAtAddress(addr);
            }
        }
    }
    return NULL;
}

BasicBlock* TextSection::getBasicBlockAtAddress(uint64_t addr){
    SectionHeader* sectionHeader = elfFile->getSectionHeader(getSectionIndex());
    if (!sectionHeader->inRange(addr)){
        return NULL;
    }

    for (uint32_t i = 0; i < sortedTextObjects.size(); i++){
        if (sortedTextObjects[i]->getType() == PebilClassType_Function){
            Function* f = (Function*)sortedTextObjects[i];
            if (f->inRange(addr)){
                return f->getBasicBlockAtAddress(addr);
            }
        }
    }
    return NULL;
}

bool TextSection::verify(){
    SectionHeader* sectionHeader = elfFile->getSectionHeader(getSectionIndex());

    if (sortedTextObjects.size()){

        for (uint32_t i = 0; i < sortedTextObjects.size(); i++){
            
            uint64_t entrAddr = sortedTextObjects[i]->getBaseAddress();
            uint64_t exitAddr = entrAddr + sortedTextObjects[i]->getSizeInBytes();
            
            // make sure each function entry resides within the bounds of this section
            if (!sectionHeader->inRange(entrAddr)){
                sectionHeader->print();
                PRINT_ERROR("The function entry address 0x%016llx is not in the range of section %d", entrAddr, sectionHeader->getIndex());
                return false;
            }
            
            // make sure each function exit resides within the bounds of this section
            if (!sectionHeader->inRange(exitAddr) && exitAddr != sectionHeader->GET(sh_addr) + sectionHeader->GET(sh_size)){
                sortedTextObjects[i]->print();
                sectionHeader->print();
                PRINT_INFOR("Section range [0x%016llx,0x%016llx]", sectionHeader->GET(sh_addr), sectionHeader->GET(sh_addr) + sectionHeader->GET(sh_size));
                PRINT_ERROR("The function exit address 0x%016llx is not in the range of section %d", exitAddr, sectionHeader->getIndex());
                return false;
            }
        }

        for (uint32_t i = 0; i < sortedTextObjects.size() - 1; i++){
            
            // make sure sortedTextObjects is actually sorted
            if (sortedTextObjects[i]->getBaseAddress() > sortedTextObjects[i+1]->getBaseAddress()){
                sortedTextObjects[i]->print();
                sortedTextObjects[i+1]->print();
                PRINT_ERROR("Function addresses 0x%016llx 0x%016llx are not sorted", sortedTextObjects[i]->getBaseAddress(), sortedTextObjects[i+1]->getBaseAddress());
                return false;
            }
        }
        
        // make sure functions span the entire section unless it is a plt section
        if (sortedTextObjects.size()){
            
            // check that the first function is at the section beginning
            if (sortedTextObjects[0]->getBaseAddress() != sectionHeader->GET(sh_addr)){
                PRINT_ERROR("First function in section %d should be at the beginning of the section", getSectionIndex());
                return false;
            }
        }
    }

    return true;
}


void TextSection::dump(BinaryOutputFile* binaryOutputFile, uint32_t offset){
    uint32_t currByte = 0;

    char* buff = new char[getSizeInBytes()];
    memset(buff, 0x00, getSizeInBytes());
    binaryOutputFile->copyBytes(buff, getSizeInBytes(), offset);
    delete[] buff;

    if (sortedTextObjects.size()){
        for (int32_t i = 0; i < sortedTextObjects.size() - 1; i++){
            ASSERT(sortedTextObjects[i] && "The functions in this text section should be initialized");
            sortedTextObjects[i]->dump(binaryOutputFile, offset + currByte);
            
            // functions can overlap! this puts the function in the correct original spot
            uint32_t actualFunctionSize = sortedTextObjects[i+1]->getSymbolValue() - sortedTextObjects[i]->getSymbolValue();
            currByte += actualFunctionSize;
        }
        sortedTextObjects.back()->dump(binaryOutputFile, offset + currByte);
    }
}


TextSection::~TextSection(){
    for (uint32_t i = 0; i < sortedTextObjects.size(); i++){
        delete sortedTextObjects[i];
    }
}

