"""Compiler-backed automatic bank interfaces; all scratch data stays in build/."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
LINKER = Path(os.environ.get("DCC_L80C", str(ROOT / "l80c")))

API = """#ifndef API_H
#define API_H
typedef unsigned int handle;
typedef unsigned char byte;
unsigned int onload(void);
unsigned int onunload(void);
unsigned int veg(unsigned int);
unsigned int marm(unsigned int);
handle opnobj(unsigned int seed);
unsigned int objread(handle object, unsigned int offset, byte *buf, unsigned int count);
void objclose(handle object);
#pragma dcc bank_buffer objread 3 out 4 128
#endif
"""
SOURCE = """#include "API.H"
static unsigned int internal(unsigned int n) { return n + 1; }
extern unsigned int asmheap(unsigned int n);
unsigned int onload(void) { return 1; }
unsigned int onunload(void) { return 1; }
unsigned int veg(unsigned int n) { return internal(n); }
unsigned int marm(unsigned int n) { return n + 2; }
handle opnobj(unsigned int seed) { return seed + 3; }
unsigned int objread(handle object, unsigned int offset, byte *buf, unsigned int count) {
 unsigned int i;
 for (i = 0; i < count; ++i) buf[i] = (byte)(object + offset + i);
 return count;
}
void objclose(handle object) { (void)object; }
"""

RECORD = """struct Record {
 unsigned char flags;
 unsigned int code;
 unsigned long total;
 unsigned char name[8];
};
"""


@unittest.skipUnless((ROOT / "dcc").is_file() and (ROOT / "dccmake").is_file(),
                     "built native tools required")
class TypedLibraryTests(unittest.TestCase):
    def setUp(self):
        (ROOT / "build").mkdir(exist_ok=True)
        work = tempfile.TemporaryDirectory(prefix="typed-bank-", dir=ROOT / "build")
        self.addCleanup(work.cleanup)
        self.folder = Path(work.name)
        (self.folder / "API.H").write_text(API)
        (self.folder / "LIB.C").write_text(SOURCE)
        (self.folder / "BANKLIB.H").write_text(
            "#define BL_ARGUMENT 7\n#define BL_BOUNDS 8\n"
            "typedef struct { unsigned int generation, index; } bank_library_binding_t;\n"
            "#define bank_library_call_bound blbnd\n"
            "unsigned int bank_library_call_bound(bank_library_binding_t *, "
            "const char *, const char *, void *, unsigned int);\n"
            "unsigned int blcall(const char *, const char *, void *, unsigned int);\n"
            "unsigned int blerr(void);\nvoid blfail(unsigned int);\n")
        self.configure()

    def configure(self, **overrides):
        settings = {
            "dcc-input": "LIB.C", "dcc-output": "SPREADS",
            "dcc-output-format": "lib", "dcc-exports": "auto",
            "dcc-library-api": "API.H", "dcc-library-host-dir": "generated",
            "dcc-stack-bytes": "512", "dcc-peep": "false",
            "dcc-runtime": str(ROOT / "DCCRTL.MAC"),
            "dcc-include-directory": f"{self.folder},{ROOT}",
            "dcc-tool": str(ROOT / "dcc"),
            "dccpeep-tool": str(ROOT / "dccpeep"),
            "dccrtlstrip-tool": str(ROOT / "dccrtlstrip"),
            "m80c-tool": str(ROOT / "m80c"),
            "l80c-tool": str(LINKER),
        }
        settings.update(overrides)
        (self.folder / "dccmake.txt").write_text(
            "".join(f"{key}={value}\n" for key, value in settings.items()))

    def build(self, *arguments):
        return subprocess.run([str(ROOT / "dccmake"), *arguments], cwd=self.folder,
                              capture_output=True, text=True,
                              env={**os.environ, "DCC_ARGS": ""})

    def automatic_source(self):
        (self.folder / "API.H").unlink()
        (self.folder / "LIB.C").write_text(
            "typedef unsigned int handle;\ntypedef unsigned char byte;\n"
            "#pragma dcc bank_buffer objread 3 out 4 128\n" +
            SOURCE.replace('#include "API.H"\n', ""))
        self.configure(**{"dcc-library-api": "auto"})

    def record_source(self, source):
        (self.folder / "LIB.C").write_text(source)
        self.configure(**{"dcc-library-api": "auto"})

    def test_record_metadata_contract_bulk_copies_and_target_layout_guards(self):
        self.record_source(
            "struct Record;\nunsigned int adjust(struct Record *record);\n" +
            RECORD + "typedef struct Record PublicRecord;\n"
            "#pragma dcc bank_record adjust 1 inout\n"
            "unsigned int adjust(PublicRecord *record) { "
            "record->total += record->code; return record->flags; }\n")
        result = self.build("dcc-contract-only=true")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        metadata = (self.folder / "build/B1M01.API").read_text()
        self.assertTrue(metadata.startswith("DCCBANK 2\n"))
        self.assertIn("F adjust 1 u16 1 0 pr:Record", metadata)
        self.assertIn("R Record 15 4 1", metadata)
        self.assertIn("M code u16 1 0", metadata)
        self.assertIn("M total u32 3 0", metadata)
        self.assertIn("M name u8 7 8", metadata)
        header = (self.folder / "generated/API.H").read_text()
        host = (self.folder / "generated/B1HOST.C").read_text()
        adapter = (self.folder / "build/B1ADAPT.C").read_text()
        self.assertTrue(header.startswith(
            "/* Generated by dccmake; library source is authoritative. */\n#pragma once\n"))
        self.assertNotIn("#ifndef", header)
        self.assertIn("#if !defined(__INTELLISENSE__)", header)
        self.assertIn("struct Record {", header)
        self.assertIn("unsigned long total;", header)
        self.assertIn("unsigned char name[8];", header)
        self.assertIn("#pragma dcc bank_record adjust 1 inout", header)
        self.assertIn("adjust(struct Record * record);", header)
        self.assertIn("offsetof(struct Record, total) == 3", header)
        self.assertIn("u16(inout:rec1le:Record[15]{flags:u8@0;code:u16@1;"
                      "total:u32@3;name:u8[8]@7})", host)
        self.assertIn("unsigned char f[17];", host)
        self.assertNotIn("memset(f, 0, n)", host)
        self.assertIn("memcpy(f + 2, a0, 15)", host)
        self.assertIn("memcpy(a0, f + 2, 15)", host)
        self.assertIn("a0 = (struct Record *)(f + 2)", adapter)
        self.assertEqual(adapter.count("b1get(wire)"), 1)
        self.assertNotIn("unsigned int n =", host + adapter)
        self.assertNotIn("malloc", host + adapter)
        self.compile(self.folder / "generated/B1HOST.C", self.folder / "H.MAC")
        self.compile(self.folder / "build/B1ADAPT.C", self.folder / "A.MAC")
        repeated = self.folder / "TWICE.C"
        repeated.write_text(
            '#include "generated/API.H"\n#include "generated/../generated/API.H"\n'
            'unsigned int recsize(void) { return sizeof(struct Record); }\n')
        self.compile(repeated, self.folder / "TWICE.MAC")
        if shutil.which("clang"):
            result = subprocess.run(
                ["clang", "-x", "c", "-std=c11", "-fsyntax-only",
                 str(self.folder / "generated/API.H")],
                capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("bank record size: Record", result.stderr)
            result = subprocess.run(
                ["clang", "-x", "c", "-std=c11", "-Wno-unknown-pragmas",
                 "-D__INTELLISENSE__", "-fsyntax-only", str(repeated)],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        (self.folder / "generated/API.H").write_text(
            header.replace("sizeof(struct Record) == 15", "sizeof(struct Record) == 16"))
        result = subprocess.run(
            [str(ROOT / "dcc"), "-module", "-I", str(ROOT), str(repeated),
             "-o", str(self.folder / "BAD.MAC")],
            capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("bank record size: Record", result.stderr)

    def test_record_annotations_and_unsupported_layouts_are_rejected(self):
        cases = [
            ("struct Bad { unsigned int n; };", "", "bank_record annotation"),
            ("struct Bad { unsigned int n; };",
             "#pragma dcc bank_record bad 1 wrong\n", "invalid copy direction"),
            ("struct Bad { unsigned int n; };",
             "#pragma dcc bank_record bad 0 in\n", "1-based index"),
            ("struct Bad { unsigned int n; };",
             "#pragma dcc bank_record bad 1 in 2\n", "bank_record FUNCTION"),
            ("struct Bad { unsigned int n; };",
             "#pragma dcc bank_record bad 1 in\n"
             "#pragma dcc bank_record bad 1 out\n", "conflicting copy"),
            ("struct Bad { unsigned char *pointer; };", None, "pointer members"),
            ("struct Bad { unsigned int bits:3; };", None, "bitfields"),
            ("struct Bad { float value; };", None, "flat struct"),
            ("struct Inner { int value; }; struct Bad { struct Inner inner; };",
             None, "nested structures"),
            ("struct Bad { unsigned char grid[2][3]; };", None, "one-dimensional"),
            ("struct Bad { volatile unsigned int value; };", None, "non-volatile"),
            ("struct Bad { const unsigned int value; };", None, "non-const"),
            ("typedef const unsigned int Readonly; struct Bad { Readonly value; };",
             None, "non-const"),
            ("struct Bad;", None, "complete flat struct"),
        ]
        for definition, annotation, message in cases:
            with self.subTest(message=message, definition=definition):
                if annotation is None:
                    annotation = "#pragma dcc bank_record bad 1 inout\n"
                self.record_source(definition + "\n" + annotation +
                                   "unsigned int bad(struct Bad *record) { return 1; }\n")
                result = self.build("dcc-contract-only=true")
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertIn(message, result.stderr)
                self.assertFalse((self.folder / "generated/API.H").exists())
        self.record_source("union Bad { unsigned int value; unsigned char byte; };\n"
                           "#pragma dcc bank_record bad 1 inout\n"
                           "unsigned int bad(union Bad *record) { return 1; }\n")
        result = self.build("dcc-contract-only=true")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unions", result.stderr)
        self.record_source(RECORD + "#pragma dcc bank_record bad 1 out\n"
                           "unsigned int bad(const struct Record *record) { return 1; }\n")
        result = self.build("dcc-contract-only=true")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("const pointer permits only direction in", result.stderr)
        self.record_source(RECORD + "typedef const struct Record Readonly;\n"
                           "#pragma dcc bank_record bad 1 out\n"
                           "unsigned int bad(Readonly *record) { return 1; }\n")
        result = self.build("dcc-contract-only=true")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("const pointer permits only direction in", result.stderr)

    def test_record_exact_frame_limit_and_signature_limit(self):
        source = ("struct Block { unsigned char data[256]; };\n"
                  "#pragma dcc bank_record touch 1 inout\n"
                  "void touch(struct Block *block) { block->data[255] = 9; }\n")
        self.record_source(source)
        result = self.build("dcc-contract-only=true")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("unsigned char f[256];",
                      (self.folder / "generated/B1HOST.C").read_text())
        if shutil.which("clang"):
            self.run_native(r'''
#include <assert.h>
#include <string.h>
#include "generated/API.H"
#include "BANKLIB.H"
unsigned int b1e00(unsigned char *);
void blfail(unsigned int value) { (void)value; assert(0); }
unsigned int blcall(const char *name, const char *sig, void *frame, unsigned int n) {
 unsigned char wire[258]; (void)name; (void)sig;
 assert(n == 256);
 wire[0] = 0; wire[1] = 1; memcpy(wire + 2, frame, n);
 assert(b1e00(wire)); memcpy(frame, wire + 2, n); return 1;
}
int main(void) {
 struct { unsigned char before; struct Block block; unsigned char after; } guarded;
 unsigned char wire[258];
 unsigned int i;
 memset(&guarded, 0xA5, sizeof guarded);
 touch(&guarded.block);
 assert(guarded.before == 0xA5 && guarded.after == 0xA5);
 for (i = 0; i < 255; ++i) assert(guarded.block.data[i] == 0xA5);
 assert(guarded.block.data[255] == 9);
 memset(wire, 0xA5, sizeof wire);
 wire[0] = 1; wire[1] = 1; assert(!b1e00(wire));
 wire[0] = 255; wire[1] = 0; assert(!b1e00(wire));
 for (i = 2; i < sizeof wire; ++i) assert(wire[i] == 0xA5);
 return 0;
}
''', ("touch",))
        self.record_source(source.replace("void touch", "unsigned char touch")
                           .replace("= 9;", "= 9; return 1;"))
        result = self.build("dcc-contract-only=true")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("256-byte cap", result.stderr)
        tag = "R" * 63
        fields = "".join(f"unsigned char f{i}{'x' * 60};" for i in range(3))
        self.record_source(f"struct {tag} {{ {fields} }};\n"
                           "#pragma dcc bank_record touch 1 inout\n"
                           f"void touch(struct {tag} *record) {{ }}\n")
        result = self.build("dcc-contract-only=true")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("255 characters", result.stderr)

    def test_record_layouts_match_across_modules_and_handwritten_contracts(self):
        first = ("#pragma dcc bank_record first 1 in\n"
                 "unsigned int first(const struct Record *record) { return record->code; }\n")
        second = ("#pragma dcc bank_record second 1 in\n"
                  "unsigned int second(const struct Record *record) { return record->code; }\n")
        self.record_source(RECORD + first)
        (self.folder / "EXTRA.C").write_text(RECORD + second)
        result = self.build("dcc-contract-only=true", "dcc-input=LIB.C,EXTRA.C")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.folder / "generated/API.H").read_text()
                         .count("struct Record {"), 1)
        (self.folder / "EXTRA.C").write_text(
            RECORD.replace("unsigned int code", "int code") + second)
        result = self.build("dcc-contract-only=true", "dcc-input=LIB.C,EXTRA.C")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("incompatible record fields across modules", result.stderr)
        (self.folder / "API.H").write_text(
            RECORD + "#pragma dcc bank_record first 1 in\n"
            "unsigned int first(const struct Record *record);\n")
        self.configure()
        result = self.build("dcc-contract-only=true")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.compile(self.folder / "generated/B1HOST.C", self.folder / "H.MAC")
        self.compile(self.folder / "build/B1ADAPT.C", self.folder / "A.MAC")
        (self.folder / "LIB.C").write_text(
            RECORD.replace("unsigned int code", "int code") + first)
        result = self.build("dcc-contract-only=true")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("record fields differ from authoritative", result.stderr)

    @unittest.skipUnless(shutil.which("clang"), "native clang required for executable harness")
    def test_executable_record_directions_guards_aliasing_and_failure_copyback(self):
        self.record_source(r'''
struct Record { unsigned char flags; signed char shift; unsigned char name[3]; };
#pragma dcc bank_record input 1 in
#pragma dcc bank_record output 1 out
#pragma dcc bank_record mutate 1 inout
#pragma dcc bank_record pair 1 in
#pragma dcc bank_record pair 2 out
#pragma dcc bank_buffer mix 1 inout 2 8
#pragma dcc bank_record mix 3 inout
unsigned int input(const struct Record *record) {
 unsigned int value = record->flags;
 ((struct Record *)record)->flags = 200;
 return value;
}
void output(struct Record *record) {
 record->flags += 10; record->shift -= 3; record->name[2] = record->name[2] + 5;
}
void mutate(struct Record *record) {
 record->flags += 10; record->shift += 5; record->name[2] = record->name[2] + 3;
}
void pair(const struct Record *source, struct Record *destination) {
 *destination = *source;
}
void mix(unsigned char *data, unsigned int count, struct Record *record) {
 if (count) data[0] += record->flags;
 record->flags += count;
}
''')
        result = self.build("dcc-contract-only=true")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.run_native(r'''
#include <assert.h>
#include <string.h>
#include "generated/API.H"
#include "BANKLIB.H"
unsigned int b1e00(unsigned char *);
unsigned int b1e01(unsigned char *);
unsigned int b1e02(unsigned char *);
unsigned int b1e03(unsigned char *);
unsigned int b1e04(unsigned char *);
static unsigned int error, calls, fail;
void blfail(unsigned int value) { error = value; }
unsigned int blerr(void) { return error; }
unsigned int blcall(const char *name, const char *sig, void *frame, unsigned int n) {
 unsigned char wire[258]; unsigned int ok = 0; (void)sig;
 ++calls;
 if (fail) { error = 6; return 0; }
 wire[0] = n & 255; wire[1] = n >> 8; memcpy(wire + 2, frame, n);
 if (!strcmp(name, "INPUT")) { assert(n == 7); ok = b1e00(wire); }
 if (!strcmp(name, "OUTPUT")) { assert(n == 5); ok = b1e01(wire); }
 if (!strcmp(name, "MUTATE")) { assert(n == 5); ok = b1e02(wire); }
 if (!strcmp(name, "PAIR")) { assert(n == 10); ok = b1e03(wire); }
 if (!strcmp(name, "MIX")) { assert(n >= 7 && n <= 15); ok = b1e04(wire); }
 assert(ok); memcpy(frame, wire + 2, n); error = 0; return 1;
}
int main(void) {
 struct { unsigned char before; struct Record record; unsigned char after; } box;
 struct Record copy;
 unsigned int before, i;
 unsigned char wire[258];
 unsigned char data[2] = {1, 2};
 memset(&box, 0xA5, sizeof box);
 box.record.flags = 12; box.record.shift = -3; box.record.name[2] = 9;
 copy = box.record;
 assert(input(&box.record) == 12 && !blerr());
 assert(!memcmp(&box.record, &copy, sizeof copy));
 output(&box.record);
 assert(!blerr() && box.record.flags == 10 && box.record.shift == -3 &&
        box.record.name[0] == 0 && box.record.name[2] == 5);
 mutate(&box.record);
 assert(!blerr() && box.record.flags == 20 && box.record.shift == 2 &&
        box.record.name[2] == 8);
 pair(&box.record, &copy);
 assert(!blerr() && !memcmp(&box.record, &copy, sizeof copy));
 mix(data, 2, &box.record);
 assert(!blerr() && data[0] == 21 && data[1] == 2 && box.record.flags == 22);
 mix(0, 0, &box.record); assert(!blerr() && box.record.flags == 22);
 copy = box.record;
 before = calls;
 assert(input(0) == 0 && blerr() == BL_ARGUMENT);
 output(0); assert(blerr() == BL_ARGUMENT);
 mutate(0); assert(blerr() == BL_ARGUMENT);
 mix(data, 9, &box.record); assert(blerr() == BL_BOUNDS);
 mix(0, 1, &box.record); assert(blerr() == BL_ARGUMENT);
 mix((unsigned char *)&box.record, 1, &box.record);
 assert(blerr() == BL_ARGUMENT);
 pair(&box.record, &box.record); assert(blerr() == BL_ARGUMENT);
 pair(&box.record, (struct Record *)((unsigned char *)&box.record + 1));
 assert(blerr() == BL_ARGUMENT && calls == before);
 assert(!memcmp(&box.record, &copy, sizeof copy));
 fail = 1;
 output(&box.record); assert(blerr() == 6);
 mutate(&box.record); assert(blerr() == 6);
 assert(!memcmp(&box.record, &copy, sizeof copy));
 assert(box.before == 0xA5 && box.after == 0xA5);
 assert(!b1e02(0));
 for (i = 0; i < sizeof wire; ++i) {
   memset(wire, 0, sizeof wire);
   wire[0] = i & 255; wire[1] = i >> 8;
   if (i != 5) assert(!b1e02(wire));
 }
 return 0;
}
''', ("input", "output", "mutate", "pair", "mix"))

    @unittest.skipUnless(LINKER.is_file() and importlib.util.find_spec("z80"),
                         "special library linker and Z80 emulator required")
    def test_fixed_record_adapter_cycle_budget(self):
        import z80

        self.record_source(r'''
struct Block { unsigned char data[16]; };
#pragma dcc bank_record record 1 inout
#pragma dcc bank_buffer buffer 1 inout 2 16
unsigned int record(struct Block *block) {
 block->data[0] = block->data[0] + 1;
 return block->data[15];
}
unsigned int buffer(unsigned char *data, unsigned int count) {
 if (count != 16) return 0;
 data[0] = data[0] + 1;
 return data[15];
}
''')
        result = self.build()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        library = (self.folder / "build/SPREADS.LIB").read_bytes()
        word = lambda offset: int.from_bytes(library[offset:offset + 2], "little")
        payload = library[word(18):word(18) + word(10)]
        timings = []
        for index in range(2):
            machine = z80.Z80Machine()
            machine.memory[0x100:0x100 + len(payload)] = payload
            body = b"\0\0" + (b"\x10\0" if index else b"") + bytes(range(16))
            wire = len(body).to_bytes(2, "little") + body
            machine.memory[0x9000:0x9000 + len(wire)] = wire
            machine.memory[0xD000:0xD004] = b"\x30\0\0\x90"
            machine.pc = 0x100 + word(26 + index * 12 + 8)
            machine.sp = 0xD000
            machine.set_breakpoint(0x30)
            ticks = 0
            for _ in range(10):
                machine.ticks_to_stop = 10000
                events = machine.run()
                ticks += 10000 - machine.ticks_to_stop
                if events & machine._BREAKPOINT_HIT:
                    break
            else:
                self.fail("generated adapter did not return")
            self.assertEqual(machine.pc, 0x30)
            self.assertEqual(machine.sp, 0xD002)
            self.assertEqual(machine.hl, 1)
            updated = bytes(machine.memory[0x9002:0x9002 + len(body)])
            self.assertEqual(updated[:2], b"\x0f\0")
            self.assertEqual(updated[-16:], b"\x01" + bytes(range(1, 16)))
            timings.append(ticks)
        self.assertLessEqual(timings[0], 1500)
        self.assertLess(timings[0], timings[1])
        self.adapter_tstates = tuple(timings)

    def test_source_contract_only_without_header_linker_or_runtime(self):
        self.automatic_source()
        result = self.build("dcc-contract-only=true", "m80c-tool=missing-assembler",
                            "l80c-tool=missing-linker", "dcc-runtime=missing-runtime",
                            "dcc-debug=true", "dcc-use-emulated-m80=true",
                            "dcc-use-emulated-l80=true")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        header = self.folder / "generated/API.H"
        text = header.read_text()
        self.assertIn("unsigned int opnobj(unsigned int seed);", text)
        self.assertIn("unsigned char * buf, unsigned int count", text)
        self.assertIn("#pragma dcc bank_buffer objread 3 out 4 128", text)
        self.assertNotIn("asmheap", text)
        self.assertNotIn("internal", text)
        self.assertFalse((self.folder / "API.H").exists())
        self.assertFalse((self.folder / "build/SPREADS.LIB").exists())
        self.assertFalse(list((self.folder / "build").glob("*.REL")))
        self.assertIn("no assembly or linking", result.stdout)
        self.compile(self.folder / "generated/B1HOST.C", self.folder / "H.MAC")
        self.compile(self.folder / "build/B1ADAPT.C", self.folder / "A.MAC")

    def test_source_contract_normal_build_and_refresh_preserves_linked_library(self):
        self.automatic_source()
        result = self.build()
        self.assertTrue((self.folder / "generated/API.H").is_file(),
                        result.stdout + result.stderr)
        if LINKER.is_file():
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        artifact = self.folder / "build/SPREADS.LIB"
        artifact.write_bytes(b"existing linked library")
        source = self.folder / "LIB.C"
        source.write_text(source.read_text().replace(
            "veg(unsigned int n)", "veg(unsigned int value)").replace(
            "return internal(n);", "return internal(value);"))
        result = self.build("dcc-contract-only=true")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("veg(unsigned int value);",
                      (self.folder / "generated/API.H").read_text())
        self.assertEqual(artifact.read_bytes(), b"existing linked library")

    def test_source_contract_failure_removes_stale_generated_interface(self):
        self.automatic_source()
        result = self.build("dcc-contract-only=true")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        source = self.folder / "LIB.C"
        source.write_text(source.read_text().replace(
            "#pragma dcc bank_buffer objread 3 out 4 128", ""))
        result = self.build("dcc-contract-only=true")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("every byte pointer", result.stderr)
        for path in ("generated/API.H", "generated/B1HOST.C", "build/B1ADAPT.C"):
            self.assertFalse((self.folder / path).exists(), path)

    def test_source_contract_collects_annotations_across_modules(self):
        self.automatic_source()
        source = self.folder / "LIB.C"
        source.write_text(source.read_text().replace(
            "#pragma dcc bank_buffer objread 3 out 4 128\n", ""))
        (self.folder / "EXTRA.C").write_text(
            "#pragma dcc bank_buffer objread 3 out 4 128\n"
            "unsigned int extra(unsigned int value) { return value; }\n")
        result = self.build("dcc-contract-only=true", "dcc-input=LIB.C,EXTRA.C")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        text = (self.folder / "generated/API.H").read_text()
        self.assertIn("extra(unsigned int value);", text)
        self.assertIn("#pragma dcc bank_buffer objread 3 out 4 128", text)
        self.compile(self.folder / "generated/B1HOST.C", self.folder / "H.MAC")

    def test_contract_only_composite_skips_client_build(self):
        self.automatic_source()
        library, client = self.folder / "lib", self.folder / "host"
        library.mkdir()
        client.mkdir()
        (self.folder / "LIB.C").rename(library / "LIB.C")
        config = self.folder / "dccmake.txt"
        text = config.read_text().replace(
            "dcc-library-host-dir=generated", "dcc-library-host-dir=../host/generated")
        (library / "dccmake.txt").write_text(text)
        (client / "dccmake.txt").write_text("dcc-input=MISSING.C\n")
        config.write_text("dcc-projects=lib,host\n")
        result = self.build("dcc-contract-only=true")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("skipping client build", result.stdout)
        self.assertTrue((client / "generated/API.H").is_file())
        self.assertFalse((client / "build").exists())
        self.assertFalse((library / "build/SPREADS.LIB").exists())

    def test_source_contract_does_not_overwrite_handwritten_header(self):
        self.automatic_source()
        (self.folder / "generated").mkdir()
        header = self.folder / "generated/API.H"
        header.write_text("/* Handwritten client API. */\n")
        result = self.build("dcc-contract-only=true")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("refusing to overwrite", result.stderr)
        self.assertEqual(header.read_text(), "/* Handwritten client API. */\n")

    def test_source_contract_creates_client_directories(self):
        self.automatic_source()
        result = self.build("dcc-contract-only=true",
                            "dcc-library-host-dir=client/sdk/generated")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue((self.folder / "client/sdk/generated/API.H").is_file())
        self.assertTrue((self.folder / "client/sdk/generated/B1HOST.C").is_file())

    def test_help_aliases_work_without_valid_configuration(self):
        (self.folder / "dccmake.txt").write_text("invalid-setting=true\n")
        for alias in ("-help", "-h", "--help", "/?"):
            with self.subTest(alias=alias):
                result = self.build(alias)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stderr, "")
                for topic in ("dcc-projects=lib,host", "dcc-contract-only=true",
                              "dcc-library-api=auto", "Exit status",
                              "BANKLIB.C", "1..8 exports", "key=value"):
                    self.assertIn(topic, result.stdout)
        self.assertFalse((self.folder / "build").exists())

    def test_contract_only_rejects_non_typed_builds(self):
        self.configure(**{"dcc-output-format": "com", "dcc-exports": ""})
        result = self.build("dcc-contract-only=true")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("requires lib output and dcc-exports=auto", result.stderr)

    def compile(self, source, output, *extra):
        result = subprocess.run(
            [str(ROOT / "dcc"), "-module", "-I", str(ROOT), "-I", str(self.folder),
             *extra, str(source), "-o", str(output)],
            capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_metadata_uses_definitions_and_resolved_typedef_types(self):
        path = self.folder / "meta.api"
        self.compile(self.folder / "LIB.C", self.folder / "LIB.MAC",
                     "-bank-api", str(path))
        lines = path.read_text().splitlines()
        self.assertIn("F opnobj 1 u16 1 0 u16", lines)
        self.assertIn("F objread 1 u16 4 0 u16 u16 pu8 u16", lines)
        self.assertIn("F asmheap 0 u16 1 0 u16", lines)
        self.assertFalse(any(" internal " in line for line in lines))
        self.assertIn("B objread 3 out 4 128", lines)
        self.assertIn("N objread object offset buf count", lines)

    def test_generation_and_host_compilation(self):
        result = self.build()
        # A stock l80c cannot link LIBs; generation/assembly still runs first.
        host = self.folder / "generated/B1HOST.C"
        adapter = self.folder / "build/B1ADAPT.C"
        self.assertTrue(host.is_file(), result.stdout + result.stderr)
        self.assertTrue(adapter.is_file(), result.stdout + result.stderr)
        self.assertIn("typed export ONLOAD=_b1e00 u16()", result.stdout)
        self.assertIn('u16(u16,u16,out:u8[4<=128],u16)', host.read_text())
        self.assertIn('void(u16)', host.read_text())
        self.assertNotIn("asmheap(", host.read_text())
        self.assertNotIn("internal(", host.read_text())
        self.compile(host, self.folder / "HOST.MAC")
        self.compile(adapter, self.folder / "ADAPT.MAC")
        self.assertIn("f = wire + 2;", adapter.read_text())
        self.assertIn("b1get(wire) < 8", adapter.read_text())
        self.assertIn("b1get(wire) != n", adapter.read_text())
        self.assertIn("blfail(BL_BOUNDS)", host.read_text())
        self.assertIn("blfail(BL_ARGUMENT)", host.read_text())
        if LINKER.is_file():
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            data = (self.folder / "build/SPREADS.LIB").read_bytes()
            word = lambda i: int.from_bytes(data[i:i + 2], "little")
            self.assertEqual(data[:8], b"B1LB\x01\x01\x00\x01")
            self.assertEqual(word(12), 7)
            header = word(18)
            self.assertEqual(header, 26 + 12 * 7)
            for i, name in enumerate(("ONLOAD", "ONUNLOAD", "VEG", "MARM",
                                      "OPNOBJ", "OBJREAD", "OBJCLOSE")):
                pos = 26 + 12 * i
                self.assertEqual(data[pos:pos + 8].rstrip(b"\0").decode(), name)
                self.assertLess(word(pos + 8), word(10))
                sig = header + word(pos + 10)
                end = data.index(0, sig)
                self.assertLessEqual(end - sig, 255)
                self.assertIn(data[sig:end].decode(), host.read_text())
            self.assertEqual(word(14), 0)

    def test_rejects_unsupported_and_unlisted_public_definitions(self):
        cases = [
            ("unsigned int stray(void) { return 1; }\n", "", "missing from authoritative"),
            ("unsigned int bad(unsigned int *p) { return *p; }\n",
             "unsigned int bad(unsigned int *p);\n", "every byte pointer"),
            ("unsigned int bad(unsigned char **p) { return **p; }\n",
             "unsigned int bad(unsigned char **p);\n", "pointer graphs"),
            ("unsigned int bad(unsigned int n, ...) { return n; }\n",
             "unsigned int bad(unsigned int n, ...);\n", "varargs"),
            ("unsigned int bad(unsigned int (*fn)(void)) { return fn(); }\n",
             "unsigned int bad(unsigned int (*fn)(void));\n", "function pointers"),
            ("unsigned int bad(unsigned char fn(void), unsigned int n) { return fn() + n; }\n",
             "unsigned int bad(unsigned char fn(void), unsigned int n);\n",
             "function pointers"),
            ("unsigned int bad(unsigned char (*p)[4], unsigned int n) { return (*p)[n]; }\n",
             "unsigned int bad(unsigned char (*p)[4], unsigned int n);\n",
             "pointer graphs"),
            ("unsigned char *bad(void) { return 0; }\n",
             "unsigned char *bad(void);\n", "unsupported result"),
            ("struct X { unsigned int n; }; unsigned int bad(struct X x) { return x.n; }\n",
             "struct X { unsigned int n; }; unsigned int bad(struct X x);\n", "structures"),
        ]
        for source, header, message in cases:
            with self.subTest(message=message):
                (self.folder / "API.H").write_text(header)
                (self.folder / "LIB.C").write_text(source)
                result = self.build()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(message, result.stderr)

    def test_annotations_and_header_mismatches_fail(self):
        cases = [
            (API.replace("3 out 4 128", "3 wrong 4 128"), SOURCE, "separate u16"),
            (API.replace("3 out 4 128", "3 out 1 256"), SOURCE, "256-byte cap"),
            (API.replace("3 out 4 128", "3 out 4 0"), SOURCE, "MAX 1..256"),
            (API.replace("#pragma dcc bank_buffer objread 3 out 4 128", ""),
             SOURCE, "every byte pointer"),
            (API, SOURCE.replace("veg(unsigned int n)", "veg(int n)"),
             "differs from authoritative"),
        ]
        for header, source, message in cases:
            with self.subTest(message=message):
                (self.folder / "API.H").write_text(header)
                (self.folder / "LIB.C").write_text(source)
                result = self.build()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(message, result.stderr)

    def test_integer_widths_macros_conditional_discovery_and_void_frame(self):
        header = """
