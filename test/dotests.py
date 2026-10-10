#! /usr/bin/python3

import os
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

ROOTDIR = os.path.join(os.path.dirname(sys.argv[0]), "..", "bin")
tree = ET.parse(os.path.join(os.path.dirname(sys.argv[0]), "tests.xml"))

"""
	The output of tests is generally of the following format:
		<output>
			<!-- logged entries -->
		</output>
		<result>
			<!-- resulting value or empty -->
		</result>
		<error>
			<!-- thrown value if any, otherwise the error element is not present -->
		</error>

	This script attempts to match XML outputs to templates. Not all elements have to be inspected,
	but if an element appears in a template, it must appear in the XML output as well, and
	if an attribute or text appears in the template, it must be identical in the output.

	To avoid having to deal with ordering of elements, every element is placed in a canonical order.
	Named slots, properties (and for ECMAScript 4, fields) are ordered according to their numerical
	index, string value and for symbols and namespaces, name and creation order.

	The following methods are provided for object reference equivalence. For symbols and namespaces, they
	have an id value composed of the name, followed by '#' and the creation index for that name.
	For example, the first symbol allocated with description 'a' will have an id of 'a#0'. These symbols
	will be listed in a separate <symbols> (or in the case of namespaces, <namespaces>) element preceding
	<output>.

	For objects, each logged object is assigned an identifier in succession, so the first logged object
	will have id 0, the second one (which might, and probably will, occur as a property or slot of the first
	object) will have id 1, and so on. Repeated properties are not logged again (unless the _relog function
	is called on them), but instead reference the previous entry with the ref attribute. The XML tester
	does not expect the template ids to match, it just needs to verify that objects with identical ids
	in the template have matching ids in the output. The content of object element also do not have to
	match perfectly in the sense that an object element with a 'ref' attribute gets replaced with the fully
	printed one.

	Aside from 'ref' and 'id' which get treated in a special way, the checker recognizes a couple of other
	values. Setting the 'empty' attribute to 'true' will make sure the contents of the element are empty.
	The 'immediate' attribute can be set to 'child' to mark that the first child member element must match
	the one in the template. If set to 'next', the following sibling member element must match the one in
	the template. Both 'child' and 'next' can be set, separated by a space.
"""

CURRENT_ROOT = None
LAST_ERROR = None
def find_actual(tag, refid, starting_node = None):
	global CURRENT_ROOT
	if starting_node is None:
		starting_node = CURRENT_ROOT
	for child in starting_node:
		if child.tag == tag and child.attrib.get('id') == refid:
			return child
		else:
			result = find_actual(tag, refid, starting_node = child)
			if result is not None:
				return result
	return None

class Atom:
	# distinguishable text-like values
	def __init__(self, label):
		self.label = label

	def __repr__(self):
		return f"Atom({self.label!r})"

CHILD_TEXT = Atom('child')
NEXT_TEXT = Atom('next')
CHILD_NODE = Atom('child-node')

PERFECT_MATCHES = set()
CONSIDERED_MATCHES = set()
CONSIDERED_FAILS = {}
ABSOLUTE_FAILS = {}
LAST_MATCHES = {}

def get_matches(template):
	global LAST_MATCHES
	return LAST_MATCHES.get(template, set())

def mark_previous_attempt(element, template):
	global PERFECT_MATCHES, CONSIDERED_MATCHES, ABSOLUTE_FAILS, CONSIDERED_FAILS, LAST_MATCHES
	for match in get_matches(template):
		if match in PERFECT_MATCHES:
			PERFECT_MATCHES.remove(match)
			CONSIDERED_MATCHES.add(match)
		if match in ABSOLUTE_FAILS:
			CONSIDERED_FAILS[match] = ABSOLUTE_FAILS[match]
			del ABSOLUTE_FAILS[match]

def make_match(element, template):
	global LAST_MATCHES
	if template not in LAST_MATCHES:
		LAST_MATCHES[template] = set()
	LAST_MATCHES[template].add(element)

