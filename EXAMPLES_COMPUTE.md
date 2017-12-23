## Compute Examples

Demonstrate the use of compute shaders to achieve effects

### [(Compute shader) Particle system](examples/compute/computeparticles.cpp)
<img src="./documentation/screenshots/compute_particles.jpg" height="96px" align="right">

Attraction based particle system. A shader storage buffer is used to store particle 
on which the compute shader does some physics calculations. The buffer is then used 
by the graphics pipeline for rendering with a gradient texture for. Demonstrates the 
use of memory barriers for synchronizing vertex buffer access between a compute and 
graphics pipeline
<br><br>

### [(Compute shader) Ray tracing](examples/compute/raytracing.cpp)
<img src="./documentation/screenshots/compute_raytracing.png" height="96px" align="right">

Implements a simple ray tracer using a compute shader. No primitives are rendered by 
the traditional pipeline except for a fullscreen quad that displays the ray traced 
results of the scene rendered by the compute shaders. Also implements shadows and 
basic reflections.
<br><br>

### [(Compute shader) Image processing](examples/compute/computeshader.cpp)
<img src="./documentation/screenshots/compute_imageprocessing.jpg" height="96px" align="right">

Demonstrates the use of a separate compute queue (and command buffer) to apply 
different convolution kernels on an input image in realtime.
<br><br>
