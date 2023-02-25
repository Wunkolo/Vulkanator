cmake_minimum_required( VERSION 3.24.0 )
project( Vulkanator )

set( CMAKE_CXX_STANDARD 20 )

set( LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin )

set( CMAKE_COLOR_MAKEFILE ON )
set( CMAKE_VERBOSE_MAKEFILE ON )

if( WIN32 )
elseif( APPLE )
	# Warnings
	add_compile_options( -Wall  )
	add_compile_options( -Wextra  )
	set( CMAKE_CXX_VISIBILITY_PRESET hidden)
endif()

# Libraries
find_package(
	Vulkan REQUIRED
	COMPONENTS 
		glslangValidator
)

add_subdirectory( extern )
add_subdirectory( shaders )

add_library(
	${PROJECT_NAME}
	MODULE
	source/VulkanUtils.cpp
	source/Vulkanator.cpp
)
target_include_directories(
	${PROJECT_NAME}
	PRIVATE
	include
)

if( WIN32 )
	set(
		PLUGIN_FOLDER
		"C:/Program Files/Adobe/Adobe After Effects 2022/Support Files/Plug-ins"
		CACHE
		STRING
		"After Effects Plugin folder"
	)

	#cl /I "..\extern\Adobe After Effects SDK\Headers" /EP "%(Filename).r" > "$(IntDir)%(Filename).rr"
	#"$(ProjectDir)..\extern\Adobe After Effects SDK\Resources\PiPLTool" "$(IntDir)%(Filename).rr" "$(IntDir)%(Filename).rrc"
	#cl /D "MSWindows" /EP $(IntDir)%(Filename).rrc >			   "$(ProjectDir)"\\"%(Filename)".rc
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
	target_sources(
		${PROJECT_NAME} PRIVATE
		${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.rc
	)
elseif( APPLE )
	set(
		PLUGIN_FOLDER
		"/Applications/Adobe After Effects 2022/Plug-ins/"
		CACHE
		STRING
		"After Effects Plugin folder"
	)
	set( ${PIPL_INCLUDES} "" )

	foreach( INC ${AESDK_INCLUDE} )
		list( APPEND PIPL_INCLUDES -i ${INC} )
	endforeach()

	add_custom_command(
		OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.rsrc
		COMMAND ${AFX_REZ}
		ARGS ${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}.r
			-o ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.rsrc
			-useDF
			${PIPL_INCLUDES}
			-D __MACH__
	)
	set_source_files_properties(
		${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.rsrc
		PROPERTIES
		MACOSX_PACKAGE_LOCATION Resources/
	)
	target_sources( ${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.rsrc )
endif()

cmrc_add_resource_library(
	${PROJECT_NAME}-Resources
	ALIAS Resource::${PROJECT_NAME}
	NAMESPACE ${PROJECT_NAME}
	WHENCE ${CMAKE_BINARY_DIR}
	"${CMAKE_BINARY_DIR}/shaders/Vulkanator.vert.spv"
	"${CMAKE_BINARY_DIR}/shaders/Vulkanator.frag.spv"
)
add_dependencies( ${PROJECT_NAME}-Resources Shaders)

target_link_libraries(
	${PROJECT_NAME}
	AESDK
	glm
	Vulkan::Vulkan
	Resource::${PROJECT_NAME}
)

install(
	TARGETS ${PROJECT_NAME}
	DESTINATION ${PLUGIN_FOLDER}
)

if( WIN32 )
	set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".aex")
elseif( APPLE )
	set_target_properties( ${PROJECT_NAME} PROPERTIES PREFIX "" )
	set_target_properties( ${PROJECT_NAME} PROPERTIES SUFFIX "" )

	set_target_properties( ${PROJECT_NAME} PROPERTIES BUNDLE ON )
	set_target_properties( ${PROJECT_NAME} PROPERTIES BUNDLE_EXTENSION "plugin" )

	set( MACOSX_BUNDLE_INFO_STRING "${PROJECT_NAME}" )
	set( MACOSX_BUNDLE_GUI_IDENTIFIER "com.adobe.AfterEffects.${PROJECT_NAME}" )
	set( MACOSX_BUNDLE_BUNDLE_NAME ${PROJECT_NAME} )
	string( TIMESTAMP BUILD_YEAR "%Y" )
	set( MACOSX_BUNDLE_COPYRIGHT "Copyright Wunkolo ${BUILD_YEAR}. All Rights Reserved." )

	#PkgInfo
	add_custom_command(
		OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/PkgInfo
		COMMAND echo "eFKTFXTC" >> ${CMAKE_CURRENT_BINARY_DIR}/PkgInfo
	)
	set_source_files_properties(
		${CMAKE_CURRENT_BINARY_DIR}/PkgInfo
		PROPERTIES
		MACOSX_PACKAGE_LOCATION ""
	)
	target_sources( ${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/PkgInfo )
endif()