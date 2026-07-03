# dcc
[![CI](https://github.com/davidly/dcc/actions/workflows/ci.yml/badge.svg)](https://github.com/davidly/dcc/actions/workflows/ci.yml)

C compiler targeting CP/M 2.2 on a Z80

The [dcc documentation](https://davidly.github.io/dcc/) covers all features, usage, and API reference.

## What dcc is
dcc has a C89 core plus target-appropriate C99/C11 front-end support. For every source file it accepts, dcc generates a .MAC assembly file that can be assembled by M80 and linked by L80 to produce CP/M .COM files.

A separate app dccpeep.c is a peephole optimizer that rewrites portions of .MAC files so apps run faster. It's not necessary to use dccpeep; apps will work just fine without it. But if you need your app to be both smaller and faster it's worth running.

DCCRTL.MAC is the dcc C Runtime Library. It's written in Z80 assembly for size and performance. It has the entrypoint start for apps that initializes the heap (for malloc/free) and command-line arguments so main's argc and argv work. It implements a small subset of the C89 C runtime including floating point.

dccrtlstrip.c is an app that examines the code of your .c file and strips portions of the DCCRTL.MAC C runtime so only the parts needed are linked into the .COM file. It's not necessary to run this program for your app to work. But the resulting .COM file may be smaller if you do.

The 3 compiler apps dcc, dccpeep, and dccrtlstrip all build and run on Windows, Linux, and MacOS. They are too big to run on CP/M. Use m.bat, m.sh, mmacos.sh to build these apps using msvc (Windows), gcc (Linux), or clang (MacOS) respectively. You may need to chmod 777 *.sh on Linux and MacOS prior to running dcc's scripts.

## Documentation

Two reference documents in the [docs](docs) directory cover the runtime in depth:

  - [docs/dcc-c89-reference-guide.md](docs/dcc-c89-reference-guide.md): a practical guide to the C89 language features dcc accepts and the C runtime library implemented in DCCRTL.MAC. It documents type sizes and conventions, the recognized keywords and operators, every standard-header function that is actually linkable (stdio, stdlib, string, ctype, math, setjmp, stdarg, and the CP/M extensions), the supported printf/scanf conversions, and the limitations to keep in mind (no double, 16-bit int, integer-only `%`, etc.). Start here to learn what you can call and how.
  - [docs/dccrtlstrip-inclusion-table.md](docs/dccrtlstrip-inclusion-table.md): an internals reference explaining how dccrtlstrip decides which blocks of DCCRTL.MAC are linked into a program. It maps each C-level construct to the runtime block it pulls in and gives the transitive dependency closures and the marginal .COM size cost of each function. Use it when optimizing binary size or to understand exactly what a given call drags into the link.

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

I use my [ntvcm](https://github.com/davidly/ntvcm) CP/M 2.2 emulator to run m80.com, l80.com, and apps built with dcc. The widely-used CPM emulator works equally well; all tests build and pass with that emulator. I haven't run other emulators but I suspect they'll all just work. The compiler and runtime don't push emulator compatibility limits. Only CP/M 2.2 bdos functions are used (and no BIOS functions). One exception: app exit codes (returned from main() or passed to exit()) are set using CP/M 3.0 BDOS call 108. Some emulators like ntvcm reflect that value in their exit code.

## M80 and L80

m80.com and l80.com are part of the M80 Assembler product from Microsoft. I didn't write them. They are included in this repo to ease development, but they can be found in dozens of locations on the internet.

## C89+ language 

The compiler accepts some syntax from later C standards including declaring variables where you like and initializing them with complex expressions. Only 4-byte floats are supported; 8-byte doubles are not. I'm certain more arcane C89 expressions/features aren't implemented (yet), but the test cases have pretty good coverage. Only a small subset of the C runtime is implemented in DCCRTL.MAC, but the samples implement a bunch more that you can copy/paste where needed. The register, volatile, and const keywords are ignored aside from constant folding for const variables.

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
This produces `dcc`, `dccpeep`, and `dccrtlstrip` in the dcc directory.
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
`dccrtlstrip`, `ntvcm`, and the `m80`/`l80` assembler/linker.

The simplest setup, especially when building C apps in a project *outside* the
dcc repo, is to add the directories containing the built `dcc` and `ntvcm`
binaries to your `PATH`. Then no per-tool environment variables are needed.

### macOS / Linux

Add this to your shell profile (e.g., `~/.zshrc`, `~/.bash_profile`, or
`~/.bashrc`), or run it in the terminal before invoking the scripts:

```bash
# Add the directories that contain the built dcc and ntvcm binaries to PATH.
# dcc's directory also provides dccpeep, dccrtlstrip, m80.com, l80.com, and DCCRTL.MAC.
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
