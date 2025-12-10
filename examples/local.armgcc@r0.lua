IDENTITY = "local.armgcc@r0"

VALIDATE = function(opts)
  if opts.version == nil then
    return "'version' is a required option"
  end
end

local sha256_fingerprints = {
  ["14.3.rel1-darwin-arm64"] =
    "30f4d08b219190a37cded6aa796f4549504902c53cfc3c7e044a8490b6eba1f7",
  ["14.3.rel1-x86_64"] = "8f6903f8ceb084d9227b9ef991490413014d991874a1e34074443c2a72b14dbd",
}

local url_prefix = "https://developer.arm.com/-/media/Files/downloads/gnu/"
local arch = "-" .. ({darwin="darwin-arm64", linux=ENVY_ARCH})[ENVY_PLATFORM]
local tail = "-arm-none-eabi." .. ((ENVY_PLATFORM == "windows") and "zip" or "tar.xz")

FETCH = function(ctx, opts)
  local filename = "arm-gnu-toolchain-" .. opts.version .. arch .. tail

  local fingerprint = sha256_fingerprints[opts.version .. arch]
  assert(fingerprint)

  return {
    source = url_prefix .. opts.version .. "/binrel/" .. filename,
    sha256 = fingerprint
  }
end

STAGE = { strip = 1 }
