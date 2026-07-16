#!/usr/bin/env python3
"""
dccprof.py - correlate an ntvcm "-g:<file>" per-PC execution-count profile
with dcc-generated .PRN/.SYM listings, producing:
  - a Markdown summary ranking the hottest functions
  - annotated listings (app + touched RTL routines), each in two forms:
      .txt  - a per-line hit count and opcode bytes prefixed onto every
              instruction line, directly viewable/searchable in any editor
      .html - the same content, self-contained, with each line's hit count
              cell color-coded on a log-scaled heatmap (open in a browser)

This is the formalized version of the ad-hoc address-correlation scripts
built by hand during interactive profiling investigations this session
(bucket.py -> bucket2.py -> bucket3.py) - see the two lessons that made
those iterations necessary, both handled correctly here:

  1. RTL code and app code are assembled STANDALONE (their own .PRN, its own
     from-zero addresses) before being linked together at final addresses
     that differ from those standalone addresses - and by DIFFERENT amounts
     for the RTL vs the app, since each is a separate module in the final
     link. Two independently-computed offsets are required (one per
     module), found by matching one known symbol's address between that
     module's own standalone .PRN and the final linked .SYM.

  2. A dcc/M80-style .PRN listing's address column is the address AFTER the
     current line's own emitted bytes (i.e. where the NEXT line's bytes
     begin), not the current line's own starting address - confirmed against
     known Z80 instruction encodings (e.g. "push ix" is 2 bytes; if it is
     immediately preceded by a label at address 0000, "push ix" itself is
     listed with address 0002, not 0000). A line's own true starting
     address is therefore the PREVIOUS line's listed address (or the
     module's base address for the first line). Getting this backwards
     silently shifts every hit count by one listing line - there is no
     crash to reveal the mistake, so this is verified against several
     independent instructions of different byte-lengths before trusting it
     anywhere in this file.

Usage:
    dccprof.py --app NAME --build-dir DIR --profile-csv FILE [--out-dir DIR]

Designed for two audiences equally:
  - scripts/dccprof.sh / dccprof.bat, which build + run an app under the
    profiler and then call this tool to produce the final report.
  - direct, ad-hoc invocation against an already-built app + an
    already-captured profile.csv, exactly the workflow used throughout this
    session's own performance investigations - point this at the same
    build directory instead of hand-rolling a new correlation script next
    time.
"""
import argparse
import html
import math
import os
import re
import sys
from collections import defaultdict


# ---------------------------------------------------------------------- #
# .SYM parsing - the final, linked symbol table L80 emits. Names are
# truncated to 6 significant characters and upper-cased by the linker, and
# the file is a CP/M text file that may have a ^Z (0x1A) EOF marker with
# stray content after it in the final 128-byte record - read as raw bytes
# and truncate there before decoding, rather than risk mis-parsing garbage.
# ---------------------------------------------------------------------- #
def parse_sym(path):
    with open(path, 'rb') as f:
        data = f.read()
    eof = data.find(b'\x1a')
    if eof >= 0:
        data = data[:eof]
    text = data.decode('ascii', errors='replace')
    syms = {}
    for m in re.finditer(r'([0-9A-Fa-f]{4})\s+(\S+)', text):
        addr = int(m.group(1), 16)
        name = m.group(2).upper()
        # First occurrence wins; the .SYM lists each symbol once in
        # practice, but be defensive rather than let a stray duplicate or
        # post-EOF fragment silently override a real entry.
        syms.setdefault(name, addr)
    return syms


# ---------------------------------------------------------------------- #
# .PRN parsing.
# ---------------------------------------------------------------------- #
_PRN_LINE_RE = re.compile(r'^([0-9A-Fa-f]{4})\s+(\d+)\s+(.*)$')
_LABEL_RE = re.compile(r'^([A-Za-z_$][A-Za-z0-9_$]*):$')
_PUBLIC_RE = re.compile(r'^public\s+(\S+)', re.IGNORECASE)
_STATIC_FN_RE = re.compile(r'^;\s*static function\s+(\S+)', re.IGNORECASE)


