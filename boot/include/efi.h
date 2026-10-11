/* SPDX-License-Identifier: GPL-2.0-only
 * NX System On Linux -- minimal UEFI definitions
 * Copyright (C) 2026 rainbowcrr-sys -- GPL-2.0-only
 *
 * Only what NX Boot Manager actually uses. Written out in full rather than
 * pulled from a SDK so the build needs nothing but a C compiler.
 *
 * Two things here are easy to get wrong and fail silently:
 *
 *   - Every UEFI function is called with the Microsoft x64 ABI, so the
 *     EFIAPI macro is __attribute__((ms_abi)). Without it the arguments
 *     land in the wrong registers and the call returns nonsense, or faults.
 *
 *   - EFI_BOOT_SERVICES is a table of function pointers whose order is fixed
 *     by the specification. The unused slots are declared as void *reservedN
 *     rather than omitted, so that the ones we do call sit at the offsets the
 *     firmware expects. Getting this wrong means calling into whatever
 *     happens to be next in the table.
 */

#ifndef NXSOL_EFI_H
#define NXSOL_EFI_H

#include <stdint.h>
#include <stddef.h>

#ifdef __x86_64__
#define EFIAPI __attribute__((ms_abi))
#else
#define EFIAPI
#endif

typedef uint64_t  UINTN;
typedef uint8_t   BOOLEAN;
#define FALSE 0
#define TRUE  1
typedef uint16_t  CHAR16;
typedef void     *EFI_HANDLE;
typedef void     *EFI_EVENT;
typedef UINTN     EFI_STATUS;
typedef uint64_t  EFI_PHYSICAL_ADDRESS;
typedef uint64_t  EFI_VIRTUAL_ADDRESS;
typedef uint64_t  EFI_TPL;

/* InterfaceType argument of InstallProtocolInterface. */
#define EFI_NATIVE_INTERFACE 0

typedef struct {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t  Data4[8];
} EFI_GUID;

typedef struct {
    uint64_t Signature;
    uint32_t Revision;
    uint32_t HeaderSize;
    uint32_t CRC32;
    uint32_t Reserved;
} EFI_TABLE_HEADER;

/* Success / error ------------------------------------------------------- */
#define EFI_SUCCESS 0
#define EFI_ERROR_MASK (UINTN)0x8000000000000000ULL
#define EFI_ERR(x) (EFI_ERROR_MASK | (x))
#define EFI_LOAD_ERROR      EFI_ERR(1)
#define EFI_INVALID_PARAMETER EFI_ERR(2)
#define EFI_UNSUPPORTED     EFI_ERR(3)
#define EFI_BUFFER_TOO_SMALL EFI_ERR(5)
#define EFI_NOT_FOUND       EFI_ERR(14)
#define EFI_OUT_OF_RESOURCES EFI_ERR(9)

static inline int EFI_ERROR(EFI_STATUS s) { return (int64_t)s < 0; }

/* Text output ----------------------------------------------------------- */
struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_TEXT_STRING)(
    struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, const CHAR16 *String);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_CLEAR_SCREEN)(
    struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_SET_ATTRIBUTE)(
    struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN Attribute);

typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    void                     *Reset;
    EFI_TEXT_STRING           OutputString;
    void                     *TestString;
    void                     *QueryMode;
    void                     *SetMode;
    EFI_TEXT_SET_ATTRIBUTE    SetAttribute;
    EFI_TEXT_CLEAR_SCREEN     ClearScreen;
    void                     *SetCursorPosition;
    void                     *EnableCursor;
    struct { int MaxMode, Mode, Attribute, CursorColumn, CursorRow;
             BOOLEAN CursorVisible; } *Mode;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

