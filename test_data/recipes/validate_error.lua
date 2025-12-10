IDENTITY = "test.validate_error@v1"

VALIDATE = function(opts)
  error("boom")
end

CHECK = function(ctx) return true end
INSTALL = function(ctx) end
