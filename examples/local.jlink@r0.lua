IDENTITY = "local.jlink@r0"

VALIDATE = function(opts)
  if opts.version == nil then
    return "version option is required"
  end
end

local function jlink_filename(opts)
  local arch = ({ arm64 = "arm64", aarch64 = "arm64", x86_64 = "x86_64" })[envy.ARCH]
  local name = ({
    darwin = "JLink_MacOSX_V" .. opts.version .. "_" .. arch .. ".pkg",
    linux = "JLink_Linux_V" .. opts.version .. "_" .. arch .. ".tgz",
  })[envy.PLATFORM]

  if not name then error("unsupported platform: " .. envy.PLATFORM) end
  return name
end

FETCH = function(tmp_dir, opts)
  return {
    source = "https://www.segger.com/downloads/jlink/" .. jlink_filename(opts),
    post_data = "accept_license_agreement=accepted",
  }
end

STAGE = function(fetch_dir, stage_dir, tmp_dir, opts)
  local src = fetch_dir .. jlink_filename(opts)

  if envy.PLATFORM == "darwin" then
    envy.run("pkgutil --expand-full " .. src .. " " .. stage_dir .. "pkg")
  else
    envy.extract(src, stage_dir, { strip = 1 })
  end
end

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, opts)
  if envy.PLATFORM == "darwin" then
    envy.move(stage_dir .. "pkg", install_dir)
  else
    envy.move(stage_dir, install_dir)
  end
end

PRODUCTS = function(opts)
  local bin, lib
  if envy.PLATFORM == "darwin" then
    bin = "JLink.pkg/Payload/Applications/SEGGER/JLink_V" .. opts.version .. "/"
    lib = bin .. "libjlinkarm.9.dylib"
  else
    bin = ""
    lib = "libjlinkarm.so.9"
  end

  return {
    JLinkExe = bin .. "JLinkExe",
    JLinkGDBServerCLExe = bin .. "JLinkGDBServerCLExe",
    JLinkGUIServerExe = bin .. "JLinkGUIServerExe",
    JLinkRemoteServerCLExe = bin .. "JLinkRemoteServerCLExe",
    JLinkRTTClientExe = bin .. "JLinkRTTClientExe",
    JLinkRTTLoggerExe = bin .. "JLinkRTTLoggerExe",
    JLinkSTM32Exe = bin .. "JLinkSTM32Exe",
    JLinkSWOViewerCLExe = bin .. "JLinkSWOViewerCLExe",
    JLinkUSBWebServerExe = bin .. "JLinkUSBWebServerExe",
    JLinkXVCDServerExe = bin .. "JLinkXVCDServerExe",
    JRunExe = bin .. "JRunExe",
    JTAGLoadExe = bin .. "JTAGLoadExe",
    libjlinkarm = lib,
  }
end

