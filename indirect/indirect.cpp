/*
* Vulkan Example - Instanced mesh rendering, uses a separate vertex buffer for instanced data
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "vulkanexamplebase.h"
#include "shapes.h"

#define SHAPES_COUNT 5
#define INSTANCES_PER_SHAPE 1000
#define INSTANCE_COUNT (INSTANCES_PER_SHAPE * SHAPES_COUNT)

class VulkanExample : public VulkanExampleBase
{
public:
	struct {
		vk::PipelineVertexInputStateCreateInfo inputState;
		std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
		std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
		vk::Buffer buffer;
		vk::DeviceMemory memory;
	} vertices;

	// Per-instance data block
	struct InstanceData {
		glm::vec3 pos;
		glm::vec3 rot;
		float scale;
	};

	struct ShapeVertexData {
		size_t baseVertex;
		size_t vertices;
	};

	struct Vertex {
		glm::vec3 position;
		glm::vec3 normal;
		glm::vec3 color;
	};

	// Contains the instanced data
	struct InstanceBuffer {
		vk::Buffer buffer;
		vk::DeviceMemory memory;
		size_t size = 0;
	} instanceBuffer;

	// Contains the instanced data
	struct IndirectBuffer {
		vk::Buffer buffer;
		vk::DeviceMemory memory;
		size_t size = 0;
	} indirectBuffer;

	struct UboVS {
		glm::mat4 projection;
		glm::mat4 view;
		float time = 0.0f;
	} uboVS;

	struct {
		vkTools::UniformData vsScene;
	} uniformData;

	struct {
		vk::Pipeline solid;
	} pipelines;

	std::vector<ShapeVertexData> shapes;

	vk::PipelineLayout pipelineLayout;
	vk::DescriptorSet descriptorSet;
	vk::DescriptorSetLayout descriptorSetLayout;

	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
		zoom = -12.0f;
		rotationSpeed = 0.25f;
		title = "Vulkan Example - Instanced mesh rendering";
		srand(time(NULL));
	}

	~VulkanExample()
	{
		device.destroyPipeline(pipelines.solid);
		device.destroyPipelineLayout(pipelineLayout);
		device.destroyDescriptorSetLayout(descriptorSetLayout);
		vkTools::destroyUniformData(device, &uniformData.vsScene);
	}

	void buildCommandBuffers()
	{
		vk::CommandBufferBeginInfo cmdBufInfo;

		vk::ClearValue clearValues[2];
		clearValues[0].color = vkTools::initializers::clearColor({ 0.0f, 0.0f, 0.0f, 0.0f });
		clearValues[1].depthStencil = { 1.0f, 0 };

		vk::RenderPassBeginInfo renderPassBeginInfo;
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.extent.width = width;
		renderPassBeginInfo.renderArea.extent.height = height;
		renderPassBeginInfo.clearValueCount = 2;
		renderPassBeginInfo.pClearValues = clearValues;

		vk::Viewport viewport = vkTools::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
		vk::Rect2D scissor = vkTools::initializers::rect2D(width, height, 0, 0);
		vk::DeviceSize offset = 0;

		for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)
		{
			// Set target frame buffer
			renderPassBeginInfo.framebuffer = frameBuffers[i];

			drawCmdBuffers[i].begin(cmdBufInfo);
			drawCmdBuffers[i].beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
			drawCmdBuffers[i].setViewport(0, viewport);
			drawCmdBuffers[i].setScissor(0, scissor);
			drawCmdBuffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSet, nullptr);
			drawCmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.solid);
			// Binding point 0 : Mesh vertex buffer
			drawCmdBuffers[i].bindVertexBuffers(VERTEX_BUFFER_BIND_ID, vertices.buffer, offset);
			// Binding point 1 : Instance data buffer
			drawCmdBuffers[i].bindVertexBuffers(INSTANCE_BUFFER_BIND_ID, instanceBuffer.buffer, offset);
			// Equivlant non-indirect commands:
			//for (size_t j = 0; j < SHAPES_COUNT; ++j) {
			//	auto shape = shapes[j];
			//	drawCmdBuffers[i].draw(shape.vertices, INSTANCES_PER_SHAPE, shape.baseVertex, j * INSTANCES_PER_SHAPE);
			//}
			drawCmdBuffers[i].drawIndirect(indirectBuffer.buffer, 0, SHAPES_COUNT, sizeof(vk::DrawIndirectCommand));
			drawCmdBuffers[i].endRenderPass();
			drawCmdBuffers[i].end();
		}
	}

	void draw()
	{
		// Get next image in the swap chain (back/front buffer)
		prepareFrame();
		// Command buffer to be sumitted to the queue
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
		// Submit to queue
		queue.submit(submitInfo, VK_NULL_HANDLE);
		submitFrame();
	}

	template<size_t N>
	void appendShape(const geometry::Solid<N>& solid, std::vector<Vertex>& vertices) {
		using namespace geometry;
		using namespace glm;
		using namespace std;
		ShapeVertexData shape;
		shape.baseVertex = vertices.size();

		auto faceCount = solid.faces.size();
		// FIXME triangulate the faces
		auto faceTriangles = triangulatedFaceTriangleCount<N>();
		vertices.reserve(vertices.size() + 3 * faceTriangles);

		
		vec3 color = vec3(rand(), rand(), rand()) / (float)RAND_MAX;
		color = vec3(0.3f) + (0.7f * color);
		for (size_t f = 0; f < faceCount; ++f) {
			const Face<N>& face = solid.faces[f];
			vec3 normal = solid.getFaceNormal(f);
			for (size_t ft = 0; ft < faceTriangles; ++ft) {
				// Create the vertices for the face
				vertices.push_back({ vec3(solid.vertices[face[0]]), normal, color });
				vertices.push_back({ vec3(solid.vertices[face[2 + ft]]), normal, color });
				vertices.push_back({ vec3(solid.vertices[face[1 + ft]]), normal, color });
			}
		}
		shape.vertices = vertices.size() - shape.baseVertex;
		shapes.push_back(shape);
	}

	void loadShapes()
	{
		std::vector<Vertex> vertices;
		size_t vertexCount = 0;
		appendShape<>(geometry::tetrahedron(), vertices);
		appendShape<>(geometry::octahedron(), vertices);
		appendShape<>(geometry::cube(), vertices);
		appendShape<>(geometry::dodecahedron(), vertices);
		appendShape<>(geometry::icosahedron(), vertices);
		for (auto& vertex : vertices) {
			vertex.position *= 0.2f;
		}
		auto bufferResults = stageToBuffer(vk::BufferUsageFlagBits::eVertexBuffer, vertices);

		this->vertices.buffer = bufferResults.buf;
		this->vertices.memory = bufferResults.mem;
	}

	void setupVertexDescriptions()
	{
		// Binding description
		vertices.bindingDescriptions.resize(2);

		// Mesh vertex buffer (description) at binding point 0
		vertices.bindingDescriptions[0] =
			vkTools::initializers::vertexInputBindingDescription(VERTEX_BUFFER_BIND_ID, sizeof(Vertex), vk::VertexInputRate::eVertex);
		vertices.bindingDescriptions[1] =
			vkTools::initializers::vertexInputBindingDescription(INSTANCE_BUFFER_BIND_ID, sizeof(InstanceData), vk::VertexInputRate::eInstance);

		// Attribute descriptions
		// Describes memory layout and shader positions
		vertices.attributeDescriptions.clear();

		// Per-Vertex attributes
		// Location 0 : Position
		vertices.attributeDescriptions.push_back(
			vkTools::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, position)));
		// Location 1 : Color
		vertices.attributeDescriptions.push_back(
			vkTools::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 1, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, color)));
		// Location 2 : Normal
		vertices.attributeDescriptions.push_back(
			vkTools::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 2, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, normal)));

		// Instanced attributes
		// Location 4 : Position
		vertices.attributeDescriptions.push_back(
			vkTools::initializers::vertexInputAttributeDescription(INSTANCE_BUFFER_BIND_ID, 5, vk::Format::eR32G32B32Sfloat, sizeof(float) * 3));
		// Location 5 : Rotation
		vertices.attributeDescriptions.push_back(
			vkTools::initializers::vertexInputAttributeDescription(INSTANCE_BUFFER_BIND_ID, 4, vk::Format::eR32G32B32Sfloat, 0));
		// Location 6 : Scale
		vertices.attributeDescriptions.push_back(
			vkTools::initializers::vertexInputAttributeDescription(INSTANCE_BUFFER_BIND_ID, 6, vk::Format::eR32Sfloat, sizeof(float) * 6));
		// Location 7 : Texture array layer index
		vertices.attributeDescriptions.push_back(
			vkTools::initializers::vertexInputAttributeDescription(INSTANCE_BUFFER_BIND_ID, 7, vk::Format::eR32Sint, sizeof(float) * 7));


		vertices.inputState = vk::PipelineVertexInputStateCreateInfo();
		vertices.inputState.vertexBindingDescriptionCount = vertices.bindingDescriptions.size();
		vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
		vertices.inputState.vertexAttributeDescriptionCount = vertices.attributeDescriptions.size();
		vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
	}

	void setupDescriptorPool()
	{
		// Example uses one ubo 
		std::vector<vk::DescriptorPoolSize> poolSizes =
		{
			vkTools::initializers::descriptorPoolSize(vk::DescriptorType::eUniformBuffer, 1),
		};

		vk::DescriptorPoolCreateInfo descriptorPoolInfo =
			vkTools::initializers::descriptorPoolCreateInfo(poolSizes.size(), poolSizes.data(), 1);

		descriptorPool = device.createDescriptorPool(descriptorPoolInfo);
	}

	void setupDescriptorSetLayout()
	{
		// Binding 0 : Vertex shader uniform buffer
		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings =
		{
			vkTools::initializers::descriptorSetLayoutBinding(vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex, 0),
		};

		descriptorSetLayout = device.createDescriptorSetLayout(
			vk::DescriptorSetLayoutCreateInfo()
				.setBindingCount(setLayoutBindings.size())
				.setPBindings(setLayoutBindings.data()));

		pipelineLayout = device.createPipelineLayout(
			vk::PipelineLayoutCreateInfo()
				.setPSetLayouts(&descriptorSetLayout)
				.setSetLayoutCount(1));
	}

	void setupDescriptorSet()
	{
		vk::DescriptorSetAllocateInfo allocInfo =
			vkTools::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);

		descriptorSet = device.allocateDescriptorSets(allocInfo)[0];

		// Binding 0 : Vertex shader uniform buffer
		vk::WriteDescriptorSet writeDescriptorSet;
		writeDescriptorSet.dstSet = descriptorSet;
		writeDescriptorSet.descriptorType = vk::DescriptorType::eUniformBuffer;
		writeDescriptorSet.dstBinding = 0;
		writeDescriptorSet.pBufferInfo = &uniformData.vsScene.descriptor;
		writeDescriptorSet.descriptorCount = 1;

		device.updateDescriptorSets(writeDescriptorSet, nullptr);
	}

	void preparePipelines()
	{
		vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState =
			vkTools::initializers::pipelineInputAssemblyStateCreateInfo(vk::PrimitiveTopology::eTriangleList);

		vk::PipelineRasterizationStateCreateInfo rasterizationState =
			vkTools::initializers::pipelineRasterizationStateCreateInfo(vk::PolygonMode::eFill, vk::CullModeFlagBits::eBack, vk::FrontFace::eCounterClockwise);

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

		// Instacing pipeline
		// Load shaders
		std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;
		{
			vulkanShaders::initGlsl();
			shaderStages[0] = loadGlslShader(getAssetPath() + "shaders/indirect/indirect.vert", vk::ShaderStageFlagBits::eVertex);
			shaderStages[1] = loadGlslShader(getAssetPath() + "shaders/indirect/indirect.frag", vk::ShaderStageFlagBits::eFragment);
			vulkanShaders::finalizeGlsl();
		}

		vk::GraphicsPipelineCreateInfo pipelineCreateInfo =
			vkTools::initializers::pipelineCreateInfo(pipelineLayout, renderPass);

		pipelineCreateInfo.pVertexInputState = &vertices.inputState;
		pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
		pipelineCreateInfo.pRasterizationState = &rasterizationState;
		pipelineCreateInfo.pColorBlendState = &colorBlendState;
		pipelineCreateInfo.pMultisampleState = &multisampleState;
		pipelineCreateInfo.pViewportState = &viewportState;
		pipelineCreateInfo.pDepthStencilState = &depthStencilState;
		pipelineCreateInfo.pDynamicState = &dynamicState;
		pipelineCreateInfo.stageCount = shaderStages.size();
		pipelineCreateInfo.pStages = shaderStages.data();

		pipelines.solid = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];
		
	}

	void prepareIndirectData() 
	{
		std::vector<vk::DrawIndirectCommand> indirectData;
		indirectData.resize(SHAPES_COUNT);
		for (auto i = 0; i < SHAPES_COUNT; ++i) {
			auto& drawIndirectCommand = indirectData[i];
			const auto& shapeData = shapes[i];
			drawIndirectCommand.firstInstance = i * INSTANCES_PER_SHAPE;
			drawIndirectCommand.instanceCount = INSTANCES_PER_SHAPE;
			drawIndirectCommand.firstVertex = shapeData.baseVertex;
			drawIndirectCommand.vertexCount = shapeData.vertices;
		}

		indirectBuffer.size = indirectData.size() * sizeof(vk::DrawIndirectCommand);
		auto stageResult = stageToBuffer(vk::BufferUsageFlagBits::eIndirectBuffer, indirectData);
		indirectBuffer.buffer = stageResult.buf;
		indirectBuffer.memory = stageResult.mem;
	}

	void prepareInstanceData()
	{
		std::vector<InstanceData> instanceData;
		instanceData.resize(INSTANCE_COUNT);

		std::mt19937 rndGenerator(time(NULL));
		std::uniform_real_distribution<double> uniformDist(0.0, 1.0);

		for (auto i = 0; i < INSTANCE_COUNT; i++)
		{
			instanceData[i].rot = glm::vec3(M_PI * uniformDist(rndGenerator), M_PI * uniformDist(rndGenerator), M_PI * uniformDist(rndGenerator));
			float theta = 2 * M_PI * uniformDist(rndGenerator);
			float phi = acos(1 - 2 * uniformDist(rndGenerator));
			glm::vec3 pos;
			instanceData[i].pos = glm::vec3(sin(phi) * cos(theta), sin(theta) * uniformDist(rndGenerator) / 1500.0f, cos(phi)) * 7.5f;
			instanceData[i].scale = 1.0f + uniformDist(rndGenerator) * 2.0f;
		}

		instanceBuffer.size = instanceData.size() * sizeof(InstanceData);
		// Staging
		// Instanced data is static, copy to device local memory 
		// This results in better performance
		auto stageResult = stageToBuffer(vk::BufferUsageFlagBits::eIndirectBuffer, instanceData);
		instanceBuffer.buffer = stageResult.buf;
		instanceBuffer.memory = stageResult.mem;
	}

	void prepareUniformBuffers()
	{
		createBuffer(vk::BufferUsageFlagBits::eUniformBuffer,
			sizeof(uboVS),
			nullptr,
			uniformData.vsScene.buffer,
			uniformData.vsScene.memory,
			uniformData.vsScene.descriptor);

		// Map for host access
		uniformData.vsScene.mapped = device.mapMemory(uniformData.vsScene.memory, 0, sizeof(uboVS), vk::MemoryMapFlags());

		updateUniformBuffer(true);
	}

	void updateUniformBuffer(bool viewChanged)
	{
		if (viewChanged)
		{
			uboVS.projection = glm::perspective(glm::radians(60.0f), (float)width / (float)height, 0.001f, 256.0f);
			uboVS.view = glm::translate(glm::mat4(), glm::vec3(0.0f, 0.0f, zoom));
			uboVS.view = glm::rotate(uboVS.view, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
			uboVS.view = glm::rotate(uboVS.view, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
			uboVS.view = glm::rotate(uboVS.view, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
		}

		if (!paused)
		{
			uboVS.time += frameTimer * 0.05f;
		}

		memcpy(uniformData.vsScene.mapped, &uboVS, sizeof(uboVS));
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		loadShapes();
		prepareInstanceData();
		prepareIndirectData();
		setupVertexDescriptions();
		prepareUniformBuffers();
		setupDescriptorSetLayout();
		preparePipelines();
		setupDescriptorPool();
		setupDescriptorSet();
		buildCommandBuffers();
		prepared = true;
	}

	virtual void render()
	{
		if (!prepared)
		{
			return;
		}
		draw();
		if (!paused)
		{
			device.waitIdle();
			updateUniformBuffer(false);
		}
	}

	virtual void viewChanged()
	{
		updateUniformBuffer(true);
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