def make_success(element, template):
	global PERFECT_MATCHES, CONSIDERED_FAILS, LAST_MATCHES
	if element in PERFECT_MATCHES:
		return
	if type(element) is not tuple:
		mark_previous_attempt(element, template)
	make_match(element, template)
	if element in CONSIDERED_FAILS:
		del CONSIDERED_FAILS[element]
	PERFECT_MATCHES.add(element)

def make_failure(element, template, fault):
	global ABSOLUTE_FAILS, PERFECT_MATCHES, CONSIDERED_MATCHES, LAST_MATCHES
	if element in ABSOLUTE_FAILS:
		return
	make_match(element, template)
	if element in PERFECT_MATCHES:
		PERFECT_MATCHES.remove(element)
	if element in CONSIDERED_MATCHES:
		CONSIDERED_MATCHES.remove(element)
	ABSOLUTE_FAILS[element] = fault

COLLECTED_IDS = {}
def check_xml(template, result):
	global PERFECT_MATCHES, CONSIDERED_MATCHES, CONSIDERED_FAILS, ABSOLUTE_FAILS, LAST_MATCHES, CHILD_TEXT, NEXT_TEXT, CHILD_NODE

	global LAST_ERROR, COLLECTED_IDS
	if template.tag != result.tag:
		LAST_ERROR = f"Expected element tag {template.tag!r} received {result.tag!r}"
		make_failure(result, template, f"<{template.tag}>")
		return False

	make_success(result, template)

	if 'id' in template.attrib:
		if template.tag == 'namespace' and template.attrib['id'] == 'public':
			if result.attrib.get('ref') == 'public':
				make_success((result, 'ref'), template)
				return True
			else:
				LAST_ERROR = f"Expected {result.attrib.get('ref')} to be public"
				make_failure((result, 'ref'), template, "ref=\"public\"")
				return False

		template_id = template.attrib['id']
		if 'id' in result.attrib:
			result_id = result.attrib['id']
			tag_name = 'id'
		elif 'ref' in result.attrib:
			result_id = result.attrib['ref']
			tag_name = 'ref'
		else:
			LAST_ERROR = f"Element {result.tag!r} does not have an 'id' or 'ref' attribute"
			make_failure(result, template, "id=\"...\" or ref=\"...\"")
			return False

		if template_id in COLLECTED_IDS:
			if COLLECTED_IDS[template_id] != result_id:
				LAST_ERROR = f"Expected id {COLLECTED_IDS[template_id]} and {result_id} to match"
				make_failure((result, tag_name), template, COLLECTED_IDS[template_id])
				return False
		else:
			COLLECTED_IDS[template_id] = result_id

		make_success((result, tag_name), template)

	if 'ref' in result.attrib:
		new_result = find_actual(result.tag, result.attrib['ref'])
		if new_result is None:
			LAST_ERROR = f"Tag {result.tag!r} id {result.attrib['ref']!r} not found"
			return False
		result = new_result

	for attr, val in template.attrib.items():
		if attr == 'id' and template.tag not in {'symbol', 'namespace'}:
			continue # already checked
		elif attr in {'immediate', 'empty', 'missing'}:
			continue # meta command
		elif attr not in result.attrib:
			LAST_ERROR = f"Attribute {attr!r} of element {template.tag!r} not present"
			make_failure((result, attr), template, f"{attr}=\"...\"")
			return False
		elif val != result.attrib[attr]:
			LAST_ERROR = f"Attribute {attr!r} of element {template.tag!r} expected to be {val!r} received {result.attrib[attr]!r}"
			make_failure((result, attr), template, f"{attr}=\"{escape_attribute_value(val)}\"")
			return False
		else:
			make_success((result, attr), template)

	if template.text is not None and template.text.strip() != '':
		if result.text is None or template.text.strip() != result.text.strip():
			LAST_ERROR = f"Element tag {template.tag!r} contains {result.text.strip() if result.text is not None else result.text!r} instead of {template.text.strip()!r}"
			make_failure((result, CHILD_TEXT), template, template.text.strip())
			return False
		else:
			make_success((result, CHILD_TEXT), template)
	elif template.attrib.get('empty') == 'true' and result.text is not None and result.text.strip() != '':
		LAST_ERROR = f"Element tag {template.tag!r} contains {result.text.strip()!r} instead of empty"
		make_failure((result, CHILD_TEXT), template, "<!-- empty -->")
		return False

	iterator = iter(template)
	next_child = None # next template child to check
	last_errors = []
	immediate = 'child' in template.attrib.get('immediate', '').split() # if the first element must match exactly
	missing_elements = []
	for child in result:
		if next_child is None:
			try:
				# look at next child in template
				while True:
					next_child = next(iterator)
					if next_child.attrib.get('missing') == 'true':
						missing_elements.append(next_child)
					else:
						break
			except StopIteration:
				if immediate:
					LAST_ERROR = f"Unexpected child {child.tag!r} in element {template.tag!r}" + (" -> { " + ", ".join(last_errors) + " }" if len(last_errors) != 0 else "")
					#make_failure(child, template) # mark as failure for parent node
					return False
				if len(missing_elements) == 0:
					return True
				else:
					next_child = None

			#if next_child is not None:
			#	immediate = 'next' in next_child.attrib.get('immediate', '').split()

		#print(next_child, child, immediate)
		if next_child is not None:
			if check_xml(next_child, child):
				immediate = 'next' in next_child.attrib.get('immediate', '').split()
				next_child = None
				last_errors.clear()
				missing_elements.clear()
			elif immediate:
				# expect to follow immediately
				make_failure(child, next_child, f"<{next_child.tag}>")
				return False

		for missing_element in missing_elements:
			if check_xml(missing_element, child):
				LAST_ERROR = f"Unexpected child {child.tag!r} when specifically requiring it to be missing"
				make_failure(child, missing_element, f"<!-- no <{missing_element.tag}> -->")
				return False
		last_errors.append(LAST_ERROR)

	if next_child is None:
		try:
			while True:
				next_child = next(iterator)
				if next_child.attrib.get('missing') == 'true':
					pass # correct, it is missing
				else:
					break
		except StopIteration:
			return True

	LAST_ERROR = f"Missing child {next_child.tag!r} in element {template.tag!r}" + (" -> { " + ", ".join(last_errors) + " }" if len(last_errors) != 0 else "")
	make_failure((result, CHILD_NODE), template, f"<!-- no <{next_child.tag}> -->")
	make_failure(result, template, f"<!-- no <{next_child.tag}> -->")
	return False