class PrnListing:
    """One standalone .PRN listing's parsed lines plus the label table
    needed both to compute this module's link offset and to attribute
    profiled addresses to source lines and functions."""

    def __init__(self, path):
        self.path = path
        self.lines = []          # list of dict: start,end,lineno,text,label
        self.label_addr = {}     # name (as written) -> standalone address
        self.public_names = set()  # upper-cased names declared "public NAME"

        with open(path, 'rb') as f:
            raw = f.read()
        text = raw.decode('ascii', errors='replace')

        running = None
        static_pending = None  # source name from the most recent
                                # "; static function NAME" comment, applied
                                # to the very next label only
        for rawline in text.splitlines():
            m = _PRN_LINE_RE.match(rawline)
            if not m:
                continue
            addr = int(m.group(1), 16)
            lineno = int(m.group(2))
            content = m.group(3)
            if running is None:
                running = addr
            start = running
            end = addr
            running = addr

            stripped = content.strip()
            label_name = None
            display_name = None
            lm = _LABEL_RE.match(stripped)
            if lm:
                label_name = lm.group(1)
                self.label_addr[label_name] = start
                if static_pending is not None:
                    display_name = static_pending
                    static_pending = None
            else:
                pm = _PUBLIC_RE.match(stripped)
                if pm:
                    self.public_names.add(pm.group(1).upper())
                sm = _STATIC_FN_RE.match(stripped)
                if sm:
                    static_pending = sm.group(1)

            self.lines.append({
                'start': start, 'end': end, 'lineno': lineno, 'text': content,
                'label': label_name, 'display_name': display_name,
            })

    def compute_offset(self, final_syms):
        """Find this module's standalone->final address offset by matching
        one label's standalone address against the final .SYM (names
        truncated to 6 chars, upper-cased, exactly as the linker does).
        Returns None if no label could be matched (caller must decide
        whether that's fatal)."""
        for name, addr in self.label_addr.items():
            key = name.upper()[:6]
            if key in final_syms:
                return final_syms[key] - addr
        return None


# ---------------------------------------------------------------------- #
# Function-boundary attribution: walk one module's parsed lines and assign
# every line to the most recent "owning" function name, using the same
# two patterns dcc's own codegen and DCCRTL.MAC both use for a function's
# entry label - collected as a set of candidate names first (a hand-written
# RTL routine may pre-declare a whole batch of "public NAME" entry points
# far above the labels they actually apply to - see this file's own module
# docstring), then matched against each label as it's encountered.
#
# A label matching neither pattern is normally left attributed to whatever
# function came before it - correct for an ordinary internal label (a loop
# target, an early-return merge point, etc. - all reached by falling
# through or branching from that same function's own preceding code, no
# matter what instruction happens to sit textually just above the label).
# An EARLIER version of this function instead asked "was the previous line
# an unconditional ret/jp/jr" - plausible-sounding, but wrong: those are
# completely ordinary within a single routine's own control flow (an early-
# return path's own "ret" immediately followed by the label the normal
# path merges back into, e.g.), so that check fired constantly on labels
# that were never anything but a continuation of the current function,
# fragmenting it into several spuriously-separate rows.
#
# The one pattern that DOES need special handling is different and much
# narrower: a label reached directly after one or more "public NAME" lines
# that have not yet been matched to any label at all - a shared preamble
# several public entry points jump INTO from below, pre-declared together
# far above it (see PrnListing's own public_names collection). This is
# exactly DCCRTL.MAC's DRSU: __divu/__modu/__divs/__mods are all declared
# public together, then DRSU's own label (matching none of those four
# names) follows directly - and without this check, DRSU's execution
# count (the vast majority of a division-heavy program's total) was
# silently misattributed to __conout, merely because __conout was the
# last successfully-matched public function several dozen lines above.
# Once DRSU is recognized here, __divu's own eventual "__divu:" label
# further down correctly reclaims the rest of that shared implementation,
# exactly as it always did for a directly-matching label.
# ---------------------------------------------------------------------- #
def attribute_functions(listing):
    """Returns a new list of (line_dict, function_name) in original order.
    function_name is None for any line before the first recognized
    function label."""
    current = None
    out = []
    pending_public = False
    for line in listing.lines:
        if line['label'] is not None:
            if line['display_name'] is not None:
                current = line['display_name']
                pending_public = False
            elif line['label'].upper() in listing.public_names:
                current = line['label']
                pending_public = False
            elif pending_public:
                current = line['label']
                pending_public = False
        else:
            code_part = line['text'].split(';', 1)[0].strip()
            if code_part:
                pending_public = bool(_PUBLIC_RE.match(code_part))
        out.append((line, current))
    return out


