nxAddModuleLibrary(nxCore
    ALIAS nx::core
    MODULES
        src/nx.core.cppm
        src/introspection.cppm
        src/source.cppm
        src/contracts.cppm
        src/failure.cppm
        src/conversion.cppm
        src/quantity.cppm
        src/units.cppm
        src/enumArray.cppm
        src/flags.cppm
        src/hash.cppm
        src/memory.cppm
        src/log.cppm
    SOURCES
        src/contracts.cpp
        src/failure.cpp
        src/memory.cpp
        src/log.cpp)

if(NX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
