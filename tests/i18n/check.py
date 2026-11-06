"""Report how the localization catalogs (src/i18n/lang-*.h) match the message keys in src/.

Findings are reported, never a failure: exit 0 after a report, 1 when the checker itself fails
(unreadable source, an unparseable catalog or language registry), 2 on bad usage. Rules:
src/i18n/AGENTS.md.
"""
import argparse
import bisect
from dataclasses import dataclass
import json
from pathlib import Path
import re
import sys
import unicodedata

REPO = Path(__file__).resolve().parents[2]
ENGINE = "src/i18n/"
REGISTRY = "src/i18n/languages.h"
MARKER = "i18n-en"  # A comment holding it marks its line's literals as intentional English.


class CheckError(Exception):
    """The checker's own failure: unreadable source, an unparseable catalog or registry."""


# C++ tokens -------------------------------------------------------------------------------------

TOKEN = re.compile(r"""
    (?P<comment>//[^\n]*|/\*.*?\*/)
  | (?P<raw>(?P<rprefix>L|u8|u|U)?R"(?P<delim>[^()\\\s"]{0,16})\((?P<rbody>.*?)\)(?P=delim)")
  | (?P<string>(?P<prefix>L|u8|u|U)?"(?P<body>(?:\\.|[^"\\\n])*)")
  | (?P<char>(?:L|u8|u|U)?'(?:\\.|[^'\\\n])+')
  | (?P<ident>[A-Za-z_]\w*)
  | (?P<number>\.?\d(?:[eEpP][+-]|[\w.'])*)
  | (?P<space>\s+)
  | (?P<punct>.)
""", re.S | re.X)
KINDS = ("comment", "raw", "string", "char", "ident", "number", "space", "punct")
ESCAPE = re.compile(r"\\(?:x([0-9A-Fa-f]+)|u([0-9A-Fa-f]{4})|U([0-9A-Fa-f]{8})|([0-7]{1,3})|(.))", re.S)
SIMPLE_ESCAPES = {"n": "\n", "t": "\t", "r": "\r", "a": "\a", "b": "\b", "f": "\f", "v": "\v", "\\": "\\",
                  "'": "'", '"': '"', "?": "?"}


@dataclass
class Token:
    kind: str
    text: str
    line: int
    value: str = ""    # String literals: the decoded text
    wide: bool = False


def decode(body):
    """A literal's text; an unknown escape is its character, as clang reads it (with a warning)."""
    def one(match):
        hex_, u4, u8, octal, simple = match.groups()
        if hex_ or u4 or u8:
            return chr(int(hex_ or u4 or u8, 16))
        if octal:
            return chr(int(octal, 8))
        return SIMPLE_ESCAPES.get(simple, simple)
    return ESCAPE.sub(one, body)


def tokenize(text, rel):
    """Tokens and the lines of comments holding the marker."""
    starts = [0] + [m.end() for m in re.finditer("\n", text)]
    tokens, marked = [], set()
    for match in TOKEN.finditer(text):
        kind = next(k for k in KINDS if match.group(k) is not None)
        line = bisect.bisect_right(starts, match.start())
        token = Token(kind, match.group(), line)
        if kind == "comment":
            if MARKER in token.text:
                marked.add(line)
        elif kind == "raw":
            token.value, token.wide = match.group("rbody"), match.group("rprefix") == "L"
        elif kind == "string":
            token.value, token.wide = decode(match.group("body")), match.group("prefix") == "L"
        tokens.append(token)
    return [t for t in tokens if t.kind not in ("space", "comment")], marked


def literal(code, at):
    """The literal of adjacent string tokens starting at `at`: (value, wide, end) or None."""
    end = at
    while end < len(code) and code[end].kind in ("string", "raw"):
        end += 1
    if end == at:
        return None
    pieces = code[at:end]
    return "".join(p.value for p in pieces), any(p.wide for p in pieces), end


def expect(code, at, text, where):
    if at >= len(code) or code[at].text != text:
        found = code[at].text if at < len(code) else "the end"
        raise CheckError(f"{where}: expected {text!r}, found {found!r}")
    return at + 1


