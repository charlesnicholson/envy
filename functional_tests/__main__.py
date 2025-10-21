from __future__ import annotations

import pathlib
import sys
import unittest


def _build_default_argv() -> list[str]:
  root = pathlib.Path(__file__).resolve().parent
  return [
      "functional_tests",
      "discover",
      "-s",
      str(root),
      "-t",
      str(root.parent),
  ]


def main() -> None:
  argv = sys.argv if len(sys.argv) > 1 else _build_default_argv()
  unittest.main(module=None, argv=argv)


if __name__ == "__main__":
  main()
