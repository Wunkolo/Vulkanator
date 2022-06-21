cmake_minimum_required( VERSION 3.7.0 )

# Get glslangValidator
if( WIN32 )
	if( ${CMAKE_HOST_SYSTEM_PROCESSOR} STREQUAL "AMD64" )
		set( GLSL_VALIDATOR "$ENV{VULKAN_SDK}/Bin/glslangValidator" )
	endif()
endif()

# Get spirv-opt
if( WIN32 )
	if( ${CMAKE_HOST_SYSTEM_PROCESSOR} STREQUAL "AMD64" )
		set( SPIRV_OPT "$ENV{VULKAN_SDK}/Bin/spirv-opt" )
	endif()
endif()

file(
	GLOB_RECURSE SHADER_SOURCE_FILES
	"*.frag"
	"*.vert"
	"*.comp"
)

foreach( SHADER ${SHADER_SOURCE_FILES} )
	get_filename_component( FILE_NAME ${SHADER} NAME )
	set( SPIRV "${PROJECT_BINARY_DIR}/shaders/${FILE_NAME}.spv" )
	message( "Building ${SHADER}")
	add_custom_command(
		OUTPUT ${SPIRV}
		COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/shaders/"
		COMMAND ${GLSL_VALIDATOR} -t --target-env vulkan1.1 -V ${SHADER} -o ${SPIRV}
		COMMAND ${SPIRV_OPT} -O ${SPIRV} -o ${SPIRV}
		DEPENDS ${SHADER}
	)
	list( APPEND SPIRV_BINARY_FILES ${SPIRV} )
endforeach()

add_custom_target(
	Shaders
	DEPENDS ${SPIRV_BINARY_FILES}
)