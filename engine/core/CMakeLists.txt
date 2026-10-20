nxAddModuleLibrary(nxCore
    ALIAS nx::core
    MODULES
        src/nx.core.cppm
        src/introspection.cppm
        src/contracts.cppm
        src/failure.cppm
        src/conversion.cppm
        src/quantity.cppm
        src/units.cppm
        src/enumArray.cppm
        src/flags.cppm
    SOURCES
        src/contracts.cpp
        src/failure.cpp)

if(NX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