# ---------------------------------------------------------------------- #
# Profile CSV ("pc,count,asm" - see ntvcm's -g:<file> option).
# ---------------------------------------------------------------------- #
def parse_profile_csv(path):
    counts = {}
    with open(path, 'r') as f:
        header = f.readline()
        if not header.startswith('pc,count'):
            raise ValueError("%s does not look like an ntvcm -g profile CSV "
                              "(expected a 'pc,count,asm' header)" % path)
        for line in f:
            line = line.rstrip('\n')
            if not line:
                continue
            parts = line.split(',', 2)
            pc = int(parts[0])
            count = int(parts[1])
            counts[pc] = count
    return counts


# ---------------------------------------------------------------------- #
# Correlation.
# ---------------------------------------------------------------------- #

# CP/M's fixed TPA (transient program area) load address: every .COM file's
# first byte lands at 0100H in memory, so a final linked address's offset
# into the .COM *file* on disk is simply (address - COM_LOAD_ADDR). Verified
# empirically against a real build (MM.COM): the linked address for
# _filla's "push ix" (1729H) landed on bytes DD E5 at file offset 1629H,
# exactly address-0100H, and the following "ld ix,0" (4 bytes, DD 21 00 00)
# confirmed it - not something to rederive per-app, this is a CP/M OS
# convention, not a toolchain choice.
COM_LOAD_ADDR = 0x0100


class CorrelatedLine:
    __slots__ = ('module', 'lineno', 'text', 'addr', 'is_instr', 'count',
                 'func', 'opcode_bytes')

    def __init__(self, module, lineno, text, addr, is_instr, count, func,
                 opcode_bytes):
        self.module = module
        self.lineno = lineno
        self.text = text
        self.addr = addr
        self.is_instr = is_instr
        self.count = count
        self.func = func
        self.opcode_bytes = opcode_bytes  # bytes, or None if unavailable


def correlate(app_listing, app_offset, app_module_name,
              rtl_listing, rtl_offset, rtl_module_name,
              profile_counts, com_data):
    """Returns (correlated_lines, uncorrelated_pcs).
    correlated_lines: list of CorrelatedLine, one per .PRN line across both
    modules, in (module, original-file-order) groups.
    uncorrelated_pcs: profiled addresses that landed in neither module's
    address range at all - diagnostic only, should normally be empty.
    com_data: the linked .COM file's raw bytes (for pulling each
    instruction's own encoded opcode bytes), or None if unavailable - in
    which case every line's opcode_bytes is None."""
    remaining = dict(profile_counts)
    correlated = []

    for listing, offset, module_name in (
        (app_listing, app_offset, app_module_name),
        (rtl_listing, rtl_offset, rtl_module_name),
    ):
        by_func = attribute_functions(listing)
        for line, func in by_func:
            start = line['start'] + offset
            end = line['end'] + offset
            is_instr = end != start
            count = 0
            opcode_bytes = None
            if is_instr:
                # A multi-byte instruction is only ever recorded once, at
                # its own first byte's address (ntvcm increments the count
                # at instruction fetch/decode, keyed by that address) - so
                # an exact match at `start` is the correct (and only)
                # lookup, not a range scan over [start, end).
                count = remaining.pop(start, 0)
                if com_data is not None:
                    file_start = start - COM_LOAD_ADDR
                    file_end = end - COM_LOAD_ADDR
                    if 0 <= file_start and file_end <= len(com_data):
                        chunk = com_data[file_start:file_end]
                        if len(chunk) == end - start:
                            opcode_bytes = chunk
            correlated.append(CorrelatedLine(
                module_name, line['lineno'], line['text'], start, is_instr,
                count, func, opcode_bytes))

    uncorrelated_pcs = remaining
    return correlated, uncorrelated_pcs


# ---------------------------------------------------------------------- #
# Report generation.
# ---------------------------------------------------------------------- #
def build_function_totals(correlated_lines):
    totals = defaultdict(int)
    modules = {}
    for cl in correlated_lines:
        key = (cl.module, cl.func or '(top level)')
        totals[key] += cl.count
        modules[key] = cl.module
    return totals


