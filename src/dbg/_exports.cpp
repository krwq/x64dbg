/**
 @file _exports.cpp

 @brief Implements the exports class.
 */

#include "_exports.h"
#include "memory.h"
#include "debugger.h"
#include "value.h"
#include "threading.h"
#include "breakpoint.h"
#include "disasm_helper.h"
#include "simplescript.h"
#include "symbolinfo.h"
#include "assemble.h"
#include "stackinfo.h"
#include "thread.h"
#include "disasm_fast.h"
#include "plugin_loader.h"
#include "_dbgfunctions.h"
#include "module.h"
#include "comment.h"
#include "label.h"
#include "bookmark.h"
#include "function.h"
#include "loop.h"
#include "error.h"
#include "x64_dbg.h"
#include "threading.h"
#include "stringformat.h"
#include "xrefs.h"
#include "encodemap.h"
#include "argument.h"
#include "watch.h"

static bool bOnlyCipAutoComments = false;

extern "C" DLL_EXPORT duint _dbg_memfindbaseaddr(duint addr, duint* size)
{
    return MemFindBaseAddr(addr, size);
}

extern "C" DLL_EXPORT bool _dbg_memread(duint addr, unsigned char* dest, duint size, duint* read)
{
    return MemRead(addr, dest, size, read, true);
}

extern "C" DLL_EXPORT bool _dbg_memwrite(duint addr, const unsigned char* src, duint size, duint* written)
{
    return MemWrite(addr, src, size, written);
}

extern "C" DLL_EXPORT bool _dbg_memmap(MEMMAP* memmap)
{
    SHARED_ACQUIRE(LockMemoryPages);

    int pagecount = (int)memoryPages.size();
    memset(memmap, 0, sizeof(MEMMAP));
    memmap->count = pagecount;
    if(!pagecount)
        return true;

    // Allocate memory that is already zeroed
    memmap->page = (MEMPAGE*)BridgeAlloc(sizeof(MEMPAGE) * pagecount);

    // Copy all elements over
    int i = 0;

    for(auto & itr : memoryPages)
        memcpy(&memmap->page[i++], &itr.second, sizeof(MEMPAGE));

    // Done
    return true;
}

extern "C" DLL_EXPORT bool _dbg_memisvalidreadptr(duint addr)
{
    return MemIsValidReadPtr(addr, true);
}

extern "C" DLL_EXPORT bool _dbg_valfromstring(const char* string, duint* value)
{
    return valfromstring(string, value);
}

extern "C" DLL_EXPORT bool _dbg_isdebugging()
{
    if(IsFileBeingDebugged())
        return true;

    return false;
}

extern "C" DLL_EXPORT bool _dbg_isjumpgoingtoexecute(duint addr)
{
    static duint cacheFlags;
    static duint cacheAddr;
    static bool cacheResult;
    if(cacheAddr != addr || cacheFlags != GetContextDataEx(hActiveThread, UE_EFLAGS))
    {
        cacheFlags = GetContextDataEx(hActiveThread, UE_EFLAGS);
        cacheAddr = addr;
        cacheResult = IsJumpGoingToExecuteEx(fdProcessInfo->hProcess, fdProcessInfo->hThread, (ULONG_PTR)cacheAddr, cacheFlags);
    }
    return cacheResult;
}

static bool shouldFilterSymbol(const char* name)
{
    if(!name)
        return true;
    if(strstr(name, "`string'"))
        return true;
    if(strstr(name, "__imp_") == name || strstr(name, "_imp_") == name)
        return true;
    return false;
}

static bool getLabel(duint addr, char* label)
{
    bool retval = false;
    if(LabelGet(addr, label))
        retval = true;
    else //no user labels
    {
        DWORD64 displacement = 0;
        char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(char)];
        PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)buffer;
        pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        pSymbol->MaxNameLen = MAX_LABEL_SIZE;
        if(SafeSymFromAddr(fdProcessInfo->hProcess, (DWORD64)addr, &displacement, pSymbol) && !displacement)
        {
            pSymbol->Name[pSymbol->MaxNameLen - 1] = '\0';
            if(!bUndecorateSymbolNames || !SafeUnDecorateSymbolName(pSymbol->Name, label, MAX_LABEL_SIZE, UNDNAME_COMPLETE))
                strcpy_s(label, MAX_LABEL_SIZE, pSymbol->Name);
            retval = !shouldFilterSymbol(label);
        }
        if(!retval)  //search for CALL <jmp.&user32.MessageBoxA>
        {
            BASIC_INSTRUCTION_INFO basicinfo;
            memset(&basicinfo, 0, sizeof(BASIC_INSTRUCTION_INFO));
            if(disasmfast(addr, &basicinfo, true) && basicinfo.branch && !basicinfo.call && basicinfo.memory.value)  //thing is a JMP
            {
                duint val = 0;
                if(MemRead(basicinfo.memory.value, &val, sizeof(val), nullptr, true))
                {
                    if(SafeSymFromAddr(fdProcessInfo->hProcess, (DWORD64)val, &displacement, pSymbol) && !displacement)
                    {
                        pSymbol->Name[pSymbol->MaxNameLen - 1] = '\0';
                        if(!bUndecorateSymbolNames || !SafeUnDecorateSymbolName(pSymbol->Name, label, MAX_LABEL_SIZE, UNDNAME_COMPLETE))
                            sprintf_s(label, MAX_LABEL_SIZE, "JMP.&%s", pSymbol->Name);
                        retval = !shouldFilterSymbol(label);
                    }
                }
            }
        }
        if(!retval)  //search for module entry
        {
            duint entry = ModEntryFromAddr(addr);
            if(entry && entry == addr)
            {
                strcpy_s(label, MAX_LABEL_SIZE, "EntryPoint");
                retval = true;
            }
        }
        if(!retval)  //search for function+offset
        {
            duint start;
            if(FunctionGet(addr, &start, nullptr) && addr == start)
            {
                sprintf_s(label, MAX_LABEL_SIZE, "sub_%" fext "X", start);
                retval = true;
            }
        }
    }
    return retval;
}

