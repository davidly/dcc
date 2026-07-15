#!/usr/bin/env python3
"""
dccprof.py - correlate an ntvcm "-g:<file>" per-PC execution-count profile
with dcc-generated .PRN/.SYM listings, producing:
  - a Markdown summary ranking the hottest functions
  - annotated listings (app + touched RTL routines) with a per-line hit
    count prefixed onto every instruction line, directly viewable/
    searchable in any editor

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
# ---------------------------------------------------------------------- #
def attribute_functions(listing):
    """Returns a new list of (line_dict, function_name) in original order.
    function_name is None for any line before the first recognized
    function label."""
    current = None
    out = []
    for line in listing.lines:
        if line['label'] is not None:
            if line['display_name'] is not None:
                current = line['display_name']
            elif line['label'].upper() in listing.public_names:
                current = line['label']
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
class CorrelatedLine:
    __slots__ = ('module', 'lineno', 'text', 'addr', 'is_instr', 'count', 'func')

    def __init__(self, module, lineno, text, addr, is_instr, count, func):
        self.module = module
        self.lineno = lineno
        self.text = text
        self.addr = addr
        self.is_instr = is_instr
        self.count = count
        self.func = func


def correlate(app_listing, app_offset, app_module_name,
              rtl_listing, rtl_offset, rtl_module_name,
              profile_counts):
    """Returns (correlated_lines, uncorrelated_pcs).
    correlated_lines: list of CorrelatedLine, one per .PRN line across both
    modules, in (module, original-file-order) groups.
    uncorrelated_pcs: profiled addresses that landed in neither module's
    address range at all - diagnostic only, should normally be empty."""
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
            if is_instr:
                # A multi-byte instruction is only ever recorded once, at
                # its own first byte's address (ntvcm increments the count
                # at instruction fetch/decode, keyed by that address) - so
                # an exact match at `start` is the correct (and only)
                # lookup, not a range scan over [start, end).
                count = remaining.pop(start, 0)
            correlated.append(CorrelatedLine(
                module_name, line['lineno'], line['text'], start, is_instr,
                count, func))

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


def write_annotated_listing(out_path, module_name, correlated_lines):
    module_lines = [cl for cl in correlated_lines if cl.module == module_name]
    with open(out_path, 'w') as f:
        f.write("; dccprof annotated listing for %s\n" % module_name)
        f.write("; format: <hits> | <original .PRN line>\n\n")
        for cl in module_lines:
            if cl.is_instr:
                count_field = "%10d" % cl.count
            else:
                count_field = " " * 10
            f.write("%s | %04X %6d  %s\n" % (
                count_field, cl.addr, cl.lineno, cl.text))


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
        f.write("; format: <hits> | <original .PRN line>\n\n")
        for cl in correlated_lines:
            if cl.module != rtl_module_name:
                continue
            if (cl.func or '(top level)') not in hot_funcs:
                continue
            if cl.is_instr:
                count_field = "%10d" % cl.count
            else:
                count_field = " " * 10
            f.write("%s | %04X %6d  %s\n" % (
                count_field, cl.addr, cl.lineno, cl.text))


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

    correlated_lines, uncorrelated_pcs = correlate(
        app_listing, app_offset, app_upper + '.MAC',
        rtl_listing, rtl_offset, 'RTLMIN.MAC',
        profile_counts)

    app_out = os.path.join(out_dir, '%s_profile_app.txt' % args.app)
    rtl_out = os.path.join(out_dir, '%s_profile_rtl.txt' % args.app)
    summary_out = os.path.join(out_dir, '%s_profile_summary.md' % args.app)

    write_annotated_listing(app_out, app_upper + '.MAC', correlated_lines)
    write_filtered_rtl_listing(rtl_out, correlated_lines, 'RTLMIN.MAC')
    write_summary_md(summary_out, args.app, correlated_lines, uncorrelated_pcs,
                      total_hits,
                      [(app_upper + '.MAC', app_out), ('RTLMIN.MAC (hot routines only)', rtl_out)])

    print("wrote %s" % summary_out)
    print("wrote %s" % app_out)
    print("wrote %s" % rtl_out)
    if uncorrelated_pcs:
        print("warning: %d profiled addresses were not correlated to any "
              "listing line (%d total hits)" % (
                  len(uncorrelated_pcs), sum(uncorrelated_pcs.values())),
              file=sys.stderr)


if __name__ == '__main__':
    main()
