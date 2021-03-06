#include <Library/DebugLib.h>
#include <Library/LoadLinuxLib.h>
#include <Library/BaseMemoryLib.h>

#include <IndustryStandard/LinuxBzimage.h>
#include <IndustryStandard/PeImage.h>

#include <util/file_utils.h>
#include <Guid/FileInfo.h>
#include <Protocol/SimpleFileSystem.h>
#include <Library/BaseLib.h>
#include "linux_loader.h"

#define POKE(type, addr, offset) (*(type*)((UINTN)(addr) + (UINTN)(offset)))
#define POKE16(addr, offset) POKE(UINT16, addr, offset)
#define POKE32(addr, offset) POKE(UINT32, addr, offset)

// TODO: This whole thing can be optimized by not loading and copying but by loading into
//       the correct buffer

/**
 * Implementation References
 * - https://github.com/qemu/qemu/blob/master/hw/i386/x86.c#L333
 * - https://github.com/tianocore/edk2/blob/master/OvmfPkg/Library/PlatformBootManagerLib/QemuKernel.c
 *
 */
void load_linux_binary(boot_entry_t* entry) {
// clear everything, this is going to be a simple log of how we loaded the stuff
    ASSERT_EFI_ERROR(gST->ConOut->SetAttribute(gST->ConOut, EFI_TEXT_ATTR(EFI_LIGHTGRAY, EFI_BLACK)));
    ASSERT_EFI_ERROR(gST->ConOut->SetCursorPosition(gST->ConOut, 0, 0));
    ASSERT_EFI_ERROR(gST->ConOut->ClearScreen(gST->ConOut));

    DebugPrint(0, "Loading kernel image\n");
    UINTN KernelSize;
    UINT8* KernelImage;
    load_file(entry->path, EfiBootServicesData, (UINT64*)&KernelImage, &KernelSize);

    ASSERT(POKE32(KernelImage, 0x202) == 0x53726448);

    // get the setup size
    UINTN SetupSize = KernelImage[0x1f1];
    if(SetupSize == 0) {
        SetupSize = 4;
    }
    SetupSize  = (SetupSize + 1) * 512;
    ASSERT(SetupSize < KernelSize);
    KernelSize -= SetupSize;

    DebugPrint(0, "Setup size: 0x%x\n", SetupSize);

    // load the setup
    UINT8* SetupBuf = LoadLinuxAllocateKernelSetupPages(EFI_SIZE_TO_PAGES(SetupSize));
    ASSERT(SetupBuf != NULL);
    CopyMem(SetupBuf, KernelImage, SetupSize);
    ASSERT_EFI_ERROR(LoadLinuxCheckKernelSetup(SetupBuf, SetupSize));
    ASSERT_EFI_ERROR(LoadLinuxInitializeKernelSetup(SetupBuf));

    // we don't have a type :(
    SetupBuf[0x210] = 0xF;
    SetupBuf[0x211] = 0xF;

    // load the kernel
    UINT64 KernelInitialSize  = LoadLinuxGetKernelSize(SetupBuf, KernelSize);
    ASSERT(KernelInitialSize  != 0);
    DebugPrint(0, "Kernel size: 0x%x\n", KernelSize);
    UINT8* KernelBuf = LoadLinuxAllocateKernelPages(SetupBuf, EFI_SIZE_TO_PAGES(KernelInitialSize));
    ASSERT(KernelBuf != NULL);
    CopyMem(KernelBuf, KernelImage + SetupSize, KernelSize);

    // we can free the kernel image now
    ASSERT_EFI_ERROR(gBS->FreePages((EFI_PHYSICAL_ADDRESS)KernelImage, EFI_SIZE_TO_PAGES(KernelSize)));

    // load the command line arguments, if any
    CHAR8* CommandLineBuf = NULL;
    if(entry->cmd) {
        DebugPrint(0, "Command line: `%a`\n", entry->cmd);
        UINTN CommandLineSize = AsciiStrLen(entry->cmd) + 1;
        CommandLineBuf = LoadLinuxAllocateCommandLinePages(EFI_SIZE_TO_PAGES(CommandLineSize));
        ASSERT(CommandLineBuf != NULL);
        CopyMem(CommandLineBuf, entry->cmd, CommandLineSize);
    }
    ASSERT_EFI_ERROR(LoadLinuxSetCommandLine(SetupBuf, CommandLineBuf));

    // TODO: don't assume the first module is the initrd
    // load the initrd, if any
    UINT64 InitrdSize = 0;
    UINT8* InitrdBuf = NULL;
    if(entry->modules_count > 0) {
        boot_module_t* initrd = &entry->modules[0];

        UINT8* InitrdBase;
        load_file(initrd->path, EfiBootServicesData, (UINT64*)&InitrdBase, &InitrdSize);
        DebugPrint(0, "Initrd size: 0x%x\n", InitrdSize);

        InitrdBuf = LoadLinuxAllocateInitrdPages(SetupBuf, EFI_SIZE_TO_PAGES(InitrdSize));
        ASSERT(InitrdBuf != NULL);
        DebugPrint(0, "Initrd Buf: 0x%p\n", InitrdBuf);
        CopyMem(InitrdBuf, InitrdBase, InitrdSize);

        // can free the initrd now
        ASSERT_EFI_ERROR(gBS->FreePages((EFI_PHYSICAL_ADDRESS)InitrdBase, EFI_SIZE_TO_PAGES(InitrdSize)));
    }
    ASSERT_EFI_ERROR(LoadLinuxSetInitrd(SetupBuf, InitrdBuf, InitrdSize));

    // call the kernel
    ASSERT_EFI_ERROR(LoadLinux(KernelBuf, SetupBuf));
}