extern "C" DLL_EXPORT bool _dbg_addrinfoget(duint addr, SEGMENTREG segment, ADDRINFO* addrinfo)
{
    if(!DbgIsDebugging())
        return false;
    bool retval = false;
    if(addrinfo->flags & flagmodule) //get module
    {
        if(ModNameFromAddr(addr, addrinfo->module, false)) //get module name
            retval = true;
    }
    if(addrinfo->flags & flaglabel)
    {
        retval = getLabel(addr, addrinfo->label);
    }
    if(addrinfo->flags & flagbookmark)
    {
        addrinfo->isbookmark = BookmarkGet(addr);
        retval = true;
    }
    if(addrinfo->flags & flagfunction)
    {
        if(FunctionGet(addr, &addrinfo->function.start, &addrinfo->function.end, &addrinfo->function.instrcount))
            retval = true;
    }
    if(addrinfo->flags & flagloop)
    {
        if(LoopGet(addrinfo->loop.depth, addr, &addrinfo->loop.start, &addrinfo->loop.end))
            retval = true;
    }
    if(addrinfo->flags & flagargs)
    {
        if(ArgumentGet(addr, &addrinfo->args.start, &addrinfo->args.end, &addrinfo->function.instrcount))
            retval = true;
    }
    if(addrinfo->flags & flagcomment)
    {
        *addrinfo->comment = 0;
        if(CommentGet(addr, addrinfo->comment))
        {
            retval = true;
        }
        else
        {
            DWORD dwDisplacement;
            IMAGEHLP_LINE64 line;
            line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
            if(SafeSymGetLineFromAddr64(fdProcessInfo->hProcess, (DWORD64)addr, &dwDisplacement, &line) && !dwDisplacement)
            {
                char filename[deflen] = "";
                strcpy_s(filename, line.FileName);
                int len = (int)strlen(filename);
                while(filename[len] != '\\' && len != 0)
                    len--;
                if(len)
                    len++;
                sprintf_s(addrinfo->comment, "\1%s:%u", filename + len, line.LineNumber);
                retval = true;
            }
            else if(!bOnlyCipAutoComments || addr == GetContextDataEx(hActiveThread, UE_CIP)) //no line number
            {
                DISASM_INSTR instr;
                String temp_string;
                String comment;
                ADDRINFO newinfo;
                char string_text[MAX_STRING_SIZE] = "";

                memset(&instr, 0, sizeof(DISASM_INSTR));
                disasmget(addr, &instr);
                int len_left = MAX_COMMENT_SIZE;
                for(int i = 0; i < instr.argcount; i++)
                {
                    memset(&newinfo, 0, sizeof(ADDRINFO));
                    newinfo.flags = flaglabel;

                    STRING_TYPE strtype = str_none;

                    if(instr.arg[i].constant == instr.arg[i].value) //avoid: call <module.label> ; addr:label
                    {
                        if(instr.type == instr_branch)
                            continue;
                        if(DbgGetStringAt(instr.arg[i].constant, string_text))
                        {
                            temp_string = instr.arg[i].mnemonic;
                            temp_string.append(":");
                            temp_string.append(string_text);
                        }
                    }
                    else if(instr.arg[i].memvalue && (DbgGetStringAt(instr.arg[i].memvalue, string_text) || _dbg_addrinfoget(instr.arg[i].memvalue, instr.arg[i].segment, &newinfo)))
                    {
                        if(*string_text)
                        {
                            temp_string = "[";
                            temp_string.append(instr.arg[i].mnemonic);
                            temp_string.append("]:");
                            temp_string.append(string_text);
                        }
                        else if(*newinfo.label)
                        {
                            temp_string = "[";
                            temp_string.append(instr.arg[i].mnemonic);
                            temp_string.append("]:");
                            temp_string.append(newinfo.label);
                        }
                    }
                    else if(instr.arg[i].value && (DbgGetStringAt(instr.arg[i].value, string_text) || _dbg_addrinfoget(instr.arg[i].value, instr.arg[i].segment, &newinfo)))
                    {
                        if(instr.type != instr_normal) //stack/jumps (eg add esp,4 or jmp 401110) cannot directly point to strings
                        {
                            if(*newinfo.label)
                            {
                                temp_string = instr.arg[i].mnemonic;
                                temp_string.append(":");
                                temp_string.append(newinfo.label);
                            }
                        }
                        else if(*string_text)
                        {
                            temp_string = instr.arg[i].mnemonic;
                            temp_string.append(":");
                            temp_string.append(string_text);
                        }
                    }
                    else
                        continue;

                    if(!strstr(comment.c_str(), temp_string.c_str())) //avoid duplicate comments
                    {
                        if(comment.length())
                            comment.append(", ");
                        comment.append(temp_string);
                        retval = true;
                    }
                }
                comment.resize(MAX_COMMENT_SIZE - 2);
                String fullComment = "\1";
                fullComment += comment;
                strcpy_s(addrinfo->comment, fullComment.c_str());
            }
        }
    }
    return retval;
}

