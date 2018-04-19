## Offscreen Rendering Examples

Demonstrate the use of offsreen framebuffer for rendering effects

### [Offscreen rendering](examples/offscreen/offscreen.cpp)
<img src="./documentation/screenshots/basic_offscreen.png" height="96px" align="right">

Uses a separate framebuffer (that is not part of the swap chain) and a texture 
target for offscreen rendering. The texture is then used as a mirror.
<br><br>

### [Radial blur](examples/radialblur/radialblur.cpp)
<img src="./documentation/screenshots/radial_blur.png" height="96px" align="right">

Demonstrates basic usage of fullscreen shader effects. The scene is rendered 
offscreen first, gets blitted to a texture target and for the final draw this 
texture is blended on top of the 3D scene with a radial blur shader applied.
<br><br>

### [Bloom](examples/bloom/bloom.cpp)
<img src="./documentation/screenshots/bloom.png" height="96px" align="right">

Implements a bloom effect to simulate glowing parts of a 3D mesh. A two pass 
gaussian blur (horizontal and then vertical) is used to generate a blurred low res 
version of the scene only containing the glowing parts of the 3D mesh. This then 
gets blended onto the scene to add the blur effect.
<br><br>

### [Deferred shading](examples/deferred/deferred.cpp)
<img src="./documentation/screenshots/deferred_shading.png" height="96px" align="right">

Demonstrates the use of multiple render targets to fill a G-Buffer for deferred 
shading.

Deferred shading collects all values (color, normal, position) into different render 
targets in one pass thanks to multiple render targets, and then does all shading and 
lighting calculations based on these in screen space, thus allowing for much more 
light sources than traditional forward renderers.
<br><br>

### [Shadowmapping](examples/shadowmapping/shadowmapping.cpp)
<img src="./documentation/screenshots/shadowmapping.png" height="96px" align="right">

Shows how to implement directional dynamic shadows with a single shadow map in two passes. Pass one renders the scene from the light's point of view and copies the depth buffer to a depth texture.
The second pass renders the scene from the camera's point of view using the depth texture to compare the depth value of the texels with the one stored in the depth texture to determine whether a texel is shadowed or not and also applies a PCF filter for smooth shadow borders.
To avoid shadow artifacts the dynamic depth bias state ([vkCmdSetDepthBias](https://www.khronos.org/registry/vulkan/specs/1.0/man/html/vkCmdSetDepthBias.html)) is used to apply a constant and slope dept bias factor.

<br><br>

### [Omnidirectional shadow mapping](examples/shadowmappingomni/shadowmappingomni.cpp)
<img src="./documentation/screenshots/shadow_omnidirectional.png" height="96px" align="right">

Uses a dynamic 32 bit floating point cube map for a point light source that casts shadows in all directions (unlike projective shadow mapping).
The cube map faces contain the distances from the light sources, which are then used in the scene rendering pass to determine if the fragment is shadowed or not.
<br><br>


