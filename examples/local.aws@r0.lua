-- @envy schema "1"
IDENTITY = "local.aws@r0"

OPTIONS = function(opts)
  if envy.PLATFORM ~= "linux" then
    return "local.aws@r0 is only supported on Linux"
  end
  envy.options({ version = { required = true } })
end

FETCH = function(tmp_dir, opts)
  return "https://awscli.amazonaws.com/awscli-exe-linux-" ..
      envy.ARCH .. "-" .. opts.version .. ".zip"
end

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, opts)
  return stage_dir .. "aws/install" ..
      " --install-dir " .. install_dir .. "aws-cli" ..
      " --bin-dir " .. install_dir .. "bin"
end

PRODUCTS = { aws = "bin/aws" }