# Template grammar: the runtime's (src/i18n/template.h) rules ------------------------------------

CATEGORIES = ("zero", "one", "two", "few", "many", "other")
RULE_CATEGORIES = {
    "English": {"one", "other"},
    "Spanish": {"one", "many", "other"},
    "French": {"one", "many", "other"},
    "Russian": {"one", "few", "many", "other"},
    "OtherOnly": {"other"},
}
ESCAPABLE = "{}<#\\"
MAX_NAMES, MAX_SELECTORS = 16, 4
NAME = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
IN_BRANCH, IN_TAG = 1, 2


class TemplateError(ValueError):
    pass


@dataclass
class Shape:
    args: set
    plurals: set
    links: set
    linked: set  # Arguments inside a link
    uses: dict  # Argument: (fewest, most) {name} uses in an expansion ('#' and plurals aside)


class Parser:
    def __init__(self, text, rule):
        self.s, self.at, self.rule = text, 0, rule
        self.shape = Shape(set(), set(), set(), set(), {})
        self.branch_links = None  # Inside a plural branch: its links and argument uses
        self.branch_uses = None
        self.in_link = False
        self.top = {}  # Uses outside plurals
        self.picked = {}  # Selector: {category it can select: uses in the branches that picks}

    def fail(self, why):
        raise TemplateError(f"{why} (at {self.at})")

    def peek(self, text):
        return self.s.startswith(text, self.at)

    def spaces(self):
        while self.peek(" "):
            self.at += 1

    def name(self):
        match = NAME.match(self.s, self.at)
        if not match:
            return ""
        self.at = match.end()
        return match.group()

    def message(self, scope):
        while self.at < len(self.s):
            c = self.s[self.at]
            if c == "}" or self.peek("</b>") or self.peek("</link>"):
                return
            if c == "\\":
                if self.at + 1 >= len(self.s) or self.s[self.at + 1] not in ESCAPABLE:
                    self.fail("a backslash escapes only { } < # \\")
                self.at += 2
            elif c == "{":
                self.placeholder(scope)
            elif c == "<":
                self.tag(scope)
            else:
                self.at += 1

    def placeholder(self, scope):
        self.at += 1
        self.spaces()
        arg = self.name()
        self.spaces()
        if not arg:
            self.fail("missing argument name")
        self.shape.args.add(arg)
        if self.peek("}"):
            self.at += 1
            if self.in_link:
                self.shape.linked.add(arg)
            uses = self.branch_uses if self.branch_uses is not None else self.top
            uses[arg] = uses.get(arg, 0) + 1
            return
        if scope & IN_BRANCH:
            self.fail("a plural inside a plural")
        if not self.peek(","):
            self.fail("expected '}' or ', plural,'")
        self.at += 1
        self.spaces()
        if self.name() != "plural":
            self.fail("only plural placeholders exist")
        self.spaces()
        if not self.peek(","):
            self.fail("expected ',' after plural")
        self.at += 1
        self.shape.plurals.add(arg)
        first, branches = None, {}
        self.spaces()
        while not self.peek("}"):
            keyword = self.name()
            if keyword not in CATEGORIES:
                self.fail(f"unknown plural category {keyword!r}" if keyword else "expected a plural category or '}'")
            if keyword in branches:
                self.fail(f"plural category {keyword} twice")
            if keyword not in RULE_CATEGORIES[self.rule]:
                self.fail(f"plural category {keyword} isn't one of {self.rule}'s")
            self.spaces()
            if not self.peek("{"):
                self.fail("expected '{' after a plural category")
            self.at += 1
            self.branch_links, self.branch_uses = set(), {}
            self.message(scope | IN_BRANCH)
            if not self.peek("}"):
                self.fail("unclosed plural branch")
            self.at += 1
            branches[keyword] = self.branch_uses
            links, self.branch_links, self.branch_uses = self.branch_links, None, None
            if first is None:
                first = links
            elif links != first:
                self.fail("plural branches hold different links")
            self.spaces()
        if "other" not in branches:
            self.fail("a plural without other")
        self.at += 1
        for link in sorted(first):
            self.add_link(self.shape.links, link)
        # Plurals on one selector pick together; a missing category picks other, as a selector that
        # isn't a number does (Russian's included).
        for category in RULE_CATEGORIES[self.rule]:
            picked = self.picked.setdefault(arg, {}).setdefault(category, {})
            for name, count in branches.get(category, branches["other"]).items():
                picked[name] = picked.get(name, 0) + count

    def add_link(self, links, link):
        if link in links:
            self.fail(f"link {link} twice")
        links.add(link)

    def tag(self, scope):
        if scope & IN_TAG:
            self.fail("a tag inside a tag")
        if self.peek("<b>"):
            self.at += 3
            closer = "</b>"
        elif self.peek('<link id="'):
            self.at += 10
            link = self.name()
            if not link or not self.peek('">'):
                self.fail('expected <link id="name">')
            self.at += 2
            self.add_link(self.branch_links if self.branch_links is not None else self.shape.links, link)
            closer = "</link>"
        else:
            self.fail("an unknown tag (a literal '<' is \\<)")
        content = self.at
        self.in_link = closer == "</link>"
        self.message(scope | IN_TAG)
        self.in_link = False
        if self.at == content:
            self.fail("an empty tag")
        if not self.peek(closer):
            self.fail(f"expected {closer}")
        self.at += len(closer)


