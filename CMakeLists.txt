cmake_minimum_required( VERSION 3.24.0 )

if( APPLE )
	# Must be set before `project(...)`
	# Minimum target is "Big Sur" MacOS 11.0
	set( CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum OSX Platform Version" )
endif()

project( Vulkanator )

set( CMAKE_CXX_STANDARD 20 )

set( LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin )

set( CMAKE_COLOR_MAKEFILE ON )
set( CMAKE_VERBOSE_MAKEFILE ON )

if( WIN32 )
	add_compile_options(
		/MP # Parallel builds 
		/permissive- # Stricter C++ conformance

		# Warnings
		/W3

		# Consider these warnings as errors
		/we4018 # 'expression': signed/unsigned mismatch
		/we4062 # Enumerator 'identifier' in a switch of enum 'enumeration' is not handled
		/we4101 # 'identifier': unreferenced local variable
		/we4265 # 'class': class has virtual functions, but destructor is not virtual
		/we4305 # 'context': truncation from 'type1' to 'type2'
		/we4388 # 'expression': signed/unsigned mismatch
		/we4389 # 'operator': signed/unsigned mismatch

		/we4456 # Declaration of 'identifier' hides previous local declaration
		/we4457 # Declaration of 'identifier' hides function parameter
		/we4458 # Declaration of 'identifier' hides class member
		/we4459 # Declaration of 'identifier' hides global declaration

		/we4505 # 'function': unreferenced local function has been removed
		/we4547 # 'operator': operator before comma has no effect; expected operator with side-effect
		/we4549 # 'operator1': operator before comma has no effect; did you intend 'operator2'?
		/we4555 # Expression has no effect; expected expression with side-effect
		/we4715 # 'function': not all control paths return a value
		/we4834 # Discarding return value of function with 'nodiscard' attribute
		/we5038 # data member 'member1' will be initialized after data member 'member2'
		/we5245 # 'function': unreferenced function with internal linkage has been removed

	)
elseif( APPLE )
	# Create Universal Binary
	set( CMAKE_OSX_ARCHITECTURES "arm64;x86_64" )

	# Warnings
	add_compile_options(
		-Wall
		-Warray-bounds
		-Wextra
		-Wimplicit-fallthrough
		-Wmissing-declarations
		-Wmissing-declarations
		-Wmissing-field-initializers
		-Wno-attributes
		-Wno-invalid-offsetof
		-Wno-unused-parameter
		-Wreorder
		-Wshadow
		-Wsign-compare
		-Wswitch
		-Wuninitialized
		-Wunused-function
		-Wunused-result
		-Wunused-variable
	)

	# Disable warnings
	add_compile_options(
		-Wno-pragma-pack
	)
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
