#!/usr/bin/env python3
"""Check that formatting preserves C89 macros, literals and declarations."""

import os
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
KNF = runpy.run_path(str(ROOT / "tools/format_c.py"))["knf"]
CLANG_FORMAT = os.getenv("CLANG_FORMAT", "clang-format")


def formatted(source):
	result = subprocess.run(
		[CLANG_FORMAT, "--style=file:" + str(ROOT / ".clang-format")],
		input=source, text=True, capture_output=True, check=True,
	)

	return KNF(result.stdout)


class FormattingTests(unittest.TestCase):
	def test_javascript_literals_and_check_mode(self):
		source = r'''
const pattern=/[{}]\/\/[^\n]+/g;
const text=`first line
    struct sample { int field; };
    return ${value?.name ?? 'fallback'};
last line`;
class Reader { static count=0; async read(value) {
value ??= {}; value.name ||= "ready"; return {...value, 'quotedKey': text};
} }
'''

		with tempfile.TemporaryDirectory(prefix="mh-format-js-") as temporary:
			path = Path(temporary)
			(path / '.clang-format').write_text((ROOT / '.clang-format').read_text())
			script = path / 'fixture.js'
			script.write_text(source)
			command = [sys.executable, str(ROOT / 'tools/format.py'),
				'--clang-format', CLANG_FORMAT, str(script)]
			before = subprocess.run(command + ['--check'], capture_output=True, text=True)
			self.assertEqual(before.returncode, 1, before.stderr)
			self.assertEqual(script.read_text(), source)
			subprocess.run(command, check=True)
			output = script.read_text()
			self.assertIn(source[source.index('`'):source.rindex('`') + 1], output)
			self.assertIn(r'/[{}]\/\/[^\n]+/g', output)
			self.assertIn("'quotedKey'", output)
			self.assertIn("value.name ||= 'ready'", output)
			self.assertIn('value ??= {}', output)
			after = subprocess.run(command + ['--check'], capture_output=True, text=True)
			self.assertEqual(after.returncode, 0, after.stdout + after.stderr)

	def test_macro_and_literals_compile(self):
		source = r'''
#include <string.h>
#define SET(value) do { \
    const char *text; \
    text = "/* not a comment */ // text"; \
    (value) = strcmp(text, "/* not a comment */ // text"); \
} while (0)
int main(void) { int result; SET(result); return result; }
'''
		output = formatted(source)
		self.assertEqual(output, formatted(output))

		with tempfile.TemporaryDirectory(prefix="mh-format-") as temporary:
			path = Path(temporary)
			(path / "check.c").write_text(output)
			subprocess.run([os.getenv("CC", "cc"), "-std=c90", "-pedantic-errors",
				"-Wall", "-Wextra", "-Werror", str(path / "check.c"),
				"-o", str(path / "check")], check=True)
			subprocess.run([str(path / "check")], check=True)

	def test_style_and_stability(self):
		source = '''
struct entry { unsigned long value; const char *name; };
int inspect(const struct entry *entry);
void release(struct entry *entry);
int inspect(const struct entry *entry) {
    unsigned long value;
    value = entry->value;
    if (value > 1) { return 1; }
    return 0;
}
'''
		output = formatted(source)
		self.assertEqual(output, formatted(output))
		self.assertIn("int\ninspect(", output)
		self.assertIn("\tunsigned long value;\n\n\tvalue =", output)
		self.assertIn("if (value > 1) {", output)
		self.assertIn("unsigned long\t", output)

	def test_comment_examples_and_long_diagnostic(self):
		message = "complete diagnostic " * 8
		source = ('/* An example: int sample(void); */\n'
			'const char *message(void) { return "' + message + '"; }\n')
		output = formatted(source)
		self.assertIn('"' + message + '"', output)
		self.assertIn("int sample(void);", output)
		self.assertEqual(output, formatted(output))

	def test_long_array_member_alignment(self):
		source = """
union example {
    struct mh_retro_mesh meshes[MH_RETRO_MESH_COUNT];
    float motion[MH_RETRO_INSTANCE_COUNT][MH_RETRO_MOTION_VECTORS][3];
    float palette[MH_RETRO_PALETTE_COUNT][3];
    unsigned char text[MH_RETRO_LINES][MH_RETRO_LINE_BYTES];
    unsigned char *items[MH_RETRO_INSTANCE_COUNT][MH_RETRO_MOTION_VECTORS];
    int count; /* This member's explanation should not destroy alignment. */
};
"""
		output = formatted(source)
		self.assertEqual(output, formatted(output))
		columns = []

		for name in ('meshes', 'motion', 'palette', 'text', '*items', 'count;'):
			line = next(line for line in output.splitlines() if name in line)
			columns.append(line.expandtabs(8).index(name))

		self.assertEqual(len(set(columns)), 1, output)
		motion = next(line for line in output.splitlines() if 'motion[' in line)
		self.assertGreater(len(motion.expandtabs(8)), 80)
		self.assertIn('motion[MH_RETRO_INSTANCE_COUNT][MH_RETRO_MOTION_VECTORS][3];',
			motion)
		self.assertIn('/* This member', output)

	def test_single_dimension_member_alignment(self):
		source = """
struct layout {
    size_t strides
        [MH_SHADER_VERTEX_BUFFERS];
    const struct mh_shader_attribute *attributes;
};
"""
		output = formatted(source)
		self.assertEqual(output, formatted(output))
		strides = next(line for line in output.splitlines() if 'strides' in line)
		attributes = next(line for line in output.splitlines() if '*attributes' in line)
		self.assertIn('strides[MH_SHADER_VERTEX_BUFFERS];', strides)
		self.assertEqual(strides.expandtabs(8).index('strides'),
			attributes.expandtabs(8).index('*attributes'))
		self.assertGreater(len(strides.expandtabs(8)), 80)

	def test_documented_groups(self):
		source = r"""
struct callbacks {
    void (*request)(void *context);
    /* Called independently from request. */
    void (*service)(void *context);
};
/* Construction copies the callbacks. */
int open_device(const struct callbacks *callbacks);
/* Lock/unlock share one contract. */
int lock_device(void);
int unlock_device(void);
/* Finish before releasing ownership. */
void close_device(void);
void run(void) {
    open_device(0);
    /* Keep both statements in this operation together. */
    lock_device();
    unlock_device();
    if (lock_device()) {
        /* First statement in a block needs no leading gap. */
        close_device();
    }
}
"""
		output = formatted(source)
		self.assertEqual(output, formatted(output))
		self.assertIn('context);\n\n\t/* Called', output)
		self.assertIn('};\n\n/* Construction', output)
		self.assertIn('callbacks);\n\n/* Lock', output)
		self.assertRegex(output, r'lock_device\(void\);\n[^\n]*unlock_device')
		self.assertIn('unlock_device(void);\n\n/* Finish', output)
		self.assertIn('open_device(0);\n\n\t/* Keep', output)
		self.assertIn('together. */\n\tlock_device();\n\tunlock_device();', output)
		self.assertIn('if (lock_device()) {\n\t\t/* First', output)

	def test_comment_spacing_preserves_literals_and_macros(self):
		source = r'''
#define RUN() do { \
    step(); \
    /* This comment is part of the macro. */ \
    step(); \
} while (0)
void step(void);
void run(void) {
    const char *example = "first;\n/* not a comment */";
    /* An explanation after a declaration. */
    (void)example;
    RUN(); /* Trailing comments stay attached. */
    /* The next operation. */
    step();
}
'''
		output = formatted(source)
		self.assertEqual(output, formatted(output))
		self.assertIn(r'"first;\n/* not a comment */"', output)
		self.assertIn('RUN(); /* Trailing comments stay attached. */', output)
		self.assertIn('attached. */\n\n\t/* The next', output)
		self.assertIn('declaration. */\n\t(void)example;', output)
		macro = output[output.index('#define'):output.index('void')]
		self.assertNotIn('\n\n', macro.rstrip())

		for line in macro.rstrip().splitlines()[:-1]:
			self.assertTrue(line.endswith('\\'), line)

	def test_statement_groups(self):
		source = '''
int update(int *values, int count) {
    int i;
    if (values == 0)
        return 1;
    for (i = 0; i < count; i++) {
        values[i] = i;
        if (values[i] < 0)
            return 2;
    }
    for (i = 0; i < count; i++)
        values[i]++;
    /* Keep this explanation with its guard. */
    if (count == 0) {
        return 3;
    } else {
        count--;
    }
    values[0] = count;
    do {
        count--;
    } while (count > 0);
    values[1] = count;
    return 0;
}
'''
		output = formatted(source)
		self.assertEqual(output, formatted(output))
		self.assertIn('return 1;\n\n\tfor', output)
		self.assertIn('values[i] = i;\n\n\t\tif', output)
		self.assertIn('\t}\n\n\tfor', output)
		self.assertIn('values[i]++;\n\n\t/* Keep', output)
		self.assertIn('with its guard. */\n\tif', output)
		self.assertIn('} else {', output)
		self.assertIn('\t}\n\n\tvalues[0]', output)
		self.assertIn('values[0] = count;\n\n\tdo', output)
		self.assertIn('} while (count > 0);\n\n\tvalues[1]', output)


if __name__ == "__main__":
	unittest.main()