def shape(text, rule):
    """(Shape, None) when the template is valid under the plural rule, else (None, why)."""
    parser = Parser(text, rule)
    try:
        parser.message(0)
        if parser.at != len(text):
            parser.fail(f"unmatched {text[parser.at:parser.at + 7]!r}")
        if len(parser.shape.args) > MAX_NAMES or len(parser.shape.links) > MAX_NAMES:
            parser.fail(f"more than {MAX_NAMES} argument names or link IDs")
        if len(parser.shape.plurals) > MAX_SELECTORS:
            parser.fail(f"more than {MAX_SELECTORS} plural selectors")
    except TemplateError as error:
        return None, str(error)
    for name in parser.shape.args:
        fewest = most = parser.top.get(name, 0)
        for categories in parser.picked.values():
            counts = [uses.get(name, 0) for uses in categories.values()]
            fewest, most = fewest + min(counts), most + max(counts)
        parser.shape.uses[name] = (fewest, most)
    return parser.shape, None


def translation_problem(translation, rule, english):
    """Why the runtime falls back to English for this translation, or None when it uses it."""
    if not translation:
        return "empty translation"
    english_shape, why = shape(english, "English")
    if why:
        return f"the English key is malformed: {why}"
    translated, why = shape(translation, rule)
    if why:
        return why
    for label, ours, theirs in (("argument names", translated.args, english_shape.args),
                                ("plural selectors", translated.plurals, english_shape.plurals),
                                ("link IDs", translated.links, english_shape.links)):
        if ours != theirs:
            return f"{label} {sorted(ours)} differ from English {sorted(theirs)}"
    for name in sorted(english_shape.args - english_shape.plurals):
        fewest, most = english_shape.uses[name]
        ours = translated.uses[name]
        if fewest and not ours[0]:
            return f"argument {name} is missing from an expansion that English always shows it in"
        if ours[1] > most:
            return f"argument {name} shows {ours[1]} times in an expansion, English at most {most}"
    if moved := sorted(translated.linked - english_shape.linked):
        return f"argument(s) {moved} inside a link where English has none"
    return None


# Source scans ----------------------------------------------------------------------------------

def extract_keys(code, rel):
    """(keys, problems): every Tr(L"...") and TrCtx(L"...", L"...") call, as (context, English, where)."""
    keys, problems = [], []
    for i, token in enumerate(code):
        if token.kind != "ident" or token.text not in ("Tr", "TrCtx"):
            continue
        # Declarations ("I18nKey Tr("); any other use must be a literal call.
        if i and code[i - 1].text == "I18nKey":
            continue
        where, count = f"{rel}:{token.line}", 1 if token.text == "Tr" else 2
        args, at = [], i + 2 if i + 1 < len(code) and code[i + 1].text == "(" else len(code)
        while (found := literal(code, at)) and found[1]:
            args.append(found[0])
            at = found[2]
            if at < len(code) and code[at].text == ",":
                at += 1
                continue
            if at < len(code) and code[at].text == ")":
                at = -1
            break
        if at != -1 or len(args) != count:
            problems.append({"where": where,
                             "problem": f"{token.text} must be called with {count} wide string literal(s)"})
            continue
        keys.append(("", args[0], where) if count == 1 else (args[0], args[1], where))
    return keys, problems