#define EFI_BLACK     0x00
#define EFI_BLUE      0x01
#define EFI_GREEN     0x02
#define EFI_CYAN      0x03
#define EFI_RED       0x04
#define EFI_MAGENTA   0x05
#define EFI_BROWN     0x06
#define EFI_LIGHTGRAY 0x07
#define EFI_DARKGRAY  0x08
#define EFI_LIGHTBLUE 0x09
#define EFI_LIGHTGREEN 0x0A
#define EFI_LIGHTCYAN 0x0B
#define EFI_LIGHTRED  0x0C
#define EFI_LIGHTMAGENTA 0x0D
#define EFI_YELLOW    0x0E
#define EFI_WHITE     0x0F

#define EFI_TEXT_ATTR(fg, bg) ((fg) | ((bg) << 4))
#define EFI_BACKGROUND_BLACK 0x00

/* Boot services --------------------------------------------------------- */
typedef enum {
    EfiReservedMemoryType, EfiLoaderCode, EfiLoaderData, EfiBootServicesCode,
    EfiBootServicesData, EfiRuntimeServicesCode, EfiRuntimeServicesData,
    EfiConventionalMemory, EfiUnusableMemory, EfiACPIReclaimMemory,
    EfiACPIMemoryNVS, EfiMemoryMappedIO, EfiMemoryMappedIOPortSpace,
    EfiPalCode, EfiPersistentMemory, EfiMaxMemoryType
} EFI_MEMORY_TYPE;

typedef struct {
    uint32_t Type;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    EFI_VIRTUAL_ADDRESS  VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
} EFI_MEMORY_DESCRIPTOR;

typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_PAGES)(EFI_MEMORY_TYPE, EFI_MEMORY_TYPE,
                                                UINTN, EFI_PHYSICAL_ADDRESS *);
typedef EFI_STATUS (EFIAPI *EFI_GET_MEMORY_MAP)(UINTN *, EFI_MEMORY_DESCRIPTOR *,
                                                UINTN *, UINTN *, uint32_t *);
typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_POOL)(EFI_MEMORY_TYPE, UINTN, void **);
typedef EFI_STATUS (EFIAPI *EFI_FREE_POOL)(void *);
typedef EFI_STATUS (EFIAPI *EFI_HANDLE_PROTOCOL)(EFI_HANDLE, EFI_GUID *, void **);
typedef EFI_STATUS (EFIAPI *EFI_LOCATE_PROTOCOL)(EFI_GUID *, void *, void **);
typedef EFI_STATUS (EFIAPI *EFI_INSTALL_PROTOCOL_INTERFACE)(EFI_HANDLE *,
                        EFI_GUID *, void *, void *);
typedef EFI_STATUS (EFIAPI *EFI_IMAGE_LOAD)(BOOLEAN, EFI_HANDLE, void *,
                        void *, UINTN, EFI_HANDLE *);
typedef EFI_STATUS (EFIAPI *EFI_IMAGE_START)(EFI_HANDLE, UINTN *, CHAR16 **);
typedef EFI_STATUS (EFIAPI *EFI_EXIT_BOOT_SERVICES)(EFI_HANDLE, UINTN);
typedef EFI_STATUS (EFIAPI *EFI_STALL)(UINTN);
typedef EFI_STATUS (EFIAPI *EFI_OPEN_PROTOCOL)(EFI_HANDLE, EFI_GUID *, void **,
                        EFI_HANDLE, EFI_HANDLE, uint32_t);

