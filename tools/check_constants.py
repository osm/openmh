#!/usr/bin/env python3
"""Catch a few high-confidence magic-value patterns in production C sources.

This is a lexical guardrail, not a C parser or a replacement for code review.
The reviewed baseline matches exact expressions and occurrence counts, not files
or line numbers. There is deliberately no automatic baseline-update option.
"""

import argparse
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
BASELINE = ROOT / "tools/constants_baseline.tsv"
LITERALS = re.compile(
	r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
	re.DOTALL,
)
CALL = re.compile(
	r'\b(?P<name>mh_(?:read|write)_[uf](?:16|32|64)(?:le|be)'
	r'|mh_rtc_(?:read|write)(?:16|32)|memcmp|memcpy|malloc|calloc|realloc)\s*\('
)
INTEGER = r'(?:0[xX][0-9a-fA-F]+|[0-9]+)[uUlL]*\b'
OFFSET = re.compile(r'[+-]\s*(' + INTEGER + r')(?!\s*[*\/%])')
SUM = re.compile(r'\b(' + INTEGER + r')\s*\+\s*(' + INTEGER + r')')
ESCAPE = re.compile(r'\\(?:[0-7]{1,3}|x[0-9a-fA-F]+)')
SIZE_ASSIGNMENT = re.compile(r'\b(?:\w+\s*(?:\.|->)\s*)?size\s*=\s*([^;]+);')
ASSET_DIRECTORY = re.compile(
	r'"(?:/(?:Data|Game|Cars|Tracks|Demos|Sounds|Music|Movies)'
	r'|(?:Data|Game|Cars|Tracks|Demos|Sounds|Music|Movies)/)"')


def integer(value):
	value = value.rstrip('uUlL')

	return int(value, 16 if value.lower().startswith('0x') else
		8 if value.startswith('0') else 10)


def mask(text, strings=True):
	return LITERALS.sub(
		lambda match: re.sub(r'[^\n]', ' ', match[0])
		if strings or match[0].startswith(('/*', '//')) else match[0], text)


def arguments(code, start):
	"""Return argument spans; literals/comments have already been masked."""

	depth = 0
	first = start
	result = []

	for index in range(start, len(code)):
		char = code[index]

		if char in '([':
			depth += 1
		elif char == ')' and depth == 0:
			result.append((first, index))

			return result, index + 1
		elif char in ')]':
			depth -= 1
		elif char == ',' and depth == 0:
			result.append((first, index))
			first = index + 1

	return [], start


def literal_sum(expression):
	return any(integer(a) > 1 and integer(b) > 1
		for a, b in SUM.findall(expression))


@dataclass(frozen=True)
class Finding:
	path: str
	line: int
	rule: str
	expression: str

	@property
	def key(self):
		return self.path, self.rule, self.expression


def scan(text, path):
	code = mask(text)
	without_comments = mask(text, strings=False)
	findings = []

	def report(start, end, rule):
		# Token normalization survives formatting, without changing string data.
		expression = ' '.join(re.findall(
			r'"(?:\\.|[^"\\])*"|\w+|[^\s]', without_comments[start:end]))
		findings.append(Finding(path, text.count('\n', 0, start) + 1,
			rule, expression))

	for call in CALL.finditer(code):
		spans, end = arguments(code, call.end())

		if not spans:
			continue

		name = call['name']
		first = code[slice(*spans[0])]

		if name.startswith('mh_'):
			if any(integer(value) > 1 for value in OFFSET.findall(first)):
				report(call.start(), end, 'binary-offset')
		elif name in ('memcmp', 'memcpy'):
			for start, stop in spans[:2]:
				literals = LITERALS.finditer(without_comments[start:stop])

				if any(item[0].startswith('"') and ESCAPE.search(item[0])
					for item in literals):
					report(call.start(), end, 'binary-signature')
					break
		else:
			sizes = spans[1:] if name == 'realloc' else spans

			if any(literal_sum(code[start:stop]) for start, stop in sizes):
				report(call.start(), end, 'byte-size-sum')

	for assignment in SIZE_ASSIGNMENT.finditer(code):
		if literal_sum(assignment[1]):
			report(assignment.start(), assignment.end(), 'byte-size-sum')

	for literal in ASSET_DIRECTORY.finditer(without_comments):
		line_start = without_comments.rfind('\n', 0, literal.start()) + 1

		if not re.match(r'\s*#\s*define\b',
			without_comments[line_start:literal.start()]):
			report(literal.start(), literal.end(), 'asset-directory')

	return findings


def source_findings(root):
	return [finding for path in sorted((root / 'src').rglob('*'))
		if path.suffix in ('.c', '.h')
		for finding in scan(path.read_text(), path.relative_to(root).as_posix())]


def read_baseline(path):
	entries = Counter()

	for number, line in enumerate(path.read_text().splitlines(), 1):
		if not line or line.startswith('#'):
			continue

		fields = line.split('\t')

		if len(fields) != 4 or not fields[3].strip():
			raise ValueError(f'{path}:{number}: expected path, rule, expression, reason')

		entries[tuple(fields[:3])] += 1

	return entries


def compare(findings, baseline):
	remaining = baseline.copy()
	failures = []

	for finding in findings:
		if remaining[finding.key]:
			remaining[finding.key] -= 1
		else:
			failures.append(f'{finding.path}:{finding.line}: {finding.rule}: '
				f'{finding.expression}')

	for key, count in remaining.items():
		if count:
			failures.append(f'{key[0]}: remove {count} stale baseline entry: '
				f'{key[1]}: {key[2]}')

	return failures


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.parse_args()

	try:
		failures = compare(source_findings(ROOT), read_baseline(BASELINE))
	except (OSError, ValueError) as error:
		parser.exit(1, f'{error}\n')

	if failures:
		parser.exit(1, '\n'.join(failures) + '\nUse format-owned constants or '
			'sizeof; keep shared definitions with their owning module.\n')

	print('Constant checks passed.')


if __name__ == '__main__':
	main()