def get_color(element):
	global PERFECT_MATCHES, CONSIDERED_MATCHES, CONSIDERED_FAILS, ABSOLUTE_FAILS
	if element in PERFECT_MATCHES:
		return "\33[1;92m" # green
	elif element in ABSOLUTE_FAILS:
		return "\33[1;91m" # red
	elif element in CONSIDERED_MATCHES:
		return "\33[1;94m" # blue
	elif element in CONSIDERED_FAILS:
		return "\33[1;93m" # yellow
	else:
		return ""

def switch_color(color1, color2 = ""):
	# change color from color1 to color2 without excessive printing
	if color2 == "":
		if color1 != "":
			return "\33[m"
	elif color2 != color1:
		return color2
	return ""

def escape_attribute_value(text):
	return text.replace('&', '&amp;').replace('"', '&quot;')

def escape_text(text):
	return text.replace('&', '&amp;').replace('<', '&lt;')

def get_failure_message(element, current_color = "", expected_color = ""):
	# displays an optional failure message
	global CONSIDERED_FAILS, ABSOLUTE_FAILS
	if element in ABSOLUTE_FAILS:
		message = ABSOLUTE_FAILS[element]
	elif element in CONSIDERED_FAILS:
		message = CONSIDERED_FAILS[element]
	else:
		return switch_color(current_color, expected_color)

	if expected_color == "\33[1;95m":
		expected_color = ""
	elif expected_color == "":
		expected_color = "\33[m"

	return f" \33[1;95m({message}){expected_color}" # magenta

