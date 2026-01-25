IDENTITY = "local.jlink@r0"

VALIDATE = function(opts)
  if opts.version == nil then
    return "version option is required"
  end
  if not opts.version:find("%.") then
    return "version must contain a dot (e.g., '9.12' not '912')"
  end
end

local function version_nodot(version)
  return version:gsub("%.", "")
end

local function jlink_filename(opts)
  local ver = version_nodot(opts.version)
  local arch = ({ arm64 = "arm64", aarch64 = "arm64", x86_64 = "x86_64" })[envy.ARCH]
  local name = ({
    darwin = "JLink_MacOSX_V" .. ver .. "_" .. arch .. ".pkg",
    linux = "JLink_Linux_V" .. ver .. "_" .. arch .. ".tgz",
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

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, opts)
  local src = fetch_dir .. jlink_filename(opts)

  if envy.PLATFORM == "darwin" then
    local cmd = "pkgutil --expand-full " .. src .. " " .. install_dir .. "jlink"
    envy.run(cmd, { check = true })
  else
    envy.extract(src, install_dir, { strip = 1 })
  end
end

PRODUCTS = function(opts)
  local bin, lib
  if envy.PLATFORM == "darwin" then
    bin = "jlink/JLink.pkg/Payload/Applications/SEGGER/JLink_V" ..
        version_nodot(opts.version) .. "/"
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
    libjlinkarm = { value = lib, script = false },
  }
end
