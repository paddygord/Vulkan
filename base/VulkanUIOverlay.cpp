/*
* UI overlay class using ImGui
*
* Copyright (C) 2017 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "VulkanUIOverlay.h"
#include "../external/imgui/imgui.h"

namespace vks 
{
    UIOverlay::UIOverlay(const UIOverlayCreateInfo& createInfo) : createInfo(createInfo) {
#if defined(__ANDROID__)		
		if (android::screenDensity >= ACONFIGURATION_DENSITY_XXHIGH) {
			scale = 3.5f;
		}
		else if (android::screenDensity >= ACONFIGURATION_DENSITY_XHIGH) {
			scale = 2.5f;
		}
		else if (android::screenDensity >= ACONFIGURATION_DENSITY_HIGH) {
			scale = 2.0f;
		};
#endif

		// Init ImGui
		// Color scheme
		ImGuiStyle& style = ImGui::GetStyle();
		style.Colors[ImGuiCol_TitleBg] = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
		style.Colors[ImGuiCol_TitleBgActive] = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
		style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(1.0f, 0.0f, 0.0f, 0.1f);
		style.Colors[ImGuiCol_MenuBarBg] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
		style.Colors[ImGuiCol_Header] = ImVec4(0.8f, 0.0f, 0.0f, 0.4f);
		style.Colors[ImGuiCol_HeaderActive] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
		style.Colors[ImGuiCol_HeaderHovered] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
		style.Colors[ImGuiCol_CheckMark] = ImVec4(1.0f, 0.0f, 0.0f, 0.8f);
		// Dimensions
		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = ImVec2(createInfo.size.x, createInfo.size.y);
		io.FontGlobalScale = scale;

		prepareResources();
        if (createInfo.renderPass) {
            renderPass = createInfo.renderPass;
        } else {
            prepareRenderPass();
        }
		preparePipeline();
	}

	/** Free up all Vulkan resources acquired by the UI overlay */
	UIOverlay::~UIOverlay()
	{
		vertexBuffer.destroy();
		indexBuffer.destroy();
        font.destroy();
        context.device.destroyDescriptorSetLayout(descriptorSetLayout);
        context.device.destroyDescriptorPool(descriptorPool);
        context.device.destroyPipelineLayout(pipelineLayout);
        context.device.destroyPipeline(pipeline);
        if (!createInfo.renderPass) {
            context.device.destroyRenderPass(renderPass);
        }
        context.device.freeCommandBuffers(commandPool, cmdBuffers);
        context.device.destroyCommandPool(commandPool);
        context.device.destroyFence(fence);
	}

	/** Prepare all vulkan resources required to render the UI overlay */
	void UIOverlay::prepareResources()
	{
		ImGuiIO& io = ImGui::GetIO();

		// Create font texture
        std::vector<uint8_t> fontData;
        int texWidth, texHeight;
        {
            unsigned char* fontBuffer;
            io.Fonts->GetTexDataAsRGBA32(&fontBuffer, &texWidth, &texHeight);
            vk::DeviceSize uploadSize = texWidth*texHeight * 4 * sizeof(char);
            fontData.resize(uploadSize);
            memcpy(fontData.data(), fontBuffer, uploadSize);
        }

		// Create target image for copy
        vk::ImageCreateInfo imageInfo;
		imageInfo.imageType = vk::ImageType::e2D;
		imageInfo.format = vk::Format::eR8G8B8A8Unorm;
		imageInfo.extent.width = texWidth;
		imageInfo.extent.height = texHeight;
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.usage = vk::ImageUsageFlagBits::eSampled;

        font = context.stageToDeviceImage(imageInfo, fontData);

        // Image view
        vk::ImageViewCreateInfo viewInfo;
        viewInfo.image = font.image;
        viewInfo.viewType = vk::ImageViewType::e2D;
        viewInfo.format = vk::Format::eR8G8B8A8Unorm;
        viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        font.view = context.device.createImageView(viewInfo);

		// Font texture Sampler
		vk::SamplerCreateInfo samplerInfo ;
		samplerInfo.magFilter = vk::Filter::eLinear;
		samplerInfo.minFilter = vk::Filter::eLinear;
		samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
		samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;
        font.sampler = context.device.createSampler(samplerInfo);

		// Command buffer

		vk::CommandPoolCreateInfo cmdPoolInfo;
		cmdPoolInfo.queueFamilyIndex = context.graphicsQueueIndex;
		cmdPoolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        commandPool = context.device.createCommandPool(cmdPoolInfo);

        vk::CommandBufferAllocateInfo cmdBufAllocateInfo;
        cmdBufAllocateInfo.commandBufferCount = createInfo.framebuffers.size();
        cmdBuffers = context.device.allocateCommandBuffers(cmdBufAllocateInfo);

		// Descriptor pool
        vk::DescriptorPoolSize poolSize;
        poolSize.type = vk::DescriptorType::eCombinedImageSampler;
        poolSize.descriptorCount = 1;
        vk::DescriptorPoolCreateInfo descriptorPoolInfo;
        descriptorPoolInfo.maxSets = 2;
        descriptorPoolInfo.poolSizeCount = 1;
        descriptorPoolInfo.pPoolSizes = &poolSize;
        descriptorPool = context.device.createDescriptorPool(descriptorPoolInfo);

		// Descriptor set layout
        vk::DescriptorSetLayoutBinding setLayoutBinding;
        setLayoutBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        setLayoutBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;
        vk::DescriptorSetLayoutCreateInfo descriptorLayout;
        descriptorLayout.bindingCount = 1;
        descriptorLayout.pBindings = &setLayoutBinding;
        descriptorSetLayout = context.device.createDescriptorSetLayout(descriptorLayout);

		// Descriptor set
        vk::DescriptorSetAllocateInfo allocInfo;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.pSetLayouts = &descriptorSetLayout;
        allocInfo.descriptorSetCount = 1;
        descriptorSet = context.device.allocateDescriptorSets(allocInfo)[0];

        
        vk::DescriptorImageInfo fontDescriptor;
        fontDescriptor.imageView = font.view;
        fontDescriptor.sampler = font.sampler;
        fontDescriptor.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        vk::WriteDescriptorSet writeDescriptorSet;
        writeDescriptorSet.descriptorType = vk::DescriptorType::eCombinedImageSampler;
			// initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &fontDescriptor)
        context.device.updateDescriptorSets(writeDescriptorSet);

		// Pipeline cache
		VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
		pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
		VK_CHECK_RESULT(vkCreatePipelineCache(createInfo.device->logicalDevice, &pipelineCacheCreateInfo, nullptr, &pipelineCache));

		// Pipeline layout
		// Push constants for UI rendering parameters
		VkPushConstantRange pushConstantRange = initializers::pushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, sizeof(PushConstBlock), 0);
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);
		pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
		pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;
		VK_CHECK_RESULT(vkCreatePipelineLayout(createInfo.device->logicalDevice, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));

		// Command buffer execution fence
		VkFenceCreateInfo fenceCreateInfo = initializers::fenceCreateInfo();
		VK_CHECK_RESULT(vkCreateFence(createInfo.device->logicalDevice, &fenceCreateInfo, nullptr, &fence));
	}

	/** Prepare a separate pipeline for the UI overlay rendering decoupled from the main application */
	void UIOverlay::preparePipeline()
	{
		// Setup graphics pipeline for UI rendering
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
			initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);

		VkPipelineRasterizationStateCreateInfo rasterizationState =
			initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);

		// Enable blending
		VkPipelineColorBlendAttachmentState blendAttachmentState{};
		blendAttachmentState.blendEnable = VK_TRUE;
		blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
		blendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		blendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;

		std::vector<VkPipelineColorBlendAttachmentState> blendStates(createInfo.attachmentCount);
		for (uint32_t i = 0; i < createInfo.attachmentCount; i++) {
			blendStates[i].blendEnable = VK_TRUE;
			blendStates[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			blendStates[i].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			blendStates[i].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			blendStates[i].colorBlendOp = VK_BLEND_OP_ADD;
			blendStates[i].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			blendStates[i].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			blendStates[i].alphaBlendOp = VK_BLEND_OP_ADD;
		}

		VkPipelineColorBlendStateCreateInfo colorBlendState =
			initializers::pipelineColorBlendStateCreateInfo(static_cast<uint32_t>(blendStates.size()), blendStates.data());

		VkPipelineDepthStencilStateCreateInfo depthStencilState =
			initializers::pipelineDepthStencilStateCreateInfo(VK_FALSE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL);

		VkPipelineViewportStateCreateInfo viewportState =
			initializers::pipelineViewportStateCreateInfo(1, 1, 0);

		VkPipelineMultisampleStateCreateInfo multisampleState =
			initializers::pipelineMultisampleStateCreateInfo(createInfo.rasterizationSamples);

		std::vector<VkDynamicState> dynamicStateEnables = {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR
		};
		VkPipelineDynamicStateCreateInfo dynamicState =
			initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);

		VkGraphicsPipelineCreateInfo pipelineCreateInfo = initializers::pipelineCreateInfo(pipelineLayout, renderPass);

		pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
		pipelineCreateInfo.pRasterizationState = &rasterizationState;
		pipelineCreateInfo.pColorBlendState = &colorBlendState;
		pipelineCreateInfo.pMultisampleState = &multisampleState;
		pipelineCreateInfo.pViewportState = &viewportState;
		pipelineCreateInfo.pDepthStencilState = &depthStencilState;
		pipelineCreateInfo.pDynamicState = &dynamicState;
		pipelineCreateInfo.stageCount = static_cast<uint32_t>(createInfo.shaders.size());
		pipelineCreateInfo.pStages = createInfo.shaders.data();

		// Vertex bindings an attributes based on ImGui vertex definition
		std::vector<VkVertexInputBindingDescription> vertexInputBindings = {
			initializers::vertexInputBindingDescription(0, sizeof(ImDrawVert), VK_VERTEX_INPUT_RATE_VERTEX),
		};
		std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
			initializers::vertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert, pos)),	// Location 0: Position
			initializers::vertexInputAttributeDescription(0, 1, VK_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert, uv)),	// Location 1: UV
			initializers::vertexInputAttributeDescription(0, 2, VK_FORMAT_R8G8B8A8_UNORM, offsetof(ImDrawVert, col)),	// Location 0: Color
		};
		VkPipelineVertexInputStateCreateInfo vertexInputState = initializers::pipelineVertexInputStateCreateInfo();
		vertexInputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexInputBindings.size());
		vertexInputState.pVertexBindingDescriptions = vertexInputBindings.data();
		vertexInputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
		vertexInputState.pVertexAttributeDescriptions = vertexInputAttributes.data();

		pipelineCreateInfo.pVertexInputState = &vertexInputState;

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(createInfo.device->logicalDevice, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipeline));
	}

	/** Prepare a separate render pass for rendering the UI as an overlay */
	void UIOverlay::prepareRenderPass()
	{
		VkAttachmentDescription attachments[2] = {};

		// Color attachment
		attachments[0].format = createInfo.colorformat;
		attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[0].initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		// Depth attachment
		attachments[1].format = createInfo.depthformat;
		attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference colorReference = {};
		colorReference.attachment = 0;
		colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentReference depthReference = {};
		depthReference.attachment = 1;
		depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkSubpassDependency subpassDependencies[2] = {};

		// Transition from final to initial (VK_SUBPASS_EXTERNAL refers to all commmands executed outside of the actual renderpass)
		subpassDependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		subpassDependencies[0].dstSubpass = 0;
		subpassDependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		subpassDependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		subpassDependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		subpassDependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		subpassDependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		// Transition from initial to final
		subpassDependencies[1].srcSubpass = 0;
		subpassDependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		subpassDependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		subpassDependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		subpassDependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		subpassDependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		subpassDependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		VkSubpassDescription subpassDescription = {};
		subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescription.flags = 0;
		subpassDescription.inputAttachmentCount = 0;
		subpassDescription.pInputAttachments = NULL;
		subpassDescription.colorAttachmentCount = 1;
		subpassDescription.pColorAttachments = &colorReference;
		subpassDescription.pResolveAttachments = NULL;
		subpassDescription.pDepthStencilAttachment = &depthReference;
		subpassDescription.preserveAttachmentCount = 0;
		subpassDescription.pPreserveAttachments = NULL;

		VkRenderPassCreateInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.pNext = NULL;
		renderPassInfo.attachmentCount = 2;
		renderPassInfo.pAttachments = attachments;
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpassDescription;
		renderPassInfo.dependencyCount = 2;
		renderPassInfo.pDependencies = subpassDependencies;

		VK_CHECK_RESULT(vkCreateRenderPass(createInfo.device->logicalDevice, &renderPassInfo, nullptr, &renderPass));
	}

	/** Update the command buffers to reflect UI changes */
	void UIOverlay::updateCommandBuffers()
	{
		VkCommandBufferBeginInfo cmdBufInfo = initializers::commandBufferBeginInfo();

		VkRenderPassBeginInfo renderPassBeginInfo = initializers::renderPassBeginInfo();
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.extent.width = createInfo.width;
		renderPassBeginInfo.renderArea.extent.height = createInfo.height;
		renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(createInfo.clearValues.size());
		renderPassBeginInfo.pClearValues = createInfo.clearValues.data();

		ImGuiIO& io = ImGui::GetIO();

		for (size_t i = 0; i < cmdBuffers.size(); ++i) {
			renderPassBeginInfo.framebuffer = createInfo.framebuffers[i];

			VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffers[i], &cmdBufInfo));

			if (debugmarker::active) {
				debugmarker::beginRegion(cmdBuffers[i], "UI overlay", glm::vec4(1.0f, 0.94f, 0.3f, 1.0f));
			}

			vkCmdBeginRenderPass(cmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			vkCmdBindPipeline(cmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
			vkCmdBindDescriptorSets(cmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);

			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindVertexBuffers(cmdBuffers[i], 0, 1, &vertexBuffer.buffer, offsets);
			vkCmdBindIndexBuffer(cmdBuffers[i], indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT16);

			VkViewport viewport = initializers::viewport(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, 0.0f, 1.0f);
			vkCmdSetViewport(cmdBuffers[i], 0, 1, &viewport);

			VkRect2D scissor = initializers::rect2D((int32_t)ImGui::GetIO().DisplaySize.x, (int32_t)ImGui::GetIO().DisplaySize.y, 0, 0);
			vkCmdSetScissor(cmdBuffers[i], 0, 1, &scissor);

			// UI scale and translate via push constants
			pushConstBlock.scale = glm::vec2(2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y);
			pushConstBlock.translate = glm::vec2(-1.0f);
			vkCmdPushConstants(cmdBuffers[i], pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstBlock), &pushConstBlock);

			// Render commands
			ImDrawData* imDrawData = ImGui::GetDrawData();
			int32_t vertexOffset = 0;
			int32_t indexOffset = 0;
			for (int32_t j = 0; j < imDrawData->CmdListsCount; j++) {
				const ImDrawList* cmd_list = imDrawData->CmdLists[j];
				for (int32_t k = 0; k < cmd_list->CmdBuffer.Size; k++) {
					const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[k];
					VkRect2D scissorRect;
					scissorRect.offset.x = std::max((int32_t)(pcmd->ClipRect.x), 0);
					scissorRect.offset.y = std::max((int32_t)(pcmd->ClipRect.y), 0);
					scissorRect.extent.width = (uint32_t)(pcmd->ClipRect.z - pcmd->ClipRect.x);
					scissorRect.extent.height = (uint32_t)(pcmd->ClipRect.w - pcmd->ClipRect.y);
					vkCmdSetScissor(cmdBuffers[i], 0, 1, &scissorRect);
					vkCmdDrawIndexed(cmdBuffers[i], pcmd->ElemCount, 1, indexOffset, vertexOffset, 0);
					indexOffset += pcmd->ElemCount;
				}
				vertexOffset += cmd_list->VtxBuffer.Size;
			}

			// Add empty subpasses if requested
			if (createInfo.subpassCount > 1) {
				for (uint32_t j = 1; j < createInfo.subpassCount; j++) {
					vkCmdNextSubpass(cmdBuffers[i], VK_SUBPASS_CONTENTS_INLINE);
				}
			}

			vkCmdEndRenderPass(cmdBuffers[i]);

			if (debugmarker::active) {
				debugmarker::endRegion(cmdBuffers[i]);
			}

			VK_CHECK_RESULT(vkEndCommandBuffer(cmdBuffers[i]));
		}
	}

	/** Update vertex and index buffer containing the imGui elements when required */
	void UIOverlay::update()
	{
		ImDrawData* imDrawData = ImGui::GetDrawData();
		bool updateCmdBuffers = false;

		if (!imDrawData) { return; };

		// Note: Alignment is done inside buffer creation
		VkDeviceSize vertexBufferSize = imDrawData->TotalVtxCount * sizeof(ImDrawVert);
		VkDeviceSize indexBufferSize = imDrawData->TotalIdxCount * sizeof(ImDrawIdx);

		// Update buffers only if vertex or index count has been changed compared to current buffer size

		// Vertex buffer
		if ((vertexBuffer.buffer == VK_NULL_HANDLE) || (vertexCount != imDrawData->TotalVtxCount)) {
			vertexBuffer.unmap();
			vertexBuffer.destroy();
			VK_CHECK_RESULT(createInfo.device->createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &vertexBuffer, vertexBufferSize));
			vertexCount = imDrawData->TotalVtxCount;
			vertexBuffer.unmap();
			vertexBuffer.map();
			updateCmdBuffers = true;
		}

		// Index buffer
		VkDeviceSize indexSize = imDrawData->TotalIdxCount * sizeof(ImDrawIdx);
		if ((indexBuffer.buffer == VK_NULL_HANDLE) || (indexCount < imDrawData->TotalIdxCount)) {
			indexBuffer.unmap();
			indexBuffer.destroy();
			VK_CHECK_RESULT(createInfo.device->createBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &indexBuffer, indexBufferSize));
			indexCount = imDrawData->TotalIdxCount;
			indexBuffer.map();
			updateCmdBuffers = true;
		}

		// Upload data
		ImDrawVert* vtxDst = (ImDrawVert*)vertexBuffer.mapped;
		ImDrawIdx* idxDst = (ImDrawIdx*)indexBuffer.mapped;

		for (int n = 0; n < imDrawData->CmdListsCount; n++) {
			const ImDrawList* cmd_list = imDrawData->CmdLists[n];
			memcpy(vtxDst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
			memcpy(idxDst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
			vtxDst += cmd_list->VtxBuffer.Size;
			idxDst += cmd_list->IdxBuffer.Size;
		}

		// Flush to make writes visible to GPU
		vertexBuffer.flush();
		indexBuffer.flush();

		if (updateCmdBuffers) {
			updateCommandBuffers();
		}
	}

	void UIOverlay::resize(const uvec2& size, const std::vector<VkFramebuffer>& framebuffers)
	{
		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = ImVec2((float)(size.x), (float)(size.y));
		createInfo.size = size;
		createInfo.framebuffers = framebuffers;
		updateCommandBuffers();
	}

	/** Submit the overlay command buffers to a queue */
	void UIOverlay::submit(VkQueue queue, uint32_t bufferindex, VkSubmitInfo submitInfo)
	{
		if (!visible) {
			return;
		}

		submitInfo.pCommandBuffers = &cmdBuffers[bufferindex];
		submitInfo.commandBufferCount = 1;

		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, fence));

		VK_CHECK_RESULT(vkWaitForFences(createInfo.device->logicalDevice, 1, &fence, VK_TRUE, UINT64_MAX));
		VK_CHECK_RESULT(vkResetFences(createInfo.device->logicalDevice, 1, &fence));
	}

	bool UIOverlay::header(const char *caption)
	{
		return ImGui::CollapsingHeader(caption, ImGuiTreeNodeFlags_DefaultOpen);
	}

	bool UIOverlay::checkBox(const char *caption, bool *value)
	{
		return ImGui::Checkbox(caption, value);
	}

	bool UIOverlay::checkBox(const char *caption, int32_t *value)
	{
		bool val = (*value == 1);
		bool res = ImGui::Checkbox(caption, &val);
		*value = val;
		return res;
	}

	bool UIOverlay::inputFloat(const char *caption, float *value, float step, uint32_t precision)
	{
		return ImGui::InputFloat(caption, value, step, step * 10.0f, precision);
	}

	bool UIOverlay::sliderFloat(const char* caption, float* value, float min, float max)
	{
		return ImGui::SliderFloat(caption, value, min, max);
	}

	bool UIOverlay::sliderInt(const char* caption, int32_t* value, int32_t min, int32_t max)
	{
		return ImGui::SliderInt(caption, value, min, max);
	}

	bool UIOverlay::comboBox(const char *caption, int32_t *itemindex, std::vector<std::string> items)
	{
		if (items.empty()) {
			return false;
		}
		std::vector<const char*> charitems;
		charitems.reserve(items.size());
		for (size_t i = 0; i < items.size(); i++) {
			charitems.push_back(items[i].c_str());
		}
		uint32_t itemCount = static_cast<uint32_t>(charitems.size());
		return ImGui::Combo(caption, itemindex, &charitems[0], itemCount, itemCount);
	}

	bool UIOverlay::button(const char *caption)
	{
		return ImGui::Button(caption);
	}

	void UIOverlay::text(const char *formatstr, ...)
	{
		va_list args;
		va_start(args, formatstr);
		ImGui::TextV(formatstr, args);
		va_end(args);
	}
}