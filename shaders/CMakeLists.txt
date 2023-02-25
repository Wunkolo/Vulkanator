cmake_minimum_required( VERSION 3.24.0 )

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
		COMMAND Vulkan::glslangValidator -t --target-env vulkan1.1 -V ${SHADER} -o ${SPIRV}
		# Todo: Detect and run spirv-opt 
		# COMMAND "spirv-opt" -O ${SPIRV} -o ${SPIRV}
		DEPENDS ${SHADER}
	)
	list( APPEND SPIRV_BINARY_FILES ${SPIRV} )
endforeach()

add_custom_target(
	Shaders
	DEPENDS
		Vulkan::glslangValidator
		${SPIRV_BINARY_FILES}
)