## Intermediate Examples

### [Gears](examples/basic/gears.cpp)
<img src="./documentation/screenshots/basic_gears.png" height="96px" align="right">

Vulkan interpretation of glxgears. Procedurally generates separate meshes for each 
gear, with every mesh having it's own uniform buffer object for animation. Also 
demonstrates how to use different descriptor sets.
<br><br>

### [Texture mapping](examples/basic/texture.cpp)
<img src="./documentation/screenshots/basic_texture.png" height="96px" align="right">

Loads a single texture and displays it on a simple quad. Shows how to upload a 
texture including mip maps to the gpu in an optimal (tiling) format. Also 
demonstrates how to display the texture using a combined image sampler with 
anisotropic filtering enabled.
<br><br>

### [Cubemaps](examples/basic/texturecubemap.cpp)
<img src="./documentation/screenshots/texture_cubemap.png" height="96px" align="right">

Building on the basic texture loading example a cubemap is loaded into host visible 
memory and then transformed into an optimal format for the GPU.

The demo uses two different pipelines (and shader sets) to display the cubemap as a 
skybox (background) and as a source for reflections.
<br><br>

### [Texture array](examples/basic/texturearray.cpp)
<img src="./documentation/screenshots/texture_array.png" height="96px" align="right">

Texture arrays allow storing of multiple images in different layers without any 
interpolation between the layers.
This example demonstrates the use of a 2D texture array with instanced rendering. 
Each instance samples from a different layer of the texture array.
<br><br>

### [Particle system](examples/basic/particlefire.cpp)
<img src="./documentation/screenshots/particlefire.png" height="96px" align="right">

Point sprite based particle system simulating a fire. Particles and their attributes 
are stored in a host visible vertex buffer that's updated on the CPU on each frame. 
Also makes use of pre-multiplied alpha for rendering particles with different 
blending modes (smoke and fire) in one single pass.

### [Pipelines](examples/basic/pipelines.cpp)
<img src="./documentation/screenshots/basic_pipelines.png" height="96px" align="right">

