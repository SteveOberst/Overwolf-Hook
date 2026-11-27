include(FetchContent)
# Interprocess is header-only. Keep Boost private; consumers need only our ABI.
FetchContent_Declare(owh_boost
    URL https://archives.boost.io/release/1.92.0/source/boost_1_92_0.tar.gz
    URL_HASH SHA256=c4a3b310ddd2472416e091067166b0713be97c63f38c212c484ada022fd296ce
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR owh-headers-only)
# No CMakeLists in SOURCE_SUBDIR: populate the headers without configuring the
# complete Boost superproject or inheriting its build options.
FetchContent_MakeAvailable(owh_boost)
add_library(owh_boost_interprocess INTERFACE)
target_include_directories(owh_boost_interprocess SYSTEM INTERFACE "${owh_boost_SOURCE_DIR}")
target_compile_definitions(owh_boost_interprocess INTERFACE BOOST_USE_WINDOWS_H)
