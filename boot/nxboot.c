/* SPDX-License-Identifier: GPL-2.0-only
 * NX System On Linux -- NX Boot Manager
 * Copyright (C) 2026 rainbowcrr-sys
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*
 * NX Boot Manager -- milestone 1.
 *
 * There is no GRUB here. The Linux kernel is built with CONFIG_EFI_STUB,
 * which makes the bzImage itself a PE/COFF EFI application, so firmware can
 * execute it directly. This program is the piece that finds it, hands it the
 * command line and the initramfs, and starts it.
 *
 * The initramfs is passed through the Linux initrd media device path: an
 * EFI_LOAD_FILE2_PROTOCOL installed on a vendor media path carrying
 * LINUX_EFI_INITRD_MEDIA_GUID. The kernel's EFI stub calls LoadFile() on it
 * during early boot, twice -- once to learn the size, once to receive the
 * data. systemd-boot and GRUB both use this mechanism; the alternative,
 * putting "initrd=..." on the command line, depends on the stub being able
 * to reach the filesystem, which it cannot when the kernel was loaded from a
 * buffer with no device handle behind it.
 *
 * Sequence:
 *   1. locate our own volume through EFI_LOADED_IMAGE_PROTOCOL
 *   2. read \nx\vmlinuz and \nx\initrd.img into memory
 *   3. publish the initrd via LoadFile2
 *   4. LoadImage() the kernel from its buffer
 *   5. attach the command line as LoadOptions
 *   6. StartImage() -- after this the kernel owns the machine
 */

#include "efi.h"

/* Paths on the ESP. */
#define NX_KERNEL_PATH L"\\vmlinuz"
#define NX_INITRD_PATH L"\\initrd.img"

/* Handed to the kernel. Serial console so the run is capturable headless. */
#define NX_CMDLINE "console=ttyS0 rdinit=/init initrd=\\initrd.img"

#define EFI_FILE_MODE_READ 0x0000000000000001ULL

static EFI_GUID g_loaded_image_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static EFI_GUID g_sfs_guid          = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static EFI_GUID g_file_info_guid    = EFI_FILE_INFO_GUID;
static EFI_GUID g_dp_guid           = EFI_DEVICE_PATH_PROTOCOL_GUID;
static EFI_GUID g_loadfile2_guid    = EFI_LOAD_FILE2_PROTOCOL_GUID;
static EFI_GUID g_initrd_media_guid = LINUX_EFI_INITRD_MEDIA_GUID;

static EFI_SYSTEM_TABLE *g_st;
static EFI_BOOT_SERVICES *g_bs;

/* ------------------------------------------------------------------ text */

static void print(const char *ascii)
{
    CHAR16 buf[512];
    size_t i = 0;

    if (!g_st || !g_st->ConOut) return;

    while (ascii[i] && i < sizeof(buf)/sizeof(buf[0]) - 2) {
        buf[i] = (CHAR16)ascii[i];
        i++;
    }
    buf[i++] = L'\r';
    buf[i] = L'\0';

    g_st->ConOut->OutputString(g_st->ConOut, buf);
}

static void print_attr(const char *ascii, UINTN attr)
{
    if (!g_st || !g_st->ConOut) return;
    g_st->ConOut->SetAttribute(g_st->ConOut, attr);
    print(ascii);
    g_st->ConOut->SetAttribute(g_st->ConOut,
                               EFI_TEXT_ATTR(EFI_LIGHTGRAY, EFI_BLACK));
}

/* ------------------------------------------------------------------ files */

/* Read a whole file from the ESP. Returns NULL on any failure; `size_out`
 * receives the byte count. */
static void *read_file(EFI_FILE_PROTOCOL *root, const CHAR16 *path,
                       UINTN *size_out)
{
    EFI_FILE_PROTOCOL *f = NULL;
    EFI_FILE_INFO *info = NULL;
    UINTN info_size = 0;
    UINTN file_size;
    void *buffer = NULL;
    EFI_STATUS s;

    *size_out = 0;

    s = root->Open(root, &f, path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(s) || !f) return NULL;

    /*
     * GetInfo is called twice. The first call passes a zero buffer and comes
     * back with EFI_BUFFER_TOO_SMALL and the size actually needed -- it does
     * NOT fill the struct. Reading FileSize after only that call yields
     * uninitialised stack, which is how "kernel not found" was produced while
     * the file was sitting intact on the volume.
     */
    s = f->GetInfo(f, &g_file_info_guid, &info_size, NULL);
    if (s != EFI_BUFFER_TOO_SMALL || info_size == 0) goto out;

    s = g_bs->AllocatePool(EfiLoaderData, info_size, (void **)&info);
    if (EFI_ERROR(s)) { info = NULL; goto out; }

    s = f->GetInfo(f, &g_file_info_guid, &info_size, info);
    if (EFI_ERROR(s)) goto out;

    file_size = (UINTN)info->FileSize;

    s = g_bs->AllocatePool(EfiLoaderData, file_size, &buffer);
    if (EFI_ERROR(s)) { buffer = NULL; goto out; }

    s = f->Read(f, &file_size, buffer);
    if (EFI_ERROR(s)) { g_bs->FreePool(buffer); buffer = NULL; goto out; }

    *size_out = file_size;

out:
    if (info) g_bs->FreePool(info);
    f->Close(f);
    return buffer;
}