extern "C" DLL_EXPORT bool _dbg_addrinfoset(duint addr, ADDRINFO* addrinfo)
{
    bool retval = false;
    if(addrinfo->flags & flaglabel) //set label
    {
        if(LabelSet(addr, addrinfo->label, true))
            retval = true;
    }
    if(addrinfo->flags & flagcomment) //set comment
    {
        if(CommentSet(addr, addrinfo->comment, true))
            retval = true;
    }
    if(addrinfo->flags & flagbookmark) //set bookmark
    {
        if(addrinfo->isbookmark)
            retval = BookmarkSet(addr, true);
        else
            retval = BookmarkDelete(addr);
    }
    return retval;
}

extern "C" DLL_EXPORT bool _dbg_encodetypeset(duint addr, duint size, ENCODETYPE type)
{
    return EncodeMapSetType(addr, size, type);
}

extern "C" DLL_EXPORT PROCESS_INFORMATION* _dbg_getProcessInformation()
{
    return fdProcessInfo;
}

extern "C" DLL_EXPORT int _dbg_bpgettypeat(duint addr)
{
    static duint cacheAddr;
    static int cacheBpCount;
    static int cacheResult;
    int bpcount = BpGetList(nullptr);
    if(cacheAddr != addr || cacheBpCount != bpcount)
    {
        BREAKPOINT bp;
        cacheAddr = addr;
        cacheResult = 0;
        cacheBpCount = bpcount;
        if(BpGet(addr, BPNORMAL, 0, &bp))
            if(bp.enabled)
                cacheResult |= bp_normal;
        if(BpGet(addr, BPHARDWARE, 0, &bp))
            if(bp.enabled)
                cacheResult |= bp_hardware;
        if(BpGet(addr, BPMEMORY, 0, &bp))
            if(bp.enabled)
                cacheResult |= bp_memory;
    }
    return cacheResult;
}

static void GetMxCsrFields(MXCSRFIELDS* MxCsrFields, DWORD MxCsr)
{
    MxCsrFields->DAZ = valmxcsrflagfromstring(MxCsr, "DAZ");
    MxCsrFields->DE = valmxcsrflagfromstring(MxCsr, "DE");
    MxCsrFields->FZ = valmxcsrflagfromstring(MxCsr, "FZ");
    MxCsrFields->IE = valmxcsrflagfromstring(MxCsr, "IE");
    MxCsrFields->IM = valmxcsrflagfromstring(MxCsr, "IM");
    MxCsrFields->DM = valmxcsrflagfromstring(MxCsr, "DM");
    MxCsrFields->OE = valmxcsrflagfromstring(MxCsr, "OE");
    MxCsrFields->OM = valmxcsrflagfromstring(MxCsr, "OM");
    MxCsrFields->PE = valmxcsrflagfromstring(MxCsr, "PE");
    MxCsrFields->PM = valmxcsrflagfromstring(MxCsr, "PM");
    MxCsrFields->UE = valmxcsrflagfromstring(MxCsr, "UE");
    MxCsrFields->UM = valmxcsrflagfromstring(MxCsr, "UM");
    MxCsrFields->ZE = valmxcsrflagfromstring(MxCsr, "ZE");
    MxCsrFields->ZM = valmxcsrflagfromstring(MxCsr, "ZM");

    MxCsrFields->RC = valmxcsrfieldfromstring(MxCsr, "RC");
}

static void Getx87ControlWordFields(X87CONTROLWORDFIELDS* x87ControlWordFields, WORD ControlWord)
{
    x87ControlWordFields->DM = valx87controlwordflagfromstring(ControlWord, "DM");
    x87ControlWordFields->IC = valx87controlwordflagfromstring(ControlWord, "IC");
    x87ControlWordFields->IEM = valx87controlwordflagfromstring(ControlWord, "IEM");
    x87ControlWordFields->IM = valx87controlwordflagfromstring(ControlWord, "IM");
    x87ControlWordFields->OM = valx87controlwordflagfromstring(ControlWord, "OM");
    x87ControlWordFields->PM = valx87controlwordflagfromstring(ControlWord, "PM");
    x87ControlWordFields->UM = valx87controlwordflagfromstring(ControlWord, "UM");
    x87ControlWordFields->ZM = valx87controlwordflagfromstring(ControlWord, "ZM");

    x87ControlWordFields->RC = valx87controlwordfieldfromstring(ControlWord, "RC");
    x87ControlWordFields->PC = valx87controlwordfieldfromstring(ControlWord, "PC");
}

