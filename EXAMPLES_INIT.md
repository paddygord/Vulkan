## Beginner Examples

### [Context](examples/init/context.cpp)

Basic example of creating a Vulkan instance, physical device, queue, command pool, 
etc.  However, it does not create a rendering window and produces no graphical 
output, instead printing out some basic information of about the current device.

### [Swap Chain](examples/init/swapchain.cpp)

Create a window and a Vulkan swap chain connected to it.  Uses an empty command 
buffer to clear the frame with a different color for each swap chain image.  This is 
the most basic possible application that colors any pixels on a window surface.

### [Triangle](examples/init/triangle.cpp)
<img src="./documentation/screenshots/basic_triangle.png" height="96px" align="right">

Most basic example that renders geometry. Renders a colored triangle using an 
indexed vertex buffer. Vertex and index data are uploaded to device local memory 
using so-called "staging buffers". Uses a single pipeline with basic shaders loaded 
from SPIR-V and and single uniform block for passing matrices that is updated on 
changing the view.

This example is far more explicit than the other examples and is meant to be a 
starting point for learning Vulkan from the ground up. Much of the code is 
boilerplate that you'd usually encapsulate in helper functions and classes (which is 
what the other examples do).
<br><br>

### [Triangle Revisited](examples/init/triangleRevisited.cpp)
<img src="./documentation/screenshots/basic_triangle.png" height="96px" align="right">

A repeat of the triangle example, except this time using the base class that will be 
used for all future examples.  Much of the boilerplate from the previous example has 
been moved into the base class or helper functions.  

This is the first example that allows you to resize the window, demonstrating the 
ability to create the swap chain and any objects which depend on the swap chain.

### [Triangle Animated](examples/init/triangleAnimated.cpp)
<img src="./documentation/screenshots/basic_triangle.png" height="96px" align="right">

Another repeat of the triangle example, this time showing a mechanism by which we 
can make modifications each frame to a uniform buffer containing the projection and 
view matrices.  

