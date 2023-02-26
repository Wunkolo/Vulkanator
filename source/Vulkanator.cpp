#include "Vulkanator.hpp"
#include "vulkan/vulkan.hpp"

#include <algorithm>
#include <cstdint>

#include <array>
#include <span>

#include <AEGP_SuiteHandler.h>
#include <AE_EffectCB.h>
#include <AE_EffectCBSuites.h>
#include <AE_Macros.h>
#include <Param_Utils.h>
#include <Smart_Utils.h>

#include <cmrc/cmrc.hpp>
CMRC_DECLARE(Vulkanator);
auto DataFS = cmrc::Vulkanator::get_filesystem();

#include <VulkanUtils.hpp>
#include <glm/gtc/matrix_transform.hpp>

PF_Err About(
	PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[],
	PF_LayerDef* output)
{
	AEGP_SuiteHandler suites(in_data->pica_basicP);

	// Lock global handle
	const auto GlobalParam = static_cast<Vulkanator::GlobalParams*>(
		suites.HandleSuite1()->host_lock_handle(in_data->global_data));

	if( GlobalParam && GlobalParam->PhysicalDevice )
	{
		// Print some info about the currently used physical device
		const vk::PhysicalDeviceProperties DeviceProperties
			= GlobalParam->PhysicalDevice.getProperties();
		suites.ANSICallbacksSuite1()->sprintf(
			out_data->return_msg,
			"Vulkanator\n(Build date: " __TIMESTAMP__
			")\n"
			"GPU: %.64s",
			DeviceProperties.deviceName.data());

		suites.HandleSuite1()->host_unlock_handle(in_data->global_data);
	}

	return PF_Err_NONE;
}

// extern "C" __declspec(dllimport) void __stdcall OutputDebugStringA(const
// char* lpOutputString);

