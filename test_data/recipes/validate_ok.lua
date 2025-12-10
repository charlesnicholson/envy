IDENTITY = "test.validate_ok@v1"

VALIDATE = function(opts)
  if opts and opts.foo then
    assert(opts.foo == "bar")
  end
end

CHECK = function(ctx) return true end
INSTALL = function(ctx) end