/* --------------------------------------------------------------- initrd */

/* The initrd is handed over in one shot; LoadFile2 is called a second time
 * only because the caller asks for the size first. */
static void *g_initrd_data;
static UINTN g_initrd_size;
static UINTN g_lf2_calls;   /* debug: how often the stub called us */
static UINTN g_lf2_last_size;

static EFI_STATUS EFIAPI initrd_load_file(void *This,
                                          void *FilePath,
                                          BOOLEAN BootPolicy,
                                          UINTN *BufferSize,
                                          void *Buffer)
{
    (void)This; (void)FilePath; (void)BootPolicy;

    g_lf2_calls++;
    if (BufferSize) g_lf2_last_size = *BufferSize;

    if (!BufferSize) return EFI_INVALID_PARAMETER;

    /* Three cases, in the order the caller expects them:
     *
     *   Buffer == NULL          -> report the size, EFI_SUCCESS
     *   *BufferSize < needed    -> report the size, EFI_BUFFER_TOO_SMALL
     *   otherwise               -> copy, EFI_SUCCESS
     *
     * Returning SUCCESS for the too-small case would tell the caller its
     * buffer was accepted, which is not what happened; the kernel's stub
     * treats the distinction as meaningful.
     */
    /*
     * Both query cases report EFI_BUFFER_TOO_SMALL. A NULL buffer means the
     * caller has supplied no space at all, which is the extreme case of
     * "too small" -- and the kernel's stub treats EFI_SUCCESS on this first
     * call as "nothing to load", moving on without ever asking for the data.
     */
    if (!Buffer || *BufferSize < g_initrd_size) {
        *BufferSize = g_initrd_size;
        return EFI_BUFFER_TOO_SMALL;
    }

    {
        unsigned char *dst = (unsigned char *)Buffer;
        const unsigned char *src = (const unsigned char *)g_initrd_data;
        UINTN i;
        for (i = 0; i < g_initrd_size; i++) dst[i] = src[i];
    }
    *BufferSize = g_initrd_size;
    return EFI_SUCCESS;
}

/* Device path: vendor media node carrying the Linux initrd GUID, then END.
 * The stub matches on the GUID, so the path itself is otherwise inert. */
typedef struct {
    uint8_t  Type;
    uint8_t  SubType;
    uint16_t Length;
    EFI_GUID Guid;
} VENDOR_MEDIA_DP;

typedef struct {
    uint8_t  Type;
    uint8_t  SubType;
    uint16_t Length;
} END_DP;

static struct {
    VENDOR_MEDIA_DP vendor;
    END_DP          end;
} g_initrd_dp;

static EFI_LOAD_FILE2_PROTOCOL g_initrd_lf2 = { initrd_load_file };

static EFI_STATUS publish_initrd(void)
{
    EFI_HANDLE handle = NULL;
    EFI_STATUS s;

    g_initrd_dp.vendor.Type    = EFI_DEV_MEDIA;
    g_initrd_dp.vendor.SubType = EFI_DEV_MEDIA_VENDOR;
    g_initrd_dp.vendor.Length  = (uint16_t)sizeof(VENDOR_MEDIA_DP);
    g_initrd_dp.vendor.Guid    = g_initrd_media_guid;

    g_initrd_dp.end.Type    = EFI_DP_TYPE_END;
    g_initrd_dp.end.SubType = EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE;
    g_initrd_dp.end.Length  = (uint16_t)sizeof(END_DP);

    /* Passing a NULL handle asks the firmware to create one. Both
     * interfaces go on the same handle because the stub looks up a
     * LoadFile2 instance by walking handles that carry this device path. */
    s = g_bs->InstallProtocolInterface(&handle, &g_dp_guid,
                                       EFI_NATIVE_INTERFACE, &g_initrd_dp);
    if (EFI_ERROR(s)) return s;

    s = g_bs->InstallProtocolInterface(&handle, &g_loadfile2_guid,
                                       EFI_NATIVE_INTERFACE, &g_initrd_lf2);
    return s;
}

/* -------------------------------------------------------------- cmdline */

/* LoadOptions is CHAR16, and the kernel's stub converts it back to ASCII, so
 * every character is widened rather than the string being reinterpreted. */
