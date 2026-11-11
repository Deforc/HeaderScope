import argparse
from collections import Counter
import json
from pathlib import Path
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


def run_inspect(inspect, database, project, *options, status=0):
    command = [str(inspect), str(database), "--root", str(project)]
    result = subprocess.run(
        [*command, *map(str, options)], capture_output=True, encoding="utf-8",
        errors="replace", timeout=15,
    )
    if result.returncode != status:
        raise RuntimeError(
            f"CLI exited with {result.returncode}, expected {status}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


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
    demo = Path(__file__).resolve().parents[2] / "include-chain"
    for filename in ("main.cpp", "a.h", "b.h", "extra.h"):
        shutil.copyfile(demo / filename, project / filename)
    database = work / "compile_commands.json"
    snapshot = work / "baseline.json"

    write_database(database, project, [{"sources": ["main.cpp"], "defines": ["HEADERSCOPE_DEMO_EXTRA"]}])
    baseline = run_inspect(inspect, database, project, project / "b.h", "--save", snapshot)
    expect_output(
        baseline.stdout, "Complete scan: 4 files, 3 include directives", "a.h:6 -> extra.h",
        "Cycle groups: 0", "Dependent chains for b.h:", "main.cpp -> a.h -> b.h",
    )
    baseline_bytes = snapshot.read_bytes()

    write_database(database, project, [{"sources": ["main.cpp"], "defines": ["HEADERSCOPE_DEMO_CYCLE"]}])
    comparison = run_inspect(inspect, database, project, "--compare", snapshot, "--rank")
    expect_output(
        comparison.stdout, "Added includes: 1\n  b.h:4 -> a.h",
        "Removed includes: 1\n  a.h:6 -> extra.h",
        "New or expanded cycle groups: 1", "a.h -> b.h -> a.h",
        "Affected source files: 1\n  main.cpp", "1. a.h: sources: 1, direct includers: 2",
    )

    with (project / "main.cpp").open("a", encoding="utf-8") as source:
        source.write('\n#include "missing.h"\n')
    partial = run_inspect(inspect, database, project, "--save", snapshot, status=1)
    expect_output(partial.stdout, "Partial scan:", "main.cpp:1 -> a.h")
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
