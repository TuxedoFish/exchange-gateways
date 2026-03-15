# CMakeList.txt : Top-level CMake project file, do global configuration
# and include sub-projects here.
#
cmake_minimum_required(VERSION 3.10)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif ()

project("exchange-gateways")

set(CMAKE_CXX_STANDARD 23)

# Define generated files
set(GENERATED_DIR ${CMAKE_SOURCE_DIR}/generated)
set(SBE_SCHEMA ${CMAKE_SOURCE_DIR}/schema/messages.xml)
set(SBE_JAR ${CMAKE_SOURCE_DIR}/env/sbe-all-1.30.0.jar)
set(SBE_STAMP ${GENERATED_DIR}/sbe_generated.stamp)

# Generated files
add_custom_command(
        OUTPUT ${SBE_STAMP}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED_DIR}
        COMMAND java -Dsbe.target.language=CPP
        -Dsbe.output.dir=${GENERATED_DIR}
        -Dsbe.validation.warnings.fatal=true
        -Dsbe.validation.stop.on.error=true
        -Dsbe.xinclude.aware=true
        -jar ${SBE_JAR}
        ${SBE_SCHEMA}
        COMMAND ${CMAKE_COMMAND} -E touch ${SBE_STAMP}
        DEPENDS ${SBE_SCHEMA} ${SBE_JAR}
        COMMENT "Generating SBE C++ code from schema"
)
add_custom_target(generate_sbe_code DEPENDS ${SBE_STAMP})

# Source file groups
set(MAIN_SOURCES
        src/main.cpp
        include/main.h
        include/AppRunner.h
        src/AppRunner.cpp
)

set(FIX_SOURCES
        include/marketdata/DeribitMDApplicationBase.h
        src/marketdata/DeribitMDApplicationBase.cpp
        include/marketdata/HyperliquidMDApplicationBase.h
        src/marketdata/HyperliquidMDApplicationBase.cpp
        include/marketdata/HyperliquidMDApplication.h
        src/marketdata/HyperliquidMDApplication.cpp
        include/marketdata/DeribitMDApplication.h
        src/marketdata/DeribitMDApplication.cpp
        include/fix/FIXRunner.h
        src/fix/FIXRunner.cpp
        include/fix/FIXCustomTags.h
        include/fix/FIXUtils.h
        src/fix/FIXUtils.cpp
)

set(GATEWAY_SOURCES
        include/gateway/DeribitGWApplication.h
        src/gateway/DeribitGWApplication.cpp
        include/gateway/GWRunner.h
        src/gateway/GWRunner.cpp
        include/gateway/SecurityInfo.h
        src/gateway/SecurityInfo.cpp
        include/gateway/RefDataHolder.h
        src/gateway/RefDataHolder.cpp
        include/gateway/DeribitOrdersHandler.h
        src/gateway/DeribitOrdersHandler.cpp
        include/gateway/HyperliquidOrdersHandler.h
        src/gateway/HyperliquidOrdersHandler.cpp
        include/gateway/HyperliquidGWApplication.h
        src/gateway/HyperliquidGWApplication.cpp
        include/gateway/DeribitMessageConverter.h
        src/gateway/DeribitMessageConverter.cpp
)

set(UTIL_SOURCES
        include/util/AuthHandler.h
        src/util/AuthHandler.cpp
        include/util/CmdLineOptions.h
        include/util/SimpleConfig.h
        src/util/SimpleConfig.cpp
        include/util/DateUtils.h
        src/util/DateUtils.cpp
        include/util/DecimalTypes.h
        include/sbe/SBEUtils.h
        src/sbe/SBEUtils.cpp
)

set(MARKETDATA_SOURCES
        include/historical/MarketDataLogger.h
        src/historical/MarketDataLogger.cpp
        include/marketdata/MessageProcessor.h
        src/marketdata/MessageProcessor.cpp
        include/marketdata/DeribitMessageProcessor.h
        src/marketdata/DeribitMessageProcessor.cpp
        include/marketdata/HyperliquidMessageProcessor.h
        src/marketdata/HyperliquidMessageProcessor.cpp
        include/sbe/SBEBinaryWriter.h
        src/sbe/SBEBinaryWriter.cpp
        include/sbe/SBEMessageListener.h
        include/sbe/SBEQueuePoller.h
        src/sbe/SBEQueuePoller.cpp
)

set(HISTORICAL_SOURCES
        include/historical/DeribitPersister.h
        src/historical/DeribitPersister.cpp
        include/historical/HyperliquidPersister.h
        src/historical/HyperliquidPersister.cpp
        include/historical/FileMessageProcessor.h
        src/historical/FileMessageProcessor.cpp
        include/historical/MarketdataHistoricalRunner.h
        src/historical/MarketdataHistoricalRunner.cpp
)

# Add source to this project's executable.
add_executable(gateways
        ${MAIN_SOURCES}
        ${FIX_SOURCES}
        ${GATEWAY_SOURCES}
        ${UTIL_SOURCES}
        ${MARKETDATA_SOURCES}
        ${HISTORICAL_SOURCES}
)

# Dependencies
find_package(OpenSSL REQUIRED)
find_package(Boost REQUIRED COMPONENTS iostreams filesystem system)
find_package(spdlog CONFIG REQUIRED)
IF (Boost_FOUND)
    INCLUDE_DIRECTORIES(${Boost_INCLUDE_DIR})
    ADD_DEFINITIONS( "-DHAS_BOOST" )
ENDIF()
include(third_party/quickfix.cmake)
add_subdirectory("third_party/hyperliquid-sdk-cpp" EXCLUDE_FROM_ALL)

if (WIN32)
    target_link_libraries(gateways
            ws2_32 wsock32 winmm crypt32 secur32
    )
elseif (UNIX)
    target_link_libraries(gateways
            pthread dl
    )
endif ()

target_link_libraries(gateways
        quickfix hyperliquid-sdk
        ${OPENSSL_LIBRARIES}
        Boost::iostreams Boost::filesystem Boost::system
        spdlog::spdlog
)

# Generated code
target_include_directories(gateways PRIVATE
        ${CMAKE_SOURCE_DIR}/generated
)

add_dependencies(gateways generate_sbe_code)