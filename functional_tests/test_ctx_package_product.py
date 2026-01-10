#!/usr/bin/env python3
"""Functional coverage for envy.package() and envy.product() enforcement."""

import hashlib
import io
import shutil
import subprocess
import tarfile
import tempfile
import unittest
from pathlib import Path

from . import test_config
from .test_config import make_manifest

# Test archive contents
TEST_ARCHIVE_FILES = {
    "root/file1.txt": "Root file content\n",
    "root/file2.txt": "Another root file\n",
    "root/subdir1/file3.txt": "Subdir file content\n",
    "root/subdir1/subdir2/file4.txt": "Deep nested file\n",
    "root/subdir1/subdir2/file5.txt": "Another deep file\n",
}


def create_test_archive(output_path: Path) -> str:
    """Create test.tar.gz archive and return its SHA256 hash."""
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz") as tar:
        for name, content in TEST_ARCHIVE_FILES.items():
            data = content.encode("utf-8")
            info = tarfile.TarInfo(name=name)
            info.size = len(data)
            tar.addfile(info, io.BytesIO(data))
    archive_data = buf.getvalue()
    output_path.write_bytes(archive_data)
    return hashlib.sha256(archive_data).hexdigest()


# Inline specs - {ARCHIVE_PATH}, {ARCHIVE_HASH}, {SPECS_DIR} replaced at runtime
SPECS = {
    # ctx.package tests
    "ctx_package_provider.lua": """IDENTITY = "local.ctx_package_provider@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}",
}}

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options)
end
""",
    "ctx_package_consumer_ok.lua": """IDENTITY = "local.ctx_package_consumer_ok@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}",
}}

DEPENDENCIES = {{
  {{ spec = "local.ctx_package_provider@v1", source = "{SPECS_DIR}/ctx_package_provider.lua", needed_by = "stage" }},
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  local path = envy.package("local.ctx_package_provider@v1")
  assert(path:match("ctx_package_provider"), "package path should include provider identity")
end

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options)
end
""",
    "ctx_package_needed_by_violation.lua": """IDENTITY = "local.ctx_package_needed_by_violation@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}",
}}

DEPENDENCIES = {{
  {{ spec = "local.ctx_package_provider@v1", source = "{SPECS_DIR}/ctx_package_provider.lua", needed_by = "install" }},
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  -- needed_by is install, so this should fail when enforced
  envy.package("local.ctx_package_provider@v1")
end

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options)
end
""",
    "ctx_package_user_provider.lua": """IDENTITY = "local.ctx_package_user_provider@v1"

function CHECK(project_root, options)
  return true
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- User-managed: ephemeral workspace, no persistent cache artifacts
end
""",
    "ctx_package_user_consumer.lua": """IDENTITY = "local.ctx_package_user_consumer@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}",
}}

DEPENDENCIES = {{
  {{ spec = "local.ctx_package_user_provider@v1", source = "{SPECS_DIR}/ctx_package_user_provider.lua", needed_by = "stage" }},
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.package("local.ctx_package_user_provider@v1")
end

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options)
end
""",
    "ctx_package_missing_dep.lua": """IDENTITY = "local.ctx_package_missing_dep@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}",
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.package("local.nonexistent_dep@v1")
end

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options)
end
""",
    # ctx.product tests
    "product_provider.lua": """-- Product provider with cached package
IDENTITY = "local.product_provider@v1"
PRODUCTS = {{ tool = "bin/tool" }}

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}",
}}

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- No real payload needed; just mark complete to populate pkg_path
end
""",
    "ctx_product_consumer_ok.lua": """IDENTITY = "local.ctx_product_consumer_ok@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}",
}}

DEPENDENCIES = {{
  {{
    spec = "local.product_provider@v1",
    source = "{SPECS_DIR}/product_provider.lua",
    product = "tool",
    needed_by = "stage",
  }},
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  local val = envy.product("tool")
  assert(val:match("bin/tool"), "expected product path")
end

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options)
end
""",
    "ctx_product_needed_by_violation.lua": """IDENTITY = "local.ctx_product_needed_by_violation@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}",
}}

DEPENDENCIES = {{
  {{
    spec = "local.product_provider@v1",
    source = "{SPECS_DIR}/product_provider.lua",
    product = "tool",
    needed_by = "install",
  }},
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.product("tool")
end

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options)
end
""",
    "ctx_product_missing_dep.lua": """IDENTITY = "local.ctx_product_missing_dep@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}",
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.product("tool")
end

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options)
end
""",
}


