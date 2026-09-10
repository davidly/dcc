# MIR clobber campaigns and execution inventory

`run-mir-clobber-tests.ps1` loads every `*.json` file in this directory, in
ordinal filename order. Files contain **data only**: a JSON array of case
objects. Existing built-in cases, aliases, generated cases, and special
forced-selector/VLA checks remain enabled by default.

```json
[
  {
    "Name": "example-control",
    "Group": "example",
    "Sources": ["tests/mir-clobber/example.c"],
    "Defines": [],
    "Expected": ["example passed"],
    "Exit": 0,
    "StackModes": [true, false],
    "DebugModes": ["lines"]
  }
]
```

`Name`, `Sources`, `Expected`, and `Exit` are required. Names must be unique,
including built-in cases, and use letters, digits, dots, underscores, or
hyphens (beginning with a letter or digit). Optional `Group` is an explicit
alias; new campaigns are never added to historical groups by name prefix.
Groups cannot collide with case names.

`Sources` and optional `FixturePaths` are arrays of existing repo-relative
file paths; absolute paths and paths escaping the repository are rejected.
Other optional properties retain their existing runner meanings:

- `Defines`, `Args`, `AssemblyPatterns`, `ForbiddenAssemblyPatterns`: string arrays.
- `ExactTemplate`, `ExactFunction`, `RequireExact`, `RequireRejected`.
- `RequiredGenericFunction`, `RequiredSelectorFunction`, `RequiredSelector`,
  `RequiredCandidate`.
- `MachineMutation`, `MachineMutationFunction`, `OddUpperRuntime`.
- `StackBytes` (positive integer, default 512), `StackModes` (distinct booleans,
  default `[true, false]`).
- `DebugModes` (distinct `"true"`/`"lines"` strings): additional configurations
  alongside the normal non-debug build.

Both peep modes always run. All JSON case assertions, arguments, fixtures,
mutations, and stack settings also apply to the additional debug configurations
(built-in cases retain their existing debug contracts). Unknown
properties and invalid types fail closed rather than silently dropping checks.

## Runner interfaces

```sh
# List exact target leaves without invoking any compiler, emulator, or generator:
pwsh scripts/run-mir-clobber-tests.ps1 -ListExecutions build/clobber-expected.json

# Serial compatibility is the default; run bounded independent pwsh processes:
pwsh scripts/run-mir-clobber-tests.ps1 -Jobs 8 \
  -ExecutionManifest build/clobber-executed.json

# Reproducible zero-based external shard (do not combine with Jobs > 1):
pwsh scripts/run-mir-clobber-tests.ps1 -Cases inlines,fatal \
  -ShardIndex 1 -ShardCount 4 -ExecutionManifest build/clobber-shard-1.json
```

Both manifest options write JSON arrays of ordinally sorted
`name|stack-peep[-debug-mode]` keys (including empty/singleton arrays). Relative
manifest paths are relative to the repository. `-ListExecutions` honors case
selection and shard parameters, and exits without execution.

Shards select ordinally sorted leaf indices modulo `ShardCount`. `-Jobs` defaults
to 1 and permits 1–256 processes. Each child owns its process environment and
unique repository-local build/fixture directory; no concurrent runspaces modify
shared environment variables. Parser and semantic-proof setup runs only on
shard zero. Generated sources are created only by shards that need them.
When `LLVM_PROFILE_FILE` is inherited, each child adds a parent-PID/shard suffix
to its filename, preserving the directory, extension, and LLVM merge pattern.
Profiles stay directly under the inherited raw directory for `*.profraw` merging.

Every completed shard must match its exact expected keys. The parent also
checks the exact union: duplicate, missing, unexpected, malformed, or failed
shard results fail the run. Successful execution manifests are published only
after verification. Failure artifacts, including parent shard logs, are retained
under `build/mir-clobber-failure-*`.

Fast harness validation (no compiler or emulator needed):

```sh
python3 -m unittest discover -s scripts/tests -p test_mir_clobber_runner.py
```
