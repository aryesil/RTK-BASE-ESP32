# Arduino sources build as the project's main component. Globbed so adding a
# file does not mean editing two places. Arduino itself is a global component
# here and must not be named in REQUIRES.
file(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.cpp)
idf_component_register(SRCS ${app_sources}
                       INCLUDE_DIRS "." "${CMAKE_SOURCE_DIR}/include"
                       REQUIRES microlink)
