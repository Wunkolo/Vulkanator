#pragma once

// Override preprocessor definitions like `VULKAN_HPP_ASSERT` to provide
// vulkan-hpp your own assertion function

// Rather than using exceptions, we use error code results so we can
// specially-handle specific types of vulkan errors;
#define VULKAN_HPP_NO_EXCEPTIONS

// Used to allow aggregate initialization for structs
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS

#include <vulkan/vulkan.hpp>