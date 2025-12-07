identity = "local.armgcc@r0"

fetch = {
  source =
  "https://developer.arm.com/-/media/Files/downloads/gnu/14.3.rel1/binrel/arm-gnu-toolchain-14.3.rel1-darwin-arm64-arm-none-eabi.tar.xz",
  sha256 = "30f4d08b219190a37cded6aa796f4549504902c53cfc3c7e044a8490b6eba1f7"
}

stage = { strip = 1 }
