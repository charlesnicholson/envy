-- Test spec with string CHECK and INSTALL pair (user-managed)
IDENTITY = "local.string_check_install@v1"
USER_MANAGED = true
SETUP = {
  main = {
    CHECK = "echo checking",
    INSTALL = "echo installing",
  },
}