typedef struct {
    EFI_TABLE_HEADER Hdr;
    void *RaiseTPL;
    void *RestoreTPL;
    EFI_ALLOCATE_PAGES AllocatePages;
    void *FreePages;
    EFI_GET_MEMORY_MAP GetMemoryMap;
    EFI_ALLOCATE_POOL  AllocatePool;
    EFI_FREE_POOL      FreePool;
    void *CreateEvent;
    void *SetTimer;
    void *WaitForEvent;
    void *SignalEvent;
    void *CloseEvent;
    void *CheckEvent;
    EFI_INSTALL_PROTOCOL_INTERFACE InstallProtocolInterface;
    void *ReinstallProtocolInterface;
    void *UninstallProtocolInterface;
    EFI_HANDLE_PROTOCOL HandleProtocol;
    void *Reserved;
    void *RegisterProtocolNotify;
    void *LocateHandle;
    void *LocateDevicePath;
    void *InstallConfigurationTable;
    EFI_IMAGE_LOAD  LoadImage;
    EFI_IMAGE_START StartImage;
    void *Exit;
    void *UnloadImage;
    EFI_EXIT_BOOT_SERVICES ExitBootServices;
    void *GetNextMonotonicCount;
    EFI_STALL Stall;
    void *SetWatchdogTimer;
    void *ConnectController;
    void *DisconnectController;
    EFI_OPEN_PROTOCOL OpenProtocol;
    void *CloseProtocol;
    void *OpenProtocolInformation;
    void *ProtocolsPerHandle;
    void *LocateHandleBuffer;
    EFI_LOCATE_PROTOCOL LocateProtocol;
    void *InstallMultipleProtocolInterfaces;
    void *UninstallMultipleProtocolInterfaces;
    void *CalculateCrc32;
    void *CopyMem;
    void *SetMem;
    void *CreateEventEx;
} EFI_BOOT_SERVICES;

/* Loaded image ---------------------------------------------------------- */
#define EFI_LOADED_IMAGE_PROTOCOL_GUID \
  { 0x5B1B31A1, 0x9562, 0x11D2, \
    { 0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } }

#define EFI_LOAD_OPTION_ACTIVE 0x00000001

typedef struct {
    uint32_t Revision;
    EFI_HANDLE ParentHandle;
    /*
     * SystemTable is easy to leave out, and omitting it is not harmless:
     * every field after it shifts by one pointer, so DeviceHandle reads the
     * system table pointer instead. The boot manager then asks the wrong
     * handle for a filesystem and prints "no filesystem" -- which is exactly
     * what happened before this line was added.
     *
     * Declared as void * because EFI_SYSTEM_TABLE is defined further down and
     * the field is only a placeholder that must occupy one pointer.
     */
    void *SystemTable;
    EFI_HANDLE DeviceHandle;      /* where the SimpleFileSystem lives */
    void      *FilePath;
    void      *Reserved;
    uint32_t   LoadOptionsSize;
    void      *LoadOptions;
    void      *ImageBase;
    uint64_t   ImageSize;
    EFI_MEMORY_TYPE ImageCodeType;
    EFI_MEMORY_TYPE ImageDataType;
    void      *Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

/* File access ----------------------------------------------------------- */
#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID \
  { 0x0964E5B22, 0x6459, 0x11D2, \
    { 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } }

typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_FILE_OPEN)(EFI_FILE_PROTOCOL *, EFI_FILE_PROTOCOL **,
                        const CHAR16 *, uint64_t, uint64_t);
typedef EFI_STATUS (EFIAPI *EFI_FILE_CLOSE)(EFI_FILE_PROTOCOL *);
typedef EFI_STATUS (EFIAPI *EFI_FILE_CLOSE)(EFI_FILE_PROTOCOL *);
typedef EFI_STATUS (EFIAPI *EFI_FILE_READ)(EFI_FILE_PROTOCOL *, UINTN *, void *);
typedef EFI_STATUS (EFIAPI *EFI_FILE_GET_INFO)(EFI_FILE_PROTOCOL *, EFI_GUID *,
                        UINTN *, void *);
typedef EFI_STATUS (EFIAPI *EFI_FILE_SET_POSITION)(EFI_FILE_PROTOCOL *, uint64_t);

typedef struct EFI_FILE_PROTOCOL {
    uint64_t Revision;
    EFI_FILE_OPEN  Open;
    EFI_FILE_CLOSE  Close;
    void          *Delete;
    EFI_FILE_READ  Read;
    void          *Write;
    void          *GetPosition;
    EFI_FILE_SET_POSITION SetPosition;
    EFI_FILE_GET_INFO GetInfo;
    void          *SetInfo;
    void          *Flush;
} EFI_FILE_PROTOCOL;

