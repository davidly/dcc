# VS Code debugging

DCC source debugging is available in Visual Studio Code through the Microsoft
C/C++ extension and ntvcm's GDB/MI interface. A debug build produces a CP/M
`.COM` program and a matching `.DBG` metadata file. ntvcm runs the program and
presents the source, stack, variables, memory, and Z80 instructions to VS Code.

For the most accurate source-level experience, use the default no-peep debug
build. Optimized debug builds are also available for defects that reproduce
only after peephole optimization, with the limitations described below.

![DCC source debugging in VS Code with a breakpoint, local variables, Z80 registers, call stack, and debug controls](images/source-debugging.png)

## Prerequisites

Before configuring a project:

1. [Build and configure the DCC toolchain](00-setup-toolchain.md), including a
   current ntvcm with GDB/MI support.
2. Install the [Microsoft C/C++ extensions for VS Code](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpp-devtools){: target="_blank" }.
3. Make `dccmake` available on `PATH`, or use its absolute path in the build
   task.
4. Make sure `dccmake` can find `dcc`, `dccpeep`, `dccrtlstrip`, `m80c`,
   `DCCRTL.MAC`, and ntvcm as described on [The toolchain](00-setup-toolchain.md)
   page.

Debug metadata requires the native `m80c` assembler. Do not set
`dcc-use-emulated-m80=true` for a debug build.

## Add the build task

Create `.vscode/tasks.json` in the project. This task builds the C file active
in the editor as `build/DCCDEBUG.COM` and writes the matching
`build/DCCDEBUG.DBG` file:

```json
{
  "version": "2.0.0",
  "tasks": [
    {
      "label": "Build active DCC C file for debugging",
      "type": "shell",
      "command": "dccmake",
      "args": [
        "-g",
        "${file}",
        "dcc-output=DCCDEBUG"
      ],
      "options": {
        "cwd": "${workspaceFolder}"
      },
      "problemMatcher": []
    }
  ]
}
```

Replace `dccmake` with an absolute path if it is not on `PATH`. For a program
with several translation units, replace `${file}` with the source files or a
single setting such as:

```json
"dcc-input=main.c,module.c"
```

The `-g` option selects a debug build. Although `dccmake` normally enables
`dccpeep`, it automatically skips the peephole optimizer for `-g` builds so
source locations remain intact.

## Add the launch configuration

