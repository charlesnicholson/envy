#include "lua_envy.h"

#include "sol_util.h"

#include "doctest.h"

namespace envy {

TEST_CASE("ENVY_EXE_EXT injected into Lua state") {
  auto lua{ sol_util_make_lua_state() };
  lua_envy_install(*lua);

  sol::object ext_obj{ (*lua)["ENVY_EXE_EXT"] };
  REQUIRE(ext_obj.is<std::string>());
  std::string const ext{ ext_obj.as<std::string>() };

#if defined(_WIN32)
  CHECK(ext == ".exe");
#else
  CHECK(ext.empty());
#endif
}

}  // namespace envy
