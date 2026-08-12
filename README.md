# dcc
[![CI](https://github.com/gloveboxes/dcc/actions/workflows/ci.yml/badge.svg)](https://github.com/gloveboxes/dcc/actions/workflows/ci.yml)
[![Docs](https://github.com/gloveboxes/dcc/actions/workflows/docs.yml/badge.svg)](https://github.com/gloveboxes/dcc/actions/workflows/docs.yml)

C compiler targeting CP/M 2.2 on a Z80

The [dcc documentation](https://davidly.github.io/dcc/) covers all features, usage, and API reference.

## What dcc is
DCC C Compiler is an open source C compiler for CP/M 2.2 on the Z80. It supports C89 plus CP/M-relevant C99/C11 features. For every source file it accepts, dcc generates a .MAC assembly file that can be assembled by M80 and linked by L80 to produce CP/M .COM files.

A separate app dccpeep.c is a peephole optimizer that rewrites portions of .MAC files so apps run faster. It's not necessary to use dccpeep; apps will work just fine without it. But if you need your app to be both smaller and faster it's worth running.

DCCRTL.MAC is the dcc C Runtime Library. It's written in Z80 assembly for size and performance. It has the entrypoint start for apps that initializes the heap (for malloc/free) and command-line arguments so main's argc and argv work. It implements a small subset of the C89 C runtime including floating point.

dccrtlstrip.c is an app that examines the code of your .c file and strips portions of the DCCRTL.MAC C runtime so only the parts needed are linked into the .COM file. It's not necessary to run this program for your app to work. But the resulting .COM file may be smaller if you do.

The 3 compiler apps dcc, dccpeep, and dccrtlstrip all build and run on Windows, Linux, and MacOS. They are too big to run on CP/M. Use m.bat, m.sh, mmacos.sh to build these apps using msvc (Windows), gcc (Linux), or clang (MacOS) respectively. You may need to chmod 777 *.sh on Linux and MacOS prior to running dcc's scripts.

Dcc has been built and had regression tests run on AMD64 (Linux and Windows), Arm64(Linux, Windows, MacOS), Arm32 (Linux), and RISC-V 64 (Linux).

The binaries dcc produces have been tested in ntvcm (across many platforms), tnylpo (on Linux + AMD64), altair 8800 simulator (on Windows), cpm.exe (on Windows), cpmemu (on Linux), and on a physical Z80 with the Z80-MBC2 SBC. 

The Z80-MBC2 SBC has a TPA of just 55,558 bytes, significantly less than most emulators. Most of the test apps work, but some require more TPA than that. For example, running the Pascal interpreter pint.com with ttt.pas runs out of memory. But smaller test apps like e.pas work fine in pint.com.

When apps built by dcc exit back to the OS on CP/M they do so using the warm boot vector. The CCP is overwritten with the app's stack and heap, so it must be automatically reloaded by CP/M on warm boot. No attempt is made to detect RAM required by an app and preserve the CCP. Reloading the CCP generally takes a couple seconds on physical hardware.

## Documentation

Two reference documents in the [docs](docs) directory cover the runtime in depth:

  - [docs/dcc-c89-reference-guide.md](docs/dcc-c89-reference-guide.md): a practical guide to the C89 language features dcc accepts and the C runtime library implemented in DCCRTL.MAC. It documents type sizes and conventions, the recognized keywords and operators, every standard-header function that is actually linkable (stdio, stdlib, string, ctype, math, setjmp, stdarg, and the CP/M extensions), the supported printf/scanf conversions, and the limitations to keep in mind (no double, 16-bit int, integer-only `%`, etc.). Start here to learn what you can call and how.
  - [docs/dccrtlstrip-inclusion-table.md](docs/dccrtlstrip-inclusion-table.md): an internals reference explaining how dccrtlstrip decides which blocks of DCCRTL.MAC are linked into a program. It maps each C-level construct to the runtime block it pulls in and gives the transitive dependency closures and the marginal .COM size cost of each function. Use it when optimizing binary size or to understand exactly what a given call drags into the link.

## Development machine for modifying dcc
DCC is intended to be updated by app-writers to better optimize their apps. Typically a dev would point an AI at the code for their app and the code for dcc then ask the AI to profile the app and change dcc to generate better code for the app's scenario.

The inner loop of iterating on improving performance is governed by the speed of your dev machine. Running the full regression suite to ensure nothing was broken can take seconds or minuted depending on your hardware and OS choice. Not surprisingly, more cores really help. And using Linux instead of Windows (which has slower process creation times, anti-virus scanning, indexing, and more) works much better.

<img alt="table" src="images/tests.jpg" />

## Agent skills

This repo ships a project-scoped agent **skill** in [.github/skills/dcc-cpm-z80](.github/skills/dcc-cpm-z80). A skill is a folder containing a `SKILL.md` (plus optional `references/`) that packages domain knowledge — here, how to write, build, test, and debug C89/C99/C11-targeted code for dcc/CP/M/Z80 along with the runtime library inventory and hard-won pitfalls. An agent that supports skills reads `SKILL.md` on demand when your task matches the skill's description, so it gets dcc-specific guidance without you pasting it into every prompt.

### Invoking a skill in VS Code

With GitHub Copilot in VS Code (agent mode), the skill is picked up automatically when you open this repo — the agent loads it when your request falls within the skill's scope (anything mentioning dcc, CP/M, Z80, ntvcm, DCCRTL, etc.). You don't have to do anything special; you can also nudge it explicitly, e.g. "use the dcc-cpm-z80 skill to build and test foo.c".

### Using the skill from the GitHub Copilot CLI

The GitHub Copilot CLI discovers skills the same way: project skills from the repo you launch it in, plus any personal skills in your home-directory roots (see below). From the repo root just start a session and describe your task —

```sh
copilot
```

then, at the prompt, ask something within the skill's scope (e.g. "build and run tests/sieve.c for CP/M with dcc"). The CLI reads the matching `SKILL.md` on demand, exactly like VS Code. To make it available outside this repo, copy the skill into a personal skills root as shown next.

### Making the skill available system-wide

The copy in this repo only applies while you're working inside this repo. The main reason to deploy it system-wide is to build CP/M apps in a **separate, independent project**: with the skill in a personal root, the agent brings dcc-specific knowledge into that other workspace, and as long as the `dcc`/`dccpeep`/`dccrtlstrip` binaries and `DCCRTL.MAC` are on your `PATH` (see [Setting up your environment](#setting-up-your-environment)), you can compile and run from there without copying the toolchain into every project.

To use it from **every** workspace on your machine, copy the skill folder into a personal skills root in your home directory (`~/.agents/skills/`, `~/.copilot/skills/`, or `~/.claude/skills/` — pick one and stay consistent):

**macOS / Linux:**
```sh
mkdir -p ~/.agents/skills
cp -R .github/skills/dcc-cpm-z80 ~/.agents/skills/
```

**Windows (PowerShell):**
```powershell
New-Item -ItemType Directory -Force "$env:USERPROFILE\.agents\skills" | Out-Null
Copy-Item -Recurse ".github\skills\dcc-cpm-z80" "$env:USERPROFILE\.agents\skills\"
```

The repo copy and the personal copy are independent files, so re-copy after editing either one to keep them in sync. See [.github/skills/README.md](.github/skills/README.md) for the full list of supported skill roots and sync tips.

## How to build test apps and your apps

ma.bat and ma.sh are scripts to build your app. Run "ma foo" (or "ma.sh foo" on Linux/MacOS) to compile foo.c, optimize it, strip the DCCRTL.MAC runtime so unused code isn't included, assemble the generated FOO.MAC file, and link to FOO.COM. Use the "nopeep" argument like "ma foo nopeep" to not run the dccpeep peephole optimizer.

runall.bat and runall.sh compile and run all 90+ test cases both optimized and unoptimized. The output of that run is compared with baseline_test_dcc.txt to check for regressions. It takes under two minutes to run on my two-year-old machine.

The test apps validate compiler correctness and performance. Some test apps are small and exercise a single compiler feature. Others are larger; tchess.c plays chess (not very well) and with the -c argument can play against itself. 

Linux typically is configured to have case-sensitive filenames. CP/M files are uppercase. The convention used is that source .c files have lowercase names since only dcc works with them. Assembly files (.MAC) are all uppercase, as are output files from m80.com and l80.com including .COM, .PRN, and .REL.

### usage: dcc [-c|-module] [-ffloatio] [-stack bytes] [-Dname[=value]] input.c -o output.mac

    * -c compile a .c file without a main() to be linked by L80 later
    * -ffloatio tells the compiler the code will use %f formatting with printf family of functions so include floating point runtime
    * -stack bytes how much to reserve for the stack. default is 512 bytes
    * -Dname[=value] predeclare a macro. _DCC_=1 is defined by default
    * -o output file name. default is out.mac. Can be assembled using m80.com
    
### usage: dccpeep [-Ot|-Os] input.mac output.mac

    * -Ot | -Os. Optimize for time or size. Default is -Ot. 
    * -o output file name of optimized M80 assembly

### usage: dccrtlstrip [-k symbol ...] -r dccrtl.mac -o rtlmin.mac app.mac [app2.mac ...]

    * -k symbol tells dccrtlstrip to keep the public symbol with that name and all symbols it references
    * -r filename the C runtime starting point. public symbols used by the app or specified by -r written to the output filename
    * -o filaneme output filename
    * filename1.mac ... filenames to scan for public symbol usage; those symbols are retained from the C runtime
    
### Separate compilation units

By default dcc assumes apps have one .c file. You can #include .c files into your main app. Or, you can use dcc's -c flag to compile stand-alone .c files then link them with your main app (built without -c). See cpmenumd.c for instructions for how to do that using mrel.bat/mrel.sh and updates to ma.bat / ma.sh for linking.

## Emulators

I use my [ntvcm](https://github.com/davidly/ntvcm) CP/M 2.2 emulator to run m80.com, l80.com, and apps built with dcc; the regression suite is baselined against it. The compiler and runtime don't push emulator compatibility limits: only CP/M 2.2 BDOS functions are used (and no BIOS functions), with one exception - app exit codes (returned from main() or passed to exit()) are set using CP/M 3.0 BDOS call 108. Some emulators, ntvcm included, reflect that value in their own process exit code.

### Other CP/M emulators

I've since run dcc-built apps under several other emulators to see how portable they actually are, beyond ntvcm. Short version: the compiler and runtime themselves are fine everywhere - every gap found below is either a one-time invocation quirk (a command-line flag or how the emulator is set up) or a genuine bug in that specific emulator, not something dcc does differently per emulator. If you hit something not covered here, please open an issue.

| Emulator | Platform | Works out of the box? | Notes |
| --- | --- | --- | --- |
| [ntvcm](https://github.com/davidly/ntvcm) | Windows, Linux, macOS | Yes | Reference emulator; the regression suite is baselined against it. |
| cpm.exe (Takeda Toshiya's "[CP/M Player for Win32](http://takeda-toshiya.my.coocan.jp/cpm/index.html)") | Windows | Yes, on a current build | Builds from July 2024 or earlier have a bug where files whose length isn't a multiple of 128 bytes (CP/M's record size) can read back corrupted past the true end of the file on some hosts - this only bites on file sizes that don't align to 128, so most small test apps never trip it, but e.g. `pint.com` interpreting a `.pas` file whose length isn't a multiple of 128 can. Fixed upstream; grab a build from `http://takeda-toshiya.my.coocan.jp/cpm/` (2024/10 or later) rather than an old cached copy. On BDOS 23 (rename), it rejects renaming onto an already-existing destination name (`rename()` returns nonzero, the old name and its content survive untouched) rather than silently overwriting - same behavior as tnylpo, opposite of ntvcm/cpmemu/zxcc/z88dk's cpm/RunCPM, all of which allow the overwrite (see the rename note below the table). It appears to shell out to `cmd.exe` for at least that call - running a rename from a UNC-style path (e.g. a WSL `\\wsl.localhost\...` mount) prints a `UNC paths are not supported. Defaulting to Windows directory.` warning to stdout before the real output, worth filtering out of captured baselines if you drive it from such a path. Everything else tested (append-mode positioning, `X:` drive prefixes, record-length rounding, Ctrl-Z record padding, 8.3 truncation collisions, and wildcard delete matching every ambiguous name) is byte-for-byte identical to ntvcm. |
| [tnylpo](https://gitlab.com/gbrein/tnylpo) | Linux | Yes | Takes the CP/M command file as a literal, case-sensitive host filename (e.g. `tnylpo e.com`, not `tnylpo E.COM`) - unlike the others here, it doesn't uppercase/normalize what you pass it. Also rejects BDOS 23 (rename) onto an already-existing destination name, matching cpm.exe and unlike ntvcm/cpmemu/zxcc/z88dk's cpm/RunCPM - see the rename note below the table. |
| [iz-cpm](https://github.com/ivanizag/iz-cpm) | Windows, Linux, macOS | Needs `--cpm3` | Its default CP/M 2.2 mode doesn't implement BDOS 108 (`P_CODE`), and prints `BDOS command 108 not implemented.` to stdout instead of silently ignoring it - since DCCRTL always calls BDOS 108 on exit (see above), this shows up at the end of every app's output unless you pass `--cpm3`. Separately, and not worked around by that flag: it has a bug where a file written and closed can read back as 0 bytes on reopen (confirmed with a 3-line `fopen`/`fwrite`/`fclose` repro against ntvcm and cpm.exe, both of which handle the identical byte-for-byte BDOS call sequence correctly) - a real iz-cpm limitation, not a dcc/DCCRTL issue. The project's own README describes it as "a very basic implementation, mostly for educational purposes." |
| [zxcc](https://github.com/agn453/zxcc) | Windows, Linux, macOS | Needs a `-` prefix on each argument | Being primarily a Hi-Tech-C-compiler wrapper, zxcc treats a bare command-line argument as a *host filename to translate* into CP/M form unless it's prefixed with `-` (e.g. `zxcc TTT.COM -10`, not `zxcc TTT.COM 10`) - without the prefix the app sees a mangled argument instead of the literal text. It also lowercases CP/M filenames when mapping to the host filesystem, so a fixture file staged with an uppercase name (as CP/M convention and this repo's own test harness both do) won't be found on a case-*sensitive* host (Linux); this doesn't come up on a case-insensitive host (Windows, default macOS). Separately: `unlink()` on a nonexistent file returns success instead of `ENOENT` - a genuine BDOS-conformance gap - and its `FIND_FIRST`/`FIND_NEXT` (BDOS 17/18) never returns the currently-*executing* `.COM` file itself from a wildcard search, even though it plainly exists (root cause not tracked down further; see `cpmenumd.c`). |
| [RunCPM](https://github.com/MockbaTheBorg/RunCPM) | Windows, Linux, macOS | Needs setup, then yes | RunCPM boots to an interactive CCP prompt rather than taking a `.com` file on its own command line, and produces no output at all unless given a real PTY (nothing over a plain pipe). To drive it non-interactively: build with `globals.h`'s `BOOTONLY` set to `TRUE` so its `AUTOEXEC.TXT` auto-run mechanism fires once instead of looping forever, then run it under something that allocates a PTY (e.g. `script -qec "./RunCPM" logfile`) with the command written to `AUTOEXEC.TXT` beforehand. Once set up, its behavior matches ntvcm closely (e.g. console echo of redirected/piped stdin behaves the same, unlike cpm.exe). |
| [z88dk/cpm](https://github.com/z88dk/cpm) (a fork of jhallen/cpm, adopted by the z88dk project) | Linux, Cygwin | Needs a patch, a PTY, and a generous TPA | Same PTY requirement as RunCPM (nothing over a plain pipe), and the same BDOS-108 gap as iz-cpm - except this one doesn't just print a message, it treats any unrecognized BDOS call as fatal and hard-crashes with a register dump. No `--cpm3`-style flag exists; the fix is a one-line patch adding a no-op `case 108` to `bdos.c`. Two more, deeper bugs: BDOS 35 (compute file size) requires the FCB to already be open (`getfp()` in `bdos.c`), but the CP/M 2.2 spec explicitly doesn't require that - it just queries the directory - so any correct program calling F_SIZE on an unopened FCB (e.g. `cpmenumd.c`, or DCCRTL's `tmpfile()` cleanup before the fix below) crashes the same way. And its default-build TPA is only ~56 KB - smaller than every other emulator here, and in the same range as the physical Z80-MBC2 already noted above - so `pint.com` + `ttt.pas`/`ttt.ada` (bigger than `e.pas`/`e.ada`) can genuinely run out of memory; that's the known TPA limitation, not a bug. |
| [cpmemu](https://github.com/avwohl/cpmemu) | Linux | Yes | The cleanest of the non-reference emulators tried here: no PTY, no argument-prefix or case-folding quirks, and it already sends its own diagnostics (including an `Unimplemented BDOS function 108` notice - non-fatal, unlike z88dk/cpm) to stderr rather than stdout, so a plain `2>/dev/null` wrapper is enough for dcc's test harness (which merges stdout+stderr when capturing an emulator's output). Same stdin-echo difference as cpm.exe: doesn't echo redirected/piped input as it's consumed via BDOS console input, so `tscanin`/`tkbd` differ from the ntvcm baseline the same way. `tkbd` specifically (a console-status poll loop) runs dramatically longer than on any other emulator here before it resolves - a behavioral quirk in how cpmemu handles polling on redirected stdin, worth knowing about if you use it for anything console-poll-heavy. One real gap found while chasing wildcard-delete behavior: cpmemu's BDOS 19 (delete) doesn't honor `?` wildcards at all, even for a single-character pattern (`unlink("WA?.TMP")` in `tests/twild.c` fails there the same way it used to fail on ntvcm before that was fixed) - a genuine cpmemu BDOS-conformance gap, not something DCCRTL can work around. |

`tbdos.c`/`tbios.c` in the test suite are written with this in mind: they avoid asserting any emulator-specific value (an exact CP/M version byte, a raw BIOS jump-table address, whether a console character happens to be "pending" under a non-interactive run) and instead check internal consistency - e.g. that dcc's `bdos()`/`bdoshl()`/function-pointer call paths all agree with each other - which is genuinely portable across every emulator above.

Chasing z88dk/cpm's BDOS-35-without-open crash down turned up one genuine (if minor) DCCRTL bug, since fixed: `tmpfile()` registers its file for cleanup at `_exit`, but explicitly `fclose()`-ing it beforehand didn't cancel that registration, so `_exit` would close (and unlink) it a second time - harmless on every emulator that tolerates a redundant close, but a hard crash on one that doesn't. `_close` now clears the pending-cleanup slot itself when it closes a file that's still registered, so no double-close/double-unlink happens regardless of the underlying emulator's tolerance for one.

A follow-up pass across `tests/tappend.c`, `tdrive.c`, `tctrlz.c`, `tlongfn.c`, `twild.c`, `trenamex.c`, `tsparse.c`, and `tpadread.c` pinned down three more DCCRTL bugs, all since fixed, plus one documented (intentionally unfixed) limitation and one cross-emulator behavioral split that turned out not to be a bug at all:

  - **`fopen(path, "a")` destroyed existing content instead of appending.** `_open` took any `O_CREAT` flag - which `"a"` mode maps to, same as `"w"` - straight to the unconditional delete-then-create path (BDOS 19 then 22), with no check for "does this file already exist and should its content be kept." Fixed: `O_CREAT` without `O_TRUNC` now opens the existing file first if there is one, reads its length, and positions the stream past the real end of the existing data (via a new `__apseek` helper - see the `tpadread.c` finding below for why that positioning has to trim trailing padding) before the first write; `O_TRUNC` (from `"w"`) still always deletes-and-recreates. Verified identical, correct behavior on every tested emulator: ntvcm, tnylpo, cpmemu, zxcc, z88dk's cpm, RunCPM, and cpm.exe. (iz-cpm still shows its own unrelated pre-existing "file reads back as 0 bytes on reopen" bug, noted above.)
  - **`"X:filename"` drive-letter prefixes corrupted the filename instead of setting the FCB drive byte.** `__mkfcb` copied the whole string - including the `X:` - straight into the 8.3 name/ext fields. Fixed: it now recognizes a valid `A`-`P` drive letter followed by `:` at the start of the path, writes the corresponding `1..16` into the FCB's drive byte, and advances past the prefix before the normal name/ext copy runs. Verified across the same emulator set; zxcc is the one outlier, but in the other direction - it rejects an *explicit* same-drive (`A:`, matching the file's actual drive) FCB outright even though the file exists, a zxcc-specific BDOS strictness quirk rather than a DCCRTL defect, since the FCB drive byte DCCRTL now produces is standard and correct.
  - **A second `"a"`-mode append landed at the wrong position.** Once the first bug above was fixed, a second append-open still needed its own correct end-of-file positioning rather than assuming the stream state from the first append carried over; folded into the same `__apseek` fix.
  - **`fread()`/`ftell()` disagree on file length by design, not by bug (documented, not fixed).** A partial-record write into virgin territory pads the untouched rest of that 128-byte record with Ctrl-Z (0x1A), since CP/M has no concept of a partial record and there's no real "old data" to merge with. That padding is genuine, readable, on-disk data - `fread()` isn't bounded by the C-level tracked length (`__fdlen`, what `ftell()` reports), so a big-enough `fread()` on a short file returns the padding right along with the real bytes. An early attempt to fix this by trimming `__fdlen` itself (scanning the last record for trailing 0x1A, same idea used for the `"a"`-mode fix above) broke two other, established tests when run through the full regression suite: `fileops.c`, whose own `cpm_filelen()` helper already does this same scan *on top of* the untrimmed length - trimming `__fdlen` silently ate a trailing `^Z` byte that program had deliberately written as real data, since it's indistinguishable on disk from unwritten padding - and `tbig.c`, an exactly-8MB (65536-record) file, where the "record count fits in 16 bits" assumption used to detect an empty file wrapped to 0 and made the file read back as empty. The fix was narrowed to only affect `"a"`-mode's append-start position (which never touches the shared `__fdlen`), and this remaining `fread()`/`ftell()` mismatch is now documented behavior - see `tests/tpadread.c`. Confirmed identical across every tested emulator, so this is a genuine CP/M/DCCRTL characteristic, not an emulator-specific bug.
  - **ntvcm's BDOS 19 (delete) silently failed on any wildcard FCB.** It handed the pattern straight to the host's own `unlink()`, which only ever does a literal byte-for-byte match, so a `?`-containing name (e.g. `"WA?.TMP"`) never matched anything and delete failed outright - every other tested emulator (tnylpo, zxcc, iz-cpm, z88dk's cpm, RunCPM, cpm.exe) correctly deletes every directory entry the ambiguous FCB matches, per the CP/M 2.2 BDOS spec. Fixed in ntvcm by reusing its existing per-platform wildcard find-first/find-next machinery (already used by BDOS 17/18 search) for delete too. See `tests/twild.c`.
  - **Rename onto an existing destination name is a real, cross-emulator behavioral split - and neither camp is hardware-faithful, because neither *can* be.** Real CP/M 2.2 BDOS (confirmed against its own BDOS.ASM source) doesn't check whether the destination name already exists at all; it just overwrites the matched entry's name bytes, leaving two directory entries that share one name on real hardware - genuinely undefined which one a later `open()` finds. No emulator that maps CP/M files onto real host files can represent that: POSIX and Windows filesystems both refuse two entries with one name, so every emulator is forced to collapse this into a single winner. ntvcm, cpmemu, zxcc, z88dk's cpm, and RunCPM overwrite the destination; tnylpo and cpm.exe reject the rename instead (nonzero return, old name and content untouched). ntvcm now makes that choice explicitly and identically on every host it runs on; it used to just call the host C library's `rename()` directly, which meant the exact same ntvcm source silently overwrote on Linux/macOS (POSIX `rename()`'s semantics) but rejected on Windows (the CRT's `rename()` fails if the destination exists) - the same binary, two different outcomes, purely as an accident of which OS it was built for. If your app cares which way this goes on whichever emulator it targets, check the return value and/or delete the destination first. See `tests/trenamex.c`.

A second pass, cross-checked against [the CP/M 2.2 BDOS source](https://github.com/brouhaha/cpm22) and [the documented Interface Guide](https://www.seasip.info/Cpm/bdos.html) rather than emulator behavior alone, looked at wildcard handling across every other DCCRTL file-I/O entry point beyond delete (`tests/tstar.c`, `tdirpat.c`, `tmakewc.c`, `tfopenw.c`, `trenwild.c`). The official spec is narrower than it might look: **only BDOS 17/18 (search first/next) and 19 (delete) are documented as supporting `?` wildcards at all** - open (15), make (22), and rename (23) are silent on the subject, so anything they do with an ambiguous FCB is implementation-defined. That distinction drove two more DCCRTL fixes and ruled out a couple of things that looked like bugs but weren't:

  - **`opendir()`/`readdir()` ignored the caller's pattern entirely.** `_dopn` parsed an optional `"X:"` drive prefix but then unconditionally filled the whole 11-byte name/ext field with `?`, so `opendir("*.C")`, `opendir("T?.TMP")`, and even `opendir("SPECIFIC.TXT")` all behaved exactly like `opendir("*.*")` - full, unfiltered drive enumeration, with no way to actually filter. Fixed: `_dopn` now runs the caller's pattern through `__mkfcb` (the same parser `unlink()`/`rename()` already use), falling back to full enumeration only when nothing was actually specified (empty string, `"."`, or a bare drive prefix). See `tests/tdirpat.c`.
  - **A literal `*` was never a real wildcard, only `?` is.** CP/M's BDOS only recognizes `?`; the shell-glob-style `*` convenience is implemented by the CCP's own command-line parser (and by virtually every historic CP/M C runtime), which expands `*` to fill the rest of the field with `?` before ever building the FCB. `__mkfcb` didn't do this - a literal `*` byte reached BDOS unchanged, and since no real filename ever contains one, `unlink("SA*.TMP")` silently matched nothing (confirmed identically on ntvcm, tnylpo, and cpmemu). cpm.exe was the one outlier: it actually deleted the files, almost certainly by resolving the pattern through a host Windows API that natively globs `*` - meaning code relying on it would have silently worked on exactly one platform. Fixed by adding the same `*`-expansion `__mkfcb` performs for `?`, making the behavior consistent everywhere DCCRTL runs rather than depending on the host's own glob support. See `tests/tstar.c`. (While chasing this down, testing directly against real BDOS behavior turned up that cpmemu doesn't implement wildcard delete *at all*, not even for a single `?` - see the cpmemu row above; unrelated to this fix and not something DCCRTL can work around.)
  - **`fopen(path, "w")` with a wildcard name silently created a real, permanently-stuck file.** BDOS 22 (make) never validates the FCB - it just copies it verbatim into an empty directory slot (confirmed directly against the BDOS source) - so `fopen("MK?.TMP", "w")` created a genuine on-disk file literally named `MK?.TMP` wherever the create was allowed through (ntvcm, cpmemu). Once created, such a file becomes permanently unreachable through portable C code: `unlink()`/`opendir()` correctly treat `?` as a pattern character, not a literal one, so no wildcard-aware scan will ever match it again. Fixed at the DCCRTL level: `fopen()`/`open()` now reject a `?` or `*` in the parsed name/ext outright, in client code, before any BDOS call happens at all, whenever the call could create a new entry (`O_CREAT` or `O_TRUNC`, i.e. `"w"` or `"a"`). This makes the result **100% consistent across every tested emulator** - unlike the read-mode case below, the rejection never depends on what a given BDOS implementation does with an ambiguous FCB. See `tests/tmakewc.c`.
  - **Open (BDOS 15) with an ambiguous FCB is a genuine 3-way split, left as documented, unfixed behavior.** The BDOS source shows `open` incidentally reuses the very same directory-search primitive `delete` uses, with no check rejecting an ambiguous FCB - an implementation accident, never a documented guarantee (the Interface Guide is silent on open supporting wildcards at all). Confirmed: ntvcm, cpmemu, and zxcc reject an ambiguous open (`NULL`); tnylpo and cpm.exe silently open whatever the first matching directory entry happens to be. Neither side is "wrong" since the spec never promises either outcome, so this is deliberately left undocumented-but-observed rather than changed to match one camp - doing so would just be guessing which accident callers should be able to rely on. See `tests/tfopenw.c`.
  - **Rename with an ambiguous FCB stays correctly rejected everywhere, confirmed against the source, not just emulator agreement.** The raw BDOS source technically loops over ambiguous old-name matches for rename (reusing the same primitive as delete) - but there's no "template" substitution: it just copies the new name's bytes verbatim into every match, `?` and all, which would produce garbage directory entries rather than anything sensible. None of that raw-source permissiveness survives in any tested emulator: ntvcm, tnylpo, cpmemu, zxcc, and cpm.exe all reject an ambiguous rename outright, fully consistent with the documented (lack of) support - correct, expected, not a bug. See `tests/trenwild.c`.

## M80 and L80

m80.com and l80.com are part of the M80 Assembler product from Microsoft. I didn't write them. They are included in this repo to ease development, but they can be found in dozens of locations on the internet.

By default, dccmake/ma.sh/ma.ps1 assemble with `m80c`, a from-scratch, conservative
clone of M80 (see `src/m80c/m80c.c`) that runs natively on the host instead of
under CP/M emulation - no ntvcm involved for that step. L80 is still real M80
Assembler-product software and still runs under ntvcm, since only the assembler
was reimplemented. Pass `dcc-use-emulated-m80=true` to dccmake, `-femulated-m80`
directly to dccmake, `--emulated-m80`/`-EmulatedM80` to ma.sh/ma.ps1, or
`-UseEmulatedM80` to runall.ps1/runall-extended.ps1, to assemble with the real
M80.COM under ntvcm instead (e.g. to cross-check output, or if m80c hasn't been
built locally).

## Memory layout

Memory layout is what you would expect; CP/M loads .COM files in just one way. BSS begins just after the loaded image. The app assumes sp is set to the highest free byte by the loader. dcc sets a default stack size of 512 bytes but you can use the -stack argument to change that. dcc will quietly increase your stack size if it detects large frames. See ma.bat for an example. The heap used by malloc() uses RAM between the end of BSS and the bottom of the stack. If you need to adjust the heap and stack sizes you can change dcc's -stack argument to slide the barrier. There are no runtime checks that prevent the stack from smashing the heap. You can implement your own stack checks if you want; see spsmash.c for an example of how to do this.

## Benchmarks

I ran a subset of the test apps to measure performance of the compiler relative to other compilers. Most of the compilers in the table below are era-appropriate, from the 1970's and 1980's. The ZCC/Z88DK compilers are from 2025. I chose the best CP/M compilers I could find for the comparison. My repo [cpm_compilers](https://github.com/davidly/cpm_compilers) has a more complete set along with runtimes for some of these performance benchmarks.

Generally, dcc compares very well with all other compilers that target CP/M, especially when the dccpeep optimizer is used. Even when the optimizer isn't used dcc only loses a few benchmarks. My assembly implementations of some of the benchmarks (asmsieve.mac, asme.mac, asmttt.mac) still beat all compilers. Binary size is also competitive with other compilers. It's not always best but it's always close. Compared with Draco and Modula-2 dcc's binary size is sometimes larger generally due to the size of printf(). Avoid printf() if you want smaller binaries. ZCC is very good at code generation but occasionally hard to work with and it puts BSS in generated .COM files. 

### The benchmarks:

  - sieve.c: This is the classic from BYTE magazine in 1983. It measures loop and array performance.
  - e.c: This computes the first 192 digits of e. It measures integer division and mod operations as well as loop and array performance.
  - tm.c: Test Malloc. This is C-only and measures performance of the allocator, memset, and scanning an arrary for an expected value. Many of the C compilers for CP/M can't run it because they don't have an allocator or don't implement free().
  - ttt.c: Proves you can't win at tic-tac-toe if the opponent is competent. Tests function call performance as well as loop and array performance. Always remember it took WOPR 72 seconds to solve this problem in the 1983 movie War Games. A 2Mhz 8080 in 1974 could solve this in less than 3 seconds. Movie magic.
  - pihex.c: Computes PI in base 16. This is C-only and some of the compilers can't build or run it due to a variety of bugs. It measures unsigned long mod and floating point performance. I spent 90 minutes trying to get the two forms of ZCC to build and run it, ran into many compiler and C runtime bugs, and gave up. HiSoft v4.11 has a C runtime bug where if you cast 3.963512 to an int it gives you 4. After I worked around that and other bugs, code from that compiler ran really well -- faster than dcc.
  - mm.c: Another BYTE magazine classic from October 1982. Measures floating point initialization, addition, and multiplication performance.
  - tstring.c: Measures performance of strlen, strchr, strrchr, strstr, memcmp, memcpy, memset, memchr, rand, and integer modulus. Most compilers don't implement all of these and need them supplied.
  - tbig.c: Test sequential and random file i/o on the biggest file size CP/M 2.2 supports: 8MB. 

Benchmark times are in milliseconds on a 4Mhz Z80. CP/M file sizes are rounded up to the next multiple of 128 bytes due to how the file system works.

<img alt="table" src="images/table.jpg" />

## Notes

I built the compiler using AI. I wanted to use Claude and ChatGPT on something reasonably complicated. I used each about equally and found them both to be extemely helpful and infurriating at the same time. They are lazy, forgetful, brilliant, fast, insightful, and seemingly willfully ignorant. They remind me of the hundreds of people I worked with over the years. They wrote about half the test cases, Google wrote a few, and I had the rest from other projects. It took me hundreds of prompts to get the compiler this far along. I had to drive the architecture. I also had to do a bunch of the debugging when they got stuck. asme.mac, asmsieve.mac, and asmttt.mac are my assembly versions of the benchmarks. They proved useful in prompts to get the AIs to optimize their generated code.

Why dcc? All compilers from that era were K&R since the first ANSI C standard was C89 (1989). I wanted a compiler with modern syntax for CP/M. I was also curious how hard it would be to generate better code than the older compilers. Turns out it's generally straightforward. It's easier than ever to code for old machines, and I think that's pretty cool.

Compiler writers generally avoid adding optimizations for specific apps; that's long been considered "cheating" by those who run benchmarks. In this case, I encourage you to "cheat" for your app. Point Claude at your source code and dcc's soucrce code and tell it to make dcc optimize code generation for your app (size or speed). It's the future.

## Building dcc and ntvcm from source

Both dcc and ntvcm are self-contained projects that can be built independently. You'll need them to compile and run CP/M apps.

### Cloning the repositories

```bash
# Clone dcc compiler
git clone https://github.com/davidly/dcc.git
cd dcc

# Clone ntvcm emulator (in a parallel directory, or wherever you prefer)
cd ..
git clone https://github.com/davidly/ntvcm.git
```

### Building dcc

dcc compiles on Windows, Linux, and macOS. The build scripts are in the root directory:

**macOS:**
```bash
chmod +x mmacos.sh
./mmacos.sh
```
This produces `dcc`, `dccpeep`, `dccrtlstrip`, `dccmake`, and `m80c` in the dcc directory.
Requires the clang compiler from the Xcode Command Line Tools (install with `xcode-select --install`).

**Linux:**
```bash
chmod +x m.sh
./m.sh
```
Requires gcc (install with `sudo apt install build-essential` on Debian/Ubuntu, or the equivalent for your distribution).

**Windows:**
```batch
m.bat
```
Requires Visual Studio with C++ build tools installed.

### Building ntvcm

ntvcm is a C++ project that compiles on Windows, Linux, and macOS.

**macOS:**
```bash
cd ntvcm
chmod +x mmac.sh
./mmac.sh
```
This produces the `ntvcm` executable in the ntvcm directory.
Requires the clang/g++ compiler from the Xcode Command Line Tools (install with `xcode-select --install`).

**Linux:**
```bash
cd ntvcm
chmod +x m.sh
./m.sh
```
Requires g++ (install with `sudo apt install build-essential` on Debian/Ubuntu, or the equivalent for your distribution).

**Windows:**
Check the ntvcm repository for Windows build instructions (typically via Visual Studio or a batch script).

Once both projects are built, set up your environment as shown in the next section so the build scripts can find the binaries.

## Setting up your environment

The build scripts (`ma.sh`, `ma.bat`, `runall.sh`, `runall.bat`) resolve each
tool the same way: they use an environment variable if you set one, otherwise
they look for the tool on your `PATH`. The relevant tools are `dcc`, `dccpeep`,
`dccrtlstrip`, `m80c`, `ntvcm`, and the `l80` linker (and `m80`, only if you
pass `-UseEmulatedM80`/`dcc-use-emulated-m80=true` to assemble with the real
M80.COM instead of native `m80c`).

The simplest setup, especially when building C apps in a project *outside* the
dcc repo, is to add the directories containing the built `dcc` and `ntvcm`
binaries to your `PATH`. Then no per-tool environment variables are needed.

### macOS / Linux

Add this to your shell profile (e.g., `~/.zshrc`, `~/.bash_profile`, or
`~/.bashrc`), or run it in the terminal before invoking the scripts:

```bash
# Add the directories that contain the built dcc and ntvcm binaries to PATH.
# dcc's directory also provides dccpeep, dccrtlstrip, m80c, m80.com, l80.com, and DCCRTL.MAC.
export PATH="$PATH:/path/to/dcc:/path/to/ntvcm"
```

Replace `/path/to/dcc` and `/path/to/ntvcm` with the actual directories
(e.g., `~/GitHub/dcc` and `~/GitHub/ntvcm`). With this on your `PATH`, the
scripts find `dcc`, `dccpeep`, `dccrtlstrip`, and `ntvcm` automatically — you do
**not** need to set `DCC`, `DCCPEEP`, or `DCCRTLSTRIP`.

If you instead want to pin specific binaries (for example, when juggling
multiple dcc builds), set the env vars to explicit paths and only put ntvcm on
`PATH`:

```bash
export PATH="$PATH:/path/to/ntvcm"
export DCC=/path/to/dcc/dcc
export DCCPEEP=/path/to/dcc/dccpeep
export DCCRTLSTRIP=/path/to/dcc/dccrtlstrip
```

### Windows (native)

Both dcc and ntvcm compile to native Windows executables. Add their directories
to `PATH` (via System Properties → Environment Variables for a permanent
setting, or temporarily in your shell):

**PowerShell:**
```powershell
$env:PATH += ";C:\path\to\dcc;C:\path\to\ntvcm"
```

**CMD:**
```batch
set PATH=%PATH%;C:\path\to\dcc;C:\path\to\ntvcm
```

As with macOS/Linux, putting the dcc directory on `PATH` means `dcc`,
`dccpeep`, and `dccrtlstrip` are found automatically; the `DCC`/`DCCPEEP`/
`DCCRTLSTRIP` variables are only needed if you want to pin specific binaries.
Then use `ma.bat` and `runall.bat` to build and test your apps.