def write_summary_md(out_path, app_name, correlated_lines, uncorrelated_pcs,
                      total_hits, annotated_paths):
    totals = build_function_totals(correlated_lines)
    ranked = sorted(totals.items(), key=lambda kv: -kv[1])

    with open(out_path, 'w') as f:
        f.write("# %s profile summary\n\n" % app_name)
        f.write("Total instruction executions counted: **%d**\n\n" % total_hits)
        if uncorrelated_pcs:
            uncorrelated_total = sum(uncorrelated_pcs.values())
            frac = uncorrelated_total / total_hits if total_hits else 0.0
            addr_list = ", ".join("0x%04X" % pc for pc in sorted(uncorrelated_pcs))
            if frac < 0.01:
                f.write("_%d profiled address(es) outside %s/RTLMIN.MAC entirely "
                        "(%s) - %.2f%% of total hits, almost certainly CP/M's "
                        "BDOS entry vector (0x0005) and/or ntvcm's own BDOS trap "
                        "handlers, not a correlation problem._\n\n"
                        % (len(uncorrelated_pcs), app_name, addr_list, 100.0 * frac))
            else:
                f.write("_Warning: %d profiled addresses (%s) could not be "
                        "matched to any listing line - %.1f%% of total hits. "
                        "This is too large to be just CP/M/emulator system "
                        "vectors; the build used to generate the profile may "
                        "not match the .PRN/.SYM files in this build "
                        "directory._\n\n"
                        % (len(uncorrelated_pcs), addr_list, 100.0 * frac))
        f.write("## Hottest functions\n\n")
        f.write("| Hits | % of total | Module | Function |\n")
        f.write("|---:|---:|---|---|\n")
        for (module, func), hits in ranked:
            if hits == 0:
                continue
            pct = 100.0 * hits / total_hits if total_hits else 0.0
            f.write("| %d | %.1f%% | %s | %s |\n" % (hits, pct, module, func))
        f.write("\n## Annotated listings\n\n")
        for label, path in annotated_paths:
            f.write("- [%s](%s)\n" % (label, os.path.basename(path)))
        f.write("\nEach annotated listing prefixes every instruction line "
                "with its own hit count, in original .PRN address/line-"
                "number order - open one directly and use your editor's "
                "search/go-to-line to jump to a specific address or line "
                "number called out above.\n")


# A real instruction is at most 4 bytes; this cap is for the occasional
# "db ...(long run of data)" listing line - a string literal or a large
# zero-filled buffer reservation (e.g. ttt's console buffer) - whose byte
# count can run into the hundreds. Left uncapped, one such line would
# force every row's Bytes column (a shared table width in the HTML report)
# wide enough to shove the source text far off to the right for every
# line, not just that one.
MAX_DISPLAY_BYTES = 8


def format_opcode_bytes(opcode_bytes, max_bytes=MAX_DISPLAY_BYTES):
    """Space-separated uppercase hex, e.g. 'DD E5' - or '' if unavailable
    (no .COM was found, or the address fell outside it). Truncated past
    max_bytes with a '+N more' suffix rather than left uncapped."""
    if not opcode_bytes:
        return ''
    shown = ' '.join('%02X' % b for b in opcode_bytes[:max_bytes])
    extra = len(opcode_bytes) - max_bytes
    if extra > 0:
        shown += ' +%d more' % extra
    return shown


def write_annotated_listing(out_path, module_name, correlated_lines):
    module_lines = [cl for cl in correlated_lines if cl.module == module_name]
    with open(out_path, 'w') as f:
        f.write("; dccprof annotated listing for %s\n" % module_name)
        f.write("; format: <hits> | <addr> <opcode bytes>  <line#>  <original .PRN line>\n\n")
        for cl in module_lines:
            if cl.is_instr:
                count_field = "%10d" % cl.count
                bytes_field = format_opcode_bytes(cl.opcode_bytes)
            else:
                count_field = " " * 10
                bytes_field = ''
            f.write("%s | %04X %-34s %6d  %s\n" % (
                count_field, cl.addr, bytes_field, cl.lineno, cl.text))