def call_spans(code, names):
    """Token index ranges of calls to the named functions, parentheses included."""
    spans = []
    for i, token in enumerate(code):
        if token.kind == "ident" and token.text in names and i + 1 < len(code) and code[i + 1].text == "(":
            depth = 0
            for j in range(i + 1, len(code)):
                depth += {"(": 1, ")": -1}.get(code[j].text, 0)
                if depth == 0:
                    spans.append((i, j))
                    break
    return spans


FORMAT = re.compile(r"%[-+ #0]*(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|h|ll|l|L|z|j|t|I64|I32|I)?[diouxXeEfFgGaAcCsSpn%]")
LETTERS = re.compile(r"[^\W\d_]{2,}")
WORD = re.compile(r"[^\W\d_]+(?:[-'’][^\W\d_]+)*")


def user_facing(text):
    """Looks like UI text: two or more words, one with letters, or one capitalized word between
    punctuation ("Settings", "Custom..."). URLs and paths (anything with a backslash) don't."""
    if "://" in text or "\\" in text:
        return False
    plain = FORMAT.sub(" ", text)
    if not LETTERS.search(plain):
        return False
    if len(text.split()) >= 2:
        return True
    core = re.sub(r"^[\W\d_]+|[\W\d_]+$", "", plain)
    return bool(WORD.fullmatch(core)) and core[0].isupper() and re.sub(r"[-'’]", "", core[1:]).islower()


def unwrapped_literals(code, marked, rel):
    """Wide literals that look like UI text outside Tr, TrCtx and Wh_Log calls and marked lines."""
    excluded = call_spans(code, ("Tr", "TrCtx", "Wh_Log"))
    found, at = [], 0
    while at < len(code):
        piece = literal(code, at)
        if not piece:
            at += 1
            continue
        value, wide, end = piece
        lines = {t.line for t in code[at:end]}
        if (wide and not lines & marked and not any(a < at < b for a, b in excluded) and user_facing(value)):
            found.append({"where": f"{rel}:{code[at].line}", "text": value})
        at = end
    return found


# Catalogs and registry -------------------------------------------------------------------------

@dataclass
class Entry:
    context: str
    english: str
    translation: str
    where: str

    def key(self):
        return self.context, self.english


def sort_key(context, english):
    """The runtime's order: context, then English, by UTF-16 code units."""
    return context.encode("utf-16-be"), english.encode("utf-16-be")


def parse_catalogs(code, rel):
    """{array name: [Entry]} for each `static constexpr I18nEntry NAME[] = {...};`."""
    catalogs = {}
    head = ["static", "constexpr", "I18nEntry"]
    for i in range(len(code) - 3):
        if [t.text for t in code[i:i + 3]] != head:
            continue
        name, where = code[i + 3].text, f"{rel}:{code[i].line}"
        at = expect(code, expect(code, expect(code, expect(code, i + 4, "[", where), "]", where), "=", where), "{", where)
        entries = []
        while at < len(code) and code[at].text != "}":
            line = code[at].line
            at = expect(code, at, "{", f"{rel}:{line}")
            fields = []
            for separator in (",", ",", "}"):
                piece = literal(code, at)
                if not piece or not piece[1]:
                    raise CheckError(f"{rel}:{code[at].line if at < len(code) else line}: "
                                     "an entry holds three wide string literals")
                fields.append(piece[0])
                at = expect(code, piece[2], separator, f"{rel}:{line}")
            entries.append(Entry(*fields, f"{rel}:{line}"))
            if at < len(code) and code[at].text == ",":
                at += 1
        expect(code, expect(code, at, "}", where), ";", where)
        catalogs[name] = entries
    return catalogs


