import argparse
from collections import Counter
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
sys.dont_write_bytecode = True
from project_generator.generate import generate_sample, load_samples, write_database, write_json


def expected_snapshot(targets, graph):
    files = set()
    edges = set()
    for target in targets:
        defines = {define.split("=", 1)[0] for define in target.get("defines", [])}
        pending = list(target["sources"])
        visited = set()
        while pending:
            path = pending.pop()
            if path in visited:
                continue
            visited.add(path)
            for directive in graph[path]:
                if all((macro in defines) == required for macro, required in directive.when.items()):
                    edges.add((path, directive.header, directive.line))
                    pending.append(directive.header)
        files.update(visited)
    return {
        "version": 1,
        "files": sorted(files),
        "edges": [
            {"includer": source, "included": header, "line": line}
            for source, header, line in sorted(edges)
        ],
    }


def run_cli(inspect, *args, status=0):
    result = subprocess.run(
        [str(inspect), *map(str, args)], capture_output=True, encoding="utf-8",
        errors="replace", timeout=15,
    )
    if result.returncode != status:
        raise RuntimeError(
            f"CLI exited with {result.returncode}, expected {status}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def run_inspect(inspect, database, project, *options, status=0):
    return run_cli(inspect, database, "--root", project, *options, status=status)


def edge_counts(snapshot):
    return Counter((edge["includer"], edge["included"], edge["line"]) for edge in snapshot["edges"])


def check_graph(inspect, work, project, sample):
    database = work / "compile_commands.json"
    targets, graph = generate_sample(sample, project, database)
    expected = expected_snapshot(targets, graph)
    write_json(work / "expected.json", expected)
    snapshot = work / "actual.json"
    run_inspect(inspect, database, project, "--save", snapshot)
    actual = json.loads(snapshot.read_text(encoding="utf-8"))
    differences = []
    if actual["version"] != expected["version"]:
        differences.append(f"Snapshot version {actual['version']}, expected {expected['version']}")
    for field, wanted, observed in (
        ("files", Counter(expected["files"]), Counter(actual["files"])),
        ("edges", edge_counts(expected), edge_counts(actual)),
    ):
        if wanted != observed:
            differences.append(
                f"{field}: missing {dict(wanted - observed)}, unexpected {dict(observed - wanted)}"
            )
    if differences:
        raise RuntimeError("\n".join(differences))


def expect_output(output, *fragments):
    missing = [fragment for fragment in fragments if fragment not in output]
    if missing:
        raise RuntimeError(f"Missing output {missing!r}\n{output}")


def check_workflow(inspect, work, project):
    help_output = run_cli(inspect, "--help").stdout
    expect_output(
        help_output, "Usage:", "Examples:", "Exit codes:", "compile_commands.json",
        "--version", "--cycles-only", "--rank", "--root", "--save", "--compare", "--export",
    )
    if run_cli(inspect, "-h").stdout != help_output:
        raise RuntimeError("The -h shortcut returned different help")
    version = run_cli(inspect, "--version").stdout.strip()
    if not re.fullmatch(r"headerscope_inspect [0-9]+\.[0-9]+\.[0-9]+", version):
        raise RuntimeError(f"Unexpected version output: {version!r}")

    demo = Path(__file__).resolve().parents[2] / "include-chain"
    for filename in ("main.cpp", "a.h", "b.h", "extra.h"):
        shutil.copyfile(demo / filename, project / filename)
    database = work / "compile_commands.json"
    snapshot = work / "baseline.json"

    write_database(database, project, [{"sources": ["main.cpp"], "defines": ["HEADERSCOPE_DEMO_EXTRA"]}])
    baseline = run_inspect(inspect, database, project, project / "b.h", "--save", snapshot)
    expect_output(
        baseline.stdout, "== Analysis ==\n  Complete scan: 4 files, 3 include directives",
        "== Include graph ==\n  a.h\n    -> b.h (line 3)\n    -> extra.h (line 6)",
        "Files without direct includes (2)\n    b.h\n    extra.h",
        "== Cycles ==\n  Cycle groups: 0",
        "== Dependent chains ==\n  For b.h\n    a.h -> b.h\n    main.cpp -> a.h -> b.h",
    )
    baseline_bytes = snapshot.read_bytes()

    for input_path, output_path, options in (
        (database, database, ()),
        (database.parent, database, ()),
        (database, snapshot, ("--compare", snapshot)),
    ):
        original = output_path.read_bytes()
        rejected = run_inspect(
            inspect, input_path, project, *options, "--export", "json", output_path, status=2,
        )
        expect_output(rejected.stderr, "Graph export must not overwrite an input file")
        if output_path.read_bytes() != original:
            raise RuntimeError(f"Export changed input file {output_path}")

    write_database(database, project, [{"sources": ["main.cpp"], "defines": ["HEADERSCOPE_DEMO_CYCLE"]}])
    comparison = run_inspect(inspect, database, project, "--compare", snapshot, "--rank")
    expect_output(
        comparison.stdout, "== Snapshot comparison ==", "Added includes: 1\n    b.h:4 -> a.h",
        "Removed includes: 1\n    a.h:6 -> extra.h",
        "New or expanded cycle groups: 1", "a.h -> b.h -> a.h",
        "Affected source files: 1\n    main.cpp", "1. a.h: sources: 1, direct includers: 2",
    )

    cycles = run_inspect(inspect, database.parent, project, "--cycles-only").stdout
    expect_output(cycles, "== Cycles ==\n  Cycle groups: 1", "Group 1 (2 files, 1 cycle)", "a.h -> b.h -> a.h")
    if "== Include graph ==" in cycles:
        raise RuntimeError("--cycles-only printed the full graph")

    comparison_cycles = run_inspect(inspect, database, project, "--compare", snapshot, "--cycles-only").stdout
    expect_output(comparison_cycles, "New or expanded cycle groups: 1", "a.h -> b.h -> a.h")
    if any(section in comparison_cycles for section in ("Added includes:", "Removed includes:", "Affected source files:")):
        raise RuntimeError("--cycles-only printed non-cycle comparison details")

    incompatible = run_inspect(inspect, database, project, "--cycles-only", "--rank", status=2)
    expect_output(incompatible.stderr, "--cycles-only cannot be combined with --rank")

    with (project / "main.cpp").open("a", encoding="utf-8") as source:
        source.write('\n#include "missing.h"\n')
    partial = run_inspect(inspect, database, project, "--save", snapshot, status=1)
    expect_output(partial.stdout, "Partial scan:", "main.cpp\n    -> a.h (line 1)")
    expect_output(partial.stderr, "missing.h")
    if snapshot.read_bytes() != baseline_bytes:
        raise RuntimeError("The partial scan changed the saved baseline")


def run_case(work_dir, name, check, inspect, *args):
    work = Path(tempfile.mkdtemp(prefix="inspect-e2e-", dir=work_dir)).resolve()
    project = work / "project"
    try:
        project.mkdir()
        check(inspect, work, project, *args)
    except (KeyError, TypeError, ValueError, OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"FAIL {name} ({work})\n{error}", file=sys.stderr)
        return False
    shutil.rmtree(work)
    print(f"PASS {name}")
    return True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("suite", choices=("graphs", "workflow"))
    parser.add_argument("inspect", type=Path)
    parser.add_argument("work_dir", type=Path)
    parser.add_argument("--sample", help="Run only this generated graph")
    args = parser.parse_args()
    args.work_dir.mkdir(parents=True, exist_ok=True)
    inspect = args.inspect.resolve()

    if args.suite == "workflow":
        if args.sample:
            parser.error("--sample is only available with the graphs suite")
        return 0 if run_case(args.work_dir, "workflow", check_workflow, inspect) else 1

    samples = load_samples()
    if not samples:
        parser.error("No project samples available")
    if args.sample:
        if args.sample not in samples:
            parser.error(f"Unknown sample {args.sample!r}")
        samples = {args.sample: samples[args.sample]}
    results = [
        run_case(args.work_dir, name, check_graph, inspect, sample)
        for name, sample in samples.items()
    ]
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
