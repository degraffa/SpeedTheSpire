add_library(sts_engine
    version.cpp
    state_hash.cpp
)

target_include_directories(sts_engine PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_compile_features(sts_engine PUBLIC cxx_std_20)

# state_hash.cpp includes xxhash.h (XXH_INLINE_ALL). PRIVATE: the header is an
# implementation detail of the engine; nothing in the public headers exposes it.
target_link_libraries(sts_engine PRIVATE xxhash)

sts_set_warnings(sts_engine)
sts_enable_sanitizers(sts_engine)
