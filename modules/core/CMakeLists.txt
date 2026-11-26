add_library(owh_core INTERFACE)
target_include_directories(owh_core INTERFACE include)
target_link_libraries(owh_core INTERFACE owh_abi)