static void Getx87StatusWordFields(X87STATUSWORDFIELDS* x87StatusWordFields, WORD StatusWord)
{
    x87StatusWordFields->B = valx87statuswordflagfromstring(StatusWord, "B");
    x87StatusWordFields->C0 = valx87statuswordflagfromstring(StatusWord, "C0");
    x87StatusWordFields->C1 = valx87statuswordflagfromstring(StatusWord, "C1");
    x87StatusWordFields->C2 = valx87statuswordflagfromstring(StatusWord, "C2");
    x87StatusWordFields->C3 = valx87statuswordflagfromstring(StatusWord, "C3");
    x87StatusWordFields->D = valx87statuswordflagfromstring(StatusWord, "D");
    x87StatusWordFields->I = valx87statuswordflagfromstring(StatusWord, "I");
    x87StatusWordFields->IR = valx87statuswordflagfromstring(StatusWord, "IR");
    x87StatusWordFields->O = valx87statuswordflagfromstring(StatusWord, "O");
    x87StatusWordFields->P = valx87statuswordflagfromstring(StatusWord, "P");
    x87StatusWordFields->SF = valx87statuswordflagfromstring(StatusWord, "SF");
    x87StatusWordFields->U = valx87statuswordflagfromstring(StatusWord, "U");
    x87StatusWordFields->Z = valx87statuswordflagfromstring(StatusWord, "Z");

    x87StatusWordFields->TOP = valx87statuswordfieldfromstring(StatusWord, "TOP");
}

static void TranslateTitanFpu(const x87FPU_t* titanfpu, X87FPU* fpu)
{
    fpu->ControlWord = titanfpu->ControlWord;
    fpu->StatusWord = titanfpu->StatusWord;
    fpu->TagWord = titanfpu->TagWord;
    fpu->ErrorOffset = titanfpu->ErrorOffset;
    fpu->ErrorSelector = titanfpu->ErrorSelector;
    fpu->DataOffset = titanfpu->DataOffset;
    fpu->DataSelector = titanfpu->DataSelector;
    fpu->Cr0NpxState = titanfpu->Cr0NpxState;
}

static void TranslateTitanContextToRegContext(const TITAN_ENGINE_CONTEXT_t* titcontext, REGISTERCONTEXT* regcontext)
{
    regcontext->cax = titcontext->cax;
    regcontext->ccx = titcontext->ccx;
    regcontext->cdx = titcontext->cdx;
    regcontext->cbx = titcontext->cbx;
    regcontext->csp = titcontext->csp;
    regcontext->cbp = titcontext->cbp;
    regcontext->csi = titcontext->csi;
    regcontext->cdi = titcontext->cdi;
#ifdef _WIN64
    regcontext->r8 = titcontext->r8;
    regcontext->r9 = titcontext->r9;
    regcontext->r10 = titcontext->r10;
    regcontext->r11 = titcontext->r11;
    regcontext->r12 = titcontext->r12;
    regcontext->r13 = titcontext->r13;
    regcontext->r14 = titcontext->r14;
    regcontext->r15 = titcontext->r15;
#endif //_WIN64
    regcontext->cip = titcontext->cip;
    regcontext->eflags = titcontext->eflags;
    regcontext->gs = titcontext->gs;
    regcontext->fs = titcontext->fs;
    regcontext->es = titcontext->es;
    regcontext->ds = titcontext->ds;
    regcontext->cs = titcontext->cs;
    regcontext->ss = titcontext->ss;
    regcontext->dr0 = titcontext->dr0;
    regcontext->dr1 = titcontext->dr1;
    regcontext->dr2 = titcontext->dr2;
    regcontext->dr3 = titcontext->dr3;
    regcontext->dr6 = titcontext->dr6;
    regcontext->dr7 = titcontext->dr7;
    memcpy(regcontext->RegisterArea, titcontext->RegisterArea, sizeof(regcontext->RegisterArea));
    TranslateTitanFpu(&titcontext->x87fpu, &regcontext->x87fpu);
    regcontext->MxCsr = titcontext->MxCsr;
    memcpy(regcontext->XmmRegisters, titcontext->XmmRegisters, sizeof(regcontext->XmmRegisters));
    memcpy(regcontext->YmmRegisters, titcontext->YmmRegisters, sizeof(regcontext->YmmRegisters));
}

static void TranslateTitanFpuRegister(const x87FPURegister_t* titanReg, X87FPUREGISTER* reg)
{
    memcpy(reg->data, titanReg->data, sizeof(reg->data));
    reg->st_value = titanReg->st_value;
    reg->tag = titanReg->tag;
}

static void TranslateTitanFpuRegisters(const x87FPURegister_t titanFpu[8], X87FPUREGISTER fpu[8])
{
    for(int i = 0; i < 8; i++)
        TranslateTitanFpuRegister(&titanFpu[i], &fpu[i]);
}

extern "C" DLL_EXPORT bool _dbg_getregdump(REGDUMP* regdump)
{
    if(!DbgIsDebugging())
    {
        memset(regdump, 0, sizeof(REGDUMP));
        return true;
    }

    TITAN_ENGINE_CONTEXT_t titcontext;
    if(!GetFullContextDataEx(hActiveThread, &titcontext))
        return false;
    TranslateTitanContextToRegContext(&titcontext, &regdump->regcontext);

    duint cflags = regdump->regcontext.eflags;
    regdump->flags.c = valflagfromstring(cflags, "cf");
    regdump->flags.p = valflagfromstring(cflags, "pf");
    regdump->flags.a = valflagfromstring(cflags, "af");
    regdump->flags.z = valflagfromstring(cflags, "zf");
    regdump->flags.s = valflagfromstring(cflags, "sf");
    regdump->flags.t = valflagfromstring(cflags, "tf");
    regdump->flags.i = valflagfromstring(cflags, "if");
    regdump->flags.d = valflagfromstring(cflags, "df");
    regdump->flags.o = valflagfromstring(cflags, "of");

    x87FPURegister_t x87FPURegisters[8];
    Getx87FPURegisters(x87FPURegisters,  &titcontext);
    TranslateTitanFpuRegisters(x87FPURegisters, regdump->x87FPURegisters);

    GetMMXRegisters(regdump->mmx,  &titcontext);
    GetMxCsrFields(& (regdump->MxCsrFields), regdump->regcontext.MxCsr);
    Getx87ControlWordFields(& (regdump->x87ControlWordFields), regdump->regcontext.x87fpu.ControlWord);
    Getx87StatusWordFields(& (regdump->x87StatusWordFields), regdump->regcontext.x87fpu.StatusWord);
    LASTERROR lastError;
    lastError.code = ThreadGetLastError(ThreadGetId(hActiveThread));
    strncpy_s(lastError.name, ErrorCodeToName(lastError.code).c_str(), _TRUNCATE);
    regdump->lastError = lastError;

    return true;
}

