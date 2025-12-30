#!/usr/bin/env python3
"""Helper utilities for functional tests to prevent writes to project root.

Provides safe path resolution and recipe wrapping to ensure all test operations
happen in temporary directories, never in the envy project root.
"""

from pathlib import Path
from typing import Optional


class TestPathHelper:
    """Helper for managing test paths safely.

    Ensures all test operations happen in temp directories, preventing
    accidental writes to the project root.
    """

    def __init__(self, test_dir: Path, project_root: Path):
        """Initialize path helper.

        Args:
            test_dir: Temporary directory for test operations
            project_root: Project root (read-only reference)
        """
        self.test_dir = test_dir
        self.project_root = project_root
        self.test_data = project_root / "test_data"

    @staticmethod
    def lua_path(path: Path) -> str:
        """Convert path to Lua-safe string with forward slashes."""
        return path.as_posix()

    def get_test_data_recipe(self, recipe_name: str) -> Path:
        """Get absolute path to recipe in test_data.

        Args:
            recipe_name: Name of recipe file (e.g., "simple.lua")

        Returns:
            Absolute path to recipe
        """
        return self.test_data / "specs" / recipe_name

    def create_wrapper_recipe(
        self, recipe_name: str, wrapper_name: Optional[str] = None
    ) -> Path:
        """Create wrapper recipe in test_dir that loads actual recipe.

        This allows tests to run with cwd=test_dir (temp directory) while
        still accessing recipes from test_data. User-managed recipes will
        write to test_dir, not project root.

        Args:
            recipe_name: Name of recipe in test_data/specs/
            wrapper_name: Optional custom name for wrapper (defaults to recipe_name)

        Returns:
            Path to wrapper recipe in test_dir
        """
        wrapper_name = wrapper_name or recipe_name
        wrapper_path = self.test_dir / wrapper_name
        source_recipe = self.get_test_data_recipe(recipe_name)

        # Create wrapper that dofile's the actual recipe with absolute path
        wrapper_content = f"""-- Wrapper recipe (auto-generated for testing)
-- Loads actual recipe from: {source_recipe}
dofile("{self.lua_path(source_recipe)}")
"""
        wrapper_path.write_text(wrapper_content, encoding="utf-8")
        return wrapper_path

    def get_working_directory(self) -> Path:
        """Get safe working directory for test execution.

        Returns test_dir, ensuring all file operations happen in temp space.
        """
        return self.test_dir


def get_test_path_helper(test_dir: Path, project_root: Path) -> TestPathHelper:
    """Factory function to create TestPathHelper.

    Args:
        test_dir: Temporary directory for test operations
        project_root: Project root directory

    Returns:
        Configured TestPathHelper instance
    """
    return TestPathHelper(test_dir, project_root)
