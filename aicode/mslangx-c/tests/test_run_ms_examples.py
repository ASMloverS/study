import contextlib
import importlib.util
import io
import pathlib
import tempfile
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT_PATH = REPO_ROOT / "scripts" / "run_ms_examples.py"


def load_module():
  spec = importlib.util.spec_from_file_location("run_ms_examples", SCRIPT_PATH)
  module = importlib.util.module_from_spec(spec)
  assert spec.loader is not None
  spec.loader.exec_module(module)
  return module


class RunMsExamplesTest(unittest.TestCase):

  def test_find_ms_files_returns_only_examples_with_outputs(self):
    module = load_module()

    with tempfile.TemporaryDirectory() as temp_dir:
      root = pathlib.Path(temp_dir)
      examples_dir = root / "examples"
      basics_dir = examples_dir / "basics"
      containers_dir = examples_dir / "containers"
      basics_dir.mkdir(parents=True)
      containers_dir.mkdir(parents=True)

      (basics_dir / "hello.ms").write_text("print 1\n", encoding="utf-8", newline="\n")
      (basics_dir / "hello.ms.out").write_text("1\n", encoding="utf-8", newline="\n")
      (containers_dir / "numbers.ms").write_text("print [1, 2]\n", encoding="utf-8", newline="\n")
      (containers_dir / "numbers.ms.out").write_text("[1, 2]\n", encoding="utf-8", newline="\n")
      (examples_dir / "draft.ms").write_text("print 3\n", encoding="utf-8", newline="\n")

      actual = module.find_ms_files(root)

    self.assertEqual(
      [path.as_posix() for path in actual],
      [
        "examples/basics/hello.ms",
        "examples/containers/numbers.ms",
      ],
    )

  def test_main_runs_all_example_scripts_and_reports_summary(self):
    module = load_module()

    with tempfile.TemporaryDirectory() as temp_dir:
      root = pathlib.Path(temp_dir)
      runner = root / "fake_runner.cmd"
      runner.write_text(
        "@echo off\n"
        "echo ran %~nx1\n"
        "exit /b 0\n",
        encoding="utf-8",
        newline="\n",
      )

      examples_dir = root / "examples"
      nested_dir = examples_dir / "nested"
      nested_dir.mkdir(parents=True)
      (examples_dir / "hello.ms").write_text("print 1\n", encoding="utf-8", newline="\n")
      (examples_dir / "hello.ms.out").write_text("ran hello.ms\n", encoding="utf-8", newline="\n")
      (nested_dir / "world.ms").write_text("print 2\n", encoding="utf-8", newline="\n")
      (nested_dir / "world.ms.out").write_text("ran world.ms\n", encoding="utf-8", newline="\n")

      stdout = io.StringIO()
      stderr = io.StringIO()
      with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
        exit_code = module.main(["--runner", str(runner), "--root", str(root)])

    self.assertEqual(exit_code, 0)
    self.assertIn("PASS examples/hello.ms", stdout.getvalue())
    self.assertIn("PASS examples/nested/world.ms", stdout.getvalue())
    self.assertIn("Summary: 2 passed, 0 failed, 2 total", stdout.getvalue())
    self.assertEqual("", stderr.getvalue())

  def test_main_routes_container_examples_to_special_runner(self):
    module = load_module()

    with tempfile.TemporaryDirectory() as temp_dir:
      root = pathlib.Path(temp_dir)
      default_runner = root / "default_runner.cmd"
      default_runner.write_text(
        "@echo off\n"
        "echo %~1 | findstr /I \"\\\\examples\\\\containers\\\\\" >nul\n"
        "if %errorlevel%==0 (\n"
        "  echo wrong runner %~nx1 1>&2\n"
        "  exit /b 9\n"
        ")\n"
        "echo ran %~nx1\n"
        "exit /b 0\n",
        encoding="utf-8",
        newline="\n",
      )
      containers_runner = root / "containers_runner.cmd"
      containers_runner.write_text(
        "@echo off\n"
        "echo %~1 | findstr /I \"\\\\examples\\\\containers\\\\\" >nul\n"
        "if %errorlevel% neq 0 (\n"
        "  echo wrong script %~nx1 1>&2\n"
        "  exit /b 8\n"
        ")\n"
        "echo ran container %~nx1\n"
        "exit /b 0\n",
        encoding="utf-8",
        newline="\n",
      )

      examples_dir = root / "examples"
      container_dir = examples_dir / "containers"
      basics_dir = examples_dir / "basics"
      container_dir.mkdir(parents=True)
      basics_dir.mkdir(parents=True)
      (basics_dir / "hello.ms").write_text("print 1\n", encoding="utf-8", newline="\n")
      (basics_dir / "hello.ms.out").write_text("ran hello.ms\n", encoding="utf-8", newline="\n")
      (container_dir / "collections.ms").write_text(
        "print [1, 2]\n",
        encoding="utf-8",
        newline="\n",
      )
      (container_dir / "collections.ms.out").write_text(
        "ran collections.ms\n",
        encoding="utf-8",
        newline="\n",
      )

      stdout = io.StringIO()
      stderr = io.StringIO()
      with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
        exit_code = module.main(
          [
            "--runner",
            str(default_runner),
            "--containers-runner",
            str(containers_runner),
            "--root",
            str(root),
          ]
        )

    self.assertEqual(exit_code, 0)
    self.assertIn("PASS examples/basics/hello.ms", stdout.getvalue())
    self.assertIn("PASS examples/containers/collections.ms", stdout.getvalue())
    self.assertIn("ran container collections.ms", stdout.getvalue())
    self.assertNotIn("wrong runner", stderr.getvalue())
    self.assertIn("Summary: 2 passed, 0 failed, 2 total", stdout.getvalue())


if __name__ == "__main__":
  unittest.main()
