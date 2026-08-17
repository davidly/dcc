# System and CP/M services

This page covers low-level file descriptors, directory enumeration, error
reporting, assertions, and the CP/M-specific extensions.

## `unistd.h` / `fcntl.h` — low-level file I/O

These map onto CP/M file operations and operate on small integer file
descriptors. Include `unistd.h` and `fcntl.h`.

| Function | Summary |
| --- | --- |
| `int open(const char *path, int flags, ...)` | Open/create a file, returns a descriptor. |
| `int read(int fd, void *buf, unsigned n)` | Read up to `n` bytes. |
| `int write(int fd, const void *buf, unsigned n)` | Write up to `n` bytes. |
| `int close(int fd)` | Close a descriptor. |
| `long lseek(int fd, long off, int whence)` | Reposition the descriptor. |
| `int unlink(const char *path)` | Delete a file. |
| `int fsync(int fd)` | Flush file data to disk. |
| `int fdatasync(int fd)` | Flush file data to disk. |

`fcntl.h` also declares `open` in K&R form (`int open();`) for compatibility.
`off_t` from `unistd.h` is `long`.

| Constant | Value | Meaning |
| --- | ---: | --- |
| `O_RDONLY` | 0 | open for reading |
| `O_WRONLY` | 1 | open for writing |
| `O_RDWR` | 2 | open for reading and writing |
| `O_CREAT` | 0100 | create the file if needed |
| `O_TRUNC` | 01000 | truncate an existing file |

```c
int fd = open("OUT.BIN", O_WRONLY | O_CREAT | O_TRUNC);
if (fd >= 0) {
    write(fd, data, len);
    close(fd);
}
```

Access modes are enforced: `read()` on an `O_WRONLY` descriptor and `write()`
on an `O_RDONLY` descriptor fail with `EBADF`. `O_CREAT` alone preserves an
existing file and starts at offset zero; `O_TRUNC` truncates only an existing
file unless `O_CREAT` is also present. `O_RDONLY | O_TRUNC` is rejected with
`EINVAL`. A zero-length write never changes the file position or tracked
length.

Because both `int` and the low-level return type are 16-bit signed values, one
`read()` or `write()` reports at most `INT_MAX` (32,767) bytes. A larger request
is performed as a legal short transfer of at most 32,767 bytes; loop until the
requested total is complete. This avoids successful counts becoming
indistinguishable from `-1`.

`lseek()` rejects a negative final position, signed arithmetic overflow, and a
nonzero high byte in `whence`, leaving the old position unchanged. Successful
`lseek()` and `close()` operations discard any pending `ungetc()` pushback.

!!! tip "Single file I/O core"
    `open`/`read`/`write`/`close`/`lseek`/`unlink`/`fsync`/`fdatasync` share one
    FCB/DMA core. The first file call links that core; additional file calls are
    nearly free. See the [appendix](appendix/01-dccrtlstrip.md).

## `dirent.h` — directory enumeration

Include `dirent.h`. CP/M has no subdirectories, so this enumerates files on the
selected drive.

| Function | Summary |
| --- | --- |
| `DIR *opendir(const char *path)` | Begin a scan. `"."`, `"*.*"`, and a bare `"A:"` enumerate every file; a specific pattern (`"*.C"`, `"T?.TMP"`) filters the scan the same way `unlink()` does. |
| `struct dirent *readdir(DIR *dirp)` | Next matching entry, or `NULL` at the end. |
| `int closedir(DIR *dirp)` | End the scan. |

`struct dirent` has a single member, `char d_name[13]`, holding the 8.3 name.
The public API uses POSIX-like names through macros:

| Public name | Runtime entry name |
| --- | --- |
| `opendir` | `dopn` |
| `readdir` | `drd` |
| `closedir` | `dcls` |

Call the public names. The short runtime names exist to avoid
external-symbol collisions on the M80/L80 toolchain.

```c
DIR *d = opendir("*.*");
struct dirent *e;

while ((e = readdir(d)) != NULL)
    puts(e->d_name);
closedir(d);
```

## Standard diagnostics and errors

The standard-library reference now has dedicated pages for
[error reporting](standard-lib/02-errno.md) and
[assertions](standard-lib/01-assert.md). The CP/M file runtime uses `errno` for
file-related failures, and `assert` writes its diagnostic through `stderr` before
terminating with `exit(1)`.

## File I/O and CP/M BDOS conventions

