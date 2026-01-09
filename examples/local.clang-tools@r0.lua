IDENTITY = "local.clang-tools@r0"

DEPENDENCIES = {
  { product = "ninja" },
  { product = "cmake" }
}

-- 21.1.0
FETCH = function(tmp_dir, opts)
  return "https://github.com/llvm/llvm-project/releases/download/llvmorg-" ..
  opts.version .. "/llvm-project-" .. opts.version .. ".src.tar.xz"
end

STAGE = { strip = 1 }

BUILD = function(stage_dir, fetch_dir, tmp_dir, opts)
  local cmd = envy.template([[
  mkdir build
  cd build
  {{cmake}} -G Ninja ../llvm -DCMAKE_MAKE_PROGRAM={{ninja}} -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra"
  {{ninja}} clang-format clang-tidy
]], { cmake = envy.product("cmake"), ninja = envy.product("ninja") })

  envy.run(cmd)
end

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, opts)
  envy.move(stage_dir .. "build/bin/clang-format" .. envy.EXE_EXT, install_dir)
  envy.move(stage_dir .. "build/bin/clang-tidy" .. envy.EXE_EXT, install_dir)
end

PRODUCTS = {
  ["clang-format"] = "clang-format" .. envy.EXE_EXT,
  ["clang-tidy"] = "clang-tidy" .. envy.EXE_EXT
}
