#include "vulkanContext.hpp"

using namespace vkx;

thread_local vk::CommandPool Context::s_cmdPool;