def parse_registry(code, rel):
    """[(tag, plural rule, catalog array name or None)] from kI18nLanguages."""
    for i, token in enumerate(code):
        if token.text == "kI18nLanguages" and [t.text for t in code[i + 1:i + 4]] == ["[", "]", "="]:
            break
    else:
        raise CheckError(f"{rel}: no kI18nLanguages registry")
    languages, at = [], expect(code, i + 4, "{", rel)
    while at < len(code) and code[at].text != "}":
        where = f"{rel}:{code[at].line}"
        at = expect(code, at, "{", where)
        tag = literal(code, at)
        if not tag:
            raise CheckError(f"{where}: a language starts with its tag")
        at = tag[2]
        for _ in range(2):
            piece = literal(code, expect(code, at, ",", where))
            if not piece:
                raise CheckError(f"{where}: expected the native and locale names")
            at = piece[2]
        at = expect(code, expect(code, expect(code, expect(code, at, ",", where), "I18nPluralRule", where), ":", where),
                    ":", where)
        rule = code[at].text
        if rule not in RULE_CATEGORIES:
            raise CheckError(f"{where}: unknown plural rule {rule}")
        at = expect(code, at + 1, ",", where)
        at = expect(code, at + 1, ",", where)  # inProgress
        if code[at].text == "{":
            catalog, at = None, expect(code, at + 1, "}", where)
        else:
            catalog, at = code[at].text, at + 1
        at = expect(code, at, ",", where)
        at = expect(code, at + 1, "}", where)  # pseudo
        languages.append((tag[0], rule, catalog))
        if at < len(code) and code[at].text == ",":
            at += 1
    return languages


# The check -------------------------------------------------------------------------------------

def read_sources(src):
    """{repo-relative path: text} of every .h and .cpp file under src."""
    try:
        return {f"src/{p.relative_to(src).as_posix()}": p.read_bytes().decode("utf-8")
                for p in sorted(src.rglob("*")) if p.is_file() and p.suffix in (".h", ".cpp")}
    except (OSError, UnicodeError) as error:
        raise CheckError(f"cannot read the sources: {error}") from error


def check(sources):
    """Findings for {path: text} sources; raises CheckError on the checker's own failures."""
    if REGISTRY not in sources:
        raise CheckError(f"{REGISTRY} is missing")
    keys, unextractable, literals, special, catalogs, catalog_files = {}, [], [], [], {}, {}
    registry = None
    for rel, text in sorted(sources.items()):
        # The catalog's PR validation flags these on any line of the mod file
        for number, line in enumerate(text.split("\n"), 1):
            if chars := sorted({c for c in line if unicodedata.category(c) == "Cf"
                                or (unicodedata.category(c) == "Zs" and c != " ")}):
                special.append({"where": f"{rel}:{number}", "chars": [f"U+{ord(c):04X}" for c in chars]})
        code, marked = tokenize(text, rel)
        found, problems = extract_keys(code, rel)
        for context, english, where in found:
            keys.setdefault((context, english), []).append(where)
        unextractable += problems
        if rel.startswith(ENGINE + "lang-"):
            for name, entries in parse_catalogs(code, rel).items():
                catalogs[name], catalog_files[name] = entries, rel
        if rel == REGISTRY:
            registry = parse_registry(code, rel)
        if not rel.startswith(ENGINE):
            literals += unwrapped_literals(code, marked, rel)
    malformed = [{"key": list(key), "where": wheres, "problem": why} for key, wheres in sorted(keys.items())
                 if (why := shape(key[1], "English")[1])]
    languages, registered = [], set()
    for tag, rule, name in registry:
        if name is None:
            continue
        if name not in catalogs:
            raise CheckError(f"{REGISTRY}: {tag}'s catalog {name} isn't in any src/i18n/lang-*.h")
        registered.add(name)
        entries = catalogs[name]
        present, duplicates, unsorted, invalid = set(), [], [], []
        for index, entry in enumerate(entries):
            if entry.key() in present:
                duplicates.append({"key": list(entry.key()), "where": entry.where})
            present.add(entry.key())
            if index and not sort_key(*entries[index - 1].key()) < sort_key(*entry.key()):
                unsorted.append({"key": list(entry.key()), "where": entry.where})
            if why := translation_problem(entry.translation, rule, entry.english):
                invalid.append({"key": list(entry.key()), "where": entry.where, "translation": entry.translation,
                                "problem": why})
        languages.append({
            "tag": tag, "rule": rule, "catalog": name, "file": catalog_files[name], "entries": len(entries),
            "missing": [list(key) for key in sorted(keys) if key not in present],
            "stale": [{"key": list(e.key()), "where": e.where} for e in entries if e.key() not in keys],
            "duplicates": duplicates, "unsorted": unsorted, "invalid": invalid,
        })
    return {
        "keys": [{"key": list(key), "where": wheres} for key, wheres in sorted(keys.items())],
        "unextractable": unextractable,
        "malformed_english": malformed,
        "languages": languages,
        "unregistered_catalogs": sorted(f"{catalog_files[n]}: {n}" for n in catalogs if n not in registered),
        "unwrapped_literals": literals,
        "special_spaces": special,
    }