extern "C" DLL_EXPORT bool _dbg_valtostring(const char* string, duint value)
{
    return valtostring(string, value, true);
}

extern "C" DLL_EXPORT int _dbg_getbplist(BPXTYPE type, BPMAP* bpmap)
{
    if(!bpmap)
        return 0;
    std::vector<BREAKPOINT> list;
    int bpcount = BpGetList(&list);
    if(bpcount == 0)
    {
        bpmap->count = 0;
        return 0;
    }

    int retcount = 0;
    std::vector<BRIDGEBP> bridgeList;
    BRIDGEBP curBp;
    BP_TYPE currentBpType;
    switch(type)
    {
    case bp_normal:
        currentBpType = BPNORMAL;
        break;
    case bp_hardware:
        currentBpType = BPHARDWARE;
        break;
    case bp_memory:
        currentBpType = BPMEMORY;
        break;
    default:
        return 0;
    }
    unsigned short slot = 0;
    for(int i = 0; i < bpcount; i++)
    {
        if(list[i].type != currentBpType)
            continue;
        BpToBridge(&list[i], &curBp);
        bridgeList.push_back(curBp);
        retcount++;
    }
    if(!retcount)
    {
        bpmap->count = retcount;
        return retcount;
    }
    bpmap->count = retcount;
    bpmap->bp = (BRIDGEBP*)BridgeAlloc(sizeof(BRIDGEBP) * retcount);
    for(int i = 0; i < retcount; i++)
        memcpy(&bpmap->bp[i], &bridgeList.at(i), sizeof(BRIDGEBP));
    return retcount;
}

extern "C" DLL_EXPORT duint _dbg_getbranchdestination(duint addr)
{
    unsigned char data[MAX_DISASM_BUFFER];
    if(!MemIsValidReadPtr(addr, true) || !MemRead(addr, data, sizeof(data)))
        return 0;
    Capstone cp;
    if(!cp.Disassemble(addr, data))
        return 0;
    if(cp.InGroup(CS_GRP_JUMP) || cp.InGroup(CS_GRP_CALL) || cp.IsLoop())
    {
        auto opValue = cp.ResolveOpValue(0, [](x86_reg reg) -> size_t
        {
            switch(reg)
            {
#ifndef _WIN64 //x32
            case X86_REG_EAX:
                return GetContextDataEx(hActiveThread, UE_EAX);
            case X86_REG_EBX:
                return GetContextDataEx(hActiveThread, UE_EBX);
            case X86_REG_ECX:
                return GetContextDataEx(hActiveThread, UE_ECX);
            case X86_REG_EDX:
                return GetContextDataEx(hActiveThread, UE_EDX);
            case X86_REG_EBP:
                return GetContextDataEx(hActiveThread, UE_EBP);
            case X86_REG_ESP:
                return GetContextDataEx(hActiveThread, UE_ESP);
            case X86_REG_ESI:
                return GetContextDataEx(hActiveThread, UE_ESI);
            case X86_REG_EDI:
                return GetContextDataEx(hActiveThread, UE_EDI);
            case X86_REG_EIP:
                return GetContextDataEx(hActiveThread, UE_EIP);
#else //x64
            case X86_REG_RAX:
                return GetContextDataEx(hActiveThread, UE_RAX);
            case X86_REG_RBX:
                return GetContextDataEx(hActiveThread, UE_RBX);
            case X86_REG_RCX:
                return GetContextDataEx(hActiveThread, UE_RCX);
            case X86_REG_RDX:
                return GetContextDataEx(hActiveThread, UE_RDX);
            case X86_REG_RBP:
                return GetContextDataEx(hActiveThread, UE_RBP);
            case X86_REG_RSP:
                return GetContextDataEx(hActiveThread, UE_RSP);
            case X86_REG_RSI:
                return GetContextDataEx(hActiveThread, UE_RSI);
            case X86_REG_RDI:
                return GetContextDataEx(hActiveThread, UE_RDI);
            case X86_REG_RIP:
                return GetContextDataEx(hActiveThread, UE_RIP);
            case X86_REG_R8:
                return GetContextDataEx(hActiveThread, UE_R8);
            case X86_REG_R9:
                return GetContextDataEx(hActiveThread, UE_R9);
            case X86_REG_R10:
                return GetContextDataEx(hActiveThread, UE_R10);
            case X86_REG_R11:
                return GetContextDataEx(hActiveThread, UE_R11);
            case X86_REG_R12:
                return GetContextDataEx(hActiveThread, UE_R12);
            case X86_REG_R13:
                return GetContextDataEx(hActiveThread, UE_R13);
            case X86_REG_R14:
                return GetContextDataEx(hActiveThread, UE_R14);
            case X86_REG_R15:
                return GetContextDataEx(hActiveThread, UE_R15);
#endif //_WIN64
            default:
                return 0;
            }
        });
        if(cp[0].type == X86_OP_MEM)
        {
            if(MemRead(opValue, &opValue, sizeof(opValue)))
                return opValue;
        }
        else
            return opValue;
    }
    if(cp.InGroup(CS_GRP_RET))
    {
        auto csp = GetContextDataEx(hActiveThread, UE_CSP);
        duint dest = 0;
        if(MemRead(csp, &dest, sizeof(dest)))
            return dest;
    }
    return 0;
}