def write_filtered_rtl_listing(out_path, correlated_lines, rtl_module_name):
    """Only functions that received at least one hit - DCCRTL is large and
    a given profile run typically touches a small fraction of it, so a full
    annotated dump would mostly be dead weight."""
    totals = build_function_totals(correlated_lines)
    hot_funcs = {func for (module, func), hits in totals.items()
                 if module == rtl_module_name and hits > 0}
    with open(out_path, 'w') as f:
        f.write("; dccprof annotated listing for %s "
                "(functions with at least one hit only)\n" % rtl_module_name)
        f.write("; format: <hits> | <addr> <opcode bytes>  <line#>  <original .PRN line>\n\n")
        for cl in correlated_lines:
            if cl.module != rtl_module_name:
                continue
            if (cl.func or '(top level)') not in hot_funcs:
                continue
            if cl.is_instr:
                count_field = "%10d" % cl.count
                bytes_field = format_opcode_bytes(cl.opcode_bytes)
            else:
                count_field = " " * 10
                bytes_field = ''
            f.write("%s | %04X %-34s %6d  %s\n" % (
                count_field, cl.addr, bytes_field, cl.lineno, cl.text))


# ---------------------------------------------------------------------- #
# HTML listings - same content as the .txt listings above, but with the
# hit count's own cell background color-coded (a one-hue sequential ramp,
# log-scaled so a handful of dominant loop bodies don't wash out everything
# else down at bucket 1). Plain text can't carry that, hence a second
# format alongside (not instead of) the grep/search-friendly .txt.
# ---------------------------------------------------------------------- #

# Sequential blue ramp, light->dark (6 buckets pulled from a 13-step scale)
# paired with the text color that stays readable on each: bucket 0 is "no
# hits" and gets no fill at all, so a mostly-cold listing still reads as
# plain code, not a wall of pale blue.
_HEAT_COLORS = [
    None,
    ('#cde2fb', '#0b0b0b'),
    ('#9ec5f4', '#0b0b0b'),
    ('#5598e7', '#0b0b0b'),
    ('#2a78d6', '#ffffff'),
    ('#184f95', '#ffffff'),
    ('#0d366b', '#ffffff'),
]

_HTML_STYLE = """<style>
  :root {
    color-scheme: light dark;
    --surface: #fcfcfb;
    --page: #f9f9f7;
    --ink: #0b0b0b;
    --ink-secondary: #52514e;
    --ink-muted: #898781;
    --border: rgba(11,11,11,0.10);
    --row-hover: rgba(37,106,191,0.10);
  }
  @media (prefers-color-scheme: dark) {
    :root {
      --surface: #1a1a19;
      --page: #0d0d0d;
      --ink: #ffffff;
      --ink-secondary: #c3c2b7;
      --ink-muted: #898781;
      --border: rgba(255,255,255,0.10);
      --row-hover: rgba(57,135,229,0.18);
    }
  }
  body {
    background: var(--page);
    color: var(--ink);
    font-family: system-ui, -apple-system, "Segoe UI", sans-serif;
    margin: 0;
    padding: 24px;
  }
  h1 { font-size: 16px; margin: 0 0 4px; }
  .meta { color: var(--ink-secondary); font-size: 13px; margin-bottom: 16px; }
  .legend { display: flex; align-items: center; gap: 6px; margin-bottom: 20px; font-size: 12px; color: var(--ink-secondary); }
  .legend .swatch { width: 20px; height: 14px; border-radius: 2px; border: 1px solid var(--border); }
  table {
    border-collapse: collapse;
    font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
    font-size: 12.5px;
    background: var(--surface);
    width: 100%;
  }
  thead th {
    position: sticky; top: 0;
    background: var(--surface);
    color: var(--ink-muted);
    text-align: left;
    font-family: system-ui, -apple-system, "Segoe UI", sans-serif;
    font-size: 11px;
    text-transform: uppercase;
    letter-spacing: 0.04em;
    padding: 6px 10px;
    border-bottom: 1px solid var(--border);
  }
  td { padding: 1px 10px; white-space: pre; }
  td.hits { text-align: right; color: var(--ink-secondary); }
  td.addr { color: var(--ink-muted); }
  td.bytes { color: var(--ink-secondary); }
  td.lineno { text-align: right; color: var(--ink-muted); }
  tr:hover td { background: var(--row-hover) !important; }
  tr.heat1 td.hits { background: #cde2fb; color: #0b0b0b; }
  tr.heat2 td.hits { background: #9ec5f4; color: #0b0b0b; }
  tr.heat3 td.hits { background: #5598e7; color: #0b0b0b; }
  tr.heat4 td.hits { background: #2a78d6; color: #ffffff; }
  tr.heat5 td.hits { background: #184f95; color: #ffffff; }
  tr.heat6 td.hits { background: #0d366b; color: #ffffff; }
</style>
"""


