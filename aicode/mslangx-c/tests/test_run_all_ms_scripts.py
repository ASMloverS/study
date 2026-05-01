import contextlib
import importlib.util
import io
import pathlib
import tempfile
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT_PATH = REPO_ROOT / "tests" / "run_all_ms_scripts.py"


def load_module():
  spec = importlib.util.spec_from_file_location("run_all_ms_scripts", SCRIPT_PATH)
  module = importlib.util.module_from_spec(spec)
  assert spec.loader is not None
  spec.loader.exec_module(module)
  return module


class RunAllMsScriptsTest(unittest.TestCase):

  def test_find_ms_files_ignores_sidecar_artifacts(self):
    module = load_module()

    with tempfile.TemporaryDirectory() as temp_dir:
      root = pathlib.Path(temp_dir)
      (root / "alpha.ms").write_text("print 1\n", encoding="utf-8", newline="\n")
      (root / "alpha.ms.out").write_text("1\n", encoding="utf-8", newline="\n")
      (root / "alpha.ms.ast").write_text("", encoding="utf-8", newline="\n")
      (root / "beta.ms.diag").write_text("", encoding="utf-8", newline="\n")
      nested = root / "nested"
      nested.mkdir()
      (nested / "gamma.ms").write_text("print 2\n", encoding="utf-8", newline="\n")
      (nested / "gamma.ms.out").write_text("2\n", encoding="utf-8", newline="\n")
      (root / "note.txt").write_text("", encoding="utf-8", newline="\n")

      actual = module.find_ms_files(root)

      self.assertEqual(
        [path.as_posix() for path in actual],
        ["alpha.ms", "nested/gamma.ms"],
      )

  def test_find_ms_files_skips_fixture_directories(self):
    module = load_module()

    with tempfile.TemporaryDirectory() as temp_dir:
      root = pathlib.Path(temp_dir)
      (root / "runner.ms").write_text("print 1\n", encoding="utf-8", newline="\n")
      (root / "runner.ms.out").write_text("1\n", encoding="utf-8", newline="\n")
      fixture_dir = root / "parser_expr"
      fixture_dir.mkdir()
      (fixture_dir / "function_and_super.ms").write_text(
        "fn(left, right) { return {self: self, base: super.run} }\n",
        encoding="utf-8",
        newline="\n",
      )
      (fixture_dir / "function_and_super.ms.out").write_text(
        "",
        encoding="utf-8",
        newline="\n",
      )
      (fixture_dir / "function_and_super.ms.ast").write_text(
        "fixture\n",
        encoding="utf-8",
        newline="\n",
      )

      fixtures_root = root / "fixtures" / "modules" / "basic"
      fixtures_root.mkdir(parents=True)
      (fixtures_root / "exports.ms").write_text(
        "var value = 1\n",
        encoding="utf-8",
        newline="\n",
      )
      (fixtures_root / "exports.ms.out").write_text(
        "value\n",
        encoding="utf-8",
        newline="\n",
      )
      build_dir = root / "build"
      build_dir.mkdir()
      (build_dir / "generated.ms").write_text("print 3\n", encoding="utf-8", newline="\n")
      (build_dir / "generated.ms.out").write_text("3\n", encoding="utf-8", newline="\n")

      actual = module.find_ms_files(root)

    self.assertEqual([path.as_posix() for path in actual], ["runner.ms"])

  def test_find_ms_files_uses_cmake_allowlist_when_present(self):
    module = load_module()

    with tempfile.TemporaryDirectory() as temp_dir:
      root = pathlib.Path(temp_dir)
      tests_dir = root / "tests"
      tests_dir.mkdir()
      (tests_dir / "CMakeLists.txt").write_text(
        "mslangc_add_e2e_basic_test(e2e_basic.smoke smoke.ms)\n"
        "mslangc_add_e2e_named_test(functions.recursion\n"
        "  \"e2e/functions/recursion.ms\"\n"
        "  \"e2e/functions/recursion.ms.out\"\n"
        "  \"functions\")\n"
        "mslangc_add_cli_test(containers.truthiness\n"
        "  -DMSLANGC_EXE=\"runner\"\n"
        "  -DSCRIPT=\"${CMAKE_CURRENT_SOURCE_DIR}/e2e/containers/truthiness.ms\"\n"
        "  -DEXPECT_EXIT=0\n"
        "  -P \"check.cmake\")\n"
        "mslangc_add_cli_test(modules.missing_export\n"
        "  -DMSLANGC_EXE=\"runner\"\n"
        "  -DSCRIPT=\"${CMAKE_CURRENT_SOURCE_DIR}/e2e/modules/missing_export.ms\"\n"
        "  -DEXPECT_EXIT=70\n"
        "  -P \"check.cmake\")\n",
        encoding="utf-8",
        newline="\n",
      )
      (tests_dir / "e2e").mkdir()
      (tests_dir / "e2e" / "basic").mkdir(parents=True)
      (tests_dir / "e2e" / "functions").mkdir(parents=True)
      (tests_dir / "e2e" / "containers").mkdir(parents=True)
      (tests_dir / "e2e" / "modules").mkdir(parents=True)
      (tests_dir / "e2e" / "basic" / "smoke.ms").write_text(
        "print 1\n",
        encoding="utf-8",
        newline="\n",
      )
      (tests_dir / "e2e" / "basic" / "smoke.ms.out").write_text(
        "1\n",
        encoding="utf-8",
        newline="\n",
      )
      (tests_dir / "e2e" / "functions" / "recursion.ms").write_text(
        "fn recurse() { return 1 }\n",
        encoding="utf-8",
        newline="\n",
      )
      (tests_dir / "e2e" / "functions" / "recursion.ms.out").write_text(
        "1\n",
        encoding="utf-8",
        newline="\n",
      )
      (tests_dir / "e2e" / "containers" / "truthiness.ms").write_text(
        "print 2\n",
        encoding="utf-8",
        newline="\n",
      )
      (tests_dir / "e2e" / "containers" / "truthiness.ms.out").write_text(
        "2\n",
        encoding="utf-8",
        newline="\n",
      )
      (tests_dir / "e2e" / "modules" / "missing_export.ms").write_text(
        "import missing\n",
        encoding="utf-8",
        newline="\n",
      )
      (tests_dir / "e2e" / "modules" / "missing_export.ms.out").write_text(
        "ready\n",
        encoding="utf-8",
        newline="\n",
      )

      actual = module.find_ms_files(root)

    self.assertEqual(
      [path.as_posix() for path in actual],
      [
        "tests/e2e/basic/smoke.ms",
        "tests/e2e/containers/truthiness.ms",
        "tests/e2e/functions/recursion.ms",
      ],
    )

  def test_main_runs_all_scripts_and_reports_failures(self):
    module = load_module()

    with tempfile.TemporaryDirectory() as temp_dir:
      root = pathlib.Path(temp_dir)
      runner = root / "fake_runner.cmd"
      runner.write_text(
        "@echo off\n"
        "if /I \"%~nx1\"==\"fail.ms\" (\n"
        "  echo failed %~nx1 1>&2\n"
        "  exit /b 7\n"
        ")\n"
        "echo ran %~nx1\n"
        "exit /b 0\n",
        encoding="utf-8",
        newline="\n",
      )
      (root / "ok.ms").write_text("print 1\n", encoding="utf-8", newline="\n")
      (root / "ok.ms.out").write_text("ok\n", encoding="utf-8", newline="\n")
      (root / "fail.ms").write_text("print 2\n", encoding="utf-8", newline="\n")
      (root / "fail.ms.out").write_text("fail\n", encoding="utf-8", newline="\n")

      stdout = io.StringIO()
      stderr = io.StringIO()
      with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
        exit_code = module.main(["--runner", str(runner), "--root", str(root)])

    self.assertEqual(exit_code, 1)
    self.assertIn("PASS ok.ms", stdout.getvalue())
    self.assertIn("FAIL fail.ms", stdout.getvalue())
    self.assertIn("failed fail.ms", stderr.getvalue())
    self.assertIn("Summary: 1 passed, 1 failed, 2 total", stdout.getvalue())

  def test_main_skips_scripts_without_expected_output(self):
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
      (root / "positive.ms").write_text("print 1\n", encoding="utf-8", newline="\n")
      (root / "positive.ms.out").write_text("1\n", encoding="utf-8", newline="\n")
      (root / "negative.ms").write_text("break\n", encoding="utf-8", newline="\n")

      stdout = io.StringIO()
      stderr = io.StringIO()
      with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
        exit_code = module.main(["--runner", str(runner), "--root", str(root)])

    self.assertEqual(exit_code, 0)
    self.assertIn("PASS positive.ms", stdout.getvalue())
    self.assertNotIn("negative.ms", stdout.getvalue())
    self.assertIn("Summary: 1 passed, 0 failed, 1 total", stdout.getvalue())

  def test_main_routes_container_scripts_to_special_runner(self):
    module = load_module()

    with tempfile.TemporaryDirectory() as temp_dir:
      root = pathlib.Path(temp_dir)
      default_runner = root / "default_runner.cmd"
      default_runner.write_text(
        "@echo off\n"
        "echo %~1 | findstr /I \"\\\\e2e\\\\containers\\\\\" >nul\n"
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
        "echo %~1 | findstr /I \"\\\\e2e\\\\containers\\\\\" >nul\n"
        "if %errorlevel% neq 0 (\n"
        "  echo wrong script %~nx1 1>&2\n"
        "  exit /b 8\n"
        ")\n"
        "echo ran container %~nx1\n"
        "exit /b 0\n",
        encoding="utf-8",
        newline="\n",
      )
      (root / "general.ms").write_text("print 1\n", encoding="utf-8", newline="\n")
      (root / "general.ms.out").write_text("1\n", encoding="utf-8", newline="\n")
      container_dir = root / "e2e" / "containers"
      container_dir.mkdir(parents=True)
      (container_dir / "special.ms").write_text(
        "print 2\n",
        encoding="utf-8",
        newline="\n",
      )
      (container_dir / "special.ms.out").write_text("2\n", encoding="utf-8", newline="\n")

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
    self.assertIn("PASS general.ms", stdout.getvalue())
    self.assertIn("PASS e2e/containers/special.ms", stdout.getvalue())
    self.assertIn("ran container special.ms", stdout.getvalue())
    self.assertNotIn("wrong runner", stderr.getvalue())
    self.assertIn("Summary: 2 passed, 0 failed, 2 total", stdout.getvalue())


if __name__ == "__main__":
  unittest.main()
