identity = "local.uv@r0"

fetch = {
  source = "https://github.com/astral-sh/uv/releases/download/0.9.9/uv-aarch64-apple-darwin.tar.gz",
  sha256 = "737e1c2c4f97577aa7764141846e27de915eebb3b2a0f467451089a64824d2f7"
}

stage = { strip = 1 }

products = { uv = "uv" }