def _heat_bucket(count, max_count):
    """1-6, log-scaled against this listing's own hottest line - 0 means
    'not an instruction' or 'never executed', which gets no fill."""
    if count <= 0 or max_count <= 0:
        return 0
    frac = math.log1p(count) / math.log1p(max_count)
    return min(6, 1 + int(frac * 6))


def _write_html_listing(out_path, title, subtitle, module_lines):
    total_hits = sum(cl.count for cl in module_lines if cl.is_instr)
    instr_counts = [cl.count for cl in module_lines if cl.is_instr and cl.count > 0]
    max_count = max(instr_counts) if instr_counts else 0

    with open(out_path, 'w') as f:
        f.write("<!doctype html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n")
        f.write("<title>%s</title>\n" % html.escape(title))
        f.write(_HTML_STYLE)
        f.write("</head>\n<body>\n")
        f.write("<h1>%s</h1>\n" % html.escape(title))
        f.write('<div class="meta">%s &mdash; %s instruction executions counted, '
                'hottest line %s</div>\n' % (
                    html.escape(subtitle), format(total_hits, ',d'),
                    format(max_count, ',d')))
        if max_count > 0:
            f.write('<div class="legend"><span>cold</span>')
            for i in range(1, 7):
                f.write('<span class="swatch" style="background:%s"></span>'
                         % _HEAT_COLORS[i][0])
            f.write('<span>hot &mdash; log-scaled by hit count</span></div>\n')
        f.write('<table>\n<thead><tr>'
                '<th>Hits</th><th>Addr</th><th>Bytes</th><th>Line#</th><th>Source</th>'
                '</tr></thead>\n<tbody>\n')
        for cl in module_lines:
            bucket = 0
            hits_text = ''
            bytes_text = ''
            bytes_title = ''
            if cl.is_instr:
                bucket = _heat_bucket(cl.count, max_count)
                hits_text = format(cl.count, ',d')
                bytes_text = format_opcode_bytes(cl.opcode_bytes)
                if cl.opcode_bytes and len(cl.opcode_bytes) > MAX_DISPLAY_BYTES:
                    bytes_title = ' title="%s"' % html.escape(
                        format_opcode_bytes(cl.opcode_bytes, max_bytes=len(cl.opcode_bytes)))
            row_class = ' class="heat%d"' % bucket if bucket else ''
            f.write('<tr%s><td class="hits">%s</td><td class="addr">%04X</td>'
                     '<td class="bytes"%s>%s</td><td class="lineno">%d</td>'
                     '<td class="src">%s</td></tr>\n' % (
                         row_class, hits_text, cl.addr, bytes_title,
                         html.escape(bytes_text), cl.lineno, html.escape(cl.text)))
        f.write('</tbody>\n</table>\n</body>\n</html>\n')


def write_annotated_listing_html(out_path, module_name, correlated_lines, app_name):
    module_lines = [cl for cl in correlated_lines if cl.module == module_name]
    _write_html_listing(out_path, "%s profile: %s" % (app_name, module_name),
                         module_name, module_lines)


def write_filtered_rtl_listing_html(out_path, correlated_lines, rtl_module_name,
                                     app_name):
    totals = build_function_totals(correlated_lines)
    hot_funcs = {func for (module, func), hits in totals.items()
                 if module == rtl_module_name and hits > 0}
    module_lines = [cl for cl in correlated_lines
                     if cl.module == rtl_module_name
                     and (cl.func or '(top level)') in hot_funcs]
    _write_html_listing(
        out_path, "%s profile: %s" % (app_name, rtl_module_name),
        "%s (functions with at least one hit only)" % rtl_module_name,
        module_lines)