CP/M 2.2's BDOS file model is much simpler than POSIX/C89 assume, and DCCRTL's
`fopen`/`read`/`write`/etc. are built directly on it rather than emulating a
richer filesystem underneath. The differences below aren't DCCRTL bugs — they
follow from what BDOS itself can express — but they can surprise code ported
from a hosted C library. Every point here has been cross-checked against
several independent CP/M emulators (ntvcm, tnylpo, cpmemu, zxcc, iz-cpm,
z88dk's cpm, RunCPM, and Takeda Toshiya's cpm.exe) and, where noted, differs
between them.

### File length is tracked in 128-byte records, not bytes

CP/M has no byte-granular length field; a directory entry only knows how many
128-byte records a file occupies. A file whose true length isn't a multiple of
128 is still stored as a whole number of records, and the untouched tail of the
last record — the padding between the real data and the record boundary — is
written as Ctrl-Z (0x1A) bytes when that record is first created (there's no
"old data" to merge a partial write with). That padding is genuine, readable,
on-disk data: `fread()` isn't bounded by the tracked length the way a POSIX
`read()` would be, so a large-enough `fread()` on a short file can return the
trailing 0x1A padding right along with the real bytes, even though `ftell()`
reports the shorter, record-rounded length. This is deliberately left as
documented behavior rather than "fixed" by trimming the tracked length at the
runtime level — doing that breaks the classic CP/M convention (used by real
programs, and by this repo's own `fileops.c` test) of writing a single
trailing Ctrl-Z as the real end-of-text-file marker, which is indistinguishable
on disk from unwritten padding. See `tests/tpadread.c` for a worked repro and
`tests/tctrlz.c` for the Ctrl-Z-as-text-EOF convention in `fgets`/`fread`.

Practical implications:

- Don't assume `fread(buf, 1, sizeof(buf), f)` stops exactly at a text file's
  logical end; check for the file's own EOF convention (Ctrl-Z) if you rely on
  padding not leaking into the buffer.
- Ordinary streams initially use the record-rounded directory length for
  `fseek(f, 0, SEEK_END)`. An append stream replaces that value with its
  trimmed logical end so later append writes can restore it after a seek.
- `fopen(path, "a")` computes the logical end by scanning the last record
  backward for a run of trailing Ctrl-Z bytes. Every nonempty write restores
  that stream's tracked logical end first, so an intervening `fseek()` cannot
  redirect an append write into the middle of the file. A pre-existing
  trailing Ctrl-Z written as real text-EOF data can still shift where "end" is
  judged to be, as described above.

### Record-count overflow at exactly 8 MB

BDOS function 35 (compute file size) returns the record count in a 16-bit
register pair. CP/M 2.2's own maximum file size, 8 MB, is exactly 65536
records — one past what a 16-bit count can represent — so an exactly-8-MB file
reports a record count of 0, which looks like an empty file if read naively.
DCCRTL treats this as the one legitimate reason `__fdlen` can be genuinely
larger than what a raw record-count register pair reported; see `tests/tbig.c`
for sequential and random I/O across that boundary.

### Only search (fn 17/18) and delete (fn 19) officially support wildcards

Per the documented CP/M 2.2 Interface Guide, `?` in an FCB byte means "match
any character in this position" — but only for `opendir`/`readdir` (BDOS 17/18,
search first/next) and `unlink` (BDOS 19, delete). `fopen`/`open` (BDOS 15,
open), `fopen(path, "w")`/`fopen(path, "a")`-on-a-new-file (BDOS 22, make), and
`rename` (BDOS 23) are silent on the subject — the spec never promises
anything for an ambiguous FCB passed to them, and behavior there is either
implementation-defined or deliberately blocked at the DCCRTL level (see below).

`__mkfcb` passes a `?` through unchanged, matching real BDOS wildcard
semantics: `unlink("WA?.TMP")` deletes **every** file the pattern matches, not
just one (`WA1.TMP`, `WA2.TMP`, and `WA3.TMP` all at once, for example), and
`opendir("T?.TMP")`/`readdir()` filters to exactly the files that match. There
is no way to single out one file once a wildcard character is present. See
`tests/twild.c` and `tests/tdirpat.c`.

A literal `*`, unlike `?`, has no meaning to BDOS itself — only `?` is a real
wildcard at the BDOS level. The familiar shell-glob convention where `*`
matches any run of characters is implemented by the CCP's own command-line
parser (and by virtually every historic CP/M C runtime library), which expands
`*` by filling the rest of the current field with `?` before the FCB ever
reaches BDOS. `__mkfcb` does the same: a `*` in the name or extension fills
the remainder of that field with `?`, so `unlink("*.BAK")`,
`opendir("*.C")`, etc. behave the way C code typically expects, consistently
across every DCCRTL target rather than only on hosts whose emulator happens to
do its own glob expansion. See `tests/tstar.c`.

