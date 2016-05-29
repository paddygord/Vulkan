/*
* Vulkan Example - CPU based fire particle system 
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "vulkanexamplebase.h"

#define VERTEX_BUFFER_BIND_ID 0
#define ENABLE_VALIDATION false
#define PARTICLE_COUNT 512
#define PARTICLE_SIZE 10.0f

#define FLAME_RADIUS 8.0f

#define PARTICLE_TYPE_FLAME 0
#define PARTICLE_TYPE_SMOKE 1

struct Particle {
	glm::vec4 pos;
	glm::vec4 color;
	float alpha;
	float size;
	float rotation;
	uint32_t type;
	// Attributes not used in shader
	glm::vec4 vel;
	float rotationSpeed;
};

// Vertex layout for this example
std::vector<vkMeshLoader::VertexLayout> vertexLayout =
{
	vkMeshLoader::VERTEX_LAYOUT_POSITION,
	vkMeshLoader::VERTEX_LAYOUT_UV,
	vkMeshLoader::VERTEX_LAYOUT_NORMAL,
	vkMeshLoader::VERTEX_LAYOUT_TANGENT,
	vkMeshLoader::VERTEX_LAYOUT_BITANGENT
};

class VulkanExample : public VulkanExampleBase
{
public:
	struct {
		struct {
			vkTools::VulkanTexture smoke;
			vkTools::VulkanTexture fire;
			// We use a custom sampler to change some sampler
			// attributes required for rotation the uv coordinates
			// inside the shader for alpha blended textures
			vk::Sampler sampler;
		} particles;
		struct {
			vkTools::VulkanTexture colorMap;
			vkTools::VulkanTexture normalMap;
		} floor;
	} textures;

	struct {
		vkMeshLoader::Mesh environment;
	} meshes;

	glm::vec3 emitterPos = glm::vec3(0.0f, -FLAME_RADIUS + 2.0f, 0.0f);
	glm::vec3 minVel = glm::vec3(-3.0f, 0.5f, -3.0f);
	glm::vec3 maxVel = glm::vec3(3.0f, 7.0f, 3.0f);

	struct {
		vk::Buffer buffer;
		vk::DeviceMemory memory;
		// Store the mapped address of the particle data for reuse
		void *mappedMemory;
		// Size of the particle buffer in bytes
		size_t size;
		vk::PipelineVertexInputStateCreateInfo inputState;
		std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
		std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
	} particles;

	struct {
		vkTools::UniformData fire;
		vkTools::UniformData environment;
	} uniformData;

	struct {
		glm::mat4 projection;
		glm::mat4 model;
		glm::vec2 viewportDim;
		float pointSize = PARTICLE_SIZE;
	} uboVS;

	struct {
		glm::mat4 projection;
		glm::mat4 model;
		glm::mat4 normal;
		glm::vec4 lightPos = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
		glm::vec4 cameraPos;
	} uboEnv;

	struct {
		vk::Pipeline particles;
		vk::Pipeline environment;
	} pipelines;

	vk::PipelineLayout pipelineLayout;
	vk::DescriptorSet descriptorSet;
	vk::DescriptorSetLayout descriptorSetLayout;

	std::vector<Particle> particleBuffer;

	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
		zoom = -90.0f;
		rotation = { -15.0f, 45.0f, 0.0f };
		title = "Vulkan Example - Particle system";
		zoomSpeed *= 1.5f;
		timerSpeed *= 8.0f;
		srand(time(NULL));
	}

	~VulkanExample()
	{
		// Clean up used Vulkan resources 
		// Note : Inherited destructor cleans up resources stored in base class

		textureLoader->destroyTexture(textures.particles.smoke);
		textureLoader->destroyTexture(textures.particles.fire);
		textureLoader->destroyTexture(textures.floor.colorMap);
		textureLoader->destroyTexture(textures.floor.normalMap);

		device.destroyPipeline(pipelines.particles, nullptr);
		device.destroyPipeline(pipelines.environment, nullptr);

		device.destroyPipelineLayout(pipelineLayout, nullptr);
		device.destroyDescriptorSetLayout(descriptorSetLayout, nullptr);

		device.unmapMemory(particles.memory);
		device.destroyBuffer(particles.buffer, nullptr);
		device.freeMemory(particles.memory, nullptr);

		device.destroyBuffer(uniformData.fire.buffer, nullptr);
		device.freeMemory(uniformData.fire.memory, nullptr);

		vkMeshLoader::freeMeshBufferResources(device, &meshes.environment.buffers);

		device.destroySampler(textures.particles.sampler, nullptr);
	}

	void buildCommandBuffers()
	{
		vk::CommandBufferBeginInfo cmdBufInfo;

		vk::ClearValue clearValues[2];
		clearValues[0].color = defaultClearColor;
		clearValues[0].color = { std::array<float, 4> {0.0f, 0.0f, 0.0f, 0.0f} };
		clearValues[1].depthStencil = { 1.0f, 0 };

		vk::RenderPassBeginInfo renderPassBeginInfo;
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = width;
		renderPassBeginInfo.renderArea.extent.height = height;
		renderPassBeginInfo.clearValueCount = 2;
		renderPassBeginInfo.pClearValues = clearValues;

		vk::Result err;

		for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)
		{
			// Set target frame buffer
			renderPassBeginInfo.framebuffer = frameBuffers[i];

			drawCmdBuffers[i].begin(cmdBufInfo);

			drawCmdBuffers[i].beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);

			vk::Viewport viewport = vkTools::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
			drawCmdBuffers[i].setViewport(0, viewport);

			vk::Rect2D scissor = vkTools::initializers::rect2D(width, height, 0, 0);
			drawCmdBuffers[i].setScissor(0, scissor);

			// Environment
			meshes.environment.drawIndexed(drawCmdBuffers[i]);

			// Particle system
			drawCmdBuffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSet, nullptr);
			drawCmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.particles);
			vk::DeviceSize offsets = 0;
			drawCmdBuffers[i].bindVertexBuffers(VERTEX_BUFFER_BIND_ID, particles.buffer, offsets);
			drawCmdBuffers[i].draw(PARTICLE_COUNT, 1, 0, 0);

			drawCmdBuffers[i].endRenderPass();

			drawCmdBuffers[i].end();
			
		}
	}

	void draw()
	{

		// Get next image in the swap chain (back/front buffer)
		swapChain.acquireNextImage(semaphores.presentComplete, currentBuffer);
		

		submitPostPresentBarrier(swapChain.buffers[currentBuffer].image);

		// Command buffer to be sumitted to the queue
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];

		// Submit to queue
		queue.submit(submitInfo, VK_NULL_HANDLE);
		

		submitPrePresentBarrier(swapChain.buffers[currentBuffer].image);

		swapChain.queuePresent(queue, currentBuffer, semaphores.renderComplete);
		

		queue.waitIdle();
		
	}

	float rnd(float range)
	{
		return range * (rand() / double(RAND_MAX));
	}

	void initParticle(Particle *particle, glm::vec3 emitterPos)
	{
		particle->vel = glm::vec4(0.0f, minVel.y + rnd(maxVel.y - minVel.y), 0.0f, 0.0f);
		particle->alpha = rnd(0.75f);
		particle->size = 1.0f + rnd(0.5f);
		particle->color = glm::vec4(1.0f);
		particle->type = PARTICLE_TYPE_FLAME;
		particle->rotation = rnd(2.0f * M_PI);
		particle->rotationSpeed = rnd(2.0f) - rnd(2.0f);

		// Get random sphere point
		float theta = rnd(2 * M_PI);
		float phi = rnd(M_PI) - M_PI / 2;
		float r = rnd(FLAME_RADIUS);

		particle->pos.x = r * cos(theta) * cos(phi);
		particle->pos.y = r * sin(phi);
		particle->pos.z = r * sin(theta) * cos(phi);

		particle->pos += glm::vec4(emitterPos, 0.0f);
	}

	void transitionParticle(Particle *particle)
	{
		switch (particle->type)
		{
		case PARTICLE_TYPE_FLAME:
			// Flame particles have a chance of turning into smoke
			if (rnd(1.0f) < 0.05f)
			{
				particle->alpha = 0.0f;
				particle->color = glm::vec4(0.25f + rnd(0.25f));
				particle->pos.x *= 0.5f;
				particle->pos.z *= 0.5f;
				particle->vel = glm::vec4(rnd(1.0f) - rnd(1.0f), (minVel.y * 2) + rnd(maxVel.y - minVel.y), rnd(1.0f) - rnd(1.0f), 0.0f);
				particle->size = 1.0f + rnd(0.5f);
				particle->rotationSpeed = rnd(1.0f) - rnd(1.0f);
				particle->type = PARTICLE_TYPE_SMOKE;
			}
			else
			{
				initParticle(particle, emitterPos);
			}
			break;
		case PARTICLE_TYPE_SMOKE:
			// Respawn at end of life
			initParticle(particle, emitterPos);
			break;
		}
	}

	void prepareParticles()
	{
		particleBuffer.resize(PARTICLE_COUNT);
		for (auto& particle : particleBuffer)
		{
			initParticle(&particle, emitterPos);
			particle.alpha = 1.0f - (abs(particle.pos.y) / (FLAME_RADIUS * 2.0f));
		}

		particles.size = particleBuffer.size() * sizeof(Particle);
		createBuffer(vk::BufferUsageFlagBits::eVertexBuffer,
			particles.size,
			particleBuffer.data(),
			particles.buffer,
			particles.memory);

		// Map the memory and store the pointer for reuse
		particles.mappedMemory = device.mapMemory(particles.memory, 0, particles.size, vk::MemoryMapFlags());
		
	}

	void updateParticles()
	{
		float particleTimer = frameTimer * 0.45f;
		for (auto& particle : particleBuffer)
		{
			switch (particle.type)
			{
			case PARTICLE_TYPE_FLAME:
				particle.pos.y -= particle.vel.y * particleTimer * 3.5f;
				particle.alpha += particleTimer * 2.5f;
				particle.size -= particleTimer * 0.5f;
				break;
			case PARTICLE_TYPE_SMOKE:
				particle.pos -= particle.vel * frameTimer * 1.0f;
				particle.alpha += particleTimer * 1.25f;
				particle.size += particleTimer * 0.125f;
				particle.color -= particleTimer * 0.05f;
				break;
			}
			particle.rotation += particleTimer * particle.rotationSpeed;
			// Transition particle state
			if (particle.alpha > 2.0f)
			{
				transitionParticle(&particle);
			}
		}
		size_t size = particleBuffer.size() * sizeof(Particle);
		memcpy(particles.mappedMemory, particleBuffer.data(), size);
	}

	void loadTextures()
	{
		// Particles
		textureLoader->loadTexture(
			getAssetPath() + "textures/particle_smoke.ktx",
			vk::Format::eBc3UnormBlock,
			&textures.particles.smoke);
		textureLoader->loadTexture(
			getAssetPath() + "textures/particle_fire.ktx",
			vk::Format::eBc3UnormBlock,
			&textures.particles.fire);

		// Floor
		textureLoader->loadTexture(
			getAssetPath() + "textures/fireplace_colormap_bc3.ktx",
			vk::Format::eBc3UnormBlock,
			&textures.floor.colorMap);
		textureLoader->loadTexture(
			getAssetPath() + "textures/fireplace_normalmap_bc3.ktx",
			vk::Format::eBc3UnormBlock,
			&textures.floor.normalMap);

		// Create a custom sampler to be used with the particle textures
		// Create sampler
		vk::SamplerCreateInfo samplerCreateInfo;
		samplerCreateInfo.magFilter = vk::Filter::eLinear;
		samplerCreateInfo.minFilter = vk::Filter::eLinear;
		samplerCreateInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
		// Different address mode
		samplerCreateInfo.addressModeU = vk::SamplerAddressMode::eClampToBorder;
		samplerCreateInfo.addressModeV = samplerCreateInfo.addressModeU;
		samplerCreateInfo.addressModeW = samplerCreateInfo.addressModeU;
		samplerCreateInfo.mipLodBias = 0.0f;
		samplerCreateInfo.compareOp = vk::CompareOp::eNever;
		samplerCreateInfo.minLod = 0.0f;
		// Both particle textures have the same number of mip maps
		samplerCreateInfo.maxLod = textures.particles.fire.mipLevels;
		// Enable anisotropic filtering
		samplerCreateInfo.maxAnisotropy = 8;
		samplerCreateInfo.anisotropyEnable = VK_TRUE;
		// Use a different border color (than the normal texture loader) for additive blending
		samplerCreateInfo.borderColor = vk::BorderColor::eFloatTransparentBlack;
		textures.particles.sampler = device.createSampler(samplerCreateInfo, nullptr);
		
	}

	void loadMeshes()
	{
		loadMesh(getAssetPath() + "models/fireplace.obj", &meshes.environment.buffers, vertexLayout, 10.0f);
		meshes.environment.setupVertexInputState(vertexLayout);
	}

	void setupVertexDescriptions()
	{
		// Binding description
		particles.bindingDescriptions.resize(1);
		particles.bindingDescriptions[0] =
			vkTools::initializers::vertexInputBindingDescription(VERTEX_BUFFER_BIND_ID, sizeof(Particle), vk::VertexInputRate::eVertex);

		// Attribute descriptions
		// Describes memory layout and shader positions
		// Location 0 : Position
		particles.attributeDescriptions.push_back(
			vkTools::initializers::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID, 
				0, 
				vk::Format::eR32G32B32A32Sfloat, 
				0));
		// Location 1 : Color
		particles.attributeDescriptions.push_back(
			vkTools::initializers::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID, 
				1, 
				vk::Format::eR32G32B32A32Sfloat, 
				sizeof(float) * 4));
		// Location 2 : Alpha
		particles.attributeDescriptions.push_back(
			vkTools::initializers::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID, 
				2, 
				vk::Format::eR32Sfloat, 
				sizeof(float) * 8));
		// Location 3 : Size
		particles.attributeDescriptions.push_back(
			vkTools::initializers::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID, 
				3, 
				vk::Format::eR32Sfloat, 
				sizeof(float) * 9));
		// Location 4 : Rotation
		particles.attributeDescriptions.push_back(
			vkTools::initializers::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID, 
				4, 
				vk::Format::eR32Sfloat, 
				sizeof(float) * 10));
		// Location 5 : Type
		particles.attributeDescriptions.push_back(
			vkTools::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 
				5, 
				vk::Format::eR32Sint, 
				sizeof(float) * 11));

		particles.inputState = vk::PipelineVertexInputStateCreateInfo();
		particles.inputState.vertexBindingDescriptionCount = particles.bindingDescriptions.size();
		particles.inputState.pVertexBindingDescriptions = particles.bindingDescriptions.data();
		particles.inputState.vertexAttributeDescriptionCount = particles.attributeDescriptions.size();
		particles.inputState.pVertexAttributeDescriptions = particles.attributeDescriptions.data();
	}

	void setupDescriptorPool()
	{
		// Example uses one ubo and one image sampler
		std::vector<vk::DescriptorPoolSize> poolSizes =
		{
			vkTools::initializers::descriptorPoolSize(vk::DescriptorType::eUniformBuffer, 2),
			vkTools::initializers::descriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 4)
		};

		vk::DescriptorPoolCreateInfo descriptorPoolInfo =
			vkTools::initializers::descriptorPoolCreateInfo(poolSizes.size(), poolSizes.data(), 2);

		descriptorPool = device.createDescriptorPool(descriptorPoolInfo, nullptr);
	}

	void setupDescriptorSetLayout()
	{
		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings =
		{
			// Binding 0 : Vertex shader uniform buffer
			vkTools::initializers::descriptorSetLayoutBinding(
			vk::DescriptorType::eUniformBuffer,
				vk::ShaderStageFlagBits::eVertex,
				0),
			// Binding 1 : Fragment shader image sampler
			vkTools::initializers::descriptorSetLayoutBinding(
				vk::DescriptorType::eCombinedImageSampler,
				vk::ShaderStageFlagBits::eFragment,
				1),
			// Binding 1 : Fragment shader image sampler
			vkTools::initializers::descriptorSetLayoutBinding(
				vk::DescriptorType::eCombinedImageSampler,
				vk::ShaderStageFlagBits::eFragment,
				2)
		};

		vk::DescriptorSetLayoutCreateInfo descriptorLayout =
			vkTools::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings.data(), setLayoutBindings.size());

		descriptorSetLayout = device.createDescriptorSetLayout(descriptorLayout, nullptr);
		

		vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
			vkTools::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);

		pipelineLayout = device.createPipelineLayout(pPipelineLayoutCreateInfo, nullptr);
		
	}

	void setupDescriptorSets()
	{
		vk::DescriptorSetAllocateInfo allocInfo =
			vkTools::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);

		descriptorSet = device.allocateDescriptorSets(allocInfo)[0];

		// Image descriptor for the color map texture
		vk::DescriptorImageInfo texDescriptorSmoke =
			vkTools::initializers::descriptorImageInfo(textures.particles.sampler, textures.particles.smoke.view, vk::ImageLayout::eGeneral);
		vk::DescriptorImageInfo texDescriptorFire =
			vkTools::initializers::descriptorImageInfo(textures.particles.sampler, textures.particles.fire.view, vk::ImageLayout::eGeneral);

		std::vector<vk::WriteDescriptorSet> writeDescriptorSets =
		{
			// Binding 0 : Vertex shader uniform buffer
			vkTools::initializers::writeDescriptorSet(
			descriptorSet,
				vk::DescriptorType::eUniformBuffer,
				0,
				&uniformData.fire.descriptor),
			// Binding 1 : Smoke texture
			vkTools::initializers::writeDescriptorSet(
				descriptorSet,
				vk::DescriptorType::eCombinedImageSampler,
				1,
				&texDescriptorSmoke),
			// Binding 1 : Fire texture array
			vkTools::initializers::writeDescriptorSet(
				descriptorSet,
				vk::DescriptorType::eCombinedImageSampler,
				2,
				&texDescriptorFire)
		};

		device.updateDescriptorSets(writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);

		// Environment
		meshes.environment.descriptorSet = device.allocateDescriptorSets(allocInfo)[0];

		vk::DescriptorImageInfo texDescriptorColorMap =
			vkTools::initializers::descriptorImageInfo(textures.floor.colorMap.sampler, textures.floor.colorMap.view, vk::ImageLayout::eGeneral);
		vk::DescriptorImageInfo texDescriptorNormalMap =
			vkTools::initializers::descriptorImageInfo(textures.floor.normalMap.sampler, textures.floor.normalMap.view, vk::ImageLayout::eGeneral);

		writeDescriptorSets.clear();

		// Binding 0 : Vertex shader uniform buffer
		writeDescriptorSets.push_back(
			vkTools::initializers::writeDescriptorSet(meshes.environment.descriptorSet, vk::DescriptorType::eUniformBuffer, 0, &uniformData.environment.descriptor));
		// Binding 1 : Color map
		writeDescriptorSets.push_back(
			vkTools::initializers::writeDescriptorSet(meshes.environment.descriptorSet, vk::DescriptorType::eCombinedImageSampler, 1, &texDescriptorColorMap));
		// Binding 2 : Normal map
		writeDescriptorSets.push_back(
			vkTools::initializers::writeDescriptorSet(meshes.environment.descriptorSet, vk::DescriptorType::eCombinedImageSampler, 2, &texDescriptorNormalMap));

		device.updateDescriptorSets(writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
	}

	void preparePipelines()
	{
		vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState =
			vkTools::initializers::pipelineInputAssemblyStateCreateInfo(vk::PrimitiveTopology::ePointList, vk::PipelineInputAssemblyStateCreateFlags(), VK_FALSE);

		vk::PipelineRasterizationStateCreateInfo rasterizationState =
			vkTools::initializers::pipelineRasterizationStateCreateInfo(vk::PolygonMode::eFill, vk::CullModeFlagBits::eBack, vk::FrontFace::eClockwise);

		vk::PipelineColorBlendAttachmentState blendAttachmentState =
			vkTools::initializers::pipelineColorBlendAttachmentState();

		vk::PipelineColorBlendStateCreateInfo colorBlendState =
			vkTools::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);

		vk::PipelineDepthStencilStateCreateInfo depthStencilState =
			vkTools::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, vk::CompareOp::eLessOrEqual);

		vk::PipelineViewportStateCreateInfo viewportState =
			vkTools::initializers::pipelineViewportStateCreateInfo(1, 1);

		vk::PipelineMultisampleStateCreateInfo multisampleState =
			vkTools::initializers::pipelineMultisampleStateCreateInfo(vk::SampleCountFlagBits::e1);

		std::vector<vk::DynamicState> dynamicStateEnables = {
			vk::DynamicState::eViewport,
			vk::DynamicState::eScissor
		};
		vk::PipelineDynamicStateCreateInfo dynamicState =
			vkTools::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables.data(), dynamicStateEnables.size());

		// Load shaders
		std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;

		shaderStages[0] = loadShader(getAssetPath() + "shaders/particlefire/particle.vert.spv", vk::ShaderStageFlagBits::eVertex);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/particlefire/particle.frag.spv", vk::ShaderStageFlagBits::eFragment);

		vk::GraphicsPipelineCreateInfo pipelineCreateInfo =
			vkTools::initializers::pipelineCreateInfo(pipelineLayout, renderPass);

		pipelineCreateInfo.pVertexInputState = &particles.inputState;
		pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
		pipelineCreateInfo.pRasterizationState = &rasterizationState;
		pipelineCreateInfo.pColorBlendState = &colorBlendState;
		pipelineCreateInfo.pMultisampleState = &multisampleState;
		pipelineCreateInfo.pViewportState = &viewportState;
		pipelineCreateInfo.pDepthStencilState = &depthStencilState;
		pipelineCreateInfo.pDynamicState = &dynamicState;
		pipelineCreateInfo.stageCount = shaderStages.size();
		pipelineCreateInfo.pStages = shaderStages.data();

		depthStencilState.depthWriteEnable = VK_FALSE;

		// Premulitplied alpha
		blendAttachmentState.blendEnable = VK_TRUE;
		blendAttachmentState.srcColorBlendFactor = vk::BlendFactor::eOne;
		blendAttachmentState.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
		blendAttachmentState.colorBlendOp = vk::BlendOp::eAdd;
		blendAttachmentState.srcAlphaBlendFactor = vk::BlendFactor::eOne;
		blendAttachmentState.dstAlphaBlendFactor = vk::BlendFactor::eZero;
		blendAttachmentState.alphaBlendOp = vk::BlendOp::eAdd;
		blendAttachmentState.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

		pipelines.particles = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];
		

		// Environment rendering pipeline (normal mapped)
		shaderStages[0] = loadShader(getAssetPath() + "shaders/particlefire/normalmap.vert.spv", vk::ShaderStageFlagBits::eVertex);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/particlefire/normalmap.frag.spv", vk::ShaderStageFlagBits::eFragment);
		pipelineCreateInfo.pVertexInputState = &meshes.environment.vertexInputState;
		blendAttachmentState.blendEnable = VK_FALSE;
		depthStencilState.depthWriteEnable = VK_TRUE;
		inputAssemblyState.topology = vk::PrimitiveTopology::eTriangleList;
		pipelines.environment = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];
		
		meshes.environment.pipeline = pipelines.environment;
		meshes.environment.pipelineLayout = pipelineLayout;
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		// Vertex shader uniform buffer block
		createBuffer(vk::BufferUsageFlagBits::eUniformBuffer,
			sizeof(uboVS),
			&uboVS,
			uniformData.fire.buffer,
			uniformData.fire.memory,
			uniformData.fire.descriptor);

		// Vertex shader uniform buffer block
		createBuffer(vk::BufferUsageFlagBits::eUniformBuffer,
			sizeof(uboEnv),
			&uboEnv,
			uniformData.environment.buffer,
			uniformData.environment.memory,
			uniformData.environment.descriptor);

		updateUniformBuffers();
	}

	void updateUniformBufferLight()
	{
		// Environment
		uboEnv.lightPos.x = sin(timer * 2 * M_PI) * 1.5f;
		uboEnv.lightPos.y = 0.0f;
		uboEnv.lightPos.z = cos(timer * 2 * M_PI) * 1.5f;
		void *pData = device.mapMemory(uniformData.environment.memory, 0, sizeof(uboEnv), vk::MemoryMapFlags());
		
		memcpy(pData, &uboEnv, sizeof(uboEnv));
		device.unmapMemory(uniformData.environment.memory);
	}

	void updateUniformBuffers()
	{
		// Vertex shader
		glm::mat4 viewMatrix = glm::mat4();
		uboVS.projection = glm::perspective(glm::radians(60.0f), (float)width / (float)height, 0.001f, 256.0f);
		viewMatrix = glm::translate(viewMatrix, glm::vec3(0.0f, 0.0f, zoom));

		uboVS.model = glm::mat4();
		uboVS.model = viewMatrix * glm::translate(uboVS.model, glm::vec3(0.0f, 15.0f, 0.0f));
		uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
		uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
		uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

		uboVS.viewportDim = glm::vec2((float)width, (float)height);

		void *pData = device.mapMemory(uniformData.fire.memory, 0, sizeof(uboVS), vk::MemoryMapFlags());
		
		memcpy(pData, &uboVS, sizeof(uboVS));
		device.unmapMemory(uniformData.fire.memory);

		// Environment
		uboEnv.projection = uboVS.projection;
		uboEnv.model = uboVS.model;
		uboEnv.normal = glm::inverseTranspose(uboEnv.model);
		uboEnv.cameraPos = glm::vec4(0.0, 0.0, zoom, 0.0);
		pData = device.mapMemory(uniformData.environment.memory, 0, sizeof(uboEnv), vk::MemoryMapFlags());
		
		memcpy(pData, &uboEnv, sizeof(uboEnv));
		device.unmapMemory(uniformData.environment.memory);
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		loadTextures();
		prepareParticles();
		setupVertexDescriptions();
		prepareUniformBuffers();
		setupDescriptorSetLayout();
		loadMeshes();
		preparePipelines();
		setupDescriptorPool();
		setupDescriptorSets();
		buildCommandBuffers();
		prepared = true;
	}

	virtual void render()
	{
		if (!prepared)
			return;
		vkDeviceWaitIdle(device);
		draw();
		vkDeviceWaitIdle(device);
		if (!paused)
		{
			updateUniformBufferLight();
			updateParticles();
		}
	}

	virtual void viewChanged()
	{
		updateUniformBuffers();
	}
};


VulkanExample *vulkanExample;

#if defined(_WIN32)
LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (vulkanExample != NULL)
	{
		vulkanExample->handleMessages(hWnd, uMsg, wParam, lParam);
	}
	return (DefWindowProc(hWnd, uMsg, wParam, lParam));
}
#elif defined(__linux__) && !defined(__ANDROID__)
static void handleEvent(const xcb_generic_event_t *event)
{
	if (vulkanExample != NULL)
	{
		vulkanExample->handleEvent(event);
	}
}
#endif

// Main entry point
#if defined(_WIN32)
// Windows entry point
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow)
#elif defined(__ANDROID__)
// Android entry point
void android_main(android_app* state)
#elif defined(__linux__)
// Linux entry point
int main(const int argc, const char *argv[])
#endif
{
#if defined(__ANDROID__)
	// Removing this may cause the compiler to omit the main entry point 
	// which would make the application crash at start
	app_dummy();
#endif
	vulkanExample = new VulkanExample();
#if defined(_WIN32)
	vulkanExample->setupWindow(hInstance, WndProc);
#elif defined(__ANDROID__)
	// Attach vulkan example to global android application state
	state->userData = vulkanExample;
	state->onAppCmd = VulkanExample::handleAppCommand;
	state->onInputEvent = VulkanExample::handleAppInput;
	vulkanExample->androidApp = state;
#elif defined(__linux__)
	vulkanExample->setupWindow();
#endif
#if !defined(__ANDROID__)
	vulkanExample->initSwapchain();
	vulkanExample->prepare();
#endif
	vulkanExample->renderLoop();
	delete(vulkanExample);
#if !defined(__ANDROID__)
	return 0;
#endif
}
