-- Programmatic provider (user-managed) returning raw product value
IDENTITY = "local.product_programmatic@v1"
PRODUCTS = { tool = "programmatic-tool" }

CHECK = function(ctx)
  return true  -- Already satisfied; no cache artifact
end

INSTALL = function(ctx)
  -- User-managed; no cache artifact
end
