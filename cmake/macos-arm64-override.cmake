# Loaded after CMake's Darwin platform file. On Linux, sw_vers is missing, so
# that file treats the OS as older than 10.5 and leaves the rpath flag unset.
# Qt's imported frameworks use @rpath and CMake then refuses to generate.
set(CMAKE_SHARED_LIBRARY_RUNTIME_C_FLAG "-Wl,-rpath,")
set(CMAKE_LINK_DEPENDENT_LIBRARY_FILES FALSE)
