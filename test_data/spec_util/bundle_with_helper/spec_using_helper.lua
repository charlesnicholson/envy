-- Spec that uses bundle-local helper via require
local helpers = require("helpers")

-- If helper didn't load, this would error
assert(helpers.helper_value == "helper_loaded")

IDENTITY = "test.using_helper@v1"
