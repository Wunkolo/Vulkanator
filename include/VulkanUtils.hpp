#pragma once

#include <cstddef>
#include <cstdint>

#include <optional>
#include <span>
#include <tuple>

#include "VulkanConfig.hpp"
#include "vulkan/vulkan.hpp"

namespace VulkanUtils
{
// Converts depth to the appropriately mapped Vulkan format
// 0: eR8G8B8A8Unorm,
//
// While this is the correct datatype, remember that After Effects uses
// PF_MAX_CHAN16 rather than 0xFFFF as "white" so the maximum color value is
// actually `32768`(0x8000), not `65535`(0xFFFF). So be sure to update your
// shaders accordingly 1: eR16G16B16A16Unorm,
//
// 2: eR32G32B32A32Sfloat,
constexpr std::array<vk::Format, 3> RenderFormats = {
	vk::Format::eR8G8B8A8Unorm,
	vk::Format::eR16G16B16A16Unorm,
	vk::Format::eR32G32B32A32Sfloat,
};
inline constexpr vk::Format DepthToFormat(std::size_t Depth)
{
	return RenderFormats.at(Depth);
}

// Find memory type index of the specified criteria for the specified
// physical device
// Returns -1 if none found
std::int32_t FindMemoryTypeIndex(
	const vk::PhysicalDevice& PhysicalDevice, std::uint32_t MemoryTypeMask,
	vk::MemoryPropertyFlags Properties,
	vk::MemoryPropertyFlags ExcludeProperties
	= vk::MemoryPropertyFlagBits::eProtected);

// Allocations
std::optional<vk::UniqueDeviceMemory> AllocateDeviceMemory(
	vk::Device Device, std::size_t Size, std::uint32_t MemoryTypeIndex,
	vk::Buffer DedicatedBuffer = vk::Buffer(),
	vk::Image  DedicatedImage  = vk::Image());

std::optional<std::tuple<vk::UniqueBuffer, vk::UniqueDeviceMemory>>
	AllocateBuffer(
		vk::Device Device, vk::PhysicalDevice PhysicalDevice, std::size_t Size,
		vk::BufferUsageFlags Usage, vk::MemoryPropertyFlags Properties,
		vk::MemoryPropertyFlags ExcludeProperties
		= vk::MemoryPropertyFlagBits::eProtected,
		vk::SharingMode Sharing = vk::SharingMode::eExclusive);

std::optional<std::tuple<vk::UniqueImage, vk::UniqueDeviceMemory>>
	AllocateImage(
		vk::Device Device, vk::PhysicalDevice PhysicalDevice,
		vk::ImageCreateInfo NewImageInfo, vk::MemoryPropertyFlags Properties,
		vk::MemoryPropertyFlags ExcludeProperties
		= vk::MemoryPropertyFlagBits::eProtected);

std::optional<vk::UniqueShaderModule>
	LoadShaderModule(const vk::Device& Device, std::span<const std::byte> Code);

vk::MemoryHeap GetLargestPhysicalDeviceHeap(
	const vk::PhysicalDevice& PhysicalDevice,
	vk::MemoryHeapFlags       Flags = vk::MemoryHeapFlagBits::eDeviceLocal);

// Debug callback
VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessageCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT      MessageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT             MessageType,
	const VkDebugUtilsMessengerCallbackDataEXT* CallbackData, void* UserData);
} // namespace VulkanUtils