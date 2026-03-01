-- @envy schema "1"
IDENTITY = "local.armgcc@r0"
EXPORTABLE = true

local sha256  -- populated at end of file
local win_x86_64 = { ["14.2.rel1"] = true, ["14.3.rel1"] = true }
local darwin_arm64_only = { ["14.3.rel1"] = true }

local function platform_suffix(version)
  if envy.PLATFORM == "darwin" then return "darwin-" .. envy.ARCH
  elseif envy.PLATFORM == "linux" then return (envy.ARCH == "arm64") and "aarch64" or envy.ARCH
  else return win_x86_64[version] and "mingw-w64-x86_64" or "mingw-w64-i686" end
end

VALIDATE = function(opts)
  if opts.version == nil then return "'version' is a required option" end
  if envy.PLATFORM == "darwin" and envy.ARCH == "x86_64" and darwin_arm64_only[opts.version] then
    return "version " .. opts.version .. " not available for Intel Mac (darwin-x86_64)"
  end
  local key = opts.version .. "-" .. platform_suffix(opts.version)
  if not sha256[key] then return "unsupported version/platform: " .. key end
end

FETCH = function(tmp_dir, opts)
  local suffix = platform_suffix(opts.version)
  local ext = (envy.PLATFORM == "windows") and "zip" or "tar.xz"
  local filename = "arm-gnu-toolchain-" .. opts.version .. "-" .. suffix .. "-arm-none-eabi." .. ext
  return {
    source = "https://developer.arm.com/-/media/Files/downloads/gnu/" .. opts.version .. "/binrel/" .. filename,
    sha256 = sha256[opts.version .. "-" .. suffix]
  }
end

STAGE = { strip = 1 }

local bin = (envy.PLATFORM == "windows") and "" or "bin/"

PRODUCTS = {
  ["arm-none-eabi-addr2line"] = bin .. "arm-none-eabi-addr2line" .. envy.EXE_EXT,
  ["arm-none-eabi-ar"] = bin .. "arm-none-eabi-ar" .. envy.EXE_EXT,
  ["arm-none-eabi-as"] = bin .. "arm-none-eabi-as" .. envy.EXE_EXT,
  ["arm-none-eabi-c++"] = bin .. "arm-none-eabi-c++" .. envy.EXE_EXT,
  ["arm-none-eabi-c++filt"] = bin .. "arm-none-eabi-c++filt" .. envy.EXE_EXT,
  ["arm-none-eabi-cpp"] = bin .. "arm-none-eabi-cpp" .. envy.EXE_EXT,
  ["arm-none-eabi-elfedit"] = bin .. "arm-none-eabi-elfedit" .. envy.EXE_EXT,
  ["arm-none-eabi-g++"] = bin .. "arm-none-eabi-g++" .. envy.EXE_EXT,
  ["arm-none-eabi-gcc"] = bin .. "arm-none-eabi-gcc" .. envy.EXE_EXT,
  ["arm-none-eabi-gcc-ar"] = bin .. "arm-none-eabi-ar" .. envy.EXE_EXT,
  ["arm-none-eabi-gcc-nm"] = bin .. "arm-none-eabi-nm" .. envy.EXE_EXT,
  ["arm-none-eabi-gcc-ranlib"] = bin .. "arm-none-eabi-ranlib" .. envy.EXE_EXT,
  ["arm-none-eabi-gcov"] = bin .. "arm-none-eabi-gcov" .. envy.EXE_EXT,
  ["arm-none-eabi-gcov-dump"] = bin .. "arm-none-eabi-gcov-dump" .. envy.EXE_EXT,
  ["arm-none-eabi-gcov-tool"] = bin .. "arm-none-eabi-gcov-tool" .. envy.EXE_EXT,
  ["arm-none-eabi-gdb"] = bin .. "arm-none-eabi-gdb" .. envy.EXE_EXT,
  ["arm-none-eabi-gdb-add-index"] = bin .. "arm-none-eabi-gdb-add-index" .. envy.EXE_EXT,
  ["arm-none-eabi-gdb-add-index-py"] = bin .. "arm-none-eabi-gdb-add-index-py" .. envy.EXE_EXT,
  ["arm-none-eabi-gdb-py"] = bin .. "arm-none-eabi-gdb-py" .. envy.EXE_EXT,
  ["arm-none-eabi-gfortran"] = bin .. "arm-none-eabi-gfortran" .. envy.EXE_EXT,
  ["arm-none-eabi-gprof"] = bin .. "arm-none-eabi-gprof" .. envy.EXE_EXT,
  ["arm-none-eabi-ld"] = bin .. "arm-none-eabi-ld" .. envy.EXE_EXT,
  ["arm-none-eabi-ld.bfd"] = bin .. "arm-none-eabi-ld.bfd" .. envy.EXE_EXT,
  ["arm-none-eabi-lto-dump"] = bin .. "arm-none-eabi-lto-dump" .. envy.EXE_EXT,
  ["arm-none-eabi-nm"] = bin .. "arm-none-eabi-nm" .. envy.EXE_EXT,
  ["arm-none-eabi-objcopy"] = bin .. "arm-none-eabi-objcopy" .. envy.EXE_EXT,
  ["arm-none-eabi-objdump"] = bin .. "arm-none-eabi-objdump" .. envy.EXE_EXT,
  ["arm-none-eabi-ranlib"] = bin .. "arm-none-eabi-ranlib" .. envy.EXE_EXT,
  ["arm-none-eabi-readelf"] = bin .. "arm-none-eabi-readelf" .. envy.EXE_EXT,
  ["arm-none-eabi-size"] = bin .. "arm-none-eabi-size" .. envy.EXE_EXT,
  ["arm-none-eabi-strings"] = bin .. "arm-none-eabi-strings" .. envy.EXE_EXT,
  ["arm-none-eabi-strip"] = bin .. "arm-none-eabi-strip" .. envy.EXE_EXT,
}