def show(key):
    context, english = key
    text = json.dumps(english, ensure_ascii=False)
    return f"{context} | {text}" if context else text


def report(findings, src):
    lines = [f"Localization check of {src}"]
    files = {where.rsplit(":", 1)[0] for item in findings["keys"] for where in item["where"]}
    lines.append(f"Keys in source: {len(findings['keys'])} in {len(files)} file(s)")
    lines.append("Languages: " + ", ".join(f"{l['tag']} ({l['entries']} entries)" for l in findings["languages"]))

    def section(title, items, render):
        lines.append("")
        lines.append(f"{title}: {len(items) if items else 'none'}")
        lines.extend(f"  {render(item)}" for item in items)

    section("Unextractable key calls", findings["unextractable"], lambda i: f"{i['where']}: {i['problem']}")
    section("Malformed English keys", findings["malformed_english"],
            lambda i: f"{i['where'][0]}: {show(i['key'])}: {i['problem']}")
    section("Catalogs outside the registry", findings["unregistered_catalogs"], str)
    for title, field, render in (
            ("Missing entries", "missing", show),
            ("Unused or stale entries", "stale", lambda i: f"{i['where']}: {show(i['key'])}"),
            ("Duplicate keys", "duplicates", lambda i: f"{i['where']}: {show(i['key'])}"),
            ("Unsorted tables", "unsorted", lambda i: f"{i['where']}: {show(i['key'])} is out of order"),
            ("Argument, link and plural mismatches", "invalid",
             lambda i: f"{i['where']}: {show(i['key'])}: {i['problem']}")):
        items = [(language["tag"], item) for language in findings["languages"] for item in language[field]]
        section(title, items, lambda pair, render=render: f"{pair[0]}: {render(pair[1])}")
    literals = findings["unwrapped_literals"]
    files = {item["where"].rsplit(":", 1)[0] for item in literals}
    lines.append("")
    lines.append(f"Unwrapped user-facing literals: {f'{len(literals)} in {len(files)} file(s)' if literals else 'none'}")
    lines.extend(f"  {i['where']}: {json.dumps(i['text'], ensure_ascii=False)}" for i in literals)
    section("Invisible or non-standard spaces written as characters (write \\u escapes)", findings["special_spaces"],
            lambda i: f"{i['where']}: {', '.join(i['chars'])}")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--src", type=Path, default=REPO / "src", help="Source root (default: the repo's src/)")
    parser.add_argument("--json", type=Path, help="Also write the findings as JSON to this file")
    args = parser.parse_args()
    sys.stdout.reconfigure(errors="backslashreplace")
    try:
        findings = check(read_sources(args.src.resolve()))
        if args.json:
            args.json.write_text(json.dumps(findings, indent=2, ensure_ascii=False), encoding="utf-8")
    except (CheckError, OSError) as error:
        print(f"i18n check failed: {error}", file=sys.stderr)
        return 1
    sys.stdout.write(report(findings, args.src.resolve()))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
