IDENTITY = "local.armgcc@r0"

VALIDATE = function(opts)
  if opts.version == nil then
    return "'version' is a required option"
  end
end

FETCH = function(ctx, opts)
  local sha256_fingerprints = {
    ["14.3.rel1-darwin-arm64"] =
    "30f4d08b219190a37cded6aa796f4549504902c53cfc3c7e044a8490b6eba1f7",
    ["14.3.rel1-x86_64"] = "8f6903f8ceb084d9227b9ef991490413014d991874a1e34074443c2a72b14dbd",
    ["14.3.rel1-mingw-w64-x86_64"] = "864c0c8815857d68a1bbba2e5e2782255bb922845c71c97636004a3d74f60986",
  }

  local url_prefix = "https://developer.arm.com/-/media/Files/downloads/gnu/"
  local arch = "-" .. ({ darwin = "darwin-arm64", linux = ENVY_ARCH, windows = "mingw-w64-x86_64" })[ENVY_PLATFORM]
  local tail = "-arm-none-eabi." .. ((ENVY_PLATFORM == "windows") and "zip" or "tar.xz")

  local filename = "arm-gnu-toolchain-" .. opts.version .. arch .. tail

  local fingerprint = sha256_fingerprints[opts.version .. arch]
  assert(fingerprint)

  return {
    source = url_prefix .. opts.version .. "/binrel/" .. filename,
    sha256 = fingerprint
  }
end

STAGE = { strip = 1 }

local bin = (ENVY_PLATFORM == "windows") and "" or "bin/"

PRODUCTS = {
  ["arm-none-eabi-addr2line"] = bin .. "arm-none-eabi-addr2line" .. ENVY_EXE_EXT,
  ["arm-none-eabi-ar"] = bin .. "arm-none-eabi-ar" .. ENVY_EXE_EXT,
  ["arm-none-eabi-as"] = bin .. "arm-none-eabi-as" .. ENVY_EXE_EXT,
  ["arm-none-eabi-c++"] = bin .. "arm-none-eabi-c++" .. ENVY_EXE_EXT,
  ["arm-none-eabi-c++filt"] = bin .. "arm-none-eabi-c++filt" .. ENVY_EXE_EXT,
  ["arm-none-eabi-cpp"] = bin .. "arm-none-eabi-cpp" .. ENVY_EXE_EXT,
  ["arm-none-eabi-elfedit"] = bin .. "arm-none-eabi-elfedit" .. ENVY_EXE_EXT,
  ["arm-none-eabi-g++"] = bin .. "arm-none-eabi-g++" .. ENVY_EXE_EXT,
  ["arm-none-eabi-gcc"] = bin .. "arm-none-eabi-gcc" .. ENVY_EXE_EXT,
  ["arm-none-eabi-gcc-ar"] = bin .. "arm-none-eabi-ar" .. ENVY_EXE_EXT,
  ["arm-none-eabi-gcc-nm"] = bin .. "arm-none-eabi-nm" .. ENVY_EXE_EXT,
  ["arm-none-eabi-gcc-ranlib"] = bin .. "arm-none-eabi-ranlib" .. ENVY_EXE_EXT,
  ["arm-none-eabi-gcov"] = bin .. "arm-none-eabi-gcov" .. ENVY_EXE_EXT,
  ["arm-none-eabi-gcov-dump"] = bin .. "arm-none-eabi-gcov-dump" .. ENVY_EXE_EXT,
  ["arm-none-eabi-gcov-tool"] = bin .. "arm-none-eabi-gcov-tool" .. ENVY_EXE_EXT,
  ["arm-none-eabi-gdb"] = bin .. "arm-none-eabi-gdb" .. ENVY_EXE_EXT,
  ["arm-none-eabi-gdb-add-index"] = bin .. "arm-none-eabi-gdb-add-index" .. ENVY_EXE_EXT,
  ["arm-none-eabi-gdb-add-index-py"] = bin .. "arm-none-eabi-gdb-add-index-py" .. ENVY_EXE_EXT,
  ["arm-none-eabi-gdb-py"] = bin .. "arm-none-eabi-gdb-py" .. ENVY_EXE_EXT,
  ["arm-none-eabi-gfortran"] = bin .. "arm-none-eabi-gfortran" .. ENVY_EXE_EXT,
  ["arm-none-eabi-gprof"] = bin .. "arm-none-eabi-gprof" .. ENVY_EXE_EXT,
  ["arm-none-eabi-ld"] = bin .. "arm-none-eabi-ld" .. ENVY_EXE_EXT,
  ["arm-none-eabi-ld.bfd"] = bin .. "arm-none-eabi-ld.bfd" .. ENVY_EXE_EXT,
  ["arm-none-eabi-lto-dump"] = bin .. "arm-none-eabi-lto-dump" .. ENVY_EXE_EXT,
  ["arm-none-eabi-nm"] = bin .. "arm-none-eabi-nm" .. ENVY_EXE_EXT,
  ["arm-none-eabi-objcopy"] = bin .. "arm-none-eabi-objcopy" .. ENVY_EXE_EXT,
  ["arm-none-eabi-objdump"] = bin .. "arm-none-eabi-objdump" .. ENVY_EXE_EXT,
  ["arm-none-eabi-ranlib"] = bin .. "arm-none-eabi-ranlib" .. ENVY_EXE_EXT,
  ["arm-none-eabi-readelf"] = bin .. "arm-none-eabi-readelf" .. ENVY_EXE_EXT,
  ["arm-none-eabi-size"] = bin .. "arm-none-eabi-size" .. ENVY_EXE_EXT,
  ["arm-none-eabi-strings"] = bin .. "arm-none-eabi-strings" .. ENVY_EXE_EXT,
  ["arm-none-eabi-strip"] = bin .. "arm-none-eabi-strip" .. ENVY_EXE_EXT,
}