PF_Err GlobalSetup(
	PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[],
	PF_LayerDef* output)
{
	AEGP_SuiteHandler suites(in_data->pica_basicP);

	out_data->my_version = PF_VERSION(1, 0, 0, PF_Stage_DEVELOP, 1);

	out_data->out_flags  = PF_OutFlag_DEEP_COLOR_AWARE;
	out_data->out_flags2 = PF_OutFlag2_PARAM_GROUP_START_COLLAPSED_FLAG
						 | PF_OutFlag2_SUPPORTS_SMART_RENDER
						 | PF_OutFlag2_FLOAT_COLOR_AWARE;

	// Allocate global handle
	const PF_Handle GlobalDataHandle = suites.HandleSuite1()->host_new_handle(
		sizeof(Vulkanator::GlobalParams));

	if( !GlobalDataHandle )
	{
		return PF_Err_OUT_OF_MEMORY;
	}

	out_data->global_data = GlobalDataHandle;

	// Lock global handle
	Vulkanator::GlobalParams* GlobalParam
		= reinterpret_cast<Vulkanator::GlobalParams*>(
			suites.HandleSuite1()->host_lock_handle(out_data->global_data));
	// Global setup stuff
	// ...
	new(GlobalParam) Vulkanator::GlobalParams();

	// Create Vulkan 1.1 instance

	//////////// Vulkan Instance Creation
	vk::ApplicationInfo ApplicationInfo = {};
	ApplicationInfo.apiVersion          = VK_API_VERSION_1_1;
	ApplicationInfo.applicationVersion  = VK_MAKE_VERSION(1, 0, 0);
	ApplicationInfo.engineVersion       = VK_MAKE_VERSION(1, 0, 0);

	vk::InstanceCreateInfo InstanceInfo = {};
	InstanceInfo.pApplicationInfo       = &ApplicationInfo;

#if defined(__APPLE__)
	InstanceInfo.flags |= vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
#endif

	// Validation Layers
	static const std::vector<const char*> InstanceLayers = {
#ifdef _DEBUG
		"VK_LAYER_KHRONOS_validation"
#endif
	};
	InstanceInfo.enabledLayerCount   = std::uint32_t(InstanceLayers.size());
	InstanceInfo.ppEnabledLayerNames = InstanceLayers.data();

	static const std::vector<const char*> InstanceExtensions = {
#if defined(__APPLE__)
		VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
#endif
#ifdef _DEBUG
		VK_EXT_DEBUG_UTILS_EXTENSION_NAME
#endif
	};
	InstanceInfo.enabledExtensionCount
		= std::uint32_t(InstanceExtensions.size());
	InstanceInfo.ppEnabledExtensionNames = InstanceExtensions.data();

	if( auto InstanceResult = vk::createInstanceUnique(InstanceInfo);
		InstanceResult.result == vk::Result::eSuccess )
	{
		GlobalParam->Instance = std::move(InstanceResult.value);
	}
	else
	{
		// Error creating instance
		return PF_Err_INTERNAL_STRUCT_DAMAGED;
	}

	// Get dispatcher ready to load function pointers
	GlobalParam->Dispatcher
		= vk::DispatchLoaderDynamic(::vkGetInstanceProcAddr);
	// Load extended function pointers: Instance-level
	GlobalParam->Dispatcher.init(GlobalParam->Instance.get());

	// Enable debug utils if debug messenger was added
	if( std::find(
			InstanceExtensions.begin(), InstanceExtensions.end(),
			// std::string_view has a way to compare itself to `const char*`
			// so by casting it, we get the actual string comparisons
			// and not pointer-comparisons
			std::string_view(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
		!= InstanceExtensions.end() )
	{
		vk::DebugUtilsMessengerCreateInfoEXT DebugCreateInfo{};
		DebugCreateInfo.messageSeverity
			= vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose
			| vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo
			| vk::DebugUtilsMessageSeverityFlagBitsEXT::eError
			| vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning;
		DebugCreateInfo.messageType
			= vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance
			| vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation
			| vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral;
		DebugCreateInfo.pfnUserCallback = VulkanUtils::DebugMessageCallback;
		// DebugCreateInfo.pUserData = ; // Any extra data that you want to
		// attach to debug callbacks
		if( auto CallbackResult
			= GlobalParam->Instance->createDebugUtilsMessengerEXTUnique(
				DebugCreateInfo, nullptr, GlobalParam->Dispatcher);
			CallbackResult.result == vk::Result::eSuccess )
		{
			GlobalParam->DebugMessenger = std::move(CallbackResult.value);
		}
		else
		{
			// Error making callback function
			return PF_Err_INTERNAL_STRUCT_DAMAGED;
		}
	}

	//////////// Logical Device Creation
	// Iterate each of the physical devices and pick one

	std::vector<vk::PhysicalDevice> PhysicalDevices;

	if( auto EnumerateResult
		= GlobalParam->Instance->enumeratePhysicalDevices();
		EnumerateResult.result == vk::Result::eSuccess )
	{
		PhysicalDevices = EnumerateResult.value;
	}
	else
	{
		// Error iterating physical devices???
		return PF_Err_INTERNAL_STRUCT_DAMAGED;
	}

	auto MinCriteria = PhysicalDevices.begin();

	// Ideally, a discrete GPU...
	const auto IsDGPU = [](const vk::PhysicalDevice& PhysicalDevice) -> bool {
		const vk::PhysicalDeviceProperties PhysicalDeviceProperties
			= PhysicalDevice.getProperties();
		return PhysicalDeviceProperties.deviceType
			== vk::PhysicalDeviceType::eDiscreteGpu;
	};

	MinCriteria
		= std::stable_partition(PhysicalDevices.begin(), MinCriteria, IsDGPU);

	// The more DeviceLocal VRAM the better
	const auto ComparePhysicalDeviceVRAM
		= [&](const vk::PhysicalDevice& PhysicalDeviceA,
			  const vk::PhysicalDevice& PhysicalDeviceB) -> bool {
		return VulkanUtils::GetLargestPhysicalDeviceHeap(
				   PhysicalDeviceA, vk::MemoryHeapFlagBits::eDeviceLocal)
				   .size
			 > VulkanUtils::GetLargestPhysicalDeviceHeap(
				   PhysicalDeviceB, vk::MemoryHeapFlagBits::eDeviceLocal)
				   .size;
	};

	std::stable_sort(
		PhysicalDevices.begin(), MinCriteria, ComparePhysicalDeviceVRAM);

	// Found our most-ideal GPU!
	GlobalParam->PhysicalDevice = PhysicalDevices.at(0);

	// Allocate a graphics queue
	std::vector<vk::DeviceQueueCreateInfo> QueueInfos{};

	{
		vk::DeviceQueueCreateInfo CurQueueInfo = {};
		CurQueueInfo.queueFamilyIndex = 0; // Index 0 tends to be the generic
										   // Graphics | Compute | Copy queue
		CurQueueInfo.queueCount = 1;

		static glm::f32 QueuePriority = 0.0f;
		CurQueueInfo.pQueuePriorities = &QueuePriority;
		QueueInfos.emplace_back(CurQueueInfo);
	}

	// Create Logical Device
	vk::DeviceCreateInfo DeviceInfo = {};
	DeviceInfo.queueCreateInfoCount = std::uint32_t(QueueInfos.size());
	DeviceInfo.pQueueCreateInfos    = QueueInfos.data();

	DeviceInfo.enabledExtensionCount   = 0u;
	DeviceInfo.ppEnabledExtensionNames = nullptr;

	DeviceInfo.enabledLayerCount   = 0u;
	DeviceInfo.ppEnabledLayerNames = nullptr;

	if( auto DeviceResult
		= GlobalParam->PhysicalDevice.createDeviceUnique(DeviceInfo);
		DeviceResult.result == vk::Result::eSuccess )
	{
		GlobalParam->Device = std::move(DeviceResult.value);
	}
	else
	{
		// Error creating device
		return PF_Err_INTERNAL_STRUCT_DAMAGED;
	}

	// Load extended function pointers: Instance and Device-levels
	GlobalParam->Dispatcher.init(
		GlobalParam->Instance.get(), ::vkGetInstanceProcAddr,
		GlobalParam->Device.get(), ::vkGetDeviceProcAddr);

	// Create CommandPool

	vk::CommandPoolCreateInfo CommandPoolInfo = {};
	CommandPoolInfo.queueFamilyIndex          = 0;
	CommandPoolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;

	if( auto CommandPoolResult
		= GlobalParam->Device->createCommandPoolUnique(CommandPoolInfo);
		CommandPoolResult.result == vk::Result::eSuccess )
	{
		GlobalParam->CommandPool = std::move(CommandPoolResult.value);
	}
	else
	{
		// Error creating command pool
		return PF_Err_INTERNAL_STRUCT_DAMAGED;
	}

	// Get queue that we will be dispatching work into
	GlobalParam->Queue = GlobalParam->Device->getQueue(0, 0);

	for( std::size_t i = 0; i < GlobalParam->RenderPasses.size(); ++i )
	{
		// Create primary render pass that we'll be using for rendering
		vk::RenderPassCreateInfo RenderPassInfo = {};

		vk::AttachmentDescription RenderPassAttachment = {};
		RenderPassInfo.attachmentCount                 = 1;
		RenderPassInfo.pAttachments                    = &RenderPassAttachment;

		// Describe three different attachments for each bit-depth
		RenderPassAttachment.format = VulkanUtils::RenderFormats[i];
		// We don't care what it had in it before, since we're hitting every
		// pixel with new color values
		RenderPassAttachment.loadOp = vk::AttachmentLoadOp::eClear;
		// Store the output pixels
		RenderPassAttachment.storeOp = vk::AttachmentStoreOp::eStore;

		// We don't do anything stencil-related
		RenderPassAttachment.stencilLoadOp  = vk::AttachmentLoadOp::eDontCare;
		RenderPassAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;

		// We don't care what layout the image was initially
		RenderPassAttachment.initialLayout
			= vk::ImageLayout::eColorAttachmentOptimal;
		// When we are done rendering, we want hte image in Transfer-Src
		// optimal format for a transfer
		RenderPassAttachment.finalLayout = vk::ImageLayout::eTransferSrcOptimal;

		// Describe a subpass and what color attachments it uses
		vk::SubpassDescription RenderPassSubpasses = {};
		RenderPassInfo.subpassCount                = 1;
		RenderPassInfo.pSubpasses                  = &RenderPassSubpasses;
		RenderPassSubpasses.pipelineBindPoint
			= vk::PipelineBindPoint::eGraphics;

		// We only have 1 color attachment, so most of this can be null
		RenderPassSubpasses.inputAttachmentCount    = 0u;
		RenderPassSubpasses.pInputAttachments       = nullptr;
		RenderPassSubpasses.pResolveAttachments     = nullptr;
		RenderPassSubpasses.pDepthStencilAttachment = nullptr;
		RenderPassSubpasses.preserveAttachmentCount = 0u;
		RenderPassSubpasses.pPreserveAttachments    = nullptr;

		// Our single color attachment
		vk::AttachmentReference ColorAttachmentReference = {};

		ColorAttachmentReference.attachment = 0;
		ColorAttachmentReference.layout
			= vk::ImageLayout::eColorAttachmentOptimal;

		RenderPassSubpasses.colorAttachmentCount = 1;
		RenderPassSubpasses.pColorAttachments    = &ColorAttachmentReference;

		vk::SubpassDependency RenderPassSubpassDependency = {};
		RenderPassInfo.dependencyCount                    = 1;
		RenderPassInfo.pDependencies = &RenderPassSubpassDependency;

		// We want this subpass to wait for everything in the command buffer
		// before it
		RenderPassSubpassDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		RenderPassSubpassDependency.dstSubpass = 0;

		RenderPassSubpassDependency.srcStageMask
			= vk::PipelineStageFlagBits::eColorAttachmentOutput;
		RenderPassSubpassDependency.dstStageMask
			= vk::PipelineStageFlagBits::eColorAttachmentOutput;

		RenderPassSubpassDependency.srcAccessMask = vk::AccessFlags();
		RenderPassSubpassDependency.dstAccessMask
			= vk::AccessFlagBits::eColorAttachmentWrite;

		RenderPassSubpassDependency.dependencyFlags
			= vk::DependencyFlagBits::eByRegion;

		if( auto RenderPassResult
			= GlobalParam->Device->createRenderPassUnique(RenderPassInfo);
			RenderPassResult.result == vk::Result::eSuccess )
		{
			GlobalParam->RenderPasses[i] = std::move(RenderPassResult.value);
		}
		else
		{
			// Error creating command pool
			return PF_Err_INTERNAL_STRUCT_DAMAGED;
		}
	}

	// Load shader modules from the virtual file system
	const auto VertShaderFile = DataFS.open("shaders/Vulkanator.vert.spv");
	const auto FragShaderFile = DataFS.open("shaders/Vulkanator.frag.spv");

	const auto VertShaderCode = std::as_bytes(
		std::span(VertShaderFile.begin(), VertShaderFile.end()));
	const auto FragShaderCode = std::as_bytes(
		std::span(FragShaderFile.begin(), FragShaderFile.end()));

	vk::UniqueShaderModule VertShaderModule = {};
	if( auto ShaderModuleResult = VulkanUtils::LoadShaderModule(
			GlobalParam->Device.get(), VertShaderCode);
		ShaderModuleResult )
	{
		VertShaderModule = std::move(ShaderModuleResult.value());
	}
	else
	{
		// Error loading shader module
		return PF_Err_INTERNAL_STRUCT_DAMAGED;
	}

	vk::UniqueShaderModule FragShaderModule = {};
	if( auto ShaderModuleResult = VulkanUtils::LoadShaderModule(
			GlobalParam->Device.get(), FragShaderCode);
		ShaderModuleResult )
	{
		FragShaderModule = std::move(ShaderModuleResult.value());
	}
	else
	{
		// Error loading shader module
		return PF_Err_INTERNAL_STRUCT_DAMAGED;
	}

	///// Descriptor Pool

	// Here we describe how large the pool should be for each descriptor
	// type these should be resonably large such that it won't take up too
	// much memory but will generally contain the "worst case" In this case,
	// we are allocating a single descriptor set for each instance of the
	// effect so 512 should be more than enough. If you add more descriptor
	// types, then you will have to add to this poolsize
	static vk::DescriptorPoolSize DescriptorPoolSizes[]
		= {{vk::DescriptorType::eUniformBuffer, 512},
		   {vk::DescriptorType::eCombinedImageSampler, 512}};

	vk::DescriptorPoolCreateInfo DescriptorPoolInfo = {};
	// Maximum number of total descriptor sets, upper bound
	DescriptorPoolInfo.maxSets = 1024;

	// Maximum number of each individual descriptor type
	DescriptorPoolInfo.poolSizeCount
		= std::uint32_t(glm::countof(DescriptorPoolSizes));
	DescriptorPoolInfo.pPoolSizes = DescriptorPoolSizes;

	if( auto DescriptorPoolResult
		= GlobalParam->Device->createDescriptorPoolUnique(DescriptorPoolInfo);
		DescriptorPoolResult.result == vk::Result::eSuccess )
	{
		GlobalParam->DescriptorPool = std::move(DescriptorPoolResult.value);
	}
	else
	{
		// Error creating descriptor pool
		return PF_Err_INTERNAL_STRUCT_DAMAGED;
	}

	///// DescriptorSet Layout

	// Here we describe each of the bindings and what will be binded there
	// This will match up to what we see in the shader
	static vk::DescriptorSetLayoutBinding DescriptorLayoutBindings[]
		= {vk::DescriptorSetLayoutBinding(
			   0,                                    // Binding 0...
			   vk::DescriptorType::eUniformBuffer,   // is a Uniform Buffer...
			   1,                                    // just one of them...
			   vk::ShaderStageFlagBits::eAllGraphics // available to the any
													 // shader in the graphics
													 // pipeline
			   ),
		   vk::DescriptorSetLayoutBinding(
			   1,                                         // Binding 1...
			   vk::DescriptorType::eCombinedImageSampler, // is a combined
														  // image sampler...
			   1,                                         // just one of them...
			   vk::ShaderStageFlagBits::eFragment // available to the fragment
												  // shader
			   )};

	// All of our shader bindings will now be packaged up into a single
	// DescriptorSetLayout object
	static vk::DescriptorSetLayoutCreateInfo DescriptorLayoutInfos(
		{}, std::uint32_t(glm::countof(DescriptorLayoutBindings)),
		DescriptorLayoutBindings);

	if( auto DescriptorSetLayoutResult
		= GlobalParam->Device->createDescriptorSetLayoutUnique(
			DescriptorLayoutInfos);
		DescriptorSetLayoutResult.result == vk::Result::eSuccess )
	{
		GlobalParam->RenderDescriptorSetLayout
			= std::move(DescriptorSetLayoutResult.value);
	}
	else
	{
		// Error creating pipeline layout
		return PF_Err_INTERNAL_STRUCT_DAMAGED;
	}

	///// Pipeline Layout
	// Now, we describe the layout of the pipeline
	// This is where you describe what descriptor sets and pushconstants
	// and such that the shader will be consuming
	vk::PipelineLayoutCreateInfo RenderPipelineLayoutInfo = {};
	RenderPipelineLayoutInfo.setLayoutCount               = 1;
	RenderPipelineLayoutInfo.pSetLayouts
		= &GlobalParam->RenderDescriptorSetLayout.get();

	// This shader takes in no push constants
	RenderPipelineLayoutInfo.pushConstantRangeCount = 0;
	RenderPipelineLayoutInfo.pPushConstantRanges    = nullptr;

	if( auto RenderPipelineLayoutResult
		= GlobalParam->Device->createPipelineLayoutUnique(
			RenderPipelineLayoutInfo);
		RenderPipelineLayoutResult.result == vk::Result::eSuccess )
	{
		GlobalParam->RenderPipelineLayout
			= std::move(RenderPipelineLayoutResult.value);
	}
	else
	{
		// Error creating pipeline layout
		return PF_Err_INTERNAL_STRUCT_DAMAGED;
	}

	///// Pipeline Creation

	// Describe the stage and entry point of each shader
	const vk::PipelineShaderStageCreateInfo ShaderStagesInfo[2] = {
		vk::PipelineShaderStageCreateInfo(
			{},                               // Flags
			vk::ShaderStageFlagBits::eVertex, // Shader Stage
			VertShaderModule.get(),           // Shader Module
			"main", // Shader entry point function name
			{}      // Shader specialization info
			),
		vk::PipelineShaderStageCreateInfo(
			{},                                 // Flags
			vk::ShaderStageFlagBits::eFragment, // Shader Stage
			FragShaderModule.get(),             // Shader Module
			"main", // Shader entry point function name
			{}      // Shader specialization info
			),
	};

	// We gotta describe everything about this pipeline up-front. so this is
	// about to get pretty verbose
	vk::PipelineVertexInputStateCreateInfo VertexInputState = {};
	// Here, we describe how the shader will be consuming vertex data
	VertexInputState.vertexBindingDescriptionCount = 1;
	VertexInputState.pVertexBindingDescriptions
		= &Vulkanator::Vertex::BindingDescription();
	VertexInputState.vertexAttributeDescriptionCount
		= std::uint32_t(Vulkanator::Vertex::AttributeDescription().size());
	VertexInputState.pVertexAttributeDescriptions
		= Vulkanator::Vertex::AttributeDescription().data();

	// Here, we describe how the geometry will be drawn using the output of
	// the vertex shader(point, line, triangle) Also primitive restarting
	vk::PipelineInputAssemblyStateCreateInfo InputAssemblyState = {};
	InputAssemblyState.topology = vk::PrimitiveTopology::eTriangleStrip;
	InputAssemblyState.primitiveRestartEnable = false;

	// Here, we describe the region of the framebuffer that will be rendered
	// into This will be very dynamic in the context of after effects so we
	// put in some default values for now and then at render-time we
	// dynamically set the viewport and scissor regions
	vk::PipelineViewportStateCreateInfo ViewportState = {};
	static const vk::Viewport DefaultViewport = {0, 0, 16, 16, 0.0f, 1.0f};
	static const vk::Rect2D   DefaultScissor  = {{0, 0}, {16, 16}};
	ViewportState.viewportCount               = 1;
	ViewportState.pViewports                  = &DefaultViewport;
	ViewportState.scissorCount                = 1;
	ViewportState.pScissors                   = &DefaultScissor;

	// Here, we will basically describe how the rasterizer will dispatch
	// fragment shaders from the vertex shaders
	vk::PipelineRasterizationStateCreateInfo RasterizationState = {};
	RasterizationState.depthClampEnable                         = false;
	RasterizationState.rasterizerDiscardEnable                  = false;
	RasterizationState.polygonMode             = vk::PolygonMode::eFill;
	RasterizationState.cullMode                = vk::CullModeFlagBits::eNone;
	RasterizationState.frontFace               = vk::FrontFace::eClockwise;
	RasterizationState.depthBiasEnable         = false;
	RasterizationState.depthBiasConstantFactor = 0.0f;
	RasterizationState.depthBiasClamp          = 0.0f;
	RasterizationState.depthBiasSlopeFactor    = 0.0;
	RasterizationState.lineWidth               = 1.0f;

	// If we render into a multi-sample framebuffer, then these settings
	// will be used for now, we are not though. so we just say that we are
	// rendering into a single sample image
	vk::PipelineMultisampleStateCreateInfo MultisampleState = {};
	MultisampleState.rasterizationSamples  = vk::SampleCountFlagBits::e1;
	MultisampleState.sampleShadingEnable   = false;
	MultisampleState.minSampleShading      = 1.0f;
	MultisampleState.pSampleMask           = nullptr;
	MultisampleState.alphaToCoverageEnable = false;
	MultisampleState.alphaToOneEnable      = false;

	// Here, we describe how depth-stencil testing and comparisons will be
	// made we aren't doing any, but we map it out here anyways in case we
	// ever have a depth or stencil buffer to test against
	vk::PipelineDepthStencilStateCreateInfo DepthStencilState = {};
	DepthStencilState.depthTestEnable                         = false;
	DepthStencilState.depthWriteEnable                        = false;
	DepthStencilState.depthCompareOp        = vk::CompareOp::eNever;
	DepthStencilState.depthBoundsTestEnable = false;
	DepthStencilState.stencilTestEnable     = false;
	DepthStencilState.front                 = vk::StencilOp::eKeep;
	DepthStencilState.back                  = vk::StencilOp::eKeep;
	DepthStencilState.minDepthBounds        = 0.0f;
	DepthStencilState.maxDepthBounds        = 1.0f;

	// Here, we describe how alpha-transparency will be handled
	// per-attachment
	//
	vk::PipelineColorBlendStateCreateInfo ColorBlendState = {};
	ColorBlendState.logicOpEnable                         = false;
	ColorBlendState.logicOp                               = vk::LogicOp::eClear;
	ColorBlendState.attachmentCount                       = 1;

	// We have just 1 attachment, and we arent doing alpha blending for now
	// so we have this structure just to say "we arent doing blending"
	vk::PipelineColorBlendAttachmentState BlendAttachmentState = {};
	BlendAttachmentState.blendEnable                           = false;
	BlendAttachmentState.srcColorBlendFactor = vk::BlendFactor::eZero;
	BlendAttachmentState.dstColorBlendFactor = vk::BlendFactor::eZero;
	BlendAttachmentState.colorBlendOp        = vk::BlendOp::eAdd;
	BlendAttachmentState.srcAlphaBlendFactor = vk::BlendFactor::eZero;
	BlendAttachmentState.dstAlphaBlendFactor = vk::BlendFactor::eZero;
	BlendAttachmentState.alphaBlendOp        = vk::BlendOp::eAdd;
	BlendAttachmentState.colorWriteMask
		= vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG
		| vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

	ColorBlendState.pAttachments = &BlendAttachmentState;

	// Here, we can describe everything about this pipeline that can be
	// dynamically configured at run-time and will be read from the command
	// buffer
	vk::PipelineDynamicStateCreateInfo DynamicState = {};
	vk::DynamicState                   DynamicStates[]
		= {// The viewport and scissor of the framebuffer will be dynamic at
		   // run-time
		   // so we definately add these
		   vk::DynamicState::eViewport, vk::DynamicState::eScissor};
	DynamicState.dynamicStateCount = std::uint32_t(glm::countof(DynamicStates));
	DynamicState.pDynamicStates    = DynamicStates;

	// Create each of the graphics pipelines for each bit-depth
	// These are all the same for each of the graphics pipelines
	// so they can be out of the loop. Only renderpass will be different
	vk::GraphicsPipelineCreateInfo RenderPipelineInfo = {};
	RenderPipelineInfo.stageCount                     = 2; // Vert + Frag stages
	RenderPipelineInfo.pStages                        = ShaderStagesInfo;
	RenderPipelineInfo.pVertexInputState              = &VertexInputState;
	RenderPipelineInfo.pInputAssemblyState            = &InputAssemblyState;
	RenderPipelineInfo.pViewportState                 = &ViewportState;
	RenderPipelineInfo.pRasterizationState            = &RasterizationState;
	RenderPipelineInfo.pMultisampleState              = &MultisampleState;
	RenderPipelineInfo.pDepthStencilState             = &DepthStencilState;
	RenderPipelineInfo.pColorBlendState               = &ColorBlendState;
	RenderPipelineInfo.pDynamicState                  = &DynamicState;
	RenderPipelineInfo.subpass                        = 0;
	RenderPipelineInfo.layout = GlobalParam->RenderPipelineLayout.get();
	for( std::size_t i = 0; i < GlobalParam->RenderPasses.size(); ++i )
	{

		// ~Compatible~ render passes that this pipeline will be used for
		// https://www.khronos.org/registry/vulkan/specs/1.2-extensions/html/vkspec.html#renderpass-compatibility
		RenderPipelineInfo.renderPass = GlobalParam->RenderPasses[i].get();

		if( auto RenderPipelineResult
			= GlobalParam->Device->createGraphicsPipelineUnique(
				{}, RenderPipelineInfo);
			RenderPipelineResult.result == vk::Result::eSuccess )
		{
			GlobalParam->RenderPipelines[i]
				= std::move(RenderPipelineResult.value);
		}
		else
		{
			// Error creating graphics pipeline
			return PF_Err_INTERNAL_STRUCT_DAMAGED;
		}
	}

	// Create quad vertex buffer
	std::tie(GlobalParam->MeshBuffer, GlobalParam->MeshBufferMemory)
		= VulkanUtils::AllocateBuffer(
			  GlobalParam->Device.get(), GlobalParam->PhysicalDevice,
			  Vulkanator::Quad.size() * sizeof(Vulkanator::Vertex),
			  vk::BufferUsageFlagBits::eVertexBuffer,
			  vk::MemoryPropertyFlagBits::eHostCached
				  | vk::MemoryPropertyFlagBits::eHostCoherent)
			  .value();

	// Write vertex buffer data in
	if( auto MapResult = GlobalParam->Device->mapMemory(
			GlobalParam->MeshBufferMemory.get(), 0, VK_WHOLE_SIZE);
		MapResult.result == vk::Result::eSuccess )
	{
		std::memcpy(
			MapResult.value, Vulkanator::Quad.data(),
			Vulkanator::Quad.size() * sizeof(Vulkanator::Vertex));
		GlobalParam->Device->unmapMemory(GlobalParam->MeshBufferMemory.get());
	}
	else
	{
		// Error mapping staging buffer
		return PF_Err_INTERNAL_STRUCT_DAMAGED;
	}

	return PF_Err_NONE;
}

PF_Err GlobalSetdown(
	PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[],
	PF_LayerDef* output)
{
	AEGP_SuiteHandler suites(in_data->pica_basicP);

	// Lock global handle
	if( auto GlobalParam
		= reinterpret_cast<Vulkanator::GlobalParams*>(in_data->global_data);
		GlobalParam )
	{
		// Global setdown stuff
		// GlobalParam->~GlobalParams();
		// host_dispose_handle seems to call the deconstructor already? That's
		// weird, but cool I guess

		suites.HandleSuite1()->host_dispose_handle(in_data->global_data);
		in_data->global_data = out_data->global_data = nullptr;
	}

	return PF_Err_NONE;
}

PF_Err SequenceSetup(
	PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[],
	PF_LayerDef* output)
{
	AEGP_SuiteHandler suites(in_data->pica_basicP);

	Vulkanator::GlobalParams* GlobalParam
		= static_cast<Vulkanator::GlobalParams*>(
			suites.HandleSuite1()->host_lock_handle(in_data->global_data));

	// Cleanup previous sequence datas
	if( auto SequenceParam = reinterpret_cast<Vulkanator::SequenceParams*>(
			out_data->sequence_data);
		SequenceParam )
	{
		// Setdown sequence stuff
		SequenceParam->~SequenceParams();
		// Destroy handle
		suites.HandleSuite1()->host_dispose_handle(out_data->sequence_data);
		in_data->sequence_data = out_data->sequence_data = nullptr;
	}

	// Allocate new sequence data
	const PF_Handle SequenceDataHandle = suites.HandleSuite1()->host_new_handle(
		sizeof(Vulkanator::SequenceParams));

	if( !SequenceDataHandle )
	{
		return PF_Err_OUT_OF_MEMORY;
	}

	out_data->sequence_data = SequenceDataHandle;

	Vulkanator::SequenceParams* SequenceParam
		= reinterpret_cast<Vulkanator::SequenceParams*>(*SequenceDataHandle);
	// Setup data sequence stuff
	// ...
	new(SequenceParam) Vulkanator::SequenceParams();

	// Allocate Command Buffer
	vk::CommandBufferAllocateInfo CommandBufferInfo = {};
	CommandBufferInfo.commandBufferCount            = 1u;
	CommandBufferInfo.commandPool = GlobalParam->CommandPool.get();
	CommandBufferInfo.level       = vk::CommandBufferLevel::ePrimary;

	if( auto AllocResult
		= GlobalParam->Device->allocateCommandBuffersUnique(CommandBufferInfo);
		AllocResult.result == vk::Result::eSuccess )
	{
		SequenceParam->CommandBuffer = std::move(AllocResult.value.at(0));
	}
	else
	{
		// Error allocating command buffer
		return PF_Err_OUT_OF_MEMORY;
	}

	// Allocate Fence

	if( auto FenceResult = GlobalParam->Device->createFenceUnique({});
		FenceResult.result == vk::Result::eSuccess )
	{
		SequenceParam->Fence = std::move(FenceResult.value);
	}
	else
	{
		// Error allocating fence
		return PF_Err_OUT_OF_MEMORY;
	}

	// Allocate descriptor set

	vk::DescriptorSetAllocateInfo DescriptorAllocInfo = {};
	DescriptorAllocInfo.descriptorPool     = GlobalParam->DescriptorPool.get();
	DescriptorAllocInfo.descriptorSetCount = 1u;
	DescriptorAllocInfo.pSetLayouts
		= &GlobalParam->RenderDescriptorSetLayout.get();

	if( auto DescriptorSetResult
		= GlobalParam->Device->allocateDescriptorSetsUnique(
			DescriptorAllocInfo);
		DescriptorSetResult.result == vk::Result::eSuccess )
	{
		SequenceParam->DescriptorSet
			= std::move(DescriptorSetResult.value.at(0));
	}
	else
	{
		// Error allocating descriptor set
		return PF_Err_OUT_OF_MEMORY;
	}

	// Allocate the actual uniform buffer
	std::tie(SequenceParam->UniformBuffer, SequenceParam->UniformBufferMemory)
		= VulkanUtils::AllocateBuffer(
			  GlobalParam->Device.get(), GlobalParam->PhysicalDevice,
			  sizeof(Vulkanator::RenderParams::Uniforms),
			  vk::BufferUsageFlagBits::eUniformBuffer,
			  vk::MemoryPropertyFlagBits::eHostCached
				  | vk::MemoryPropertyFlagBits::eHostCoherent)
			  .value();

	// This is used in a bit to describe what part of the buffer we are
	// mapping to the descriptor set
	vk::DescriptorBufferInfo UniformBufferInfo = {};
	UniformBufferInfo.buffer                   = SequenceParam->UniformBuffer
								   .get(); // The uniform buffer we just created
	// This buffer is entirely used as a uniform buffer, so we map the whole
	// thing
	UniformBufferInfo.offset = 0u;
	UniformBufferInfo.range  = VK_WHOLE_SIZE;

	// Now, we write to the descriptor set so that it points to the uniform
	// buffer memory This won't change, so we only have to do this once

	// Dispatch all the writes
	GlobalParam->Device->updateDescriptorSets(
		{// Descriptor Writes
		 vk::WriteDescriptorSet(
			 SequenceParam->DescriptorSet.get(), // Target Desriptor set
			 0,                                  // Target binding
			 0,                                  // Target array element
			 1,                                  // Number of descriptor writes
			 vk::DescriptorType::eUniformBuffer, // Descriptor type at this
												 // binding
			 nullptr, // ImageInfo, if it's an image-related descriptor
			 &UniformBufferInfo, // BufferInfo, if it's a buffer-related
								 // descriptor
			 nullptr             // BufferView, if it's a texel-buffer-related
								 // descriptor
			 )},
		{
			// Descriptor Copies
		});

	return PF_Err_NONE;
}

PF_Err SequenceReSetup(
	PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[],
	PF_LayerDef* output)
{
	if( !in_data->sequence_data )
		return SequenceSetup(in_data, out_data, params, output);
	return PF_Err_NONE;
}

PF_Err SequenceSetdown(
	PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[],
	PF_LayerDef* output)
{
	AEGP_SuiteHandler suites(in_data->pica_basicP);

	if( in_data->sequence_data )
	{
		Vulkanator::SequenceParams* SequenceParam
			= reinterpret_cast<Vulkanator::SequenceParams*>(
				*out_data->sequence_data);
		// Setdown sequence stuff
		// SequenceParam->~SequenceParams();
		// host_dispose_handle seems to call the deconstructor already? That's
		// weird, but cool I guess Destroy handle
		suites.HandleSuite1()->host_dispose_handle(out_data->sequence_data);
		in_data->sequence_data = out_data->sequence_data = nullptr;
	}

	return PF_Err_NONE;
}

// We don't serialize anything, so we just deconstruct our sequence data
PF_Err SequenceFlatten(
	PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[],
	PF_LayerDef* output)
{
	return SequenceSetdown(in_data, out_data, params, output);
}

PF_Err ParamsSetup(
	PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[],
	PF_LayerDef* output)
{
	PF_Err err = PF_Err_NONE;

	PF_ParamDef def;

	def = {};
	PF_ADD_POINT("Translate", 50, 50, false, Vulkanator::ParamID::Translate);

	def = {};
	PF_ADD_ANGLE("Rotation", 0, Vulkanator::ParamID::Rotation);

	def = {};
	PF_ADD_FLOAT_SLIDERX(
		"Scale X", -300, 300, -300, 300, 100, PF_Precision_HUNDREDTHS,
		PF_ValueDisplayFlag_PERCENT, PF_ParamFlag_NONE,
		Vulkanator::ParamID::ScaleX);

	def = {};
	PF_ADD_FLOAT_SLIDERX(
		"Scale Y", -300, 300, -300, 300, 100, PF_Precision_HUNDREDTHS,
		PF_ValueDisplayFlag_PERCENT, PF_ParamFlag_NONE,
		Vulkanator::ParamID::ScaleY);

	def = {};
	PF_ADD_FLOAT_SLIDERX(
		"R Factor", -300, 300, -300, 300, 100, PF_Precision_HUNDREDTHS,
		PF_ValueDisplayFlag_PERCENT, PF_ParamFlag_NONE,
		Vulkanator::ParamID::FactorR);

	def = {};
	PF_ADD_FLOAT_SLIDERX(
		"G Factor", -300, 300, -300, 300, 100, PF_Precision_HUNDREDTHS,
		PF_ValueDisplayFlag_PERCENT, PF_ParamFlag_NONE,
		Vulkanator::ParamID::FactorG);

	def = {};
	PF_ADD_FLOAT_SLIDERX(
		"B Factor", -300, 300, -300, 300, 100, PF_Precision_HUNDREDTHS,
		PF_ValueDisplayFlag_PERCENT, PF_ParamFlag_NONE,
		Vulkanator::ParamID::FactorB);

	def = {};
	PF_ADD_FLOAT_SLIDERX(
		"A Factor", -300, 300, -300, 300, 100, PF_Precision_HUNDREDTHS,
		PF_ValueDisplayFlag_PERCENT, PF_ParamFlag_NONE,
		Vulkanator::ParamID::FactorA);

	out_data->num_params = Vulkanator::ParamID::COUNT;
	return err;
}

///////////////////////////////Quick
/// Utils////////////////////////////////////////////////

PF_Err GetParam(
	const PF_InData* in_data, std::int32_t ParamIndex, PF_ParamDef& Param)
{
	const PF_Err err = in_data->inter.checkout_param(
		in_data->effect_ref, ParamIndex, in_data->current_time,
		in_data->time_step, in_data->time_scale, &Param);
	if( err != PF_Err_NONE )
	{
		return in_data->inter.checkin_param(in_data->effect_ref, &Param);
	}
	return err;
}

PF_ParamDef GetParam(const PF_InData* in_data, std::int32_t ParamIndex)
{
	PF_ParamDef Temp = {};
	GetParam(in_data, ParamIndex, Temp);
	return Temp;
}

//////////////////////////////////////////////////////////////////////////////////////////

PF_Err SmartPreRender(
	PF_InData* in_data, PF_OutData* out_data, PF_PreRenderExtra* extra)
{
	AEGP_SuiteHandler suites(in_data->pica_basicP);

	PF_Err                   err     = PF_Err_NONE;
	PF_RenderRequest         Request = extra->input->output_request;
	const PF_PreRenderInput* Input   = extra->input;
	PF_PreRenderOutput*      Output  = extra->output;

	// Checkout all layers in their entirety
	Request.field                      = PF_Field_FRAME;
	Request.preserve_rgb_of_zero_alpha = true;
	Request.channel_mask               = PF_ChannelMask_ARGB;

	// Checkout full input
	PF_CheckoutResult InputCheckResult;
	// First, we get the full resolution of the input image with a null out
	// request, to get the `max_result_rect` field
	PF_RenderRequest FullRequest = {};
	err                          = extra->cb->checkout_layer(
								 in_data->effect_ref,
								 // checkout ID has to be unique, so ParamCount keeps it out the way of
								 // our usual parameter ids
								 Vulkanator::ParamID::Input,
								 Vulkanator::ParamID::Input + Vulkanator::ParamID::COUNT, &FullRequest,
								 in_data->current_time, in_data->time_step, in_data->time_scale,
								 &InputCheckResult);

	// Then, we checkout the whole texture using the size we got previously
	// Checkout Input layer
	FullRequest                            = Request;
	FullRequest.rect                       = InputCheckResult.max_result_rect;
	FullRequest.field                      = PF_Field_FRAME;
	FullRequest.preserve_rgb_of_zero_alpha = true;
	FullRequest.channel_mask               = PF_ChannelMask_ARGB;
	ERR(extra->cb->checkout_layer(
		in_data->effect_ref, Vulkanator::ParamID::Input,
		Vulkanator::ParamID::Input, &FullRequest, in_data->current_time,
		in_data->time_step, in_data->time_scale, &InputCheckResult));

	// UnionLRect(&InputCheckResult.result_rect, &Output->result_rect);
	// UnionLRect(&InputCheckResult.max_result_rect, &Output->max_result_rect);
	// We will always render the full frame, rather than deal with rendering to
	// subsets of the framebuffer
	Output->result_rect     = InputCheckResult.result_rect;
	Output->max_result_rect = InputCheckResult.result_rect;
	Output->flags           = PF_RenderOutputFlag_RETURNS_EXTRA_PIXELS;

	// Setup render params
	Vulkanator::RenderParams* FrameParam = new Vulkanator::RenderParams();
	Output->pre_render_data              = FrameParam;
	Output->delete_pre_render_data_func  = [](void* pre_render_data) -> void {
        delete reinterpret_cast<Vulkanator::RenderParams*>(pre_render_data);
	};

	FrameParam->Uniforms = {};

	// Pass Color-Depth information to shader
	// 32 / 16 = 2
	// 16 / 16 = 1
	// 8  / 16 = 0
	FrameParam->Uniforms.Depth = Input->bitdepth / 16;

	// Resolve Downsample
	const glm::f32 PixelRatio
		= in_data->pixel_aspect_ratio.num
		/ static_cast<glm::f32>(in_data->pixel_aspect_ratio.den);
	const glm::f32vec2 DownSample
		= glm::f32vec2(in_data->downsample_x.num, in_data->downsample_y.num)
		/ glm::f32vec2(in_data->downsample_x.den, in_data->downsample_y.den);

	PF_ParamDef CurrentParam;

	// Translation
	GetParam(in_data, Vulkanator::ParamID::Translate, CurrentParam);
	// Vulkan clip space is from [-1.0,1.0], so we remap to [0.0,1.0]
	const glm::f32vec2 Translate(
		glm::mix(
			-1.0f, 1.0f,
			static_cast<glm::f32>(FIX_2_FLOAT(CurrentParam.u.td.x_value))
				/ (in_data->width * DownSample.x)),
		glm::mix(
			-1.0f, 1.0f,
			static_cast<glm::f32>(FIX_2_FLOAT(CurrentParam.u.td.y_value))
				/ (in_data->height * DownSample.y)));

	// Rotation
	GetParam(in_data, Vulkanator::ParamID::Rotation, CurrentParam);
	const glm::f32 Rotation = glm::radians(
		static_cast<glm::f32>(FIX_2_FLOAT(CurrentParam.u.ad.value)));

	// ScaleX
	GetParam(in_data, Vulkanator::ParamID::ScaleX, CurrentParam);
	const glm::f32 ScaleX
		= static_cast<glm::f32>(CurrentParam.u.fs_d.value) / 100.0f;
	// ScaleY
	GetParam(in_data, Vulkanator::ParamID::ScaleY, CurrentParam);
	const glm::f32 ScaleY
		= static_cast<glm::f32>(CurrentParam.u.fs_d.value) / 100.0f;

	const glm::f32vec2 Scale(ScaleX, ScaleY);

	FrameParam->Uniforms.Transform = glm::identity<glm::f32mat4>();

	// map [-1.0,1.0] to [0.0,1.0]

	FrameParam->Uniforms.Transform = glm::translate(
		FrameParam->Uniforms.Transform, glm::f32vec3(Translate, 0.0f));
	FrameParam->Uniforms.Transform = glm::rotate(
		FrameParam->Uniforms.Transform, Rotation, glm::f32vec3(0, 0, 1));
	FrameParam->Uniforms.Transform
		= glm::scale(FrameParam->Uniforms.Transform, glm::f32vec3(Scale, 1.0f));

	// Color factors
	GetParam(in_data, Vulkanator::ParamID::FactorR, CurrentParam);
	const glm::f32 FactorR
		= static_cast<glm::f32>(CurrentParam.u.fs_d.value) / 100.0f;
	GetParam(in_data, Vulkanator::ParamID::FactorG, CurrentParam);
	const glm::f32 FactorG
		= static_cast<glm::f32>(CurrentParam.u.fs_d.value) / 100.0f;
	GetParam(in_data, Vulkanator::ParamID::FactorB, CurrentParam);
	const glm::f32 FactorB
		= static_cast<glm::f32>(CurrentParam.u.fs_d.value) / 100.0f;
	GetParam(in_data, Vulkanator::ParamID::FactorA, CurrentParam);
	const glm::f32 FactorA
		= static_cast<glm::f32>(CurrentParam.u.fs_d.value) / 100.0f;

	FrameParam->Uniforms.ColorFactor
		= glm::f32vec4(FactorR, FactorG, FactorB, FactorA);

	return err;
}

PF_Err SmartRender(
	PF_InData* in_data, PF_OutData* out_data, PF_SmartRenderExtra* extra)
{
	PF_Err err = PF_Err_NONE;

	AEGP_SuiteHandler suites(in_data->pica_basicP);

	PF_EffectWorld* InputLayer  = {};
	PF_EffectWorld* OutputLayer = {};

	// Checkout input/output layers
	ERR(extra->cb->checkout_layer_pixels(
		in_data->effect_ref, Vulkanator::ParamID::Input, &InputLayer));
	ERR(extra->cb->checkout_output(in_data->effect_ref, &OutputLayer));

	if( !OutputLayer || !InputLayer )
		return PF_Err_NONE;

	// Lock global handle
	Vulkanator::GlobalParams* GlobalParam
		= reinterpret_cast<Vulkanator::GlobalParams*>(*in_data->global_data);
	Vulkanator::SequenceParams* SequenceParam
		= reinterpret_cast<Vulkanator::SequenceParams*>(
			*in_data->sequence_data);
	Vulkanator::RenderParams* FrameParam
		= reinterpret_cast<Vulkanator::RenderParams*>(
			extra->input->pre_render_data);

	/////// Get some traits about this render

	// The staging buffer should be the maximum between the size of the Input
	// layer and Output layer High level process: InputLayer->data -memcpy->>>
	// Staging(Vulkan) -vkCmdCopyBufferToImage->>> InputImage(Vulkan) <Render
	// into Output Image, using InputImage> OutputImage
	// -vkCmdCopyImageToBuffer->>> Staging(Vulkan) -memcpy->>> OutputLayer->data
	const std::size_t StagingBufferSize = glm::max(
		InputLayer->height * InputLayer->rowbytes,
		OutputLayer->height * OutputLayer->rowbytes);

	// Test for cache hit
	if( (StagingBufferSize
		 <= SequenceParam->Cache
				.StagingBufferSize) // Can use a subset of the memory
	)
	{
		// Cache hit
		const std::size_t SizeDifference
			= SequenceParam->Cache.StagingBufferSize - StagingBufferSize;

		// We tripped the cache threshold, so we resize it to be smaller
		if( SizeDifference > std::size_t(
				SequenceParam->Cache.StagingBufferSize
				* Vulkanator::SequenceParams::SequenceCache::ShrinkThreshold) )
		{
			std::tie(
				SequenceParam->Cache.StagingBuffer,
				SequenceParam->Cache.StagingBufferMemory)
				= VulkanUtils::AllocateBuffer(
					  GlobalParam->Device.get(), GlobalParam->PhysicalDevice,
					  StagingBufferSize,
					  vk::BufferUsageFlagBits::eTransferDst
						  | vk::BufferUsageFlagBits::eTransferSrc,
					  vk::MemoryPropertyFlagBits::eHostCached
						  | vk::MemoryPropertyFlagBits::eHostCoherent)
					  .value();
			SequenceParam->Cache.StagingBufferSize = StagingBufferSize;
		}
	}
	else
	{
		// Cache miss, recreate buffer
		std::tie(
			SequenceParam->Cache.StagingBuffer,
			SequenceParam->Cache.StagingBufferMemory)
			= VulkanUtils::AllocateBuffer(
				  GlobalParam->Device.get(), GlobalParam->PhysicalDevice,
				  StagingBufferSize,
				  vk::BufferUsageFlagBits::eTransferDst
					  | vk::BufferUsageFlagBits::eTransferSrc,
				  vk::MemoryPropertyFlagBits::eHostCached
					  | vk::MemoryPropertyFlagBits::eHostCoherent)
				  .value();
		SequenceParam->Cache.StagingBufferSize = StagingBufferSize;
	}

	const vk::Format RenderFormat
		= VulkanUtils::DepthToFormat(FrameParam->Uniforms.Depth);

	static constexpr std::array<std::size_t, 3> PixelSizes
		= {sizeof(PF_Pixel8), sizeof(PF_Pixel16), sizeof(PF_Pixel32)};
	const std::size_t PixelSize = PixelSizes.at(FrameParam->Uniforms.Depth);

	// Create GPU-side Input Image
	vk::ImageCreateInfo InputImageInfo;
	InputImageInfo.imageType = vk::ImageType::e2D;
	InputImageInfo.format    = RenderFormat;
	InputImageInfo.extent
		= vk::Extent3D(InputLayer->width, InputLayer->height, 1);
	InputImageInfo.mipLevels   = 1;
	InputImageInfo.arrayLayers = 1;
	InputImageInfo.samples     = vk::SampleCountFlagBits::e1;
	InputImageInfo.tiling      = vk::ImageTiling::eOptimal;
	InputImageInfo.usage
		= vk::ImageUsageFlagBits::eTransferSrc
		| vk::ImageUsageFlagBits::eTransferDst // Will be trasnferring from the
											   // staging buffer into this one
		| vk::ImageUsageFlagBits::eSampled; // Will be sampling from this image
	InputImageInfo.sharingMode   = vk::SharingMode::eExclusive;
	InputImageInfo.initialLayout = vk::ImageLayout::eUndefined;

	if( InputImageInfo == SequenceParam->Cache.InputImageInfoCache )
	{
		// Cache Hit
	}
	else
	{
		// Cache Miss, recreate image
		std::tie(
			SequenceParam->Cache.InputImage,
			SequenceParam->Cache.InputImageMemory)
			= VulkanUtils::AllocateImage(
				  GlobalParam->Device.get(), GlobalParam->PhysicalDevice,
				  InputImageInfo, vk::MemoryPropertyFlagBits::eDeviceLocal)
				  .value();
		SequenceParam->Cache.InputImageInfoCache = InputImageInfo;
	}

	// This provides a mapping between the image contents and the staging buffer
	const vk::BufferImageCopy InputBufferMapping(
		0, std::uint32_t(InputLayer->rowbytes / PixelSize), 0,
		vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1),
		vk::Offset3D(0, 0, 0),
		vk::Extent3D(InputLayer->width, InputLayer->height, 1));

	// Input image view, this is used to create an interpretation of a certain
	// aspect of the image This allows things like having a 2D image array but
	// creating a view around just one of the images
	vk::ImageViewCreateInfo InputImageViewInfo = {};
	InputImageViewInfo.image
		= SequenceParam->Cache.InputImage
			  .get(); // The target image we are making a view of
	InputImageViewInfo.format   = RenderFormat;
	InputImageViewInfo.viewType = vk::ImageViewType::e2D; // This is a 2D image
	// Swizzling of color channels used during reading/sampling
	InputImageViewInfo.components.r     = vk::ComponentSwizzle::eIdentity;
	InputImageViewInfo.components.g     = vk::ComponentSwizzle::eIdentity;
	InputImageViewInfo.components.b     = vk::ComponentSwizzle::eIdentity;
	InputImageViewInfo.components.a     = vk::ComponentSwizzle::eIdentity;
	InputImageViewInfo.subresourceRange = vk::ImageSubresourceRange(
		vk::ImageAspectFlagBits::eColor, // We want the "Color" aspect of the
										 // image
		0, 1,                            // A single mipmap, mipmap 0
		0, 1                             // A single image layer, layer 0
	);

	SequenceParam->Cache.InputImageInfoCache = InputImageInfo;

	vk::UniqueImageView InputImageView = {};
	if( auto ImageViewResult
		= GlobalParam->Device->createImageViewUnique(InputImageViewInfo);
		ImageViewResult.result == vk::Result::eSuccess )
	{
		InputImageView = std::move(ImageViewResult.value);
	}
	else
	{
		// Error creating image view
		return PF_Err_INTERNAL_STRUCT_DAMAGED;
	}

	// Create GPU-side Output Image
	vk::ImageCreateInfo OutputImageInfo;
	OutputImageInfo.imageType = vk::ImageType::e2D;
	OutputImageInfo.format    = RenderFormat;
	OutputImageInfo.extent
		= vk::Extent3D(OutputLayer->width, OutputLayer->height, 1);
	OutputImageInfo.mipLevels   = 1;
	OutputImageInfo.arrayLayers = 1;
	OutputImageInfo.samples     = vk::SampleCountFlagBits::e1;
	OutputImageInfo.tiling      = vk::ImageTiling::eOptimal;
	OutputImageInfo.usage
		= vk::ImageUsageFlagBits::eTransferSrc // Will be transferring from this
											   // image into the staging buffer
		| vk::ImageUsageFlagBits::eColorAttachment; // Will be rendering into
													// this image within a
													// render pass
	OutputImageInfo.sharingMode   = vk::SharingMode::eExclusive;
	OutputImageInfo.initialLayout = vk::ImageLayout::eUndefined;

	if( OutputImageInfo == SequenceParam->Cache.OutputImageInfoCache )
	{
		// Cache Hit
	}
	else
	{
		// Cache Miss, recreate image
		std::tie(
			SequenceParam->Cache.OutputImage,
			SequenceParam->Cache.OutputImageMemory)
			= VulkanUtils::AllocateImage(
				  GlobalParam->Device.get(), GlobalParam->PhysicalDevice,
				  OutputImageInfo, vk::MemoryPropertyFlagBits::eDeviceLocal)
				  .value();
		SequenceParam->Cache.OutputImageInfoCache = OutputImageInfo;
	}

	// This provides a mapping between the image contents and the staging buffer
	const vk::BufferImageCopy OutputBufferMapping(
		0, std::uint32_t(OutputLayer->rowbytes / PixelSize), 0,
		vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1),
		vk::Offset3D(0, 0, 0),
		vk::Extent3D(OutputLayer->width, OutputLayer->height, 1));

	// Output image view, this is used to create an interpretation of a certain
	// aspect of the image This allows things like having a 2D image array but
	// creating a view around just one of the images
	vk::ImageViewCreateInfo OutputImageViewInfo = {};
	OutputImageViewInfo.image
		= SequenceParam->Cache.OutputImage
			  .get(); // The target image we are making a view of
	OutputImageViewInfo.format   = RenderFormat;
	OutputImageViewInfo.viewType = vk::ImageViewType::e2D; // This is a 2D image
	// Swizzling of color channels used during reading/sampling
	OutputImageViewInfo.components.r     = vk::ComponentSwizzle::eIdentity;
	OutputImageViewInfo.components.g     = vk::ComponentSwizzle::eIdentity;
	OutputImageViewInfo.components.b     = vk::ComponentSwizzle::eIdentity;
	OutputImageViewInfo.components.a     = vk::ComponentSwizzle::eIdentity;
	OutputImageViewInfo.subresourceRange = vk::ImageSubresourceRange(
		vk::ImageAspectFlagBits::eColor, // We want the "Color" aspect of the
										 // image
		0, 1,                            // A single mipmap, mipmap 0
		0, 1                             // A single image layer, layer 0
	);

	vk::UniqueImageView OutputImageView = {};
	if( auto ImageViewResult
		= GlobalParam->Device->createImageViewUnique(OutputImageViewInfo);
		ImageViewResult.result == vk::Result::eSuccess )
	{
		OutputImageView = std::move(ImageViewResult.value);
	}
	else
	{
		// Error creating image view
		return PF_Err_INTERNAL_STRUCT_DAMAGED;
	}
	///////

	// Create input image sampler
	vk::SamplerCreateInfo InputImageSamplerInfo = {};
	InputImageSamplerInfo.addressModeU = InputImageSamplerInfo.addressModeV
		= InputImageSamplerInfo.addressModeW
		= vk::SamplerAddressMode::eClampToEdge;
	// Port After Effect's quality setting over into the sampler setting
	switch( in_data->quality )
	{
		// Low quality -> Nearest interpolation
	case PF_Quality_LO:
	{
		InputImageSamplerInfo.magFilter = InputImageSamplerInfo.minFilter
			= vk::Filter::eNearest;
		break;
	}
	// High quality -> Linear interpolation
	case PF_Quality_HI:
	{
		InputImageSamplerInfo.magFilter = InputImageSamplerInfo.minFilter
			= vk::Filter::eLinear;
		break;
	}
	}

	if( auto SamplerResult
		= GlobalParam->Device->createSamplerUnique(InputImageSamplerInfo);
		SamplerResult.result == vk::Result::eSuccess )
	{
		FrameParam->InputImageSampler = std::move(SamplerResult.value);
	}
	else
	{
		// Error creating sampler object
		return PF_Err_INTERNAL_STRUCT_DAMAGED;
	}

	// Write combined image+sampler object into the descriptor set
	// Here, we combine both the sampler and the image, and we state the format
	// that the image will be in by the time this sampler will be in-use, which
	// is ideally "shader read only optimal" immediately after we are done
	// uploading the texture to the GPU
	vk::DescriptorImageInfo InputImageSamplerWrite(
		FrameParam->InputImageSampler.get(), InputImageView.get(),
		vk::ImageLayout::eShaderReadOnlyOptimal);
	// Write the image sampler to the descriptor set
	GlobalParam->Device->updateDescriptorSets(
		{vk::WriteDescriptorSet(
			SequenceParam->DescriptorSet.get(),
			1,                                         // Target Binding,
			0,                                         // Element 0
			1,                                         // Just 1 element
			vk::DescriptorType::eCombinedImageSampler, // Type,
			&InputImageSamplerWrite,                   // Image-write info
			nullptr,                                   // Buffer write info
			nullptr // Texel-buffer write info
			)},
		{});

	// Create Render pass Framebuffer, this maps the Output buffer as a color
	// attachment for a Renderpass to render into You can add more attachments
	// of different formats, but they must all have the same width,height,layers
	// Framebuffers will define the image data that render passes will be able
	// to address in total
	vk::FramebufferCreateInfo OutputFramebufferInfo = {};
	// This is for the framebuffer to know what ~~~compatible~~~ renderpasses
	// will be rendered into it
	// https://www.khronos.org/registry/vulkan/specs/1.2-extensions/html/vkspec.html#renderpass-compatibility
	OutputFramebufferInfo.renderPass
		= GlobalParam->RenderPasses[FrameParam->Uniforms.Depth].get();
	OutputFramebufferInfo.attachmentCount = 1;
	OutputFramebufferInfo.pAttachments    = &OutputImageView.get();

	// Specify the width, height, and layers that the framebuffer image
	// attachments are;
	OutputFramebufferInfo.width  = OutputLayer->width;
	OutputFramebufferInfo.height = OutputLayer->height;
	OutputFramebufferInfo.layers = 1;

	vk::UniqueFramebuffer OutputFramebuffer = {};
	if( auto FramebufferResult
		= GlobalParam->Device->createFramebufferUnique(OutputFramebufferInfo);
		FramebufferResult.result == vk::Result::eSuccess )
	{
		OutputFramebuffer = std::move(FramebufferResult.value);
	}
	else
	{
		// Error creating framebuffer
		return PF_Err_INTERNAL_STRUCT_DAMAGED;
	}

	// Copy Input image data into staging buffer, but keep it mapped, as we will
	// read the output image data from it later too
	void* StagingBufferMapping = nullptr;

	if( auto MapResult = GlobalParam->Device->mapMemory(
			SequenceParam->Cache.StagingBufferMemory.get(), 0, VK_WHOLE_SIZE);
		MapResult.result == vk::Result::eSuccess )
	{
		StagingBufferMapping = MapResult.value;
	}
	else
	{
		// Error mapping staging buffer
		return PF_Err_INTERNAL_STRUCT_DAMAGED;
	}

	// Copy into staging buffer
	std::memcpy(
		StagingBufferMapping, InputLayer->data,
		InputLayer->rowbytes * InputLayer->height);

	if( auto MapResult = GlobalParam->Device->mapMemory(
			SequenceParam->UniformBufferMemory.get(), 0, VK_WHOLE_SIZE);
		MapResult.result == vk::Result::eSuccess )
	{
		std::memcpy(
			MapResult.value, &FrameParam->Uniforms,
			sizeof(FrameParam->Uniforms));
		GlobalParam->Device->unmapMemory(
			SequenceParam->UniformBufferMemory.get());
	}
	else
	{
		// Error mapping staging buffer
		return PF_Err_INTERNAL_STRUCT_DAMAGED;
	}

	// Allocate input and output buffers

	//////////// Render

	SequenceParam->CommandBuffer->reset(
		vk::CommandBufferResetFlagBits::eReleaseResources);

	vk::CommandBufferBeginInfo BeginInfo = {};
	BeginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

	// Render Commands
	vk::CommandBuffer& Cmd = SequenceParam->CommandBuffer.get();
	if( auto BeginResult = Cmd.begin(BeginInfo);
		BeginResult != vk::Result::eSuccess )
	{
		// Error beginning command buffer
		return PF_Err_INTERNAL_STRUCT_DAMAGED;
	}

	{
		////// Upload staging buffer into Input Image

		// Layout transitions, prepare to copy
		// Transfer buffers into images
		Cmd.pipelineBarrier(
			vk::PipelineStageFlagBits::eHost,
			vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlags(), {},
			{// Get staging buffer ready for a read
			 vk::BufferMemoryBarrier(
				 vk::AccessFlags(), vk::AccessFlagBits::eTransferRead,
				 VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
				 SequenceParam->Cache.StagingBuffer.get(), 0u, VK_WHOLE_SIZE)},
			{
				// Get Input Image ready to be written to
				vk::ImageMemoryBarrier(
					vk::AccessFlags(), vk::AccessFlagBits::eTransferWrite,
					vk::ImageLayout::eUndefined,
					vk::ImageLayout::eTransferDstOptimal,
					VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
					SequenceParam->Cache.InputImage.get(),
					vk::ImageSubresourceRange(
						vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)),
			});

		// Upload input image data from staging buffer into Input Image
		Cmd.copyBufferToImage(
			SequenceParam->Cache.StagingBuffer.get(),
			SequenceParam->Cache.InputImage.get(),
			vk::ImageLayout::eTransferDstOptimal, {InputBufferMapping});

		// Layout transitions, copy is complete, ready input image to be sampled
		// from
		Cmd.pipelineBarrier(
			vk::PipelineStageFlagBits::eTransfer,
			vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlags(),
			{}, {},
			{// Input Image is going to be read
			 vk::ImageMemoryBarrier(
				 vk::AccessFlagBits::eTransferWrite,
				 vk::AccessFlagBits::eShaderRead,
				 vk::ImageLayout::eTransferDstOptimal,
				 vk::ImageLayout::eShaderReadOnlyOptimal,
				 VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
				 SequenceParam->Cache.InputImage.get(),
				 vk::ImageSubresourceRange(
					 vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)),
			 // Output Image is going to be written to as a color attachment
			 // within a render pass
			 vk::ImageMemoryBarrier(
				 vk::AccessFlags(), vk::AccessFlagBits::eShaderWrite,
				 vk::ImageLayout::eUndefined,
				 vk::ImageLayout::eColorAttachmentOptimal,
				 VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
				 SequenceParam->Cache.OutputImage.get(),
				 vk::ImageSubresourceRange(
					 vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1))});

		//////// RENDERING COMMANDS HERE

		// Begin Render Pass
		vk::RenderPassBeginInfo BeginInfo = {};
		// Assign our render pass, based on depth
		BeginInfo.renderPass
			= GlobalParam->RenderPasses[FrameParam->Uniforms.Depth].get();

		// Assign our output framebuffer, which has 1 color attachment
		BeginInfo.framebuffer = OutputFramebuffer.get();

		// Rectangular region of the output buffer to render into
		// TODO: we could potentially have a cached layer-sized output image,
		// and only render into a subset of this image using extent_hint if we
		// wanted to. But we use the exact output size for more immediate memory
		// savings
		BeginInfo.renderArea.offset.x = BeginInfo.renderArea.offset.y = 0;
		BeginInfo.renderArea.extent.width  = std::uint32_t(OutputLayer->width);
		BeginInfo.renderArea.extent.height = std::uint32_t(OutputLayer->height);

		// This is the color that we clear the framebuffer with
		// Default clear value is just "0" through out
		static vk::ClearValue ClearValue = {};
		BeginInfo.clearValueCount        = 1;
		BeginInfo.pClearValues           = &ClearValue;

		////////////// Render pass begin
		Cmd.beginRenderPass(BeginInfo, vk::SubpassContents::eInline);

		// Render pass commands here!!!
		// Bind our shader
		Cmd.bindPipeline(
			vk::PipelineBindPoint::eGraphics,
			GlobalParam->RenderPipelines[FrameParam->Uniforms.Depth].get());
		// Bind our Descriptor set
		Cmd.bindDescriptorSets(
			vk::PipelineBindPoint::eGraphics,
			GlobalParam->RenderPipelineLayout.get(), 0,
			{SequenceParam->DescriptorSet.get()}, {});
		// Bind our mesh
		Cmd.bindVertexBuffers(0, {GlobalParam->MeshBuffer.get()}, {0});

		// Set viewport and scissor region for this render, we draw to the
		// entire output buffer
		Cmd.setViewport(
			0, {vk::Viewport(
				   0, 0, glm::f32(OutputLayer->width),
				   glm::f32(OutputLayer->height), 0.0f, 1.0f)});
		Cmd.setScissor(
			0, {vk::Rect2D(
				   {0, 0}, {std::uint32_t(OutputLayer->width),
							std::uint32_t(OutputLayer->height)})});

		// Draw!!
		Cmd.draw(4, 1, 0, 0);

		Cmd.endRenderPass();
		////////////// Render pass end

		////// Download Output Image into staging buffer
		Cmd.pipelineBarrier(
			vk::PipelineStageFlagBits::eColorAttachmentOutput, // Wait for
															   // render pass to
															   // finish writing
			vk::PipelineStageFlagBits::eTransfer, // Get it ready for a read
			vk::DependencyFlags(), {},
			{// Staging buffer ready for a write
			 vk::BufferMemoryBarrier(
				 vk::AccessFlags(), vk::AccessFlagBits::eTransferWrite,
				 VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
				 SequenceParam->Cache.StagingBuffer.get(), 0u, VK_WHOLE_SIZE)},
			{
				//// Output Image ready for a read
				// vk::ImageMemoryBarrier(
				//	vk::AccessFlagBits::eColorAttachmentWrite,
				// vk::AccessFlagBits::eTransferRead,
				//	vk::ImageLayout::eTransferSrcOptimal,
				// vk::ImageLayout::eTransferSrcOptimal,
				// VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
				// OutputImage.get(),
				//	vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor,
				// 0, 1, 0, 1)
				//)
			});
		Cmd.copyImageToBuffer(
			SequenceParam->Cache.OutputImage.get(),
			vk::ImageLayout::eTransferSrcOptimal,
			SequenceParam->Cache.StagingBuffer.get(), {OutputBufferMapping});
	}

	if( auto EndResult = Cmd.end(); EndResult != vk::Result::eSuccess )
	{
		// Error beginning command buffer
		return PF_Err_INTERNAL_STRUCT_DAMAGED;
	}

	// Submit GPU work to queue
	vk::SubmitInfo SubmitInfo     = {};
	SubmitInfo.commandBufferCount = 1;
	SubmitInfo.pCommandBuffers    = &Cmd;

	if( auto SubmitResult
		= GlobalParam->Queue.submit(SubmitInfo, SequenceParam->Fence.get());
		SubmitResult != vk::Result::eSuccess )
	{
		// Error submitting command buffer
		return PF_Err_INTERNAL_STRUCT_DAMAGED;
	}

	// Wait for GPU work to finish
	if( GlobalParam->Device->waitForFences(
			{SequenceParam->Fence.get()}, true, ~0u)
		!= vk::Result::eSuccess )
	{
		// Error waiting on fence
		return PF_Err_INTERNAL_STRUCT_DAMAGED;
	}
	// Reset(unsignal) fence for later re-use
	GlobalParam->Device->resetFences({SequenceParam->Fence.get()});

	//////////// Download output image data into the output layer
	std::memcpy(
		OutputLayer->data, StagingBufferMapping,
		std::size_t(OutputLayer->rowbytes) * OutputLayer->height);
	GlobalParam->Device->unmapMemory(
		SequenceParam->Cache.StagingBufferMemory.get());

	return err;
}

