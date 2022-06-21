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

	FragColor *= RenderParams.ColorFactor;

	// After effects stores things in ARGB order (/_\)
	FragColor = FragColor.argb;
}