[Pipeline state objects](https://www.khronos.org/registry/vulkan/specs/1.0/xhtml/vkspec.html#pipelines) 
replace the biggest part of the dynamic state machine from OpenGL, baking state 
information for culling, blending, rasterization, etc. and shaders into a fixed stat 
that can be optimized much easier by the implementation.

This example uses three different PSOs for rendering the same scene with different 
visuals and shaders and also demonstrates the use 
[pipeline derivatives](https://www.khronos.org/registry/vulkan/specs/1.0/xhtml/vkspec.html#pipelines-pipeline-derivatives).
<br><br>

### [Mesh loading and rendering](examples/basic/pipelines.cpp)
<img src="./documentation/screenshots/basic_mesh.png" height="96px" align="right">

Uses [assimp](https://github.com/assimp/assimp) to load a mesh from a common 3D 
format including a color map. The mesh data is then converted to a fixed vertex 
layout matching the shader vertex attribute bindings.
<br><br>

### [Multi sampling](examples/basic/multisampling.cpp)
<img src="./documentation/screenshots/multisampling.png" height="96px" align="right">

Demonstrates the use of resolve attachments for doing multisampling. Instead of 
doing an explicit resolve from a multisampled image this example creates 
multisampled attachments for the color and depth buffer and sets up the render pass 
to use these as resolve attachments that will get resolved to the visible frame 
buffer at the end of this render pass. To highlight MSAA the example renders a mesh 
with fine details against a bright background. Here is a 
[screenshot without MSAA](./documentation/screenshots/multisampling_nomsaa.png) to 
compare.
<br><br>

### [Mesh instancing](examples/basic/instancing.cpp)
<img src="./documentation/screenshots/instancing.jpg" height="96px" align="right">

Shows the use of instancing for rendering many copies of the same mesh using 
different attributes and textures. A secondary vertex buffer containing instanced 
data, stored in device local memory, is used to pass instance data to the shader via 
vertex attributes with a per-instance step rate. The instance data also contains a 
texture layer index for having different textures for the instanced meshes.
<br><br>

### [Indirect rendering](examples/basic/indirect.cpp)

Shows the use of a shared vertex buffer containing multiple shapes to rendering 
numerous instances of each shape with only one draw call.
<br><br>

### [Push constants](examples/basic/pushconstants.cpp)
<img src="./documentation/screenshots/push_constants.png" height="96px" align="right">

Demonstrates the use of push constants for updating small blocks of shader data with 
high speed (and without having to use a uniform buffer). Displays several light 
sources with position updates through a push constant block in a separate command 
buffer.
<br><br>

### [Skeletal animation](examples/basic/skeletalanimation.cpp)
<img src="./documentation/screenshots/mesh_skeletalanimation.png" height="96px" align="right">

Based on the mesh loading example, this example loads and displays a rigged COLLADA 
model including animations. Bone weights are extracted for each vertex and are 
passed to the vertex shader together with the final bone transformation matrices for 
vertex position calculations.
<br><br>

### [(Tessellation shader) PN-Triangles](examples/basic/tessellation.cpp)
<img src="./documentation/screenshots/tess_pntriangles.jpg" height="96px" align="right">

Generating curved PN-Triangles on the GPU using tessellation shaders to add details 
to low-polygon meshes, based on [this paper](http://alex.vlachos.com/graphics/CurvedPNTriangles.pdf), 
with shaders from 
[this tutorial](http://onrendering.blogspot.de/2011/12/tessellation-on-gpu-curved-pn-triangles.html).
<br><br>

### [Spherical environment mapping](examples/basic/sphericalenvmapping.cpp)
<img src="./documentation/screenshots/spherical_env_mapping.png" height="96px" align="right">

Uses a (spherical) material capture texture containing environment lighting and 
reflection information to fake complex lighting. The example also uses a texture 
array to store (and select) several material caps that can be toggled at runtime.

 The technique is based on [this article](https://github.com/spite/spherical-environment-mapping).
<br><br>

### [(Geometry shader) Normal debugging](examples/basic/geometryshader.cpp)
<img src="./documentation/screenshots/geom_normals.png" height="96px" align="right">

Renders the vertex normals of a complex mesh with the use of a geometry shader. The 
mesh is rendered solid first and the a geometry shader that generates lines from the 
face normals is used in the second pass.
<br><br>

### [Distance field fonts](examples/basic/distancefieldfonts.cpp)
<img src="./documentation/screenshots/font_distancefield.png" height="96px" align="right">

Instead of just sampling a bitmap font texture, a texture with per-character signed 
distance fields is used to generate high quality glyphs in the fragment shader. This 
results in a much higher quality than common bitmap fonts, even if heavily zoomed.

Distance field font textures can be generated with tools like 
[Hiero](https://github.com/libgdx/libgdx/wiki/Hiero).
<br><br>

### [Vulkan demo scene](examples/basic/vulkanscene.cpp)
<img src="./documentation/screenshots/vulkan_scene.png" height="96px" align="right">

More of a playground than an actual example. Renders multiple meshes with different 
shaders (and pipelines) including a background.
<br><br>


### [(Tessellation shader) Displacement mapping](examples/basic/displacement.cpp)
<img src="./documentation/screenshots/tess_displacement.jpg" height="96px" align="right">

Uses tessellation shaders to generate additional details and displace geometry based 
on a displacement map (heightmap).
<br><br>

### [Parallax mapping](examples/basic/parallaxmapping.cpp)
<img src="./documentation/screenshots/parallax_mapping.jpg" height="96px" align="right">

Like normal mapping, parallax mapping simulates geometry on a flat surface. In 
addition to normal mapping a heightmap is used to offset texture coordinates 
depending on the viewing angle giving the illusion of added depth.
<br><br>

### [(Extension) VK_EXT_debug_marker](examples/basic/debugmarker.cpp)
<img src="./documentation/screenshots/ext_debugmarker.jpg" width="170px" align="right">

Example application to be used along with 
[this tutorial](http://www.saschawillems.de/?page_id=2017) for demonstrating the use 
of the new VK_EXT_debug_marker extension. Introduced with Vulkan 1.0.12, it adds 
functionality to set debug markers, regions and name objects for advanced debugging 
in an offline graphics debugger like [RenderDoc](http://www.renderdoc.org).
<br><br>

### [Multi threaded command buffer generation](examples/broken/multithreading.cpp)
<img src="./documentation/screenshots/multithreading.png" height="96px" align="right">
This example demonstrates multi threaded command buffer generation. All available hardware threads are used to generated n secondary command buffers concurrent, with each thread also checking object visibility against the current viewing frustum. Command buffers are rebuilt on each frame.

Once all threads have finished (and all secondary command buffers have been constructed), the secondary command buffers are executed inside the primary command buffer and submitted to the queue.
<br><br>

### [Occlusion queries](examples/basic/occlusionquery.cpp)
<img src="./documentation/screenshots/occlusion_queries.png" height="96px" align="right">

#### FIXME the queies seem to work, but generate validation errors every frame 

Shows how to use occlusion queries to determine object visibility depending on the number of passed samples for a given object. Does an occlusion pass first, drawing all objects (and the occluder) with basic shaders, then reads the query results to conditionally color the objects during the final pass depending on their visibility.
<br><br>

