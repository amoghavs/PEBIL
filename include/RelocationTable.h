#ifndef _RelocationTable_h_
#define _RelocationTable_h_

#include <Base.h>
#include <SectionHeader.h>
#include <ElfFile.h>
#include <RawSection.h>
#include <defines/RelocationTable.d>

class SymbolTable;
class ElfFile;
class SectionHeader;
class RawSection;

class Relocation {
protected:
    Relocation(char* relPtr, uint32_t idx) : relocationPtr(relPtr), index(idx) {}
    char* relocationPtr;
    uint32_t index;
public:
    virtual ~Relocation() {}
    virtual char* charStream() { __SHOULD_NOT_ARRIVE; return NULL; }
    virtual uint32_t read(BinaryInputFile* binaryInputFile) { __SHOULD_NOT_ARRIVE; }
    virtual void print() { __SHOULD_NOT_ARRIVE; }
    virtual uint64_t getSymbol() { __SHOULD_NOT_ARRIVE; }
    virtual uint64_t getType() { __SHOULD_NOT_ARRIVE; }

    RELOCATION_MACROS_BASIS("For the get_X field macros check the defines directory");
};

class Relocation32 : public Relocation {
protected:
    Elf32_Rel entry;
public:
    Relocation32(char* relPtr, uint32_t idx) : Relocation(relPtr,idx) {}
    ~Relocation32() {}
    char* charStream() { return (char*)&entry; }
    uint32_t read(BinaryInputFile* binaryInputFile);
    void print();
    uint64_t getSymbol() { return (uint64_t)ELF32_R_SYM (GET(r_info)); }
    uint64_t getType()   { return (uint64_t)ELF32_R_TYPE(GET(r_info)); }

    RELOCATION_MACROS_CLASS("For the get_X field macros check the defines directory");
};

class Relocation64 : public Relocation {
protected:
    Elf64_Rel entry;
public:
    Relocation64(char* relPtr, uint32_t idx) : Relocation(relPtr,idx) {}
    ~Relocation64() {}
    char* charStream() { return (char*)&entry; }
    uint32_t read(BinaryInputFile* binaryInputFile);
    void print();

    RELOCATION_MACROS_CLASS("For the get_X field macros check the defines directory");
};

class RelocationAddend32 : public Relocation {
protected:
    Elf32_Rela entry;
public:
    RelocationAddend32(char* relPtr, uint32_t idx) : Relocation(relPtr,idx) {}
    ~RelocationAddend32() {}
    char* charStream() { return (char*)&entry; }
    uint32_t read(BinaryInputFile* binaryInputFile);
    void print();
    uint64_t getSymbol() { return (uint64_t)ELF32_R_SYM (GET(r_info)); }
    uint64_t getType()   { return (uint64_t)ELF32_R_TYPE(GET(r_info)); }


    RELOCATION_MACROS_CLASS("For the get_X field macros check the defines directory");
    // need a seperate macro set (actually just 1) for the relocation addend structure
    RELOCATIONADDEND_MACROS_CLASS("For the get_X field macros check the defines directory");
};

class RelocationAddend64 : public Relocation {
protected:
    Elf64_Rela entry;
public:
    RelocationAddend64(char* relPtr, uint32_t idx) : Relocation(relPtr,idx) {}
    ~RelocationAddend64() {}
    char* charStream() { return (char*)&entry; }
    uint32_t read(BinaryInputFile* binaryInputFile);
    void print();
    uint64_t getSymbol() { return (uint64_t)ELF64_R_SYM (GET(r_info)); }
    uint64_t getType()   { return (uint64_t)ELF64_R_TYPE(GET(r_info)); }

    RELOCATION_MACROS_CLASS("For the get_X field macros check the defines directory");
    // need a seperate macro set (actually just 1) for the relocation addend structure
    RELOCATIONADDEND_MACROS_CLASS("For the get_X field macros check the defines directory");
};

class RelocationTable : public RawSection {
protected:
    
    ElfRelType type;
    SymbolTable* symbolTable;
    RawSection* relocationSection;
    uint32_t index;

    uint32_t numberOfRelocations;
    Relocation** relocations;

public:

    RelocationTable(char* rawPtr, uint64_t size, uint16_t scnIdx, uint32_t idx, ElfFile* elf)
        : RawSection(ElfClassTypes_relocation_table,rawPtr,size,scnIdx,elf),index(idx),symbolTable(NULL),relocationSection(NULL)
    {
        ASSERT(elfFile);
        ASSERT(elfFile->getSectionHeader(sectionIndex));

        sizeInBytes = size;
        uint32_t relocationSize;
        uint32_t typ = elfFile->getSectionHeader(sectionIndex)->GET(sh_type);

        ASSERT((typ == SHT_REL || typ == SHT_RELA) && "Section header type field must be relocation");


        if (elfFile->is64Bit()){
            if (typ == SHT_RELA){
                relocationSize = Size__64_bit_Relocation_Addend;
                type = ElfRelType_rela;
            } else {
                relocationSize = Size__64_bit_Relocation;
                type = ElfRelType_rel;
            }
        } else {
            if (typ == SHT_RELA){
                relocationSize = Size__32_bit_Relocation_Addend;
                type = ElfRelType_rela;
            } else {
                relocationSize = Size__32_bit_Relocation;
                type = ElfRelType_rel;
            }
        }
        ASSERT(sizeInBytes % relocationSize == 0 && "Section size is bad");
        numberOfRelocations = sizeInBytes / relocationSize;

        relocations = new Relocation*[numberOfRelocations];
    }
    ~RelocationTable(){
        if (relocations){
            for (uint32_t i = 0; i < numberOfRelocations; i++){
                if (relocations[i]){
                    delete relocations[i];
                }
            }
            delete[] relocations;
        }
    }


    void print();
    uint32_t read(BinaryInputFile* b);

    uint32_t getNumberOfRelocations() { return numberOfRelocations; }

    ElfFile* getElfFile() { return elfFile; }
    uint32_t getIndex() { return index; }

    uint16_t setSymbolTable();
    uint16_t setRelocationSection();

    const char* briefName() { return "RelocationTable"; }
//    uint32_t instrument(char* buffer,XCoffFileGen* xCoffGen,BaseGen* gen);
};

#endif

