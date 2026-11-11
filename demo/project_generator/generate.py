import argparse
from dataclasses import dataclass
import json
import posixpath
from pathlib import Path, PurePosixPath
import sys


@dataclass(frozen=True)
class Directive:
    header: str
    line: int
    when: dict[str, bool]


def write_json(path, data):
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def write_database(database, project, targets):
    commands = []
    for target in targets:
        flags = [f"-I{directory}" for directory in target.get("include_dirs", [])]
        flags += [f"-D{define}" for define in target.get("defines", [])]
        for source in target["sources"]:
            commands.append({
                "directory": str(project),
                "file": source,
                "arguments": ["clang++", "-std=c++20", *flags, "-c", source],
            })
    if not commands:
        raise ValueError("The sample needs at least one compiled source")
    write_json(database, commands)


def render_file(path, includes, is_source):
    lines = [] if is_source else ["#pragma once", ""]
    directives = []
    for include in includes:
        header = include["header"]
        when = include.get("when", {})
        if when:
            condition = " && ".join(
                f"defined({macro})" if required else f"!defined({macro})"
                for macro, required in when.items()
            )
            lines.append(f"#if {condition}")

        if "via" in include:
            spelling = PurePosixPath(header).relative_to(include["via"]).as_posix()
            argument = f"<{spelling}>"
        else:
            spelling = posixpath.relpath(header, posixpath.dirname(path) or ".")
            argument = f'"{spelling}"'
        directives.append(Directive(header, len(lines) + 1, when))
        lines.append(f"#include {argument}")

        if when:
            lines.append("#endif")
    if is_source:
        lines.extend(("", "static_assert(true);"))
    return "\n".join(lines) + "\n", directives


def check_fields(value, context, allowed):
    if not isinstance(value, dict):
        raise ValueError(f"{context} must be an object")
    unknown = value.keys() - allowed
    if unknown:
        raise ValueError(f"{context} has unknown fields: {', '.join(sorted(unknown))}")


def parse_sample(sample):
    check_fields(sample, "Sample", {"targets", "includes"})
    targets = sample["targets"]
    if not isinstance(targets, list):
        raise ValueError("Sample targets must be a list")
    for index, target in enumerate(targets, start=1):
        check_fields(target, f"Target {index}", {"sources", "include_dirs", "defines"})
        for field, values in target.items():
            if not isinstance(values, list) or not all(isinstance(value, str) for value in values):
                raise ValueError(f"Target {index} field {field!r} must be a list of strings")

    declarations = sample["includes"]
    if not isinstance(declarations, dict):
        raise ValueError("Sample includes must map file paths to include lists")
    includes = {}
    for path, rules in declarations.items():
        if not isinstance(rules, list):
            raise ValueError(f"Includes for {path!r} must be a list")
        includes[path] = []
        for index, rule in enumerate(rules, start=1):
            include = {"header": rule} if isinstance(rule, str) else rule
            context = f"Include {index} in {path!r}"
            check_fields(include, context, {"header", "via", "when"})
            for field in include.keys() & {"header", "via"}:
                if not isinstance(include[field], str):
                    raise ValueError(f"{context} field {field!r} must be a string")
            when = include.get("when", {})
            if not isinstance(when, dict) or not all(isinstance(value, bool) for value in when.values()):
                raise ValueError(f"{context} field 'when' must map macros to booleans")
            includes[path].append(include)
    return targets, includes


def generate_project(project, targets, includes):
    sources = {path for target in targets for path in target["sources"]}
    headers = {rule["header"] for rules in includes.values() for rule in rules}
    graph = {}
    for path in sorted(sources | includes.keys() | headers):
        relative = PurePosixPath(path)
        destination = (project / path).resolve()
        if (
            relative.as_posix() != path or relative.is_absolute() or ".." in relative.parts
            or destination == project or not destination.is_relative_to(project)
        ):
            raise ValueError(f"Expected a project-relative file path, got {path!r}")
        content, graph[path] = render_file(path, includes.get(path, []), path in sources)
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(content, encoding="utf-8")
    return graph


def load_samples():
    manifest = Path(__file__).with_name("samples.json")
    return json.loads(manifest.read_text(encoding="utf-8"))


def generate_sample(sample, project, database):
    project = Path(project).resolve()
    database = Path(database).resolve()
    targets, includes = parse_sample(sample)
    files = {path for target in targets for path in target["sources"]}
    files.update(includes)
    files.update(rule["header"] for rules in includes.values() for rule in rules)
    if database in {(project / path).resolve() for path in files}:
        raise ValueError(f"Compilation database conflicts with a generated file: {database}")
    graph = generate_project(project, targets, includes)
    write_database(database, project, targets)
    return targets, graph


def main():
    parser = argparse.ArgumentParser(description="Generate a HeaderScope sample project")
    parser.add_argument("sample", nargs="?", help="Sample name from samples.json")
    parser.add_argument("output", nargs="?", type=Path, help="New or empty project directory")
    parser.add_argument("--list", action="store_true", help="List available samples")
    args = parser.parse_args()

    samples = load_samples()
    if args.list:
        if args.sample or args.output:
            parser.error("--list cannot be combined with a sample or output directory")
        print("\n".join(samples))
        return 0
    if not args.sample or not args.output:
        parser.error("provide a sample name and an output directory")
    if args.sample not in samples:
        parser.error(f"unknown sample {args.sample!r}; use --list to see available samples")

    output = args.output.resolve()
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        parser.error(f"output directory must be new or empty: {output}")
    try:
        output.mkdir(parents=True, exist_ok=True)
        generate_sample(samples[args.sample], output, output / "compile_commands.json")
    except (KeyError, TypeError, ValueError, OSError) as error:
        print(f"Cannot generate sample in {output}: {error}", file=sys.stderr)
        return 1
    print(f"Generated {args.sample} in {output}")
    print(f"Compilation database: {output / 'compile_commands.json'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())


