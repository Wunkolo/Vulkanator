#version 460
#extension GL_GOOGLE_include_directive : enable
#include "Vulkanator.glsl"

layout(location = 0) in f32vec2 InCoord;

layout(location = 0) out f32vec4 FragColor;

layout(binding = 0) uniform Uniforms
{
	VulkanatorRenderParams RenderParams;
};

layout(binding = 1) uniform sampler2D InputTexture;

void main()
{
	// After effects textures are stored in ARGB format,
	// so we have to "unswizzle" It when we read (argb -> rgba)
	FragColor = texture(InputTexture, InCoord).gbar;

	// 16 bit colors have to be specially handled
	if( RenderParams.Depth == DEPTH16 )
		FragColor *= DEPTH16_LOAD_SCALE;

	FragColor *= RenderParams.ColorFactor;

	// 16 bit colors have to be specially handled
	if( RenderParams.Depth == DEPTH16 )
		FragColor = mix((0.0).xxxx, DEPTH16_STORE_SCALE.xxxx, FragColor);

	// After effects stores things in ARGB order (/_\)
	FragColor = FragColor.argb;
}