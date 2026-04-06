"""
format_watch.py — Watchdog daemon that auto-formats .c/.h files on save.

Debounce: waits 500 ms after the last event before triggering formatter,
to avoid multiple format calls when editors write files in several steps.
"""

from __future__ import annotations

import argparse
import threading
from pathlib import Path
from typing import TYPE_CHECKING

from watchdog.events import FileSystemEventHandler
from watchdog.observers import Observer

from formatter import format_file, _parse_extensions, _print_result

if TYPE_CHECKING:
    from watchdog.events import FileSystemEvent

# ---------------------------------------------------------------------------
# Event handler with debounce
# ---------------------------------------------------------------------------

DEBOUNCE_DELAY = 0.5  # seconds


class _DebounceHandler(FileSystemEventHandler):
    def __init__(self, extensions: set[str], verbose: bool) -> None:
        super().__init__()
        self._extensions = extensions
        self._verbose = verbose
        self._timers: dict[str, threading.Timer] = {}
        self._lock = threading.Lock()

    def _should_handle(self, path: str) -> bool:
        return Path(path).suffix.lstrip(".") in self._extensions

    def _schedule(self, path: str) -> None:
        with self._lock:
            existing = self._timers.get(path)
            if existing:
                existing.cancel()
            timer = threading.Timer(DEBOUNCE_DELAY, self._run, args=(path,))
            self._timers[path] = timer
            timer.start()

    def _run(self, path: str) -> None:
        with self._lock:
            self._timers.pop(path, None)
        p = Path(path)
        if not p.exists():
            return
        result = format_file(p)
        _print_result(result, self._verbose)

    # watchdog callbacks
    def on_modified(self, event: FileSystemEvent) -> None:
        if not event.is_directory and self._should_handle(event.src_path):
            self._schedule(event.src_path)

    def on_created(self, event: FileSystemEvent) -> None:
        if not event.is_directory and self._should_handle(event.src_path):
            self._schedule(event.src_path)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        prog="format_watch.py",
        description="Watch directories and auto-format .c/.h files on save.",
    )
    parser.add_argument(
        "dirs",
        nargs="*",
        metavar="DIR",
        default=["."],
        help="Directories to watch (default: current directory)",
    )
    parser.add_argument(
        "--ext",
        default="c,h",
        help="Comma-separated extensions to watch (default: c,h)",
    )
    parser.add_argument(
        "-v", "--verbose",
        action="store_true",
        help="Print OK results as well as failures",
    )
    args = parser.parse_args()

    extensions = set(_parse_extensions(args.ext))
    handler = _DebounceHandler(extensions=extensions, verbose=args.verbose)

    observer = Observer()
    for d in args.dirs:
        observer.schedule(handler, str(d), recursive=True)
        print(f"Watching {d} for {', '.join(f'.{e}' for e in extensions)} files…")

    observer.start()
    print("Press Ctrl+C to stop.")
    try:
        observer.join()
    except KeyboardInterrupt:
        observer.stop()
        observer.join()
        print("\nStopped.")


if __name__ == "__main__":
    main()
