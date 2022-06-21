cmake_minimum_required( VERSION 3.7.0 )
project( Vulkanator )

set( CMAKE_CXX_STANDARD 20 )

set( LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin )

set( CMAKE_COLOR_MAKEFILE ON )
set( CMAKE_VERBOSE_MAKEFILE ON )

if( WIN32 )
elseif( APPLE )
    # Architecture
    add_compile_options( -m64 )
    add_compile_options( -mssse3 )
    # Optimizations
    add_compile_options( -Ofast  )
    # Warnings
    add_compile_options( -Wall  )
    add_compile_options( -Wextra  )
    set( CMAKE_CXX_VISIBILITY_PRESET hidden)
endif()

add_subdirectory( extern )
add_subdirectory( shaders )

# Libraries
find_package( Vulkan REQUIRED )

set(
	PLUGIN_FOLDER
	"C:/Program Files/Adobe/Adobe After Effects 2022/Support Files/Plug-ins"
	CACHE
	STRING
	"After Effects Plugin folder"
)


#cl /I "..\extern\Adobe After Effects SDK\Headers" /EP "%(Filename).r" > "$(IntDir)%(Filename).rr"
#"$(ProjectDir)..\extern\Adobe After Effects SDK\Resources\PiPLTool" "$(IntDir)%(Filename).rr" "$(IntDir)%(Filename).rrc"
#cl /D "MSWindows" /EP $(IntDir)%(Filename).rrc >               "$(ProjectDir)"\\"%(Filename)".rc
add_custom_command(
	OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.rr
	COMMAND
	cl /I \"${AESDK_ROOT}/Headers\" /EP \"${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}.r\" > ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.rr
)

add_custom_command(
	DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.rr
	OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.rrc
	COMMAND ${AFX_REZ} ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.rr ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.rrc
)

add_custom_command(
	DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.rrc
	OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.rc
	COMMAND cl /D "MSWindows" /EP ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.rrc > ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.rc
)

add_library(
	${PROJECT_NAME}
	MODULE
	${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.rc
	source/VulkanUtils.cpp
	source/Vulkanator.cpp
)
target_include_directories(
	${PROJECT_NAME}
	PRIVATE
	include
	${Vulkan_INCLUDE_DIRS}
)

cmrc_add_resource_library(
	Vulkanator-Resources
	ALIAS Resource::Vulkanator
	NAMESPACE Vulkanator
	WHENCE ${CMAKE_BINARY_DIR}
	"${CMAKE_BINARY_DIR}/shaders/Vulkanator.vert.spv"
	"${CMAKE_BINARY_DIR}/shaders/Vulkanator.frag.spv"
)
add_dependencies( Vulkanator-Resources Shaders)

target_link_libraries(
	${PROJECT_NAME}
	AESDK
	glm
	${Vulkan_LIBRARIES}
	Resource::Vulkanator
)

install(
	TARGETS ${PROJECT_NAME}
	DESTINATION ${PLUGIN_FOLDER}
)

if( WIN32 )
	set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".aex")
elseif( APPLE )
endif()