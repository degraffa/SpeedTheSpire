add_library(sts_engine
    version.cpp
)

target_include_directories(sts_engine PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_compile_features(sts_engine PUBLIC cxx_std_20)

sts_set_warnings(sts_engine)
sts_enable_sanitizers(sts_engine)