Create `.vscode/launch.json` with the following configuration. Change
`miDebuggerPath` to the ntvcm executable on the host system. On Windows, the
path normally ends in `ntvcm.exe`.

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "DCC CP/M: Debug active C file",
      "type": "cppdbg",
      "request": "launch",
      "preLaunchTask": "Build active DCC C file for debugging",
      "program": "${workspaceFolder}/build/DCCDEBUG.COM",
      "cwd": "${workspaceFolder}/build",
      "MIMode": "gdb",
      "miDebuggerPath": "/path/to/ntvcm/ntvcm",
      "miDebuggerArgs": "--interpreter=mi",
      "targetArchitecture": "x86",
      "sourceFileMap": {
        "tests": "${workspaceFolder}/tests"
      },
      "customLaunchSetupCommands": [
        {
          "text": "-file-exec-and-symbols \"${workspaceFolder}/build/DCCDEBUG.COM\""
        }
      ],
      "launchCompleteCommand": "None",
      "externalConsole": false
    }
  ]
}
```

`targetArchitecture` satisfies the C/C++ debug adapter's launch schema; ntvcm
still executes Z80 code. The `customLaunchSetupCommands` entry tells ntvcm to
load the CP/M program and its adjacent `.DBG` metadata. Keep the `.COM` and
`.DBG` files together and give them the same base name.

Adjust `sourceFileMap` when metadata contains source paths that differ from the
workspace layout. Add one entry for each source root that needs remapping, or
remove the property when the recorded paths already resolve correctly.

## Start a debugging session

1. Open the C source file to build.
2. Set breakpoints in the editor gutter.
3. Open **Run and Debug** and select **DCC CP/M: Debug active C file**.
4. Press **F5**. VS Code runs the build task, launches ntvcm, and stops at the
   first breakpoint.
5. Use **Step Over**, **Step Into**, **Step Out**, or **Continue** as with a
   native program.

## Use the disassembly view

While the program is stopped, open the Z80 disassembly in either of these ways:

- open the Command Palette and run **Debug: Open Disassembly View**; or
- right-click the selected frame in **Call Stack** and choose **Open
  Disassembly View**.

The view opens around the current program counter and shows each instruction's
address, machine-code bytes, and decoded Z80 assembly. The current instruction
is highlighted. When debug metadata provides a source location, the view also
associates the instruction with its C source line.

![VS Code debugging a DCC program with the Z80 Disassembly View, CPU registers, call stack, and source breakpoints](images/disassembly-debugging.png)

To step through assembly code one instruction at a time:

1. Click inside the Disassembly View so it is the active editor.
2. Use **Step Over** (**F10**) to execute the highlighted instruction and stop
  at the next instruction in the current call frame. A `call` is executed
  without entering the called function.
3. Use **Step Into** (**F11**) when the highlighted instruction is a `call` and
  you want to stop at the first instruction in the called function. For other
  instructions it advances by one instruction.
4. Use **Step Out** (**Shift+F11**) to run until the current function returns,
  or **Continue** (**F5**) to run to the next breakpoint.

After a jump, call, or return, use **Debug: Open Disassembly View** again if the
current instruction has moved outside the visible range. The view recenters on
the current program counter. Select a source frame in **Call Stack** or open the
corresponding C file to move back to source-level debugging; both views remain
synchronized with the same stopped program.

Instruction stepping is especially useful for:

- checking the exact branch taken by a condition;
- following register and stack changes around calls and returns;
- correlating source expressions with generated Z80 instructions; and
- debugging a `dcc-peep-debug=true` build when optimized instructions no
  longer align exactly with source-line markers.

The Disassembly View reflects the linked `.COM` image that ntvcm is executing,
so addresses and instruction bytes are authoritative for both no-peep and peep
debug builds.

## Debugger coverage

The DCC metadata and ntvcm GDB/MI implementation support the main VS Code C/C++
debugging workflows:

- source breakpoints, temporary breakpoints, conditions, ignore counts, and
  repeated breakpoint hits;
- source-level Step Into, Step Over, Step Out, Continue, and pause;
- instruction-level stepping and Z80 disassembly with instruction bytes;
- call stacks, recursive frames, frame selection, function names, source files,
  and source lines;
- function parameters, local variables, globals, lexical scopes, and shadowed
  names;
- scalar integer types, `_Bool`, pointers, function pointers, arrays,
  variable-length arrays, structures, unions, and bit-fields;
- watch expressions including integer arithmetic, comparisons, casts,
  dereference, address-of, array subscripts, and structure member access;
- memory reads and writes, variable assignment, and register values;
- linked programs built from multiple C source modules; and
- normal program output in the VS Code Debug Console.

The expression evaluator implements a practical target-C subset rather than a
complete C compiler. In particular, floating-point expression evaluation and
function calls from the Watch or Debug Console are not supported. The target is
single-threaded CP/M, so thread-oriented VS Code controls do not represent
multiple application threads.

## No-peep debug builds

No-peep is the default and recommended mode for source debugging. `dccmake -g`
keeps every `;@dcc-line` marker where DCC emitted it, and `m80c` records those
locations in the `.DBG` file before linking relocates them to final addresses.

In this mode, source breakpoints and source stepping have strong coverage across
ordinary declarations and expressions, calls and returns, nested blocks,
`if`/`else`, loops, `break`, `continue`, `goto`, and `switch`. Closing braces
are represented for blocks that fall through, which makes skipped branches and
loop transitions easier to follow. A source line can still contain several
machine instructions, so instruction stepping remains useful when inspecting
the exact Z80 execution sequence.

Use this mode unless the behavior being investigated occurs only in an
optimized binary.

## Peephole-optimized debug builds

To debug a problem that appears only after peephole optimization, add
`dcc-peep-debug=true` to the task arguments:

```json
"args": [
  "-g",
  "${file}",
  "dcc-output=DCCDEBUG",
  "dcc-peep-debug=true"
]
```

This is an explicit opt-in. `dccpeep` works on assembly text and can delete,
combine, or move instructions without moving the associated `;@dcc-line`
markers in the same way. The resulting `.DBG` file can therefore associate an
optimized instruction with a nearby source line rather than its original line.

Typical minor source-level effects include a breakpoint moving to a neighboring
executable line, a source line being skipped, or an extra/repeated source step
around optimized control flow. Variables, call stacks, memory, registers, and
the generated program remain useful, but source-line attribution is less exact.

For an optimized build, treat the Disassembly View and instruction stepping as
authoritative: they always show the instructions ntvcm is actually executing.
Set an initial source breakpoint near the area of interest, then switch to
instruction stepping when source movement no longer matches the optimized
control flow.

To return to strong source-level debugging, remove `dcc-peep-debug=true` and
start a new session. Setting only `dcc-peep=true` does not enable peephole
optimization for a `-g` build.