#define RET unsigned int
#define PUBLIC_NAME(n) n
typedef unsigned long wide;
signed char si8(signed char value);
unsigned char ui8(unsigned char value);
int si16(int value);
RET ui16(RET value);
long si32(long value);
wide ui32(wide value);
void noargs(void);
"""
        source = """
#include "API.H"
signed char si8(signed char v) { return v; }
unsigned char ui8(unsigned char v) { return v; }
int si16(int v) { return v; }
RET PUBLIC_NAME(ui16)(RET v) { return v; }
long si32(long v) { return v; }
wide ui32(wide v) { return v; }
void noargs(void) { }
#if 0
unsigned int ignored(unsigned char **p) { return **p; }
#endif
"""
        (self.folder / "API.H").write_text(header)
        (self.folder / "LIB.C").write_text(source)
        result = self.build()
        host = self.folder / "generated/B1HOST.C"
        self.assertTrue(host.exists(), result.stdout + result.stderr)
        for signature in ("i8(i8)", "u8(u8)", "i16(i16)", "u16(u16)",
                          "i32(i32)", "u32(u32)", "void()"):
            self.assertIn(signature, host.read_text())
        self.assertNotIn("IGNORED", result.stdout)
        self.compile(host, self.folder / "H.MAC")
        self.compile(self.folder / "build/B1ADAPT.C", self.folder / "A.MAC")
        if LINKER.is_file():
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_all_public_definitions_and_symbol_conflicts_are_rejected(self):
        cases = [
            ("abcdefg", "abcdefh", "six-significant"),
            ("a", "A", "case-folded"),
            ("b1e00", "second", "reserved generated"),
            ("toolongfn", "second", "exceeds 8"),
        ]
        for first, second, message in cases:
            with self.subTest(message=message):
                header = f"unsigned int {first}(void);\nunsigned int {second}(void);\n"
                (self.folder / "API.H").write_text(header)
                (self.folder / "LIB.C").write_text(
                    header + f"unsigned int {first}(void) {{ return 1; }}\n"
                    f"unsigned int {second}(void) {{ return 2; }}\n")
                result = self.build()
                self.assertNotEqual(result.returncode, 0)
                # The compiler can diagnose public name collisions earlier.
                self.assertTrue(message in result.stderr or "6 significant" in result.stderr,
                                result.stdout + result.stderr)
        header = "".join(f"unsigned int f{i}(void);\n" for i in range(9))
        (self.folder / "API.H").write_text(header)
        (self.folder / "LIB.C").write_text(
            header + "".join(f"unsigned int f{i}(void) {{ return {i}; }}\n"
                             for i in range(9)))
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("1..8 public C definitions", result.stderr)

    def test_failed_regeneration_invalidates_old_artifacts(self):
        result = self.build()
        self.assertTrue((self.folder / "generated/B1HOST.C").exists(),
                        result.stdout + result.stderr)
        (self.folder / "LIB.C").write_text(
            SOURCE + "\nunsigned int omitted(void) { return 1; }\n")
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.folder / "generated/B1HOST.C").exists())
        self.assertFalse((self.folder / "build/B1ADAPT.C").exists())
        self.assertFalse((self.folder / "build/SPREADS.LIB").exists())

    @unittest.skipUnless(shutil.which("clang"), "native clang required for executable harness")
    def test_executable_host_adapter_framing_and_copyback(self):
        result = self.build()
        self.assertTrue((self.folder / "build/B1ADAPT.C").exists(),
                        result.stdout + result.stderr)
        harness = r'''
#include <assert.h>
#include <string.h>
#include "API.H"
#include "BANKLIB.H"
unsigned int b1e00(unsigned char *);
unsigned int b1e01(unsigned char *);
unsigned int b1e02(unsigned char *);
unsigned int b1e03(unsigned char *);
unsigned int b1e04(unsigned char *);
unsigned int b1e05(unsigned char *);
unsigned int b1e06(unsigned char *);
static unsigned int error, calls, fail;
void blfail(unsigned int n) { error = n; }
unsigned int blerr(void) { return error; }
unsigned int blcall(const char *name, const char *sig, void *f, unsigned int n) {
 unsigned char wire[258];
 unsigned int ok = 0;
 (void)sig;
 ++calls;
 if (fail) { error = 6; return 0; }
 assert(n <= 256);
 memset(wire, 0, sizeof wire);
 wire[0] = n & 255; wire[1] = n >> 8;
 memcpy(wire + 2, f, n);
 if (!strcmp(name, "ONLOAD")) ok = b1e00(wire);
 if (!strcmp(name, "ONUNLOAD")) ok = b1e01(wire);
 if (!strcmp(name, "VEG")) ok = b1e02(wire);
 if (!strcmp(name, "MARM")) ok = b1e03(wire);
 if (!strcmp(name, "OPNOBJ")) ok = b1e04(wire);
 if (!strcmp(name, "OBJREAD")) ok = b1e05(wire);
 if (!strcmp(name, "OBJCLOSE")) ok = b1e06(wire);
 if (!ok) { error = 7; return 0; }
 memcpy(f, wire + 2, n); error = 0; return 1;
}
int main(void) {
 unsigned char b[130], wire[258];
 unsigned int before, i;
 assert(onload() == 1 && !blerr());
 assert(onunload() == 1 && !blerr());
 assert(veg(23) == 24 && marm(23) == 25 && opnobj(23) == 26);
 memset(b, 0xAA, sizeof b);
 assert(objread(7, 9, b + 1, 128) == 128 && !blerr());
 assert(b[0] == 0xAA && b[129] == 0xAA);
 for (i = 0; i < 128; ++i) assert(b[1 + i] == (unsigned char)(16 + i));
 assert(objread(7, 9, 0, 0) == 0 && !blerr());
 before = calls;
 assert(objread(7, 9, b, 129) == 0 && blerr() == BL_BOUNDS);
 assert(objread(7, 9, b, 65535) == 0 && blerr() == BL_BOUNDS);
 assert(objread(7, 9, 0, 1) == 0 && blerr() == BL_ARGUMENT);
 assert(calls == before);
 memset(b, 0xAA, sizeof b);
 fail = 1;
 assert(objread(7, 9, b, 128) == 0 && blerr() == 6);
 assert(b[0] == 0xAA && b[127] == 0xAA);
 assert(veg(10) == 0 && blerr() == 6);
 objclose(7); assert(blerr() == 6);
 fail = 0;
 objclose(7); assert(!blerr());
 assert(!b1e05(0));
 for (i = 0; i < 8; ++i) {
   memset(wire, 0xFF, sizeof wire); wire[0] = i; wire[1] = 0;
   assert(!b1e05(wire));
 }
 memset(wire, 0, sizeof wire);
 wire[0] = 9; wire[8] = 128; /* body count at result2 + handle2 + offset2 */
 assert(!b1e05(wire)); /* claims 9 bytes, needs 136 */
 wire[0] = 136; assert(b1e05(wire));
 wire[0] = 137; assert(!b1e05(wire)); /* trailing data rejected */
 wire[0] = 0; wire[1] = 1; wire[8] = 255; wire[9] = 255;
 assert(!b1e05(wire)); /* overflowing/unbounded count */
 wire[0] = 1; wire[1] = 1; assert(!b1e05(wire)); /* >256 body */
 return 0;
}
'''
        names = ("onload", "onunload", "veg", "marm", "opnobj", "objread", "objclose")
        self.run_native(harness, names)

    def run_native(self, harness, names):
        harness += """