sha256 = {
  -- 12.3.rel1
  ["12.3.rel1-darwin-arm64"] = "3b2eee0bdf71c1bbeb3c3b7424fbf7bd9d5c3f0f5a3a4a78159c9e3ad219e7bd",
  ["12.3.rel1-darwin-x86_64"] = "e6ed8bf930fad9ce33e120ab90b36957b1f779fccaa6de6c9ca9a58982c04291",
  ["12.3.rel1-x86_64"] = "12a2815644318ebcceaf84beabb665d0924b6e79e21048452c5331a56332b309",
  ["12.3.rel1-aarch64"] = "14c0487d5753f6071d24e568881f7c7e67f80dd83165dec5164b3731394af431",
  ["12.3.rel1-mingw-w64-i686"] = "d52888bf59c5262ebf3e6b19b9f9e6270ecb60fd218cf81a4e793946e805a654",
  -- 13.2.rel1
  ["13.2.rel1-darwin-arm64"] = "39c44f8af42695b7b871df42e346c09fee670ea8dfc11f17083e296ea2b0d279",
  ["13.2.rel1-darwin-x86_64"] = "075faa4f3e8eb45e59144858202351a28706f54a6ec17eedd88c9fb9412372cc",
  ["13.2.rel1-x86_64"] = "6cd1bbc1d9ae57312bcd169ae283153a9572bd6a8e4eeae2fedfbc33b115fdbb",
  ["13.2.rel1-aarch64"] = "8fd8b4a0a8d44ab2e195ccfbeef42223dfb3ede29d80f14dcf2183c34b8d199a",
  ["13.2.rel1-mingw-w64-i686"] = "51d933f00578aa28016c5e3c84f94403274ea7915539f8e56c13e2196437d18f",
  -- 13.3.rel1
  ["13.3.rel1-darwin-arm64"] = "fb6921db95d345dc7e5e487dd43b745e3a5b4d5c0c7ca4f707347148760317b4",
  ["13.3.rel1-darwin-x86_64"] = "1ab00742d1ed0926e6f227df39d767f8efab46f5250505c29cb81f548222d794",
  ["13.3.rel1-x86_64"] = "95c011cee430e64dd6087c75c800f04b9c49832cc1000127a92a97f9c8d83af4",
  ["13.3.rel1-aarch64"] = "c8824bffd057afce2259f7618254e840715f33523a3d4e4294f471208f976764",
  ["13.3.rel1-mingw-w64-i686"] = "e46fda043c0ce83582bc8db4b3ef85f77f4beb7333344c2f4193c17e1167a095",
  -- 14.2.rel1
  ["14.2.rel1-darwin-arm64"] = "c7c78ffab9bebfce91d99d3c24da6bf4b81c01e16cf551eb2ff9f25b9e0a3818",
  ["14.2.rel1-darwin-x86_64"] = "2d9e717dd4f7751d18936ae1365d25916534105ebcb7583039eff1092b824505",
  ["14.2.rel1-x86_64"] = "62a63b981fe391a9cbad7ef51b17e49aeaa3e7b0d029b36ca1e9c3b2a9b78823",
  ["14.2.rel1-aarch64"] = "87330bab085dd8749d4ed0ad633674b9dc48b237b61069e3b481abd364d0a684",
  ["14.2.rel1-mingw-w64-i686"] = "6facb152ce431ba9a4517e939ea46f057380f8f1e56b62e8712b3f3b87d994e1",
  ["14.2.rel1-mingw-w64-x86_64"] = "f074615953f76036e9a51b87f6577fdb4ed8e77d3322a6f68214e92e7859888f",
  -- 14.3.rel1 (no darwin-x86_64)
  ["14.3.rel1-darwin-arm64"] = "30f4d08b219190a37cded6aa796f4549504902c53cfc3c7e044a8490b6eba1f7",
  ["14.3.rel1-x86_64"] = "8f6903f8ceb084d9227b9ef991490413014d991874a1e34074443c2a72b14dbd",
  ["14.3.rel1-aarch64"] = "2d465847eb1d05f876270494f51034de9ace9abe87a4222d079f3360240184d3",
  ["14.3.rel1-mingw-w64-i686"] = "836ebe51fd71b6542dd7884c8fb2011192464b16c28e4b38fddc9350daba5ee8",
  ["14.3.rel1-mingw-w64-x86_64"] = "864c0c8815857d68a1bbba2e5e2782255bb922845c71c97636004a3d74f60986",
}
