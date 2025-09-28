add_executable(third_party_smoke
    third_party_smoke.cpp
)

target_link_libraries(third_party_smoke
    PRIVATE
        codex::thirdparty
)

target_compile_features(third_party_smoke PRIVATE cxx_std_20)

add_test(NAME third_party_smoke COMMAND third_party_smoke)
