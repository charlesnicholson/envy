IDENTITY = "local.clang-tools@r0"

if envy.PLATFORM == "darwin" then
  DEPENDENCIES = {
    { product = "ninja" },
    { product = "cmake" }
  }
end

VALIDATE = function(opts)
  if opts.version == nil then
    return "'version' is a required option"
  end
  if opts.tools == nil or #opts.tools == 0 then
    return "'tools' is a required option (array of tool names)"
  end
  for i, tool in ipairs(opts.tools) do
    if type(tool) ~= "string" or tool == "" then
      return string.format("tool name at index %d must be a non-empty string", i)
    end
    if not tool:match("^[%w_%-]+$") then
      return string.format(
          "invalid tool name '%s': only letters, digits, '_' and '-' are allowed", tool)
    end
  end
end

FETCH = function(tmp_dir, opts)
  local base = "https://github.com/llvm/llvm-project/releases/download/llvmorg-" ..
      opts.version .. "/"

  if envy.PLATFORM == "darwin" then
    return base .. "llvm-project-" .. opts.version .. ".src.tar.xz"
  elseif envy.PLATFORM == "windows" then
    local arch = (envy.ARCH == "x86_64") and "x86_64" or "aarch64"
    return base ..
        "clang+llvm-" .. opts.version .. "-" .. arch .. "-pc-windows-msvc.tar.xz"
  elseif envy.PLATFORM == "linux" then
    local arch = (envy.ARCH == "x86_64") and "X64" or "ARM64"
    return base .. "LLVM-" .. opts.version .. "-Linux-" .. arch .. ".tar.xz"
  else
    error("unsupported platform: " .. envy.PLATFORM)
  end
end

STAGE = { strip = 1 }

if envy.PLATFORM == "darwin" then
  BUILD = function(install_dir, stage_dir, fetch_dir, tmp_dir, opts)
    envy.run(envy.template([[
mkdir build
cd build
{{cmake}} -G Ninja ../llvm -DCMAKE_MAKE_PROGRAM={{ninja}} -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra"
{{ninja}} {{targets}}
]],
      {
        cmake = envy.product("cmake"),
        ninja = envy.product("ninja"),
        targets = table.concat(opts.tools, " ")
      }))
  end
end

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, opts)
  local src_dir = (envy.PLATFORM == "darwin")
      and envy.path.join(stage_dir, "build", "bin")
      or envy.path.join(stage_dir, "bin")
  for _, tool in ipairs(opts.tools) do
    envy.move(
      envy.path.join(src_dir, tool .. envy.EXE_EXT),
      envy.path.join(install_dir, tool .. envy.EXE_EXT)
    )
  end
end

PRODUCTS = function(opts)
  local result = {}
  for _, tool in ipairs(opts.tools) do
    result[tool] = tool .. envy.EXE_EXT
  end
  return result
end
