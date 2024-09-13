#pragma once

#include <array>
#include <cmath>
#include <cstdint>

#define PF_DEEP_COLOR_AWARE 1
#include <AEConfig.h>

#include <AE_Effect.h>
#include <AE_EffectSuites.h>
#include <entry.h>

#include "VulkanConfig.hpp"

#include <glm/glm.hpp>

namespace Vulkanator
{
// Global effect variables
// See GlobalSetup and GlobalSetdown
struct GlobalParams
{
	// Vulkan
	vk::UniqueInstance Instance       = {};
	vk::UniqueDevice   Device         = {};
	vk::PhysicalDevice PhysicalDevice = {};

	// Dispatcher for loading ~extension~ function pointers
	// Use this dispatcher to load additional function pointers
	// for extensions that Vulkan-HPP does not load automatically
	vk::DispatchLoaderDynamic Dispatcher = {};

	// A heap to allocate CommandBuffers from
	vk::UniqueCommandPool CommandPool = {};

	// Queue that will be receiving GPU workloads
	vk::Queue Queue = {};

	// For each color depth, create a render pass
	// 0: Render pass with a single  8-bit attachment
	// 1: Render pass with a single 16-bit attachment
	// 2: Render pass with a single 32-bit attachment
	std::array<vk::UniqueRenderPass, 3> RenderPasses = {};

	// This is the graphics pipeline(aka shader) that will be run
	// Due to the definition of render pass compatibility, we have to make it
	// three times for each render format depth
	std::array<vk::UniquePipeline, 3> RenderPipelines = {};

	// This is the heap that we will be allocating descriptors from
	vk::UniqueDescriptorPool DescriptorPool = {};

	// Because we will be making descriptor sets at run-time. We will need
	// the pipeline's layout and Descriptor set layout which basically will
	// describe all the inputs that each of the shader stages takes in as input
	// such as uniform-buffers, sampled-images, storage-images, and so on
	vk::UniqueDescriptorSetLayout RenderDescriptorSetLayout = {};
	vk::UniquePipelineLayout      RenderPipelineLayout      = {};

	// This buffer will store our very simple quad-triangle mesh
	vk::UniqueBuffer       MeshBuffer       = {};
	vk::UniqueDeviceMemory MeshBufferMemory = {};

	// Debug Callback
	vk::UniqueHandle<vk::DebugUtilsMessengerEXT, vk::DispatchLoaderDynamic>
		DebugMessenger = {};
};

// Sequence params, per composition
// See SequenceSetup and SequenceSetdown
struct SequenceParams
{
	// Each instance of the effect will get a command buffer it may use to
	// generate GPU workloads
	vk::UniqueCommandBuffer CommandBuffer = {};
	// Each instance of the effect will get a synchronization fence it may use
	// when submitting GPU workloads
	vk::UniqueFence Fence = {};

	// Each instance of the effect will get a descriptor set to pass it's
	// uniform data over to the shader
	vk::UniqueDescriptorSet DescriptorSet = {};

	// The actual buffer that will hold the uniform buffer that the descriptor
	// set will point to
	vk::UniqueBuffer       UniformBuffer       = {};
	vk::UniqueDeviceMemory UniformBufferMemory = {};

	// This is a collection of cached memory attached to this effect-instance
	// this is so that we arent making heavy gpu-side allocations every frame
	struct SequenceCache
	{
		// If we are only using a smaller subset of the entire buffer
		// Then we should resize the entire buffer to be smaller if it is this
		// percentage smaller than the cached size

		// If an allocation comes in that is %15 smaller than the cache, then
		// the buffer to fit
		static constexpr glm::f32 ShrinkThreshold = 0.15f;

		// Cache for the staging buffer
		std::size_t            StagingBufferSize   = 0u;
		vk::UniqueBuffer       StagingBuffer       = {};
		vk::UniqueDeviceMemory StagingBufferMemory = {};

		// We use ImageCreateInfo so that we can easily "==" compare the image
		// in the cache with the image being rendered for the current frame
		// in the case that we can re-use the memory directly rather than
		// allocating a new buffer
		vk::ImageCreateInfo OutputImageInfoCache = {};

		PF_State               InputImageState  = {};
		vk::UniqueImage        InputImage       = {};
		vk::UniqueDeviceMemory InputImageMemory = {};

		vk::UniqueImage        OutputImage       = {};
		vk::UniqueDeviceMemory OutputImageMemory = {};
	} Cache;
};

// For rendering the current frame
struct RenderParams
{
	// The sampler is created upon rendering since we will
	// potentially be using different wrapping/quality settings
	// such as nearest/linear and wrapping
	// If you want, you can cache all possible sampler objects
	// ahead of time or you can create it on the fly(it's a very cheap object)
	vk::UniqueSampler InputImageSampler = {};

	// Values passed over to vulkan
	// Aligned for the std140 layout
	// scalars:	4
	// vec2:	8
	// vect3/4: 16
	// mat4:	16
	struct
	{
		alignas(4) glm::u32 Depth            = {};
		alignas(16) glm::f32mat4 Transform   = {};
		alignas(16) glm::f32vec4 ColorFactor = {};
	} Uniforms;
};

// A simple vertex definition
struct Vertex
{
	alignas(sizeof(glm::f32) * 2u) glm::f32vec2 Position;
	alignas(sizeof(glm::f32) * 2u) glm::f32vec2 UV;

	// This is to tell vulkan how to interpret per-vertex data
	static vk::VertexInputBindingDescription& BindingDescription();

	// This will tell vulkan how to interpret each of the fields of the Vertex
	// structure and where it should be mapped ot
	static std::array<vk::VertexInputAttributeDescription, 2>&
		AttributeDescription();
};

// A simple quad mesh that fills vulkan's clip space([-1,1])
// To be drawn using Triangle-strips(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP)
// In vulkan clip space, {0,0} is on the top left of the image, with Y
// increasing downwards
static constexpr std::array<Vertex, 4> Quad = {
	Vertex{glm::f32vec2(1, -1), glm::f32vec2(1, 0)},  // Bottom Right
	Vertex{glm::f32vec2(1, 1), glm::f32vec2(1, 1)},   // Top Right
	Vertex{glm::f32vec2(-1, -1), glm::f32vec2(0, 0)}, // Bottom Left
	Vertex{glm::f32vec2(-1, 1), glm::f32vec2(0, 1)},  // Top Left
};

namespace ParamID
{
enum
{
	Input,
	Translate,
	Rotation,
	ScaleX,
	ScaleY,
	FactorR,
	FactorG,
	FactorB,
	FactorA,
	COUNT
};
};
}; // namespace Vulkanator

extern "C" {
DllExport PF_Err EntryPoint(
	PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[],
	PF_LayerDef* output, void* extra
);
}
