"""Target-width constant folding and demanded-bit shift regression tests."""

import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
ENV = {**os.environ, "DCC_MIR_REQUIRE_COMPLETE": "1",
       "DCC_MIR_REQUIRE_EMIT": "1", "DCC_MIR_CACHE_VERIFY": "1"}


@unittest.skipUnless((ROOT / "dcc").is_file(), "built dcc required")
class MirIntegerOptimizationTests(unittest.TestCase):
    def compile(self, source, options=()):
        return self.compile_report(source, options)[0]

    def compile_report(self, source, options=()):
        with tempfile.TemporaryDirectory(prefix="dcc-integer-opt-") as directory:
            path = Path(directory)
            source_path = path / "probe.c"
            output_path = path / "PROBE.MAC"
            source_path.write_text(source, encoding="ascii")
            result = subprocess.run(
                [str(ROOT / "dcc"), "-c", "-I", str(ROOT), *options,
                 str(source_path), "-o", str(output_path)],
                env=ENV, capture_output=True, text=True, timeout=30,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return output_path.read_text(encoding="ascii"), result.stderr

    def test_little_endian_writers_combine(self):
        for value_type in ("unsigned int", "int"):
            for body in (
                "p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);",
                "p[0] = v & 255; p[1] = (v >> 8) & 255;",
            ):
                source = "void probe(char *p, " + value_type + " v) { " + body + " }\n"
                for options in ((), ("-fstack-check",), ("-gline",)):
                    with self.subTest(value_type=value_type, body=body, options=options):
                        with mock.patch.dict(ENV, {"DCC_MIR_CSE_REPORT": "1"}):
                            assembly, report = self.compile_report(source, options)
                        self.assertIn("little-endian-store function=probe combined=1", report)
                        self.assertNotRegex(assembly, r"\bsbc a,a\b|\bsrl h\b")
                        self.assertRegex(assembly, r"\bld \(hl\),e\n\s*inc hl\n\s*ld \(hl\),d")

    def test_little_endian_writer_near_misses(self):
        bodies = (
            "p[0] = v; p[2] = v >> 8;",
            "p[0] = v; q[1] = v >> 8;",
            "p[0] = v; p[1] = v >> 7;",
            "p[0] = v; p[1] = v >> 9;",
            "p[1] = v; p[0] = v >> 8;",
            "p[0] = v; p[1] = other >> 8;",
            "p[0] = v; p[1] = (_Bool)(v >> 8);",
            "p[0] = (_Bool)v; p[1] = v >> 8;",
            "p[0] = v; observe(p); p[1] = v >> 8;",
            "p[0] = v; other = p[0]; p[1] = v >> 8;",
            "p[0] = v; *q = 23; p[1] = v >> 8;",
            "p[0] = v; if (other) p[1] = v >> 8;",
        )
        for body in bodies:
            with self.subTest(body=body):
                source = ("extern void observe(char *);\n"
                          "void probe(char *p, char *q, unsigned int v) {\n"
                          " volatile int other = 17;\n" + body + "\n}\n")
                with mock.patch.dict(ENV, {"DCC_MIR_CSE_REPORT": "1"}):
                    _, report = self.compile_report(source)
                self.assertNotIn("little-endian-store function=probe", report)
        for qualifier in ("volatile char *p", "volatile char **p"):
            with self.subTest(qualifier=qualifier):
                access = "p" if "**" not in qualifier else "(*p)"
                source = ("void probe(" + qualifier + ", unsigned int v) { " +
                          access + "[0] = v; " + access + "[1] = v >> 8; }\n")
                with mock.patch.dict(ENV, {"DCC_MIR_CSE_REPORT": "1"}):
                    _, report = self.compile_report(source)
                self.assertNotIn("little-endian-store function=probe", report)

    def machine_sizes(self, source):
        assembly = self.compile(source)
        sizes = {}
        with tempfile.TemporaryDirectory(prefix="dcc-writer-size-") as directory:
            path = Path(directory)
            (path / "RAW.MAC").write_text(assembly, encoding="ascii")
            result = subprocess.run(
                [str(ROOT / "dccpeep"), "RAW.MAC", "PEEP.MAC"], cwd=path,
                capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            for name in ("RAW", "PEEP"):
                with self.subTest(pipeline=name):
                    result = subprocess.run(
                        [str(ROOT / "m80c"), "=" + name + ".MAC",
                         "/X", "/O", "/Z", "/L"], cwd=path,
                        capture_output=True, text=True, timeout=30)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    listing = (path / (name + ".PRN")).read_text()
                    end = re.search(
                        r"^([0-9A-F]+)\s+\d+\s+;@dcc.lto end _probe$",
                        listing, re.MULTILINE)
                    self.assertIsNotNone(end, listing)
                    sizes[name] = int(end.group(1), 16)
        return sizes

    @unittest.skipUnless((ROOT / "m80c").is_file() and (ROOT / "dccpeep").is_file(),
                         "built assembler and peephole optimizer required")
    def test_little_endian_writer_machine_size(self):
        source = ("void probe(char *p, unsigned int v) {\n"
                  " p[0] = (unsigned char)v;\n"
                  " p[1] = (unsigned char)(v >> 8);\n}\n")
        for pipeline, size in self.machine_sizes(source).items():
            self.assertLessEqual(size, {"RAW": 25, "PEEP": 20}[pipeline])

    @unittest.skipUnless((ROOT / "m80c").is_file() and (ROOT / "dccpeep").is_file(),
                         "built assembler and peephole optimizer required")
    def test_bit_window_copy_and_decimal_machine_sizes(self):
        probes = (
            ("unsigned int probe(unsigned int v) { return (v >> 7) & 255; }",
             {"RAW": 18, "PEEP": 16}),
            ('#include "string.h"\n'
             "char *probe(char *d, const char *s, unsigned int n) {"
             " memcpy(d,s,n); return d+n; }",
             {"RAW": 39, "PEEP": 39}),
            ("void probe(char *dest, int value, int width) { int i;"
             " if(value<0) value=0;"
             " for(i=width-1;i>=0;--i) {"
             " dest[i]='0'+value%10; value=value/10; } dest[width]=0; }",
             {"RAW": 177, "PEEP": 154}),
        )
        for source, maximum in probes:
            for pipeline, size in self.machine_sizes(source).items():
                with self.subTest(source=source, pipeline=pipeline):
                    self.assertLessEqual(size, maximum[pipeline])

    @staticmethod
    def code(assembly):
        return "\n".join(line.split(";", 1)[0].strip()
                         for line in assembly.splitlines()
                         if line.split(";", 1)[0].strip())

    def test_constant_trees_match_literal_returns(self):
        cases = (
            ("unsigned int", "(65535U - 24) / (15 + 2)", "3853U"),
            ("int", "(int)65535U / 2", "0"),
            ("int", "(int)65529U % 3", "-1"),
            ("int", "((int)65535U >> 7) + 2", "1"),
            ("int", "(int)65535U < 0", "1"),
            ("int", "!((unsigned long)(unsigned int)32768UL * 2UL)", "0"),
            ("int", "(_Bool)((unsigned long)(unsigned int)32768UL * 2UL)", "1"),
            ("long", "(-80000L / 7L) + (-80000L % 7L)", "-11432L"),
            ("unsigned long", "((4294967295UL - 24UL) / 17UL)",
             "252645133UL"),
            ("unsigned long", "(65535UL * 65535UL) ^ 0xffUL",
             "4294836478UL"),
            ("unsigned int", "((unsigned int)32768UL + 32768U) | 3", "3U"),
        )
        for options in ((), ("-fstack-check",), ("-gline",)):
            for return_type, expression, expected in cases:
                with self.subTest(expression=expression, options=options):
                    template = return_type + " probe(void) { return %s; }\n"
                    actual = self.compile(template % expression, options)
                    literal = self.compile(template % expected, options)
                    self.assertEqual(self.code(actual), self.code(literal))

    def test_invalid_constant_operations_are_not_folded(self):
        for expression, helper in (("(65535U - 24) / zero", "__divu"),
                                   ("(65535U - 24) % zero", "__modu")):
            with self.subTest(expression=expression):
                assembly = self.compile(
                    "unsigned int probe(void) { unsigned int zero = 0; return " +
                    expression + "; }\n")
                self.assertIn("call " + helper, assembly)

    def test_masked_shift_uses_only_the_low_word(self):
        for signedness in ("long", "unsigned long"):
            source = (
                "unsigned int probe(" + signedness + " position, int identity) {\n"
                " return ((unsigned int)(position >> 7) + identity * 3) & 15;\n"
                "}\n"
            )
            for options in ((), ("-fstack-check",), ("-gline",)):
                with self.subTest(signedness=signedness, options=options):
                    assembly = self.compile(source, options)
                    self.assertNotRegex(assembly, r"\bsra d\b|\bsrl d\b|\brr e\b")
                    self.assertRegex(assembly, r"\badd hl,hl\n\s*ld l,h\n\s*ld h,0")
                    self.assertNotRegex(assembly, r"\bcall __[lr][gs]s\b")

    def test_shift_keeps_upper_bits_when_they_are_observable(self):
        bodies = (
            "return (unsigned int)(position >> 7);",
            "return (unsigned int)(position >> 7) & 1023;",
            "unsigned int saved = (unsigned int)(position >> 7); return saved;",
            "return ((unsigned int)(position >> 16) + identity) & 15;",
            "return ((unsigned int)(position >> identity) + identity) & 15;",
            "return !!(unsigned int)(position >> 7);",
        )
        for body in bodies:
            with self.subTest(body=body):
                assembly = self.compile(
                    "unsigned int probe(long position, int identity) { " +
                    body + " }\n")
                self.assertNotEqual(
                    len(re.findall(r"\bsra d\b|\bcall __lgs\b|\bld l,e\b",
                                   assembly)), 0, assembly)

    def test_demand_proof_is_bounded(self):
        source = (
            "unsigned int probe(long position, int identity) { return (" +
            "(unsigned int)(position >> 7)" + " + identity" * 80 +
            ") & 15; }\n"
        )
        assembly = self.compile(source)
        self.assertRegex(assembly, r"\bsra d\b|\bcall __lgs\b")

    def test_word_bit_windows_and_full_width_controls(self):
        for signedness in ("int", "unsigned int"):
            for count in range(4, 8):
                with self.subTest(signedness=signedness, count=count):
                    template = ("unsigned int probe(" + signedness +
                                " v) { return (v >> " + str(count) + ") %s; }\n")
                    narrowed = self.compile(template % "& 255")
                    self.assertRegex(narrowed, r"\bld l,h\n\s*ld h,0")
                    self.assertNotRegex(narrowed, r"\bsr[al] h\b")
                    complete = self.compile(template % "")
                    self.assertRegex(complete, r"\bsr[al] h\b")
        for suffix in ("& 511", "+ 1", "!= 0"):
            with self.subTest(suffix=suffix):
                assembly = self.compile(
                    "unsigned int probe(unsigned int v) { return (v >> 7) " +
                    suffix + "; }\n")
                self.assertRegex(assembly, r"\bsrl h\b")

    def test_byte_crossing_word_shifts(self):
        for count in range(8, 16):
            for value_type, operator in (("unsigned int", "<<"),
                                         ("unsigned int", ">>"), ("int", ">>")):
                source = (f"{value_type} probe({value_type} v) {{"
                          f" return v {operator} {count}; }}\n")
                for options in ((), ("-fstack-check",), ("-gline",), ("-g",)):
                    with self.subTest(count=count, value_type=value_type,
                                      operator=operator, options=options):
                        assembly = self.compile(source, options)
                        self.assertNotRegex(assembly, r"\b(?:srl h|sra h|rr l|add hl,hl)\b")
                        if operator == ">>":
                            self.assertRegex(assembly, r"\bld l,(?:h|a)\b")
                            if value_type == "unsigned int" and count >= 12:
                                self.assertIn(f"and {255 >> (count - 8)}", assembly)
                                self.assertEqual(assembly.count("rlca"), 16 - count)
                            elif value_type == "int" and count == 15:
                                self.assertIn("sbc a,a", assembly)
                                self.assertNotRegex(assembly, r"\bsra [al]\b")
                            else:
                                shift = "srl l" if value_type == "unsigned int" else r"sra [al]"
                                self.assertEqual(len(re.findall(shift, assembly)), count - 8)
                        elif count == 8:
                            self.assertRegex(assembly, r"\bld h,(?:l|a)\b")
                        else:
                            self.assertIn("ld h,a", assembly)
                            self.assertEqual(assembly.count("add a,a"), count - 8)
        variable = self.compile(
            "unsigned int probe(unsigned int v, int n) { return v >> n; }\n")
        self.assertRegex(variable, r"\bsrl h\n\s*rr l\b")
        self.assertIn("djnz", variable)

    @unittest.skipUnless((ROOT / "m80c").is_file() and (ROOT / "dccpeep").is_file(),
                         "built assembler and peephole optimizer required")
    def test_byte_crossing_word_shift_machine_sizes(self):
        for count in range(8, 16):
            for value_type, operator, maximum in (
                    ("unsigned int", "<<", 9 + count - 8),
                    ("unsigned int", ">>",
                     9 + 2 * (count - 8) if count < 12 else 12 + 16 - count),
                    ("int", ">>", 10 + 2 * (count - 8) if count < 15 else 10)):
                source = (f"{value_type} probe({value_type} v) {{"
                          f" return v {operator} {count}; }}")
                for pipeline, size in self.machine_sizes(source).items():
                    with self.subTest(count=count, value_type=value_type,
                                      operator=operator, pipeline=pipeline):
                        self.assertLessEqual(size, maximum)

    def test_memcpy_end_pointer_and_near_misses(self):
        header = '#include "string.h"\n'
        positive = (
            "char *probe(char *d, const char *s, unsigned int n) {"
            " memcpy(d,s,n); d += n; return d; }\n")
        for options in ((), ("-fstack-check",), ("-gline",)):
            with self.subTest(options=options):
                assembly = self.compile(header + positive, options)
                self.assertRegex(assembly, r"\bcall __mcf\n\s*ex de,hl")
                tail = assembly.split("call __mcf", 1)[1]
                self.assertNotIn("add hl,de", tail)
        negatives = (
            "char *probe(char *d, const char *s, unsigned int n) {"
            " char *r=memcpy(d,s,n); d+=n; return r; }",
            "int *probe(int *d, const char *s, unsigned int n) {"
            " memcpy(d,s,n); d+=n; return d; }",
            "char *probe(char *d, const char *s, unsigned int n) {"
            " memcpy(d,s,n); d+=n+1; return d; }",
            "char *probe(char *d, const char *s, unsigned int *n) {"
            " memcpy(d,s,*n); d+=*n; return d; }",
            "char *probe(char *d, const char *s, volatile unsigned int n) {"
            " memcpy(d,s,n); d+=n; return d; }",
        )
        for body in negatives:
            with self.subTest(body=body):
                self.assertNotRegex(self.compile(header + body),
                                    r"\bcall __mcf\n\s*ex de,hl")

    def test_remainder_first_decimal_fusion(self):
        source = (
            "void probe(char *dest, int value, int width) { int i;"
            " if(value<0) value=0;"
            " for(i=width-1;i>=0;--i) {"
            " dest[i]='0'+value%10; value=value/10; } dest[width]=0; }\n")
        for options in ((), ("-fstack-check",), ("-gline",)):
            with self.subTest(options=options):
                assembly = self.compile(source, options)
                self.assertEqual(assembly.count("call __sdivmod"), 1)
                self.assertNotRegex(assembly, r"\bcall __(?:divs|mods)\b")
        for body in (
            "int r=*v%10; *p=r; return *v/10;",
            "int r=v%10; v=17; *p=r; return v/10;",
            "int r=v%10; observe(&v); *p=r; return v/10;",
        ):
            with self.subTest(body=body):
                parameter = "int *v" if "*v" in body else "int v"
                assembly = self.compile(
                    "extern void observe(int *);\n"
                    "int probe(char *p, " + parameter + ") {" + body + "}\n")
                self.assertNotIn("call __sdivmod", assembly)
        assembly = self.compile(
            "int probe(char *p, volatile int v) {"
            " int r=v%10; *p=r; return v/10; }\n")
        self.assertNotIn("call __sdivmod", assembly)

    def test_byte_only_cast_does_not_extend_but_word_use_does(self):
        source = "void probe(char *p, int v) { *p=(char)(v+48); }\n"
        for options in ((), ("-fstack-check",), ("-gline",)):
            with self.subTest(options=options):
                self.assertNotIn("sbc a,a", self.compile(source, options))
        for result in ("(char)v", "(_Bool)v", "(int)(char)v"):
            with self.subTest(result=result):
                assembly = self.compile(
                    "int probe(int v) { return " + result + "; }\n")
                self.assertRegex(assembly, r"\bsbc a,a\b|\bor l\b")
        for value_type in ("char", "unsigned char"):
            for options in ((), ("-fstack-check",), ("-gline",)):
                with self.subTest(value_type=value_type, options=options):
                    assembly = self.compile(
                        "void probe(char *p, " + value_type +
                        " v) { *p=v; }\n", options)
                    self.assertNotIn("sbc a,a", assembly)
            if value_type == "char":
                assembly = self.compile("int probe(char v) { return v; }\n")
                self.assertIn("sbc a,a", assembly)

    def test_line_debug_preserves_optimized_code(self):
        source = (
            "unsigned int probe(long position, int identity) {\n"
            " return (((unsigned int)(position >> 7) + identity * 3) & 15)\n"
            "        + (65535U - 24) / 17;\n"
            "}\n"
        )
        for options in ((), ("-fstack-check",)):
            with self.subTest(options=options):
                self.assertEqual(
                    self.code(self.compile(source, options)),
                    self.code(self.compile(source, (*options, "-gline"))))

    def test_volatile_fetch_still_reads_both_words(self):
        source = (
            "volatile long observed;\n"
            "unsigned int fetch(void) {\n"
            " return ((unsigned int)(observed >> 7) + 3) & 15;\n"
            "}\n"
        )
        assembly = self.compile(source)
        self.assertRegex(assembly, r"\bld (?:hl|de|bc|iy),\(_observed\)")
        self.assertRegex(assembly, r"\bld (?:hl|de|bc|iy),\(_observed\+2\)")

    @unittest.skipUnless(shutil.which("ntvcm"), "ntvcm required for target oracle")
    def test_target_integer_oracles_in_both_pipelines(self):
        source = r'''
#include "stdio.h"
#include "string.h"
unsigned int bucket(long position, int identity) {
    return ((unsigned int)(position >> 7) + identity * 3) & 15;
}
unsigned int unsigned_bucket(unsigned long position, int identity) {
    return (((unsigned int)(position >> 7) - identity) ^ 15) & 15;
}
unsigned int wide(long position) {
    return (unsigned int)(position >> 7) & 1023;
}
unsigned int truth(long position) {
    return !!(unsigned int)(position >> 7);
}
unsigned int window4(unsigned int v) { return (v >> 4) & 255; }
unsigned int window5(unsigned int v) { return (v >> 5) & 255; }
unsigned int window6(unsigned int v) { return (v >> 6) & 255; }
unsigned int window7(unsigned int v) { return (v >> 7) & 255; }
unsigned int signed_window4(int v) { return (v >> 4) & 255; }
unsigned int signed_window5(int v) { return (v >> 5) & 255; }
unsigned int signed_window6(int v) { return (v >> 6) & 255; }
unsigned int signed_window7(int v) { return (v >> 7) & 255; }
unsigned int xor_right9(unsigned int v) { return v ^ (v >> 9); }
unsigned int xor_left8(unsigned int v) { return v ^ (v << 8); }
void byte_store(char *p, char v) { *p=v; }
int byte_word(char v) { return v; }
int byte_shared(char *p, char v) { *p=v; return v; }
char *copy_end(char *d, const char *s, unsigned int n) {
    memcpy(d,s,n); d+=n; return d;
}
char *copy_alias(char *d, const char *s, unsigned int *n) {
    memcpy(d,s,*n); d+=*n; return d;
}
void decimal(char *dest, int value, int width) {
    int i;
    if (value < 0) value = 0;
    for (i=width-1; i>=0; --i) {
        dest[i]='0'+value%10;
        value=value/10;
    }
    dest[width]=0;
}
int signed_parts(char *p, int value) {
    *p=value%10;
    value=value/10;
    return value;
}
int alias_parts(char *p, int *value) {
    *p=*value%10;
    return *value/10;
}
int value_checks(void) {
    static char source[260], dest[260];
    static int numbers[] = {-32768,-1,0,9,10,99,100,32767};
    static char *digits[] = {"00000","00000","00000","00009",
                            "00010","00099","00100","32767"};
    static unsigned int counts[] = {0,1,7,128,255};
    union Block { unsigned int n; char bytes[16]; } block;
    unsigned int v, n;
    int i, q;
    char text[6], remainder;
    for (i=0;i<260;++i) source[i]=(char)i;
    for (i=0;i<5;++i) {
        n=counts[i];
        dest[n]=73;
        if (copy_end(dest,source,n)!=dest+n ||
            memcmp(dest,source,n) || dest[n]!=73)
            return 1;
    }
    block.n=1; source[0]=7;
    if (copy_alias(block.bytes,source,&block.n)!=block.bytes+7 ||
        block.n!=7)
        return 2;
    block.n=123;
    if (alias_parts(block.bytes,(int *)&block.n)!=0 || block.n!=3)
        return 3;
    for (i=0;i<8;++i) {
        decimal(text,numbers[i],5);
        if (strcmp(text,digits[i])) return 4;
        q=signed_parts(&remainder,numbers[i]);
        if ((long)q!=(long)numbers[i]/10L ||
            remainder!=(char)((long)numbers[i]%10L))
            return 5;
    }
    for (i=-128;i<=127;++i) {
        byte_store(&remainder,(char)i);
        if (remainder!=i || byte_word((char)i)!=i ||
            byte_shared(&remainder,(char)i)!=i || remainder!=i)
            return 8;
    }
    v=0;
    do {
        if (window4(v)!=((v/16U)&255U) ||
            signed_window4((int)v)!=((v/16U)&255U))
            return 6;
        if (window5(v)!=((v/32U)&255U) ||
            window6(v)!=((v/64U)&255U) ||
            window7(v)!=((v/128U)&255U) ||
            signed_window5((int)v)!=((v/32U)&255U) ||
            signed_window6((int)v)!=((v/64U)&255U) ||
            signed_window7((int)v)!=((v/128U)&255U))
            return 7;
        if (xor_right9(v)!=(unsigned int)((unsigned long)v ^ ((unsigned long)v >> 9)) ||
            xor_left8(v)!=(unsigned int)((unsigned long)v ^ ((unsigned long)v << 8)))
            return 9;
        ++v;
    } while (v);
    return 0;
}
void put16(char *p, unsigned int v) {
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}
void put_signed(char *p, int v) {
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}
void put_masked(char *p, unsigned int v) {
    p[0] = v & 255;
    p[1] = (v >> 8) & 255;
}
void aliasing(char *p, unsigned int *v) {
    p[0] = (unsigned char)*v;
    p[1] = (unsigned char)(*v >> 8);
}
int writer_checks(void) {
    static unsigned int values[] = {0, 1, 127, 128, 255, 256,
        0x7fff, 0x8000, 0xa581, 0xff00, 0xffff};
    unsigned char bytes[4];
    unsigned int words[2];
    unsigned int v;
    int i, writer;
    for (i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        v = values[i];
        for (writer = 0; writer < 3; ++writer) {
            bytes[0] = 0x35; bytes[3] = 0xca;
            if (writer == 0) put16((char *)bytes + 1, v);
            else if (writer == 1) put_signed((char *)bytes + 1, (int)v);
            else put_masked((char *)bytes + 1, v);
            if (bytes[0] != 0x35 || bytes[3] != 0xca ||
                bytes[1] != (v & 255) || bytes[2] != v / 256U)
                return 1;
        }
    }
    words[0] = 0xa581; words[1] = 0xce42;
    aliasing((char *)words + 1, words);
    if (words[0] != 0x8181 || words[1] != 0xce81)
        return 2;
    return 0;
}
volatile long observed;
volatile long shared_observation;
struct Flags { char pad[3]; unsigned char first; char gap; unsigned char second; };
volatile struct Flags flags;
void set_flag(int condition) {
    if (condition)
        flags.first = 0;
    else
        flags.second = 193;
}
unsigned int fetch(void) {
    return ((unsigned int)(observed >> 7) + 3) & 15;
}
unsigned int shared(long position) {
    long shifted = position >> 7;
    shared_observation = shifted;
    return (unsigned int)shifted & 15;
}
struct Cell { char prefix; int value; long tail; };
static struct Cell cells[] = {{1, 101, 1001L}, {2, 202, 2002L}, {3, 303, 3003L}};
typedef char Row[5];
typedef Row Pair[2];
static const Pair rows[] = {{"1", "12"}, {"123", "1234"}};
int addresses(void) {
    int failures = 0;
    long *tail = &cells[2].tail;
    if (*tail != 3003L)
        ++failures;
    *tail = 3333L;
    if (cells[1].value != 202 || cells[2].tail != 3333L)
        ++failures;
    if (rows[0][0][0] != '1' || rows[0][0][1] != 0 ||
        rows[1][1][3] != '4' || rows[1][1][4] != 0)
        ++failures;
    return failures;
}
int main(void) {
    static long values[] = {0L, 1L, 127L, 128L, 2047L, 2048L,
        32767L, 32768L, 65535L, 65536L, 8388608L, 2147483647L,
        -1L, -128L, -129L, -32768L, -65536L, -2147483647L - 1L};
    int i, j;
    unsigned long bits;
    unsigned int quotient;
    for (i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        bits = (unsigned long)values[i];
        quotient = (unsigned int)(bits / 128UL);
        for (j = -3; j < 4; ++j)
            if (bucket(values[i], j) != ((quotient + j * 3) & 15) ||
                unsigned_bucket(bits, j) != (((quotient - j) ^ 15) & 15))
                return 1;
        if (wide(values[i]) != (quotient & 1023) ||
            truth(values[i]) != (quotient != 0))
            return 2;
        observed = values[i];
        if (fetch() != ((quotient + 3) & 15))
            return 3;
        if (shared(values[i]) != (quotient & 15) ||
            shared_observation != values[i] >> 7)
            return 5;
    }
    if ((65535U - 24) / (15 + 2) != 3853U ||
        (int)65535U / 2 != 0 || (int)65529U % 3 != -1 ||
        -80000L / 7L != -11428L || -80000L % 7L != -4L)
        return 4;
    if (addresses())
        return 6;
    if (writer_checks())
        return 10;
    if (crossing_checks())
        return 12;
    i=value_checks();
    if (i) {
        printf("FAIL value check %d\n",i);
        return 11;
    }
    flags.first = 17;
    flags.second = 29;
    set_flag(1);
    if (flags.first != 0 || flags.second != 29)
        return 8;
    set_flag(0);
    if (flags.first != 0 || flags.second != 193)
        return 9;
    if (!((unsigned long)(unsigned int)32768UL * 2UL))
        return 7;
    puts("PASS integer optimizations");
    return 0;
}
'''
        crossing = []
        checks = []
        for count in range(8, 16):
            for name, value_type, wide_type, operator in (
                    ("left", "unsigned int", "unsigned long", "<<"),
                    ("right", "unsigned int", "unsigned long", ">>"),
                    ("signed", "int", "long", ">>")):
                function = f"crossing_{name}{count}"
                crossing.append(
                    f"{value_type} {function}({value_type} v) {{"
                    f" return v {operator} {count}; }}")
                checks.append(
                    f"if ({function}(({value_type})v) != "
                    f"({value_type})(({wide_type})({value_type})v {operator} {count}))"
                    " return 1;")
        crossing.append(
            "int crossing_checks(void) {\n"
            " static unsigned int values[] = {0,1,127,128,255,256,257,"
            "32767,32768U,32769U,65534U,65535U};\n"
            " unsigned int v; int i;\n"
            " for (i=0;i<sizeof(values)/sizeof(values[0]);++i) {\n"
            " v=values[i];\n" + "\n".join(checks) + "\n} return 0;\n}")
        source = source.replace("int main(void) {",
                                "\n".join(crossing) + "\nint main(void) {")
        with tempfile.TemporaryDirectory(prefix="dcc-integer-run-") as directory:
            path = Path(directory)
            (path / "probe.c").write_text(source, encoding="ascii")
            release = {}
            for peep, debug in ((p, d) for p in ("true", "false")
                                for d in ("false", "lines", "true")):
                with self.subTest(peep=peep, debug=debug):
                    result = subprocess.run(
                        [str(ROOT / "dccmake"), "probe.c", "dcc-output=PROBE",
                         "dcc-peep=" + peep, "dcc-stack-check=true",
                         "dcc-debug=" + debug,
                         "dcc-stack-bytes=1024", "dcc-include-directory=" + str(ROOT),
                         "dcc-runtime=" + str(ROOT / "DCCRTL.MAC")],
                        cwd=path, env=ENV, capture_output=True, text=True,
                        timeout=60,
                    )
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    binary = (path / "build" / "PROBE.COM").read_bytes()
                    if debug == "false":
                        release[peep] = binary
                    elif debug == "lines":
                        self.assertEqual(binary, release[peep])
                    result = subprocess.run(
                        [shutil.which("ntvcm"), "-p", "-s:0",
                         str(path / "build" / "PROBE.COM")],
                        cwd=path, capture_output=True, timeout=30,
                    )
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn(b"PASS integer optimizations", result.stdout)


if __name__ == "__main__":
    unittest.main()
