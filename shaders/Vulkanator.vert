#version 460
#extension GL_GOOGLE_include_directive : enable
#include "Vulkanator.glsl"

layout(location = 0) in f32vec2 InPosition;
layout(location = 1) in f32vec2 InCoord;

layout(location = 0) out f32vec2 OutCoord;

layout(binding = 0) uniform Uniforms
{
	VulkanatorRenderParams RenderParams;
};

void main()
{
	gl_Position = f32vec4(
		(RenderParams.Transform * f32vec4(InPosition, 0.0, 1.0)).xy,
		0.0,
		1.0
	);
	OutCoord = InCoord;
}