extern "C" DLL_EXPORT bool _dbg_functionoverlaps(duint start, duint end)
{
    return FunctionOverlaps(start, end);
}

extern "C" DLL_EXPORT duint _dbg_sendmessage(DBGMSG type, void* param1, void* param2)
{
    if(dbgisstopped())
    {
        switch(type)  //ignore win events
        {
        //these functions are safe to call when we did not initialize yet
        case DBG_DEINITIALIZE_LOCKS:
        case DBG_INITIALIZE_LOCKS:
        case DBG_GET_FUNCTIONS:
        case DBG_SETTINGS_UPDATED:
        case DBG_GET_THREAD_LIST:
        case DBG_WIN_EVENT:
        case DBG_WIN_EVENT_GLOBAL:
        case DBG_RELEASE_ENCODE_TYPE_BUFFER:
        case DBG_GET_TIME_WASTED_COUNTER:
            break;
        //the rest is unsafe -> throw an exception when people try to call them
        default:
            __debugbreak(); //we cannot process messages when the debugger is stopped, this must be a bug
        }
    }
    switch(type)
    {
    case DBG_SCRIPT_LOAD:
    {
        scriptload((const char*)param1);
    }
    break;

    case DBG_SCRIPT_UNLOAD:
    {
        scriptunload();
    }
    break;

    case DBG_SCRIPT_RUN:
    {
        scriptrun((int)(duint)param1);
    }
    break;

    case DBG_SCRIPT_STEP:
    {
        scriptstep();
    }
    break;

    case DBG_SCRIPT_BPTOGGLE:
    {
        return scriptbptoggle((int)(duint)param1);
    }
    break;

    case DBG_SCRIPT_BPGET:
    {
        return scriptbpget((int)(duint)param1);
    }
    break;

    case DBG_SCRIPT_CMDEXEC:
    {
        return scriptcmdexec((const char*)param1);
    }
    break;

    case DBG_SCRIPT_ABORT:
    {
        scriptabort();
    }
    break;

    case DBG_SCRIPT_GETLINETYPE:
    {
        return (duint)scriptgetlinetype((int)(duint)param1);
    }
    break;

    case DBG_SCRIPT_SETIP:
    {
        scriptsetip((int)(duint)param1);
    }
    break;

    case DBG_SCRIPT_GETBRANCHINFO:
    {
        return (duint)scriptgetbranchinfo((int)(duint)param1, (SCRIPTBRANCH*)param2);
    }
    break;

    case DBG_SYMBOL_ENUM:
    {
        SYMBOLCBINFO* cbInfo = (SYMBOLCBINFO*)param1;
        SymEnum(cbInfo->base, cbInfo->cbSymbolEnum, cbInfo->user);
    }
    break;

    case DBG_SYMBOL_ENUM_FROMCACHE:
    {
        SYMBOLCBINFO* cbInfo = (SYMBOLCBINFO*)param1;
        SymEnumFromCache(cbInfo->base, cbInfo->cbSymbolEnum, cbInfo->user);
    }
    break;

    case DBG_ASSEMBLE_AT:
    {
        return assembleat((duint)param1, (const char*)param2, 0, 0, false);
    }
    break;

    case DBG_MODBASE_FROM_NAME:
    {
        return ModBaseFromName((const char*)param1);
    }
    break;

    case DBG_DISASM_AT:
    {
        disasmget((duint)param1, (DISASM_INSTR*)param2);
    }
    break;

    case DBG_STACK_COMMENT_GET:
    {
        return stackcommentget((duint)param1, (STACK_COMMENT*)param2);
    }
    break;

    case DBG_GET_THREAD_LIST:
    {
        ThreadGetList((THREADLIST*)param1);
    }
    break;

    case DBG_SETTINGS_UPDATED:
    {
        valuesetsignedcalc(!settingboolget("Engine", "CalculationType")); //0:signed, 1:unsigned
        SetEngineVariable(UE_ENGINE_SET_DEBUG_PRIVILEGE, settingboolget("Engine", "EnableDebugPrivilege"));
        bOnlyCipAutoComments = settingboolget("Disassembler", "OnlyCipAutoComments");
        bListAllPages = settingboolget("Engine", "ListAllPages");
        bUndecorateSymbolNames = settingboolget("Engine", "UndecorateSymbolNames");
        bEnableSourceDebugging = settingboolget("Engine", "EnableSourceDebugging");
        bTraceRecordEnabledDuringTrace = settingboolget("Engine", "TraceRecordEnabledDuringTrace");

        duint setting;
        if(BridgeSettingGetUint("Engine", "BreakpointType", &setting))
        {
            switch(setting)
            {
            case 0: //break_int3short
                SetBPXOptions(UE_BREAKPOINT_INT3);
                break;
            case 1: //break_int3long
                SetBPXOptions(UE_BREAKPOINT_LONG_INT3);
                break;
            case 2: //break_ud2
                SetBPXOptions(UE_BREAKPOINT_UD2);
                break;
            }
        }
        if(BridgeSettingGetUint("Engine", "Assembler", &setting))
            assemblerEngine = AssemblerEngine(setting);
        else
            assemblerEngine = AssemblerEngine::XEDParse;

        char exceptionRange[MAX_SETTING_SIZE] = "";
        dbgclearignoredexceptions();
        if(BridgeSettingGet("Exceptions", "IgnoreRange", exceptionRange))
        {
            char* entry = strtok(exceptionRange, ",");
            while(entry)
            {
                unsigned long start;
                unsigned long end;
                if(sscanf(entry, "%08X-%08X", &start, &end) == 2 && start <= end)
                {
                    ExceptionRange range;
                    range.start = start;
                    range.end = end;
                    dbgaddignoredexception(range);
                }
                entry = strtok(0, ",");
            }
        }

        char cachePath[MAX_SETTING_SIZE];
        if(BridgeSettingGet("Symbols", "CachePath", cachePath))
        {
            // Trim the buffer to fit inside MAX_PATH
            strncpy_s(szSymbolCachePath, cachePath, _TRUNCATE);
        }
    }
    break;

    case DBG_DISASM_FAST_AT:
    {
        if(!param1 || !param2)
            return 0;
        BASIC_INSTRUCTION_INFO* basicinfo = (BASIC_INSTRUCTION_INFO*)param2;
        if(!disasmfast((duint)param1, basicinfo))
            basicinfo->size = 1;
        return 0;
    }
    break;

    case DBG_MENU_ENTRY_CLICKED:
    {
        int hEntry = (int)(duint)param1;
        pluginmenucall(hEntry);
    }
    break;

    case DBG_FUNCTION_GET:
    {
        FUNCTION_LOOP_INFO* info = (FUNCTION_LOOP_INFO*)param1;
        return (duint)FunctionGet(info->addr, &info->start, &info->end);
    }
    break;

    case DBG_FUNCTION_OVERLAPS:
    {
        FUNCTION_LOOP_INFO* info = (FUNCTION_LOOP_INFO*)param1;
        return (duint)FunctionOverlaps(info->start, info->end);
    }
    break;

    case DBG_FUNCTION_ADD:
    {
        FUNCTION_LOOP_INFO* info = (FUNCTION_LOOP_INFO*)param1;
        return (duint)FunctionAdd(info->start, info->end, info->manual);
    }
    break;

    case DBG_FUNCTION_DEL:
    {
        FUNCTION_LOOP_INFO* info = (FUNCTION_LOOP_INFO*)param1;
        return (duint)FunctionDelete(info->addr);
    }
    break;

    case DBG_ARGUMENT_GET:
    {
        FUNCTION_LOOP_INFO* info = (FUNCTION_LOOP_INFO*)param1;
        return (duint)ArgumentGet(info->addr, &info->start, &info->end);
    }
    break;

    case DBG_ARGUMENT_OVERLAPS:
    {
        FUNCTION_LOOP_INFO* info = (FUNCTION_LOOP_INFO*)param1;
        return (duint)ArgumentOverlaps(info->start, info->end);
    }
    break;

    case DBG_ARGUMENT_ADD:
    {
        FUNCTION_LOOP_INFO* info = (FUNCTION_LOOP_INFO*)param1;
        return (duint)ArgumentAdd(info->start, info->end, info->manual);
    }
    break;

    case DBG_ARGUMENT_DEL:
    {
        FUNCTION_LOOP_INFO* info = (FUNCTION_LOOP_INFO*)param1;
        return (duint)ArgumentDelete(info->addr);
    }
    break;

    case DBG_LOOP_GET:
    {
        FUNCTION_LOOP_INFO* info = (FUNCTION_LOOP_INFO*)param1;
        return (duint)LoopGet(info->depth, info->addr, &info->start, &info->end);
    }
    break;

    case DBG_LOOP_OVERLAPS:
    {
        FUNCTION_LOOP_INFO* info = (FUNCTION_LOOP_INFO*)param1;
        return (duint)LoopOverlaps(info->depth, info->start, info->end, 0);
    }
    break;

    case DBG_LOOP_ADD:
    {
        FUNCTION_LOOP_INFO* info = (FUNCTION_LOOP_INFO*)param1;
        return (duint)LoopAdd(info->start, info->end, info->manual);
    }
    break;

    case DBG_LOOP_DEL:
    {
        FUNCTION_LOOP_INFO* info = (FUNCTION_LOOP_INFO*)param1;
        return (duint)LoopDelete(info->depth, info->addr);
    }
    break;

    case DBG_IS_RUN_LOCKED:
    {
        return (duint)waitislocked(WAITID_RUN);
    }
    break;

    case DBG_IS_BP_DISABLED:
    {
        BREAKPOINT bp;
        if(BpGet((duint)param1, BPNORMAL, 0, &bp))
            return !(duint)bp.enabled;
        return (duint)false;
    }
    break;

    case DBG_SET_AUTO_COMMENT_AT:
    {
        return (duint)CommentSet((duint)param1, (const char*)param2, false);
    }
    break;

    case DBG_DELETE_AUTO_COMMENT_RANGE:
    {
        CommentDelRange((duint)param1, (duint)param2, false);
    }
    break;

    case DBG_SET_AUTO_LABEL_AT:
    {
        return (duint)LabelSet((duint)param1, (const char*)param2, false);
    }
    break;

    case DBG_DELETE_AUTO_LABEL_RANGE:
    {
        LabelDelRange((duint)param1, (duint)param2, false);
    }
    break;

    case DBG_SET_AUTO_BOOKMARK_AT:
    {
        return (duint)BookmarkSet((duint)param1, false);
    }
    break;

    case DBG_DELETE_AUTO_BOOKMARK_RANGE:
    {
        BookmarkDelRange((duint)param1, (duint)param2, false);
    }
    break;

    case DBG_SET_AUTO_FUNCTION_AT:
    {
        return (duint)FunctionAdd((duint)param1, (duint)param2, false);
    }
    break;

    case DBG_DELETE_AUTO_FUNCTION_RANGE:
    {
        FunctionDelRange((duint)param1, (duint)param2, false);
    }
    break;

    case DBG_GET_XREF_COUNT_AT:
    {
        return XrefGetCount((duint)param1);
    }
    break;

    case DBG_XREF_GET:
    {
        if(!param2)
            return false;
        XREF_INFO* info = (XREF_INFO*)param2;
        duint address = (duint)param1;
        info->refcount = XrefGetCount(address);

        if(info->refcount == 0)
        {
            return false;
        }
        else
        {
            info->references = (XREF_RECORD*)BridgeAlloc(sizeof(XREF_RECORD) * info->refcount);
            return XrefGet(address, info);
        }
    }
    break;

    case DBG_XREF_ADD:
    {
        return XrefAdd((duint)param1, (duint)param2);
    }
    break;

    case DBG_XREF_DEL_ALL:
    {
        return XrefDeleteAll((duint)param1);
    }
    break;

    case DBG_GET_XREF_TYPE_AT:
    {
        return XrefGetType((duint)param1);
    }
    break;

    case DBG_GET_ENCODE_TYPE_BUFFER:
    {
        return (duint)EncodeMapGetBuffer((duint)param1);
    }
    break;

    case DBG_ENCODE_TYPE_GET:
    {
        return EncodeMapGetType((duint)param1, (duint)param2);
    }
    break;

    case DBG_ENCODE_SIZE_GET:
    {
        return EncodeMapGetSize((duint)param1, (duint)param2);
    }
    break;

    case DBG_DELETE_ENCODE_TYPE_RANGE:
    {
        EncodeMapDelRange((duint)param1, (duint)param2);
    }
    break;

    case DBG_DELETE_ENCODE_TYPE_SEG:
    {
        EncodeMapDelSegment((duint)param1);
    }
    break;

    case DBG_RELEASE_ENCODE_TYPE_BUFFER:
    {
        EncodeMapReleaseBuffer(param1);
    }
    break;

    case DBG_GET_STRING_AT:
    {
        auto addr = duint(param1);
        if(!MemIsValidReadPtrUnsafe(addr, true))
            return false;

        auto dest = (char*)param2;
        *dest = '\0';
        char string[MAX_STRING_SIZE];
        duint addrPtr;
        STRING_TYPE strtype;
        if(MemReadUnsafe(addr, &addrPtr, sizeof(addr)) && MemIsValidReadPtrUnsafe(addrPtr, true))
        {
            if(disasmgetstringat(addrPtr, &strtype, string, string, MAX_STRING_SIZE - 5))
            {
                if(strtype == str_ascii)
                    sprintf_s(dest, MAX_STRING_SIZE, "&\"%s\"", string);
                else //unicode
                    sprintf_s(dest, MAX_STRING_SIZE, "&L\"%s\"", string);
                return true;
            }
        }
        if(disasmgetstringat(addr, &strtype, string, string, MAX_STRING_SIZE - 4))
        {
            if(strtype == str_ascii)
                sprintf_s(dest, MAX_STRING_SIZE, "\"%s\"", string);
            else //unicode
                sprintf_s(dest, MAX_STRING_SIZE, "L\"%s\"", string);
            return true;
        }
        return false;
    }
    break;

    case DBG_GET_FUNCTIONS:
    {
        return (duint)dbgfunctionsget();
    }
    break;

    case DBG_WIN_EVENT:
    {
        return (duint)pluginwinevent((MSG*)param1, (long*)param2);
    }
    break;

    case DBG_WIN_EVENT_GLOBAL:
    {
        return (duint)pluginwineventglobal((MSG*)param1);
    }
    break;

    case DBG_INITIALIZE_LOCKS:
    {
        SectionLockerGlobal::Initialize();
    }
    break;

    case DBG_DEINITIALIZE_LOCKS:
    {
        SectionLockerGlobal::Deinitialize();
    }
    break;

    case DBG_GET_TIME_WASTED_COUNTER:
        return dbggettimewastedcounter();

    case DBG_DELETE_COMMENT_RANGE:
    {
        CommentDelRange((duint)param1, (duint)param2, true);
    }
    break;

    case DBG_DELETE_LABEL_RANGE:
    {
        LabelDelRange((duint)param1, (duint)param2, true);
    }
    break;

    case DBG_DELETE_BOOKMARK_RANGE:
    {
        BookmarkDelRange((duint)param1, (duint)param2, true);
    }
    break;

    case DBG_GET_WATCH_LIST:
    {
        BridgeList<WATCHINFO>::CopyData((ListInfo*)param1, WatchGetList());
    }
    break;
    }
    return 0;
}