unsigned int bank_library_call_bound(bank_library_binding_t *binding,
 const char *name, const char *signature, void *frame, unsigned int bytes) {
 (void)binding;
 return blcall(name, signature, frame, bytes);
}
"""
        (self.folder / "HARNESS.C").write_text(harness)
        for source, output, rename in (
                ("LIB.C", "lib.o", True),
                ("build/B1ADAPT.C", "adapter.o", True),
                ("generated/B1HOST.C", "host.o", False),
                ("HARNESS.C", "harness.o", False)):
            command = ["clang", "-x", "c", "-std=c99", "-Wno-unknown-pragmas",
                       "-fsanitize=address,undefined",
                       "-I", str(self.folder), "-c", source, "-o", output]
            if rename:
                command += [f"-D{name}=lib_{name}" for name in names]
            process = subprocess.run(command, cwd=self.folder, capture_output=True, text=True)
            self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
        process = subprocess.run(
            ["clang", "-fsanitize=address,undefined",
             "lib.o", "adapter.o", "host.o", "harness.o", "-o", "verify"],
            cwd=self.folder, capture_output=True, text=True)
        self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
        process = subprocess.run([str(self.folder / "verify")], cwd=self.folder,
                                 capture_output=True, text=True)
        self.assertEqual(process.returncode, 0, process.stdout + process.stderr)

    @unittest.skipUnless(shutil.which("clang"), "native clang required for executable harness")
    def test_in_out_inout_and_const_buffer_copy_directions(self):
        (self.folder / "API.H").write_text("""
