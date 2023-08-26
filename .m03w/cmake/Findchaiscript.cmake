
list(APPEND CMAKE_PREFIX_PATH ${m03w_REPOSITORY}/chaiscript)

find_path(chaiscript_INCLUDE
    NAMES chaiscript.hpp
    PATH_SUFFIXES include/chaiscript
)
