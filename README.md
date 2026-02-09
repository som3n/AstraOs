# AstraOS

Minimal i386 hobby OS kernel with:
- VGA text console + interactive shell
- IRQ/ISR, PIC remap, PIT timer, keyboard
- Simple heap + paging (identity-mapped first 4MB)
- FAT16 filesystem on `astra_disk.img`
- Syscalls via `int 0x80`
- ELF32 `ET_EXEC` loader + ring3 userspace switch

## Build

```bash
make
```

## Run

```bash
make run
```

## User Program (ELF)

Build user ELFs and install them into the FAT16 disk image:

```bash
make userprogs
make install-userprogs
```

The kernel tries to execute `/BIN/INIT.ELF` at boot. You can also run it from the shell:

```text
run /BIN/INIT.ELF
```

Notes:
- The user ELF is linked to `0x00200000` (kernel is linked at `0x00100000`).
- `install-userprogs` uses `mtools` and expects a valid FAT16 image in `astra_disk.img`.
