-- @envy schema "1"
IDENTITY = "local.jlink@r0"
EXPORTABLE = envy.PLATFORM == "windows"

VALIDATE = function(opts)
  if opts.version == nil then
    return "version option is required"
  end
  if not opts.version:find("%.") then
    return "version must contain a dot (e.g., '9.12' not '912')"
  end
  if opts.mode ~= nil and opts.mode ~= "install" and opts.mode ~= "extract" then
    return "mode must be 'install' or 'extract'"
  end
  if opts.mode == "install" and envy.PLATFORM ~= "windows" then
    return "mode 'install' is only supported on Windows"
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
  local jlink = {
    source = "https://www.segger.com/downloads/jlink/" .. jlink_filename(opts),
    post_data = "accept_license_agreement=accepted",
  }

  if opts.mode == "extract" and envy.PLATFORM == "windows" then
    return { jlink, { source = "https://www.7-zip.org/a/7z2501-x64.exe" } }
  end

  return jlink
end

STAGE = function(fetch_dir, stage_dir, tmp_dir, opts)
  if opts.mode ~= "extract" or envy.PLATFORM ~= "windows" then return end

  envy.run(
    'Start-Process -Wait -FilePath "' .. fetch_dir .. '7z2501-x64.exe" ' ..
    '-ArgumentList "/S","/D=' .. stage_dir .. '7z"')
end

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, opts)
  local src = fetch_dir .. jlink_filename(opts)

  if envy.PLATFORM == "darwin" then
    envy.run("pkgutil --expand-full " .. src .. " " .. install_dir .. "jlink")
  elseif envy.PLATFORM == "windows" then
    if opts.mode == "extract" then
      local dest = install_dir .. "JLink_V" .. version_nodot(opts.version)
      envy.run(
        '& "' .. stage_dir .. '7z\\7z.exe" x "' .. src ..
        '" "-o' .. dest .. '" -aoa -y')
    else
      envy.run(
        'Start-Process -Wait -FilePath "' .. src ..
        '" -ArgumentList "/S","/D=' .. install_dir .. '"')
    end
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

  local products = {
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

  if envy.PLATFORM == "linux" then
    products.jlink_udev_rules = { value = "99-jlink.rules", script = false }
  end

  return products
end
