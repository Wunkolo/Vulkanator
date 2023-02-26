#include "VulkanUtils.hpp"
#include "vulkan/vulkan.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace VulkanUtils
{

std::int32_t FindMemoryTypeIndex(
	const vk::PhysicalDevice& PhysicalDevice, std::uint32_t MemoryTypeMask,
	vk::MemoryPropertyFlags Properties,
	vk::MemoryPropertyFlags ExcludeProperties
)
{
	const vk::PhysicalDeviceMemoryProperties DeviceMemoryProperties
		= PhysicalDevice.getMemoryProperties();
	// Iterate the physical device's memory types until we find a match
	for( std::size_t i = 0; i < DeviceMemoryProperties.memoryTypeCount; i++ )
	{
		if(
			// Is within memory type mask
			(((MemoryTypeMask >> i) & 0b1) == 0b1) &&
			// Has property flags
			(DeviceMemoryProperties.memoryTypes[i].propertyFlags & Properties)
				== Properties
			&&
			// None of the excluded properties are enabled
			!(DeviceMemoryProperties.memoryTypes[i].propertyFlags
			  & ExcludeProperties) )
		{
			return static_cast<std::uint32_t>(i);
		}
	}

	return -1;
}

std::optional<vk::UniqueDeviceMemory> AllocateDeviceMemory(
	vk::Device Device, std::size_t Size, std::uint32_t MemoryTypeIndex,
	vk::Buffer DedicatedBuffer, vk::Image DedicatedImage
)
{
	vk::StructureChain<vk::MemoryAllocateInfo, vk::MemoryDedicatedAllocateInfo>
		AllocSettings;

	auto& AllocInfo     = AllocSettings.get<vk::MemoryAllocateInfo>();
	auto& AllocDediInfo = AllocSettings.get<vk::MemoryDedicatedAllocateInfo>();

	AllocInfo.allocationSize  = Size;
	AllocInfo.memoryTypeIndex = MemoryTypeIndex;

	AllocDediInfo.buffer = DedicatedBuffer;
	AllocDediInfo.image  = DedicatedImage;

	vk::UniqueDeviceMemory NewDeviceMemory;

	if( auto AllocResult = Device.allocateMemoryUnique(AllocInfo);
		AllocResult.result == vk::Result::eSuccess )
	{
		NewDeviceMemory = std::move(AllocResult.value);
	}
	else
	{
		// Error allocating device memory
		return std::nullopt;
	}

	return NewDeviceMemory;
}

std::optional<std::tuple<vk::UniqueBuffer, vk::UniqueDeviceMemory>>
	AllocateBuffer(
		vk::Device Device, vk::PhysicalDevice PhysicalDevice, std::size_t Size,
		vk::BufferUsageFlags Usage, vk::MemoryPropertyFlags Properties,
		vk::MemoryPropertyFlags ExcludeProperties, vk::SharingMode Sharing
	)
{
	// Create the buffer object
	vk::BufferCreateInfo NewBufferInfo = {};
	NewBufferInfo.size                 = Size;
	NewBufferInfo.usage                = Usage;
	NewBufferInfo.sharingMode          = Sharing;

	vk::UniqueBuffer NewBuffer = {};

	if( auto BufferResult = Device.createBufferUnique(NewBufferInfo);
		BufferResult.result == vk::Result::eSuccess )
	{
		NewBuffer = std::move(BufferResult.value);
	}
	else
	{
		// Error creating buffer
		return std::nullopt;
	}

	// Get buffer memory requirements
	const vk::MemoryRequirements NewBufferRequirements
		= Device.getBufferMemoryRequirements(NewBuffer.get());

	const auto BufferMemoryIndex = FindMemoryTypeIndex(
		PhysicalDevice, NewBufferRequirements.memoryTypeBits, Properties,
		ExcludeProperties
	);

	if( BufferMemoryIndex < 0 )
		return std::nullopt;

	vk::UniqueDeviceMemory NewBufferDeviceMemory{};
	if( auto NewDeviceMemory = AllocateDeviceMemory(
			Device, NewBufferRequirements.size, BufferMemoryIndex,
			NewBuffer.get(), vk::Image()
		);
		NewDeviceMemory.has_value() )
	{
		NewBufferDeviceMemory = std::move(NewDeviceMemory.value());
	}
	else
	{
		return std::nullopt; // Error allocating device memory
	}

	if( auto BindResult = Device.bindBufferMemory(
			NewBuffer.get(), NewBufferDeviceMemory.get(), 0
		);
		BindResult != vk::Result::eSuccess )
	{
		// Error binding buffer object to device memory
		return std::nullopt;
	}

	return std::make_tuple(
		std::move(NewBuffer), std::move(NewBufferDeviceMemory)
	);
}

std::optional<std::tuple<vk::UniqueImage, vk::UniqueDeviceMemory>>
	AllocateImage(
		vk::Device Device, vk::PhysicalDevice PhysicalDevice,
		vk::ImageCreateInfo NewImageInfo, vk::MemoryPropertyFlags Properties,
		vk::MemoryPropertyFlags ExcludeProperties
	)
{
	vk::UniqueImage NewImage = {};

	if( auto ImageResult = Device.createImageUnique(NewImageInfo);
		ImageResult.result == vk::Result::eSuccess )
	{

		NewImage = std::move(ImageResult.value);
	}
	else
	{
		// Error creating image
		return std::nullopt;
	}

	// Get image memory requirements
	const vk::MemoryRequirements NewImageRequirements
		= Device.getImageMemoryRequirements(NewImage.get());

	const auto ImageMemoryIndex = FindMemoryTypeIndex(
		PhysicalDevice, NewImageRequirements.memoryTypeBits, Properties,
		ExcludeProperties
	);

	if( ImageMemoryIndex < 0 )
	{
		return std::nullopt; // Unable to find suitable memory index for buffer
	}

	vk::UniqueDeviceMemory NewImageDeviceMemory{};
	if( auto NewDeviceMemory = AllocateDeviceMemory(
			Device, NewImageRequirements.size, ImageMemoryIndex, vk::Buffer(),
			NewImage.get() // ShouldDedicate ? NewImage.get() : vk::Image()
		) )
	{
		NewImageDeviceMemory = std::move(NewDeviceMemory.value());
	}
	else
	{
		return std::nullopt; // Error allocating device memory
	}

	if( auto BindResult
		= Device.bindImageMemory(NewImage.get(), NewImageDeviceMemory.get(), 0);
		BindResult != vk::Result::eSuccess )
	{
		// Error binding image object to device memory
		return std::nullopt;
	}
	return std::make_tuple(
		std::move(NewImage), std::move(NewImageDeviceMemory)
	);
}

std::optional<vk::UniqueShaderModule>
	LoadShaderModule(const vk::Device& Device, std::span<const std::byte> Code)
{
	// Must be greater than, and must be a multiple of 4
	if( !Code.size() || (Code.size() % 4 != 0) )
	{
		return std::nullopt;
	}

	vk::ShaderModuleCreateInfo ShaderModuleInfo = {};
	ShaderModuleInfo.codeSize                   = Code.size();
	ShaderModuleInfo.pCode
		= reinterpret_cast<const std::uint32_t*>(Code.data());

	vk::UniqueShaderModule ShaderModule = {};
	if( auto ShaderModuleResult
		= Device.createShaderModuleUnique(ShaderModuleInfo);
		ShaderModuleResult.result == vk::Result::eSuccess )
	{
		ShaderModule = std::move(ShaderModuleResult.value);
	}
	else
	{
		// Error creating shader module
		return std::nullopt;
	}
	return ShaderModule;
}

vk::MemoryHeap GetLargestPhysicalDeviceHeap(
	const vk::PhysicalDevice& PhysicalDevice, vk::MemoryHeapFlags Flags
)
{
	const vk::PhysicalDeviceMemoryProperties PhysicalDeviceMemoryProperties
		= PhysicalDevice.getMemoryProperties();
	vk::DeviceSize MaxSize   = 0;
	std::size_t    HeapIndex = ~std::size_t(0);
	for( std::size_t i = 0; i < PhysicalDeviceMemoryProperties.memoryHeapCount;
		 ++i )
	{
		if( (PhysicalDeviceMemoryProperties.memoryHeaps[i].flags & Flags)
			== Flags )
		{
			if( PhysicalDeviceMemoryProperties.memoryHeaps[i].size > MaxSize )
			{
				MaxSize   = PhysicalDeviceMemoryProperties.memoryHeaps[i].size;
				HeapIndex = i;
			}
		}
	}
	return PhysicalDeviceMemoryProperties.memoryHeaps[HeapIndex];
}

// This function will be called whenever the Vulkan backend has something to say
// about what you are doing How ever you want to handle this, implement it here.
// For now, I put an "ASSERT" whenever there is a warning or error
VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessageCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT      MessageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT             MessageType,
	const VkDebugUtilsMessengerCallbackDataEXT* CallbackData, void* UserData
)
{
	switch( vk::DebugUtilsMessageSeverityFlagBitsEXT(MessageSeverity) )
	{
	case vk::DebugUtilsMessageSeverityFlagBitsEXT::eError:
	case vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning:
	{
		// Something bad happened! Check message!
		const char* Message = CallbackData->pMessage;
		assert(0);
	}
	case vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo:
	case vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose:
	{
		break;
	}
	}
	return VK_FALSE;
}
} // namespace VulkanUtils
