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
    windows = "JLink_Windows_V" .. ver .. "_x86_64.exe",
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
  elseif envy.PLATFORM == "windows" then
    envy.run('Start-Process -Wait -FilePath "' .. src .. '" -ArgumentList "/S","/D=' .. install_dir .. '"', { check = true })
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
  elseif envy.PLATFORM == "windows" then
    bin = "JLink_V" .. version_nodot(opts.version) .. "/"
    lib = bin .. "JLink_x64.dll"
  else
    bin = ""
    lib = "libjlinkarm.so.9"
  end

  local function exe(name)
    if envy.PLATFORM == "windows" then
      return bin .. name .. ".exe"
    else
      return bin .. name .. "Exe"
    end
  end

  return {
    JLinkExe = exe("JLink"),
    JLinkGDBServerCLExe = exe("JLinkGDBServerCL"),
    JLinkGUIServerExe = exe("JLinkGUIServer"),
    JLinkRemoteServerCLExe = exe("JLinkRemoteServerCL"),
    JLinkRTTClientExe = exe("JLinkRTTClient"),
    JLinkRTTLoggerExe = exe("JLinkRTTLogger"),
    JLinkSTM32Exe = exe("JLinkSTM32"),
    JLinkSWOViewerCLExe = exe("JLinkSWOViewerCL"),
    JLinkUSBWebServerExe = exe("JLinkUSBWebServer"),
    JLinkXVCDServerExe = exe("JLinkXVCDServer"),
    JRunExe = exe("JRun"),
    JTAGLoadExe = exe("JTAGLoad"),
    libjlink = { value = lib, script = false },
  }
end
