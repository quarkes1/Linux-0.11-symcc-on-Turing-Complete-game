# Project working notes

- Edit this repository in place on a Git branch. The user explicitly requested no `.worktrees` directories or full-project worktree copies.
- Preserve the SymphonyPlus CPU and ISA unless the user asks for hardware changes. Instructions, RAM data and DiskA words are big-endian.
- Build tools with MSYS2 GCC (`D:/Downloads/msys64/ucrt64/bin` in this environment). Git Bash and Python 3 are required by the system build scripts.
- `mingw32-make test` rebuilds both the interactive images and the separate M3 image and runs the complete suite. It includes target-machine execution, process reuse, terminal overflow/scrolling, disk corruption and persistence tests.
- `bash scripts/build_system.sh` produces `build/kernel.bin` and `build/rootfs.bin`. `bash scripts/build_kernel.sh --m3` produces the historical stopping image under `build/m3/`.
- Never paste the full kernel assembly into the game. Load the `.bin` images through the RAM/DiskA external-file mode.
- Keep Linux adaptations marked `SYMPLUS-PORT`. Preserve vendored source line endings (`Reference/** -text`); use `git -c core.whitespace=trailing-space,space-before-tab,cr-at-eol diff --check` when checking those files.
- Report simulator verification separately from actual game verification. A user screenshot confirmed the previous shell boots and accepts commands in the game; updates still need game revalidation. The installed game's key map uses Backspace=0x0D and Enter=0x0A; do not convert raw hardware 0x0D to LF. Host terminal CR normalization belongs in the emulator front end.
- The process model switches the fixed 2MiB user window at syscall boundaries. Default RAM and DiskA are 32MiB; runtime/platform.h is the single numeric ABI source. Kernel stacks, process metadata and exec staging must remain outside that window. Check every user pointer and every nonzero MINIX zone before using it.
- Native symcc uses the existing compiler core in scpp/scc1/sld guest processes. Keep native integer intermediates explicit (CcInt/CcUInt), SDK puts separate from raw puts, and publish successful compiler output with chmod + rename. Failed compilation must preserve old output. Never substitute host compilation for guest acceptance.
- Raw Shift codes are left=16 and right=20; track both independently and map US punctuation. Shell heredocs must be collected before opening/truncating output and drained to their delimiter even on spool or long-line failure. Never run failed heredoc body lines as commands.
