# Skills

This folder holds **project-scoped** agent skills that ship with the dcc repo
(`dcc-cpm-z80/` for writing CP/M apps with dcc, `dcc-project/` for developing
the dcc toolchain itself, and `mkdocs-stdlib-docs/` for maintaining generated
standard-library reference pages). Any clone of this repo picks them up
automatically when opened in an agent that supports skills (e.g. GitHub Copilot
in VS Code).

A skill is just a folder containing a `SKILL.md` (plus optional `references/`,
`scripts/`, `assets/`). The folder name must match the `name:` in `SKILL.md`'s
front matter, and `SKILL.md` must sit at the folder's root.

## Make a skill available in *all* your projects

The copy in this repo only applies when you're working inside this repo. To use
a skill from every workspace on your machine, copy the skill folder into a
**personal** skills root in your home directory. Any of these roots work; pick
one and stay consistent (this machine uses `.agents`):

| Scope    | Location (`~` / `%USERPROFILE%` = your home directory) |
| -------- | ------------------------------------------------------ |
| Personal | `~/.agents/skills/<name>/`                             |
| Personal | `~/.copilot/skills/<name>/`                            |
| Personal | `~/.claude/skills/<name>/`                             |
| Project  | `<repo>/.github/skills/<name>/` (this folder)          |
| Project  | `<repo>/.agents/skills/<name>/`                        |
| Project  | `<repo>/.claude/skills/<name>/`                        |

### macOS

```sh
mkdir -p ~/.agents/skills
cp -R .github/skills/dcc-cpm-z80 ~/.agents/skills/
```

Resulting path: `/Users/<you>/.agents/skills/dcc-cpm-z80/`

### Linux

```sh
mkdir -p ~/.agents/skills
cp -R .github/skills/dcc-cpm-z80 ~/.agents/skills/
```

Resulting path: `/home/<you>/.agents/skills/dcc-cpm-z80/`

### Windows (PowerShell)

```powershell
New-Item -ItemType Directory -Force "$env:USERPROFILE\.agents\skills" | Out-Null
Copy-Item -Recurse ".github\skills\dcc-cpm-z80" "$env:USERPROFILE\.agents\skills\"
```

Resulting path: `C:\Users\<you>\.agents\skills\dcc-cpm-z80\`

## Keeping copies in sync

The repo copy and a personal copy are independent files. After editing one,
copy it over the other so they don't drift. From the repo root:

```sh
# macOS / Linux: push the repo copy to the personal copy
cp -R .github/skills/dcc-cpm-z80/. ~/.agents/skills/dcc-cpm-z80/
diff -rq .github/skills/dcc-cpm-z80 ~/.agents/skills/dcc-cpm-z80 && echo "in sync"
```

If you only need a skill in one place, keep just that copy — fewer copies, less
drift.