typedef struct {
    EFI_HANDLE Volume;   /* EFI_SIMPLE_FILE_SYSTEM_PROTOCOL */
    EFI_FILE_PROTOCOL *Root;
} /* not a real UEFI type -- kept for clarity */ sfs_dummy_t;

typedef EFI_STATUS (EFIAPI *EFI_SFS_OPEN_VOLUME)(void *, EFI_FILE_PROTOCOL **);

typedef struct {
    uint64_t Revision;
    EFI_SFS_OPEN_VOLUME OpenVolume;
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

#define EFI_FILE_INFO_GUID \
  { 0x09576E92, 0x6D3F, 0x11D2, \
    { 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } }

/* EFI_TIME is 16 bytes. Declaring it as a void * (8) would shrink the
 * struct by 24 bytes, which matters twice over: the size passed to GetInfo
 * would be too small for the driver to fill, and everything after the first
 * time field would sit at the wrong offset. */
typedef struct {
    uint16_t Year;
    uint8_t  Month, Day, Hour, Minute, Second;
    uint8_t  Pad1;
    uint32_t Nanosecond;
    int16_t  TimeZone;
    uint8_t  Daylight, Pad2;
} EFI_TIME;

typedef struct {
    uint64_t Size;
    uint64_t FileSize;
    uint64_t PhysicalSize;
    EFI_TIME CreateTime;
    EFI_TIME LastAccessTime;
    EFI_TIME ModificationTime;
    uint64_t Attribute;
    CHAR16   FileName[1];
} EFI_FILE_INFO;

/* Device path ----------------------------------------------------------- */
#define EFI_DEVICE_PATH_PROTOCOL_GUID \
  { 0x09576E91, 0x6D3F, 0x11D2, \
    { 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } }

typedef struct {
    uint8_t Type;
    uint8_t SubType;
    uint16_t Length;
} EFI_DEVICE_PATH_PROTOCOL;

#define EFI_DEV_MEDIA    0x04
#define EFI_DEV_MEDIA_VENDOR 0x03
#define EFI_DP_TYPE_END  0x7F
#define EFI_DP_SUBTYPE_END 0xFF
#define EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE 0xFF

/* Linux-specific: the initrd is handed over through a vendor media device
 * path carrying this GUID, with an EFI_LOAD_FILE2_PROTOCOL installed on it.
 * The kernel's EFI stub calls LoadFile() on it during early boot.
 *
 * systemd-boot and GRUB both use exactly this mechanism. */
#define LINUX_EFI_INITRD_MEDIA_GUID \
  { 0x5568E427, 0x68FC, 0x4F3D, \
    { 0xAC, 0x74, 0xCA, 0x55, 0x52, 0x31, 0xCC, 0x68 } }

#define EFI_LOAD_FILE2_PROTOCOL_GUID \
  { 0x4006C0C1, 0xFCB3, 0x403E, \
    { 0x99, 0x6D, 0x4A, 0x6C, 0x87, 0x24, 0xE0, 0x6D } }

typedef EFI_STATUS (EFIAPI *EFI_LOAD_FILE2)(void *This, void *FilePath,
                        BOOLEAN BootPolicy, UINTN *BufferSize, void *Buffer);

typedef struct {
    EFI_LOAD_FILE2 LoadFile;
} EFI_LOAD_FILE2_PROTOCOL;

/* System table ---------------------------------------------------------- */
typedef struct {
    EFI_TABLE_HEADER Hdr;
    CHAR16 *FirmwareVendor;
    uint32_t FirmwareRevision;
    EFI_HANDLE ConsoleInHandle;
    void *ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    EFI_HANDLE StandardErrorHandle;
    void *StdErr;
    void *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices;
    UINTN NumberOfTableEntries;
    void *ConfigurationTable;
} EFI_SYSTEM_TABLE;

#endif /* NXSOL_EFI_H */