DllExport PF_Err EntryPoint(
	PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[],
	PF_LayerDef* output, void* extra)
{
	try
	{
		switch( cmd )
		{
		case PF_Cmd_ABOUT:
			return About(in_data, out_data, params, output);
		case PF_Cmd_GLOBAL_SETUP:
			return GlobalSetup(in_data, out_data, params, output);
		case PF_Cmd_GLOBAL_SETDOWN:
			return GlobalSetdown(in_data, out_data, params, output);
		case PF_Cmd_SEQUENCE_SETUP:
			return SequenceSetup(in_data, out_data, params, output);
		case PF_Cmd_SEQUENCE_RESETUP:
			return SequenceReSetup(in_data, out_data, params, output);
		case PF_Cmd_SEQUENCE_SETDOWN:
			return SequenceSetdown(in_data, out_data, params, output);
		case PF_Cmd_SEQUENCE_FLATTEN:
			return SequenceFlatten(in_data, out_data, params, output);
		case PF_Cmd_PARAMS_SETUP:
			return ParamsSetup(in_data, out_data, params, output);
		case PF_Cmd_SMART_PRE_RENDER:
			return SmartPreRender(
				in_data, out_data, static_cast<PF_PreRenderExtra*>(extra));
		case PF_Cmd_SMART_RENDER:
			return SmartRender(
				in_data, out_data, static_cast<PF_SmartRenderExtra*>(extra));
		}
	}
	catch( PF_Err& err )
	{
		return err;
	}
	return PF_Err_NONE;
}

