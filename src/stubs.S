/*
    Plugin Manager for ARK-5
    stubs.S: system functions the PSP SDK has no stubs for.

    They get an import entry of their own, laid out like pspimport.s does it.
    The macros there can't be used: they name their symbols after the library,
    and the SDK's own sceUtility entry already has those names.
    tools/check_imports.py checks the tables after linking.
*/

    .set noreorder

    .section .rodata.sceResident, "a"
    .word   0
pm_stub_utility_name:
    .asciz  "sceUtility"
    .align  2

    .section .lib.stub, "a", @progbits
pm_stub_utility:
    .word   pm_stub_utility_name
    .word   0x40090000          /* same flags as the SDK's sceUtility entry */
    .word   0x5
    .word   __executable_start  /* NID and stub tables: set by psp-fixup-imports */
    .word   __executable_start

/* int sceUtilityGetNetParamLatestID(int *id): the network connection the PSP
   used last (1 to n) */
    .section .sceStub.text, "ax", @progbits
    .globl  sceUtilityGetNetParamLatestID
    .type   sceUtilityGetNetParamLatestID, @function
    .ent    sceUtilityGetNetParamLatestID, 0
sceUtilityGetNetParamLatestID:
    .word   pm_stub_utility
    .word   0x4FED24D8
    .end    sceUtilityGetNetParamLatestID
    .size   sceUtilityGetNetParamLatestID, .-sceUtilityGetNetParamLatestID

    .section .rodata.sceNid, "a"
    .word   0x4FED24D8