def show_tree(tree, indent = ''):
	global CHILD_TEXT, NEXT_TEXT
	tag_color = get_color(tree)
	line = f'{tag_color}<{tree.tag}'
	for name, value in tree.attrib.items():
		escaped_value = escape_attribute_value(value)
		attrib_color = get_color((tree, name))
		line += " " + switch_color(tag_color, attrib_color)
		line += f"{name}=\"{escaped_value}\""
		line += get_failure_message((tree, name), attrib_color, tag_color)
	if len(tree) == 0 and (tree.text is None or tree.text.strip() == ''):
		line += "/>" + get_failure_message(tree, tag_color)
		print(f"{indent}{line}", file = sys.stderr)
	elif len(tree) == 0:
		text = escape_text(tree.text)
		text_color = get_color((tree, CHILD_TEXT))
		line += ">" + switch_color(tag_color, text_color)
		line += tree.text + switch_color(text_color, tag_color)
		line += f"</{tree.tag}>" + get_failure_message((tree, CHILD_TEXT)) + get_failure_message(tree)
		print(f"{indent}{line}", file = sys.stderr)
	else:
		line += ">" + get_failure_message(tree, tag_color)
		print(f"{indent}{line}", file = sys.stderr)
		if tree.text is not None and tree.text.strip() != '':
			text = escape_text(tree.text)
			text_color = get_color((tree, CHILD_TEXT))
			line = f"{indent}  {text_color}{text}" + get_failure_message((tree, CHILD_TEXT), text_color)
			print(line, file = sys.stderr)
		for element in tree:
			show_tree(element, f"{indent}  ")
			if tree.tail is not None and tree.tail.strip() != '':
				text = escape_text(tree.tail)
				text_color = get_color((element, NEXT_TEXT))
				line = f"{indent}  {text_color}{text}" + get_failure_message((tree, CHILD_NEXT), text_color)
				print(line, file = sys.stderr)
		message = get_failure_message((tree, CHILD_NODE))
		if message != "":
			print(f"{indent} {message}", file = sys.stderr)
		line = f"{indent}{tag_color}</{tree.tag}>" + switch_color(tag_color)
		print(line, file = sys.stderr)

used_tags = set(sys.argv[1:])