unsigned int input(const unsigned char *data, unsigned int count);
void output(unsigned char *data, unsigned int count);
void mutate(unsigned char *data, unsigned int count);
#pragma dcc bank_buffer input 1 in 2 32
#pragma dcc bank_buffer output 1 out 2 32
#pragma dcc bank_buffer mutate 1 inout 2 32
""")
        (self.folder / "LIB.C").write_text("""
#include "API.H"
unsigned int input(const unsigned char *data, unsigned int count) {
 unsigned int r = count ? data[0] : 0;
 if (count) ((unsigned char *)data)[0] = 200;
 return r;
}
void output(unsigned char *data, unsigned int count) {
 unsigned int i; for (i = 0; i < count; ++i) data[i] += 10;
}
void mutate(unsigned char *data, unsigned int count) {
 unsigned int i; for (i = 0; i < count; ++i) data[i] += 10;
}
""")
        result = self.build()
        self.assertTrue((self.folder / "build/B1ADAPT.C").exists(),
                        result.stdout + result.stderr)
        host = (self.folder / "generated/B1HOST.C").read_text()
        self.assertIn("const unsigned char * a0", host)
        self.assertIn("u16(in:u8[2<=32],u16)", host)
        self.assertIn("void(out:u8[2<=32],u16)", host)
        self.assertIn("void(inout:u8[2<=32],u16)", host)
        self.compile(self.folder / "generated/B1HOST.C", self.folder / "H.MAC")
        self.run_native(r'''
#include <assert.h>
#include <string.h>
#include "API.H"
#include "BANKLIB.H"
unsigned int b1e00(unsigned char *);
unsigned int b1e01(unsigned char *);
unsigned int b1e02(unsigned char *);
static unsigned int error;
void blfail(unsigned int n) { error = n; }
unsigned int blerr(void) { return error; }
unsigned int blcall(const char *name, const char *sig, void *f, unsigned int n) {
 unsigned char wire[258]; unsigned int ok = 0; (void)sig;
 wire[0] = n & 255; wire[1] = n >> 8; memcpy(wire + 2, f, n);
 if (!strcmp(name, "INPUT")) ok = b1e00(wire);
 if (!strcmp(name, "OUTPUT")) ok = b1e01(wire);
 if (!strcmp(name, "MUTATE")) ok = b1e02(wire);
 assert(ok); memcpy(f, wire + 2, n); error = 0; return 1;
}
int main(void) {
 unsigned char b[3] = {12, 13, 14};
 assert(input(b, 3) == 12 && !blerr() && b[0] == 12);
 output(b, 3); assert(!blerr() && b[0] == 10 && b[1] == 10 && b[2] == 10);
 mutate(b, 3); assert(!blerr() && b[0] == 20 && b[1] == 20 && b[2] == 20);
 output(0, 0); assert(!blerr());
 mutate(0, 0); assert(!blerr());
 assert(input(0, 0) == 0 && !blerr());
 return 0;
}
''', ("input", "output", "mutate"))


if __name__ == "__main__":
    unittest.main()
