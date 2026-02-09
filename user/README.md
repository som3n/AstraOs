# User Programs

This folder builds a minimal i386 ELF32 `ET_EXEC` user program intended to be loaded by the kernel ELF loader.

Targets:
- `make userprogs` builds several ELFs in `build/user/` linked at `0x00200000`.
- `make install-userprogs` copies them into `astra_disk.img` under `/BIN/*.ELF` (requires `mtools`).