for test in tree.getroot():
	tmpfiles = []
	try:
		test_tags = set(test.attrib.get('tags', '').split(' '))
		if len(used_tags) != 0 and len(used_tags.intersection(test_tags)) == 0:
			continue

		name = None
		source = None
		source_type = 'script'
		expects = {}
		addargs = []
		for element in test:
			if element.tag == 'name':
				name = element.text
			elif element.tag == 'input':
				if 'mapes4' in element.attrib or 'map' in element.attrib:
					fd, fn = tmpfile = tempfile.mkstemp(suffix = '.js', text = True)
					tmpfiles.append(tmpfile)
					os.write(fd, element.text.encode('utf8'))
					os.close(fd)
					if 'mapes4' in element.attrib:
						addargs.append('-mapes4')
						addargs.append(element.attrib['mapes4'] + '=' + fn)
					else:
						addargs.append('-map')
						addargs.append(element.attrib['map'] + '=' + fn)
				else:
					source = element.text
					source_type = element.attrib.get('type', 'script')
					# the type can be 'batch' (evaluate each line separately), 'script' or 'module'
				if 'module-system' in element.attrib:
					module_system = element.attrib['module-system']
					if module_system.lower() == 'commonjs':
						if '-cjs' not in addargs:
							addargs.append('-cjs')
					elif module_system.lower() == 'amd':
						if '-amd' not in addargs:
							addargs.append('-amd')
					else:
						print(f"Error, unknown module system {module_system!r}", file = sys.stderr)
						exit(1)
				if 'flags' in element.attrib:
					addargs += element.attrib['flags'].split(' ')
			elif element.tag == 'results':
				skip_versions = 'lowest-version' in element.attrib
				for version in ['ES1', 'ES3', 'ES4', 'ES5', 'ES6', 'ESNext']:
					if skip_versions:
						if element.attrib['lowest-version'] == version or version == 'ESNext':
							skip_versions = False
						else:
							continue
					if 'version' in element.attrib and element.attrib['version'] != version:
						continue
					for E4X in [False, True]:
						if version == 'ES1' and E4X:
							continue
						if element.attrib.get('E4X') == 'enabled' and not E4X:
							continue
						if element.attrib.get('E4X') == 'disabled' and E4X:
							continue
						for Strict in [False, True]:
							if version in {'ES1', 'ES3'} and Strict:
								continue
							if element.attrib.get('strict') == 'enabled' and not Strict:
								continue
							if element.attrib.get('strict') == 'disabled' and Strict:
								continue
							if (version, E4X, Strict) in expects:
								continue
							expects[version, E4X, Strict] = element
					if element.get('highest-version') == version:
						break

		if source is None:
			continue # TODO: report

		print("=", "\33[1m" + name + "\33[m", "=")
		for version in ['ES1', 'ES3', 'ES4', 'ES5', 'ES6', 'ESNext']:
			for E4X in [False, True]:
				if version == 'ES1' and E4X: # and E4X or version == 'ES4' and not E4X:
					continue
				for Strict in [False, True]:
					if version in {'ES1', 'ES3'} and Strict:
						continue
					for UseES4 in [True, False]:
						if version == 'ES4' and not UseES4:
							continue
						print('*', version, "E4X" if E4X else "", "Strict" if Strict else "", "(alternative)" if not UseES4 else "")
						if (version, E4X, Strict) not in expects:
							print("\33[37;1mSKIPPED\33[m")
							continue
						expect = expects[version, E4X, Strict]
						if expect.attrib['status'] == 'ignored':
							print("\33[37;1mSKIPPED\33[m")
							continue
						arguments = [os.path.join(ROOTDIR, 'jsrun' if UseES4 else 'jsno4'), '-debug', '-' + source_type]
						arguments.append('-' + version.lower())
						#if E4X and version != 'ES4': # E4X is included automatically
						#	arguments.append('-e4x')
						if E4X:
							arguments.append('-e4x')
						if Strict:
							arguments.append('-strict')
						process_output = subprocess.run(arguments + addargs, input = source.encode('UTF-8'), capture_output = True)
						status = process_output.returncode == 0
						# TODO: instead of looking at stderr, check actual error type thrown (error types need to be readable)
						if not status and b"assert" in process_output.stderr.lower(): #len(process_output.stderr) != 0:
							print("\33[91;1mERRORS \33[m")
#							print(process_output.stderr.decode('UTF8'))
							exit(1)
#						print(expect.attrib['status'], process_output.returncode)
						if ['failure', 'success'][status] != expect.attrib['status']:
							#print(process_output.stdout.decode(u'utf8'))
							print("\33[91;1mFAILURE\33[m")
							print("Failed command line:", ' '.join(arguments + addargs))
							exit(1)

						if len(expect) > 0:
							output = process_output.stdout
							#print(output.decode(u'utf8'))
							jslogs = ET.fromstring(output)
							CURRENT_ROOT = jslogs
							COLLECTED_IDS.clear()
							if not check_xml(expect[0], jslogs):
								print("\33[91;1mFAILURE\33[m")
								print(LAST_ERROR)
								show_tree(jslogs)
								print("Failed command line:", ' '.join(arguments + addargs))
								exit(1)
						print("\33[92;1mSUCCESS\33[m")
	finally:
		for fd, fn in tmpfiles:
			os.remove(fn)
		tmpfiles.clear()