// This is to tell vulkan how to interpret per-vertex data

vk::VertexInputBindingDescription& Vulkanator::Vertex::BindingDescription()
{
	static vk::VertexInputBindingDescription Result = {};
	Result.binding                                  = 0; // Binding 0
	Result.stride    = sizeof(Vertex); // Size between one vertex and another
	Result.inputRate = vk::VertexInputRate::eVertex; // Move by "stride" bytes
													 // for each vertex
	return Result;
}

// This will tell vulkan how to interpret each of the fields of the Vertex
// structure and where it should be mapped ot

std::array<vk::VertexInputAttributeDescription, 2>&
	Vulkanator::Vertex::AttributeDescription()
{
	static std::array<vk::VertexInputAttributeDescription, 2>
		AttributeDescriptions = {};

	// Position
	AttributeDescriptions[0].binding  = 0;
	AttributeDescriptions[0].location = 0;
	AttributeDescriptions[0].format   = vk::Format::eR32G32Sfloat;
	AttributeDescriptions[0].offset   = offsetof(Vertex, Position);
	// UV
	AttributeDescriptions[1].binding  = 0;
	AttributeDescriptions[1].location = 1;
	AttributeDescriptions[1].format   = vk::Format::eR32G32Sfloat;
	AttributeDescriptions[1].offset   = offsetof(Vertex, UV);

	return AttributeDescriptions;
}
