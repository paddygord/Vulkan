/*
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Based on code from the Apple Metal/OpenGL interop example:
 * https://developer.apple.com/documentation/metal/mixing_metal_and_opengl_rendering_in_a_view
 *
 */

#ifndef vks_macos_h
#define vks_macos_h

#include <vulkan/vulkan.h>

void InitSharedTextures(VkInstance instance, VkPhysicalDevice vkPhysicalDevice);
void* CreateSharedTexture(VkDevice vkDevice, uint32_t width, uint32_t height, VkFormat format);
uint32_t GetSharedGLTexture(void* sharedTexture);
VkImage GetSharedVkImage(void* sharedTexture);
void DestroySharedTexture(void* sharedTexture);

#endif