`rename()` with an ambiguous FCB (either the old or new name) is rejected
outright (`-1`, `errno = EINVAL`, nothing renamed) — this is correct,
spec-compliant behavior, not a limitation worth working around. Cross-drive
renames are also rejected before BDOS (`-1`, `errno = EXDEV`); a missing drive
prefix is resolved through the current CP/M drive before comparison. (CP/M
2.2's own BDOS source
technically loops over ambiguous *old*-name matches the same way delete does,
but that's undocumented, and it would just copy the new name's bytes verbatim
— `?` and all — into every match, producing garbage entries rather than any
kind of sensible template substitution; no tested BDOS implementation actually
does this.) See `tests/trenwild.c`.

### Low-level read errors versus EOF

BDOS random-read statuses 1 and 4 mean unwritten data/extent and remain the
runtime's EOF-or-hole result. Unambiguous failures such as an invalid FCB,
media change, verification failure, hardware error, or out-of-range record set
`errno` and the stream error indicator instead. If earlier records were
transferred, `read()` returns that positive partial count; otherwise it returns
`-1`.

### `fsync()` and `fdatasync()` on CP/M

Both functions first require a currently open real-file descriptor. They then
issue BDOS function 48 with `E = 0` and report its documented hardware-error
return as `EIO`. CP/M 2.2 itself has no function 48: some emulators implement
or safely ignore it, while others terminate the guest on the unsupported call.
On implementations that support it, the call flushes global disk buffers. It
cannot force still-pending FCB
allocation/extent metadata to the directory without closing the file, so
`close()` remains the operation that finalizes that metadata.

`fopen()`/`open()` for **reading** an ambiguous name is genuinely
implementation-defined: real BDOS's open call happens to reuse the same
directory-search primitive delete uses internally, with no explicit check
against an ambiguous FCB, so on some implementations it silently opens
whatever the first matching directory entry happens to be, while others
reject it outright. Don't rely on this either way. See `tests/tfopenw.c`.

`fopen()`/`open()` for **creating** a file (`"w"`, or `"a"` on a file that
doesn't exist yet) is different: DCCRTL explicitly rejects a `?` or `*` in the
parsed name/ext before making any BDOS call at all, returning failure (`NULL`
from `fopen`, `-1` from `open`, `errno = EINVAL`). BDOS's make call never
validates the FCB it's given — it just copies it into an empty directory slot
— so letting a wildcard through would silently create a real, permanent file
whose name contains that literal character, and such a file can never again be
matched by a wildcard-aware `unlink()`/`opendir()` scan (which correctly treat
`?`/`*` as pattern characters, not literal ones). Rejecting it up front avoids
creating a file with no portable way to clean it back up. See
`tests/tmakewc.c`.

### `rename()` onto an existing (unambiguous) destination name

This is a separate case from the ambiguous-FCB one above: both names are
ordinary, unambiguous filenames, but the destination already exists as its
own file. Real BDOS (confirmed against its own source) doesn't check for
this at all — it just overwrites the matched entry's name/extension bytes
with the new name, with no awareness that another directory entry already
has that name. On real hardware that leaves **two** directory entries
sharing one name, and which one a later `open()` finds is undefined.

No emulator that maps CP/M files onto real host files can reproduce that:
POSIX and Windows filesystems both refuse two directory entries with the
same name, so every emulator is forced to collapse this into a single
winner, one way or the other — there's no hardware-faithful option
available at all, on any host. Emulators split on which winner they pick:
ntvcm, cpmemu, zxcc, and z88dk's cpm overwrite the destination (nonzero
return means failure — 0 means the destination now holds the source's old
content and the source name is gone); tnylpo and cpm.exe reject the rename
outright instead (nonzero return, original name and content untouched).
Neither is "more correct." If your program cares which way this goes, check
`rename()`'s return value and/or `unlink()` the destination first rather
than relying on either outcome. See `tests/trenamex.c`.

### Drive-letter prefixes

