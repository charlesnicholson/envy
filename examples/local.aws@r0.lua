IDENTITY = "local.aws@r0"

VALIDATE = function(opts)
  if envy.PLATFORM ~= "linux" then
    return "fi.aws is only supported on linux"
  end
  if opts.version == nil then
    return "'version' is a required option"
  end
end

FETCH = function(tmp_dir, opts)
  return "https://awscli.amazonaws.com/awscli-exe-linux-" ..
      envy.ARCH .. "-" .. opts.version .. ".zip"
end

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, opts)
  envy.run(
    stage_dir .. "aws/install" ..
    " --install-dir " .. install_dir .. "aws-cli" ..
    " --bin-dir " .. install_dir .. "bin")
end

PRODUCTS = { aws = "bin/aws" }
