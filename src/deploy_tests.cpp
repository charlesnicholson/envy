#include "deploy.h"

#include "doctest.h"
#include "embedded_init_resources.h"
#include "platform.h"

#include <string>
#include <string_view>

namespace {

std::string_view embedded_posix_template() {
  return { reinterpret_cast<char const *>(envy::embedded::kProductScriptPosix),
           envy::embedded::kProductScriptPosixSize };
}

std::string_view embedded_windows_template() {
  return { reinterpret_cast<char const *>(envy::embedded::kProductScriptWindows),
           envy::embedded::kProductScriptWindowsSize };
}

}  // namespace

TEST_CASE("deploy: kProductScriptVersion is positive") {
  CHECK(envy::kProductScriptVersion > 0);
}

TEST_CASE("deploy: embedded POSIX template has version baked at build time") {
  std::string_view const tmpl{ embedded_posix_template() };
  std::string const expected_marker{ "_ENVY_PRODUCT_SCRIPT_VERSION=" +
                                     std::to_string(envy::kProductScriptVersion) };
  CHECK(tmpl.find(expected_marker) != std::string_view::npos);
  CHECK(tmpl.find("@@ENVY_PRODUCT_SCRIPT_VERSION@@") == std::string_view::npos);
  CHECK(tmpl.find("@@ENVY_VERSION@@") == std::string_view::npos);
  CHECK(tmpl.find("envy-managed") != std::string_view::npos);
}

TEST_CASE("deploy: embedded Windows template has version baked at build time") {
  std::string_view const tmpl{ embedded_windows_template() };
  std::string const expected_marker{ "_ENVY_PRODUCT_SCRIPT_VERSION=" +
                                     std::to_string(envy::kProductScriptVersion) };
  CHECK(tmpl.find(expected_marker) != std::string_view::npos);
  CHECK(tmpl.find("@@ENVY_PRODUCT_SCRIPT_VERSION@@") == std::string_view::npos);
  CHECK(tmpl.find("@@ENVY_VERSION@@") == std::string_view::npos);
  CHECK(tmpl.find("envy-managed") != std::string_view::npos);
}

TEST_CASE("deploy: stamp_product_script substitutes product name on POSIX") {
  std::string const stamped{ envy::deploy_stamp_product_script("foo",
                                                               envy::platform_id::POSIX) };
  CHECK(stamped.find("foo") != std::string::npos);
  CHECK(stamped.find("@@PRODUCT_NAME@@") == std::string::npos);
  CHECK(stamped.find("envy-managed") != std::string::npos);
  CHECK(stamped.find("_ENVY_PRODUCT_SCRIPT_VERSION=") != std::string::npos);
}

TEST_CASE("deploy: stamp_product_script substitutes product name on Windows") {
  std::string const stamped{
    envy::deploy_stamp_product_script("foo", envy::platform_id::WINDOWS)
  };
  CHECK(stamped.find("foo") != std::string::npos);
  CHECK(stamped.find("@@PRODUCT_NAME@@") == std::string::npos);
  CHECK(stamped.find("envy-managed") != std::string::npos);
  CHECK(stamped.find("_ENVY_PRODUCT_SCRIPT_VERSION=") != std::string::npos);
}

TEST_CASE("deploy: stamp_product_script is deterministic") {
  std::string const a{ envy::deploy_stamp_product_script("foo",
                                                         envy::platform_id::POSIX) };
  std::string const b{ envy::deploy_stamp_product_script("foo",
                                                         envy::platform_id::POSIX) };
  CHECK(a == b);
}

TEST_CASE("deploy: stamp_product_script differs by product name") {
  std::string const foo{ envy::deploy_stamp_product_script("foo",
                                                           envy::platform_id::POSIX) };
  std::string const bar{ envy::deploy_stamp_product_script("bar",
                                                           envy::platform_id::POSIX) };
  CHECK(foo != bar);
  CHECK(foo.find("foo") != std::string::npos);
  CHECK(foo.find("bar") == std::string::npos);
  CHECK(bar.find("bar") != std::string::npos);
  CHECK(bar.find("foo") == std::string::npos);
}

TEST_CASE("deploy: stamp_product_script differs by platform") {
  std::string const posix{ envy::deploy_stamp_product_script("foo",
                                                             envy::platform_id::POSIX) };
  std::string const win{ envy::deploy_stamp_product_script("foo",
                                                           envy::platform_id::WINDOWS) };
  CHECK(posix != win);
  CHECK(posix.find("#!/usr/bin/env bash") != std::string::npos);
  CHECK(win.find("@echo off") != std::string::npos);
}
