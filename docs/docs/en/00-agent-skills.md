# Agentic skills

The DCC C Compiler repo ships **agentic skills** under `.github/skills/`.
A skill is a folder containing a `SKILL.md` and optional references.

| Skill | Use it for |
| --- | --- |
| `dcc-cpm-z80` | Writing, building, running, and debugging CP/M programs with DCC, including its language and runtime conventions. |
| `dcc-fuzz-testing` | Choosing and running the clang oracle, typed expression, runtime library, or syntax crash fuzzer. It also covers reproducing findings and verifying fixes. See [Fuzz testing](appendix/06-differential-testing.md). |

An agent that supports skills reads `SKILL.md` on demand when your task matches
the skill's description, so it gets guidance for the DCC C Compiler without you pasting it
into every prompt.

## Invoking a skill in VS Code

With GitHub Copilot in VS Code (agent mode), the agent discovers these skills
when you open the DCC C Compiler repo and loads the skill that matches your
request. You can also request a skill explicitly.

> use the dcc-cpm-z80 skill to build and test foo.c

> use the dcc-fuzz-testing skill to choose a driver for a runtime library campaign

## Using skills from the GitHub Copilot CLI

The GitHub Copilot CLI discovers project skills from the repo you launch it in,
plus any personal skills in your home-directory roots. From the repo root, start
a session:

    copilot

Then ask something within the skill's scope, for example: "build and run sieve.c
with the DCC C Compiler".

## Making `dcc-cpm-z80` available system-wide

The copy in the DCC C Compiler repo only applies while you work inside that repo. To use
the skill from other projects, copy it to a personal skills root. The DCC C Compiler tools
and `DCCRTL.MAC` must still be on your `PATH`; see
[Setting up the toolchain](00-setup-toolchain.md).

Copy the skill folder into one personal skills root, for example
`~/.agents/skills/`:

=== "Windows"

    New-Item -ItemType Directory -Force "$env:USERPROFILE\.agents\skills" | Out-Null
    Copy-Item -Recurse ".github\skills\dcc-cpm-z80" "$env:USERPROFILE\.agents\skills\"

=== "macOS"

    mkdir -p ~/.agents/skills
    cp -R .github/skills/dcc-cpm-z80 ~/.agents/skills/

=== "Ubuntu (x64 and ARM64)"

    mkdir -p ~/.agents/skills
    cp -R .github/skills/dcc-cpm-z80 ~/.agents/skills/

=== "Windows ARM64"

    New-Item -ItemType Directory -Force "$env:USERPROFILE\.agents\skills" | Out-Null
    Copy-Item -Recurse ".github\skills\dcc-cpm-z80" "$env:USERPROFILE\.agents\skills\"

The repo copy and the personal copy are independent files, so re-copy after
editing either one to keep them in sync.