`fopen("A:FILE.TXT", ...)` and similar are supported: a leading `A`-`P`
(case-insensitive) followed by `:` is parsed into the FCB's drive byte, and the
rest of the name follows normal 8.3 rules. There is no directory/path concept
beyond this single-letter drive prefix — CP/M has no subdirectories at all (see
[`dirent.h`](#direnth--directory-enumeration) above).

### 8.3 filenames and truncation collisions

Every filename is 8 characters plus a 3-character extension, uppercased, with
no further validation. A longer host-supplied or generated name is silently
truncated to fit; two different names that happen to truncate to the same 8.3
form collide and refer to the same underlying CP/M file, with no error raised
at creation time. See `tests/tlongfn.c`.

### No atomic append, no `O_APPEND`-style write positioning

BDOS has no equivalent of POSIX's `O_APPEND` (every write goes to wherever the
FCB's current record pointer is, and nothing serializes that against other
processes). `fopen(path, "a")` computes the append position once, at open
time; it does not re-seek to end before every subsequent write the way a true
`O_APPEND` descriptor would if another process extended the file in between.
Programs that share or alternate writes to the same file across processes must
coordinate at a higher level.

## CP/M extensions

The runtime exposes the raw CP/M BDOS entry point for things the standard
library doesn't cover (console status, direct disk calls, and so on). It is
declared in `stdlib.h`:

```c
int bdos(int fn, int dearg);
```

`fn` is the BDOS function number and `dearg` is the value passed in `DE`; the
byte result comes back in the low byte of the returned `int`. Calls whose useful
result is an FCB/DMA region (directory and file operations) return their data
through the memory `dearg` points at, not in the return value.

The BIOS jump table is also available when an application genuinely needs
machine-specific services:

```c
int bios(int fn, int arg);                    /* byte result from A */
int bioshl(int fn, int arg);                  /* full result from HL */
int biosreg(int fn, int bcarg, int dearg);    /* independent BC/DE, result HL */
```

`bios()` and `bioshl()` are the source-compatible convenience forms: `arg` is
copied into `BC` and `DE`, and its low byte is therefore also in `C`. That
covers the ordinary no-argument, character-in-`C`, and one-word-in-`BC` CP/M
2.2 calls. Use `biosreg()` only when the BIOS contract needs two different
register values, notably `SECTRAN` (logical sector in `BC`, translation-table
address in `DE`) or CP/M 3 `SELDSK` (drive in `C`, login flag in `E`).
`biosreg()` returns the full `HL` result used by those calls.

BIOS disk services are hardware- and emulator-specific. In particular, ntvcm
provides the standard 17-entry jump table but implements console functions
only; its disk BIOS entries diagnose an unhandled call. Prefer BDOS file APIs
for portable CP/M programs, and use BIOS disk calls only where the target BIOS
is known.

### Non-blocking console input

The standard input calls (`getchar`, `getc`, `fgets`, `scanf`) are blocking
C-style input. For games, menus, terminal UIs, and other polling loops, use the
runtime's `kbhit()` and `getch()` (declared in `stdio.h`):

- `int kbhit(void)` returns nonzero when a key is waiting and `0` otherwise. It
  never blocks and does not consume the character.
- `int getch(void)` reads one key without echo. It blocks until a key is ready,
  so it is normally guarded by `kbhit()`.

```c
#include <stdio.h>

int main(void)
{
    int ch;

    if (kbhit()) {          /* non-blocking test */
        ch = getch();       /* safe: a key is already waiting */
        if (ch)
            handle_key(ch);
    }
    return 0;
}
```

Under the hood `kbhit()` is CP/M BDOS function 11 (console status) and `getch()`
is BDOS function 6 (direct console input, `E = 0xff`). Raw calls can be made
through `bdos()`, but the named functions are clearer and also flush pending
buffered output before blocking:

```c
#include <stdlib.h>

int raw_kbhit(void)        { return bdos(11, 0) != 0; }
int raw_getch_nonblock(void) { return bdos(6, 0xff); }  /* 0 = no key ready */
```

BDOS function 6 uses `0` as the "no character" sentinel, so it is best for
keyboard-style console input rather than protocols where NUL is meaningful.
Do not mix raw `bdos()` console I/O with buffered console functions in the same
code path; direct BDOS calls bypass the console output buffer used by
`printf`/`puts`.

### Direct port I/O

For talking to hardware or an emulator's virtual devices, the runtime also
provides 8-bit port I/O, declared alongside `bdos` in `stdlib.h`:

```c
int  inp(unsigned port);                 /* IN  A,(port) -> 0..255 */
void outp(unsigned port, unsigned val);  /* OUT (port),A           */
```

`inp` runs the Z80 `IN A,(port)` instruction and returns the byte read,
zero-extended to `int` (so the result is always 0..255). `outp` runs
`OUT (port),A`, sending the low byte of `val` to the port. Only the low 8 bits
of `port` are significant. Neither is part of C89.