# ---------------------------------------------------------------------- #
# Main.
# ---------------------------------------------------------------------- #
def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--app', required=True, help='application name, e.g. tbig')
    ap.add_argument('--build-dir', required=True,
                     help='directory containing <APP>.PRN/.SYM/.MAC and RTLMIN.PRN/.MAC')
    ap.add_argument('--profile-csv', required=True,
                     help='path to the ntvcm -g:<file> profile CSV')
    ap.add_argument('--out-dir', default=None,
                     help='output directory for the report (default: build-dir)')
    args = ap.parse_args()

    app_upper = args.app.upper()
    build_dir = args.build_dir
    out_dir = args.out_dir or build_dir
    os.makedirs(out_dir, exist_ok=True)

    app_prn = os.path.join(build_dir, app_upper + '.PRN')
    app_sym = os.path.join(build_dir, app_upper + '.SYM')
    rtl_prn = os.path.join(build_dir, 'RTLMIN.PRN')
    app_com = os.path.join(build_dir, app_upper + '.COM')

    for required in (app_prn, app_sym, rtl_prn, args.profile_csv):
        if not os.path.isfile(required):
            print("error: required input not found: %s" % required, file=sys.stderr)
            if required == rtl_prn:
                print("hint: RTLMIN.PRN is not produced by a normal build - "
                      "assemble it with the /L listing flag first "
                      "(dccprof.sh/.bat does this automatically):\n"
                      "  m80c '=RTLMIN.MAC' '/X' '/O' '/Z' '/L'", file=sys.stderr)
            sys.exit(1)

    final_syms = parse_sym(app_sym)
    app_listing = PrnListing(app_prn)
    rtl_listing = PrnListing(rtl_prn)

    app_offset = app_listing.compute_offset(final_syms)
    rtl_offset = rtl_listing.compute_offset(final_syms)
    if app_offset is None:
        print("error: could not compute %s's link offset - no label in "
              "%s matched any symbol in %s" % (app_upper, app_prn, app_sym),
              file=sys.stderr)
        sys.exit(1)
    if rtl_offset is None:
        print("error: could not compute RTLMIN's link offset - no label in "
              "%s matched any symbol in %s" % (rtl_prn, app_sym),
              file=sys.stderr)
        sys.exit(1)

    profile_counts = parse_profile_csv(args.profile_csv)
    total_hits = sum(profile_counts.values())

    com_data = None
    if os.path.isfile(app_com):
        with open(app_com, 'rb') as f:
            com_data = f.read()
    else:
        print("warning: %s not found - opcode-bytes columns will be blank"
              % app_com, file=sys.stderr)

    correlated_lines, uncorrelated_pcs = correlate(
        app_listing, app_offset, app_upper + '.MAC',
        rtl_listing, rtl_offset, 'RTLMIN.MAC',
        profile_counts, com_data)

    app_out = os.path.join(out_dir, '%s_profile_app.txt' % args.app)
    rtl_out = os.path.join(out_dir, '%s_profile_rtl.txt' % args.app)
    app_out_html = os.path.join(out_dir, '%s_profile_app.html' % args.app)
    rtl_out_html = os.path.join(out_dir, '%s_profile_rtl.html' % args.app)
    summary_out = os.path.join(out_dir, '%s_profile_summary.md' % args.app)

    write_annotated_listing(app_out, app_upper + '.MAC', correlated_lines)
    write_filtered_rtl_listing(rtl_out, correlated_lines, 'RTLMIN.MAC')
    write_annotated_listing_html(app_out_html, app_upper + '.MAC', correlated_lines,
                                  args.app)
    write_filtered_rtl_listing_html(rtl_out_html, correlated_lines, 'RTLMIN.MAC',
                                     args.app)
    write_summary_md(summary_out, args.app, correlated_lines, uncorrelated_pcs,
                      total_hits,
                      [(app_upper + '.MAC (plain text)', app_out),
                       (app_upper + '.MAC (color-coded HTML)', app_out_html),
                       ('RTLMIN.MAC (hot routines only, plain text)', rtl_out),
                       ('RTLMIN.MAC (hot routines only, color-coded HTML)', rtl_out_html)])

    print("wrote %s" % summary_out)
    print("wrote %s" % app_out)
    print("wrote %s" % rtl_out)
    print("wrote %s" % app_out_html)
    print("wrote %s" % rtl_out_html)
    if uncorrelated_pcs:
        print("warning: %d profiled addresses were not correlated to any "
              "listing line (%d total hits)" % (
                  len(uncorrelated_pcs), sum(uncorrelated_pcs.values())),
              file=sys.stderr)


if __name__ == '__main__':
    main()
