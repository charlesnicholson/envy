IDENTITY = "local.cmake@r0"

VALIDATE = function(opts)
  if opts.version == nil then
    return "'version' is a required option"
  end
end

local sha256_fingerprints = {
  ["4.2.1-macos-universal"] =
  "0bb18f295e52d7e9309980e361e79e76a1d8da67a1587255cbe3696ea998f597",
  ["4.2.1-linux-x86_64"] =
  "c059bff1e97a2b6b5b0c0872263627486345ad0ed083298cb21cff2eda883980",
  ["4.2.1-linux-aarch64"] =
  "3e178207a2c42af4cd4883127f8800b6faf99f3f5187dccc68bfb2cc7808f5f7",
  ["4.2.1-windows-x86_64"] =
  "dfc2b2afac257555e3b9ce375b12b2883964283a366c17fec96cf4d17e4f1677",
  ["4.2.3-macos-universal"] =
  "c2302d3e9c48daabee5ea7c4db4b2b93b989bcc89dae8b760880e00120641b5b",
  ["4.2.3-linux-x86_64"] =
  "5bb505d5e0cca0480a330f7f27ccf52c2b8b5214c5bba97df08899f5ef650c23",
  ["4.2.3-linux-aarch64"] =
  "e529c75f18f27ba27c52b329efe7b1f98dc32ccc0c6d193c7ab343f888962672",
  ["4.2.3-windows-x86_64"] =
  "eb4ebf5155dbb05436d675706b2a08189430df58904257ae5e91bcba4c86933c",
}

FETCH = function(tmp_dir, opts)
  local platform_arch = ({
    darwin = "macos-universal",
    linux = "linux-" .. envy.ARCH,
    windows = "windows-x86_64"
  })[envy.PLATFORM]

  local ext = (envy.PLATFORM == "windows") and ".zip" or ".tar.gz"
  local filename = "cmake-" .. opts.version .. "-" .. platform_arch .. ext
  local fingerprint = sha256_fingerprints[opts.version .. "-" .. platform_arch]
  assert(fingerprint,
    "unsupported version/platform: " .. opts.version .. "-" .. platform_arch)

  return {
    source = "https://github.com/Kitware/CMake/releases/download/v" ..
        opts.version .. "/" .. filename,
    sha256 = fingerprint
  }
end

STAGE = { strip = 1 }

local bin = (envy.PLATFORM == "darwin") and "CMake.app/Contents/bin/" or "bin/"

PRODUCTS = { cmake = bin .. "cmake" .. envy.EXE_EXT }