class TestCtxPackageProduct(unittest.TestCase):
    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-ctx-package-product-cache-"))
        self.test_dir = Path(tempfile.mkdtemp(prefix="envy-ctx-package-product-manifest-"))
        self.specs_dir = Path(tempfile.mkdtemp(prefix="envy-ctx-package-product-specs-"))
        self.envy = test_config.get_envy_executable()
        self.project_root = Path(__file__).parent.parent

        # Create test archive and get its hash
        self.archive_path = self.specs_dir / "test.tar.gz"
        self.archive_hash = create_test_archive(self.archive_path)

        # Write inline specs to temp directory with placeholders substituted
        for name, content in SPECS.items():
            spec_content = content.format(
                ARCHIVE_PATH=self.archive_path.as_posix(),
                ARCHIVE_HASH=self.archive_hash,
                SPECS_DIR=self.specs_dir.as_posix(),
            )
            (self.specs_dir / name).write_text(spec_content, encoding="utf-8")

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)
        shutil.rmtree(self.test_dir, ignore_errors=True)
        shutil.rmtree(self.specs_dir, ignore_errors=True)

    def manifest(self, content: str) -> Path:
        manifest_path = self.test_dir / "envy.lua"
        manifest_path.write_text(make_manifest(content), encoding="utf-8")
        return manifest_path

    def run_envy(self, args):
        cmd = [str(self.envy), "--cache-root", str(self.cache_root), *args]
        return subprocess.run(
            cmd, cwd=self.project_root, capture_output=True, text=True
        )

    def lua_path(self, name: str) -> str:
        return (self.specs_dir / name).as_posix()

    # ===== ctx.package =====

    def test_ctx_package_success(self):
        manifest = self.manifest(
            f"""
PACKAGES = {{
  {{
    spec = "local.ctx_package_provider@v1",
    source = "{self.lua_path("ctx_package_provider.lua")}",
  }},
  {{
    spec = "local.ctx_package_consumer_ok@v1",
    source = "{self.lua_path("ctx_package_consumer_ok.lua")}",
  }},
}}
"""
        )

        result = self.run_envy(["sync", "--install-all", "--manifest", str(manifest)])
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_ctx_package_needed_by_violation(self):
        manifest = self.manifest(
            f"""
PACKAGES = {{
  {{
    spec = "local.ctx_package_provider@v1",
    source = "{self.lua_path("ctx_package_provider.lua")}",
  }},
  {{
    spec = "local.ctx_package_needed_by_violation@v1",
    source = "{self.lua_path("ctx_package_needed_by_violation.lua")}",
  }},
}}
"""
        )

        result = self.run_envy(["sync", "--install-all", "--manifest", str(manifest)])
        self.assertNotEqual(result.returncode, 0, "expected needed_by violation to fail")
        self.assertIn("needed_by 'install' but accessed during 'stage'", result.stderr)

    def test_ctx_package_user_managed_fails(self):
        manifest = self.manifest(
            f"""
PACKAGES = {{
  {{
    spec = "local.ctx_package_user_provider@v1",
    source = "{self.lua_path("ctx_package_user_provider.lua")}",
  }},
  {{
    spec = "local.ctx_package_user_consumer@v1",
    source = "{self.lua_path("ctx_package_user_consumer.lua")}",
  }},
}}
"""
        )

        result = self.run_envy(["sync", "--install-all", "--manifest", str(manifest)])
        self.assertNotEqual(result.returncode, 0, "expected user-managed package access to fail")
        self.assertIn("is user-managed and has no pkg path", result.stderr)

    def test_ctx_package_missing_dependency(self):
        manifest = self.manifest(
            f"""
PACKAGES = {{
  {{
    spec = "local.ctx_package_missing_dep@v1",
    source = "{self.lua_path("ctx_package_missing_dep.lua")}",
  }},
}}
"""
        )

        result = self.run_envy(["sync", "--install-all", "--manifest", str(manifest)])
        self.assertNotEqual(result.returncode, 0, "expected missing dependency to fail")
        self.assertIn("has no strong dependency", result.stderr)

    # ===== ctx.product =====

    def test_ctx_product_success(self):
        manifest = self.manifest(
            f"""
PACKAGES = {{
  {{
    spec = "local.product_provider@v1",
    source = "{self.lua_path("product_provider.lua")}",
  }},
  {{
    spec = "local.ctx_product_consumer_ok@v1",
    source = "{self.lua_path("ctx_product_consumer_ok.lua")}",
  }},
}}
"""
        )

        result = self.run_envy(["sync", "--install-all", "--manifest", str(manifest)])
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_ctx_product_needed_by_violation(self):
        manifest = self.manifest(
            f"""
PACKAGES = {{
  {{
    spec = "local.product_provider@v1",
    source = "{self.lua_path("product_provider.lua")}",
  }},
  {{
    spec = "local.ctx_product_needed_by_violation@v1",
    source = "{self.lua_path("ctx_product_needed_by_violation.lua")}",
  }},
}}
"""
        )

        result = self.run_envy(["sync", "--install-all", "--manifest", str(manifest)])
        self.assertNotEqual(result.returncode, 0, "expected product needed_by violation to fail")
        self.assertIn("needed_by 'install' but accessed during 'stage'", result.stderr)

    def test_ctx_product_missing_dependency(self):
        manifest = self.manifest(
            f"""
PACKAGES = {{
  {{
    spec = "local.ctx_product_missing_dep@v1",
    source = "{self.lua_path("ctx_product_missing_dep.lua")}",
  }},
}}
"""
        )

        result = self.run_envy(["sync", "--install-all", "--manifest", str(manifest)])
        self.assertNotEqual(result.returncode, 0, "expected missing product dependency to fail")
        self.assertIn("does not declare product dependency", result.stderr)


if __name__ == "__main__":
    unittest.main()
