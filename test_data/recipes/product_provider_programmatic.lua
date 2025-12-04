-- Programmatic provider (user-managed) returning raw product value
identity = "local.product_programmatic@v1"
products = { tool = "programmatic-tool" }

check = function(ctx)
  return true  -- Already satisfied; no cache artifact
end

install = function(ctx)
  -- User-managed; no cache artifact
end