static CHAR16 *to_utf16(const char *ascii, UINTN *chars_out)
{
    static CHAR16 buf[256];
    size_t i = 0;

    while (ascii[i] && i < sizeof(buf)/sizeof(buf[0]) - 1) {
        buf[i] = (CHAR16)ascii[i];
        i++;
    }
    buf[i] = L'\0';
    *chars_out = (UINTN)i;
    return buf;
}

/* ------------------------------------------------------------------ main */

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *st)
{
    EFI_LOADED_IMAGE_PROTOCOL *li = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *sfs = NULL;
    EFI_FILE_PROTOCOL *root = NULL;
    void *kernel = NULL;
    UINTN kernel_size = 0;
    EFI_HANDLE kernel_handle = NULL;
    UINTN cmdline_chars;
    CHAR16 *cmdline;
    EFI_STATUS s;

    g_st = st;
    g_bs = st->BootServices;

    st->ConOut->ClearScreen(st->ConOut);
    st->ConOut->SetAttribute(st->ConOut,
                             EFI_TEXT_ATTR(EFI_LIGHTCYAN, EFI_BLACK));
    print("");
    print("  NN    NN    XXXXXXX");
    print("  NNN   NN    XX   XX");
    print("  NN N  NN      XXXX");
    print("  NN  N NN    XX   XX");
    print("  NN   NNN    XXXXXXX");
    print("");
    print_attr("  NX Boot Manager", EFI_TEXT_ATTR(EFI_WHITE, EFI_BLACK));
    print("");

    /* Our own volume: the ESP we were loaded from. */
    s = g_bs->HandleProtocol(image, &g_loaded_image_guid, (void **)&li);
    if (EFI_ERROR(s) || !li) { print("  no loaded image"); goto fail; }

    s = g_bs->HandleProtocol(li->DeviceHandle, &g_sfs_guid, (void **)&sfs);
    if (EFI_ERROR(s) || !sfs) { print("  no filesystem"); goto fail; }

    s = sfs->OpenVolume(sfs, &root);
    if (EFI_ERROR(s) || !root) { print("  cannot open volume"); goto fail; }

    print("  loading kernel ...");
    kernel = read_file(root, NX_KERNEL_PATH, &kernel_size);
    if (!kernel || !kernel_size) { print("  kernel not found"); goto fail; }

    print("  loading initramfs ...");
    g_initrd_data = read_file(root, NX_INITRD_PATH, &g_initrd_size);
    if (!g_initrd_data || !g_initrd_size) { print("  initrd not found"); goto fail; }

    print("  publishing initrd ...");
    s = publish_initrd();
    if (EFI_ERROR(s)) { print("  initrd publish failed"); goto fail; }

    print("  starting kernel ...");
    s = g_bs->LoadImage(FALSE, image, NULL, kernel, kernel_size,
                        &kernel_handle);
    if (EFI_ERROR(s)) { print("  LoadImage failed"); goto fail; }

    /* The command line travels with the image, not as an argument. */
    {
        EFI_LOADED_IMAGE_PROTOCOL *kli = NULL;
        s = g_bs->HandleProtocol(kernel_handle, &g_loaded_image_guid,
                                 (void **)&kli);
        if (!EFI_ERROR(s) && kli) {
            /*
             * Hand the kernel our own DeviceHandle.
             *
             * Loading from a buffer gives the new image no device handle, and
             * the stub's "initrd=..." fallback resolves that path through
             * this field -- with it left empty the stub reports
             * "Failed to load initrd!" even though the file is sitting on the
             * same volume.
             */
            kli->DeviceHandle = li->DeviceHandle;

            cmdline = to_utf16(NX_CMDLINE, &cmdline_chars);
            kli->LoadOptions = cmdline;
            kli->LoadOptionsSize = (uint32_t)((cmdline_chars + 1)
                                              * sizeof(CHAR16));
        }
    }

    s = g_bs->StartImage(kernel_handle, NULL, NULL);

    /* StartImage only returns if the kernel refused to run. */
    {
        char dbg[128];
        const char *digits = "0123456789";
        int i = 0, j;
        UINTN v = g_lf2_calls;
        print("  StartImage failed");
        /* minimal itoa: print the counters by hand */
        if (v == 0) print("  LoadFile2 was never called");
        else {
            char tmp[24];
            int n = 0;
            while (v > 0) { tmp[n++] = digits[v % 10]; v /= 10; }
            for (j = n - 1; j >= 0; j--) dbg[i++] = tmp[j];
            dbg[i] = 0;
            print("  LoadFile2 calls: ");
            print(dbg);
        }
    }

fail:
    print("");
    print_attr("  NX Boot Manager halted.",
               EFI_TEXT_ATTR(EFI_LIGHTRED, EFI_BLACK));
    for (;;) { __asm__ volatile ("hlt"); }
}
