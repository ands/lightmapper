# Lightmapper
lightmapper.h is a C/C++ single-file library for lightmap baking by using your existing OpenGL renderer.

To paste the implementation into your project, insert the following lines:
```
#define LIGHTMAPPER_IMPLEMENTATION
#include "lightmapper.h"
```

While the simple example application demonstrates the baking of ambient occlusion only, the library can also be used to precompute global illumination for static scene geometry by doing multiple bounces.

Any light shapes and emissive surfaces are supported as long as you draw them with their corresponding emissive light colors.

You may want to use [thekla_atlas](https://github.com/Thekla/thekla_atlas) or [uvatlas](https://uvatlas.codeplex.com/) to prepare your geometry for lightmapping.

# Example application
![Lightmapper Example Screenshot](https://github.com/ands/lightmapper/raw/master/example_images/example.png)
The provided [example application](https://github.com/ands/lightmapper/blob/master/example/example.c) should build on Windows/Linux/MacOSX.

Linux dependencies for glfw: xorg-dev libgl1-mesa-dev
```
git clone --recursive https://github.com/ands/lightmapper.git
cd lightmapper/example
cmake .
make
./example
```

# Example usage
```
lm_context *ctx = lmCreate(
	64,               // hemicube rendering resolution/quality
	0.001f, 100.0f,   // zNear, zFar
	1.0f, 1.0f, 1.0f, // sky/clear color
	2, 0.01f);        // hierarchical selective interpolation for speedup (passes, threshold)
if (!ctx)
{
	printf("Could not initialize lightmapper.\n");
	exit(-1);
}

for (int b = 0; b < bounces; b++)
{
	// render all geometry to their lightmaps
	for (int i = 0; i < meshes; i++)
	{
		memset(mesh[i].lightmap, 0, sizeof(float) * mesh[i].lightmapWidth * mesh[i].lightmapHeight * 3); // clear lightmap to black
		lmSetTargetLightmap(ctx, mesh[i].lightmap, mesh[i].lightmapWidth, mesh[i].lightmapHeight, 3);
		
		lmSetGeometry(ctx, mesh[i].modelMatrix,
			LM_FLOAT, (uint8_t*)mesh[i].vertices + positionOffset, vertexSize,
			LM_FLOAT, (uint8_t*)mesh[i].vertices + lightmapUVOffset, vertexSize,
			mesh[i].indexCount, LM_UNSIGNED_SHORT, mesh[i].indices);
	
		int vp[4];
		mat4 view, proj;
		while (lmBegin(&ctx, vp, &view[0][0], &proj[0][0]))
		{
			// don't glClear on your own here!
			glViewport(vp[0], vp[1], vp[2], vp[3]);
			drawScene(view, proj);
			printf("\r%6.2f%%", lmProgress(ctx) * 100.0f); // don't actually call printf that often ;) it's slow.
			lmEnd(&ctx);
		}
	}

	// postprocess and upload all lightmaps to the GPU now to use them for indirect lighting during the next bounce.
	for (int i = 0; i < meshes; i++)
	{
		// you can also call other lmImage* here to postprocess the lightmap.
		lmImageDilate(mesh[i].lightmap, temp, mesh[i].lightmapWidth, mesh[i].lightmapHeight, 3);
		lmImageDilate(temp, mesh[i].lightmap, mesh[i].lightmapWidth, mesh[i].lightmapHeight, 3);

		glBindTexture(GL_TEXTURE_2D, mesh[i].lightmapHandle);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, mesh[i].lightmapWidth, mesh[i].lightmapHeight, 0, GL_RGB, GL_FLOAT, data);
	}
}

lmDestroy(&ctx);

// gamma correct and save lightmaps to disk
for (int i = 0; i < meshes; i++)
{
	lmImagePower(mesh[i].lightmap, mesh[i].lightmapWidth, mesh[i].lightmapHeight, 3, 1.0f / 2.2f);
	lmImageSaveTGAf(mesh[i].lightmapFilename, mesh[i].lightmap, mesh[i].lightmapWidth, mesh[i].lightmapHeight, 3);
}
```

# Quality improvement
To improve the lightmapping quality on closed meshes it is recommended to disable backface culling and to write `(gl_FrontFacing ? 1.0 : 0.0)` into the alpha channel during scene rendering to mark valid and invalid geometry (look at [example.c](https://github.com/ands/lightmapper/blob/master/example/example.c) for more details). The lightmapper will use this information to discard lightmap texel results with too many invalid samples. These texels can then be filled in by calls to `lmImageDilate` during postprocessing.

# Selective hierarchical interpolation for speedup
The lightmapper can do several passes traversing the geometry to render to the lightmap.
If interpolation passes are turned on, only every (1 << interpolationPasses) texel gets rendered in the first pass.
Consecutive passes either render or interpolate the values in between based on the interpolationThreshold value.
If the neighbours deviate too much or don't exist yet, the texel gets also rendered, otherwise it is simply interpolated between the neighbours.
If interpolationPasses or interpolationThreshold are set too high, lighting variations can be missed.
If they are very low, very many lightmap texels get rendered, which is very expensive compared to interpolating them.
On the example above this technique gives about a ~3.3x speedup without degrading the quality.
Larger lightmaps can even get a ~10x speedup depending on the scene.
This image shows which texels get rendered (red) and which get interpolated (green) in the above example:

![Interpolated texels](https://github.com/ands/lightmapper/raw/master/example_images/debug_interpolation.png)

This technique is also described by Hugo Elias over [here](http://web.archive.org/web/20160311085440/http://freespace.virgin.net/hugo.elias/radiosity/radiosity.htm).

# Example media
The following video shows several lighting effects. The static/stationary and indirect lighting were precomputed with lightmapper.h:

<p align="center">
  <a href="https://www.youtube.com/watch?v=ukiTqj8JBKY">
    <img width="100%" src="https://img.youtube.com/vi/ukiTqj8JBKY/0.jpg" alt="Showing Some Editor Features (Master Thesis)"/>
  </a>
</p>

![Sphere Light](https://github.com/ands/lightmapper/raw/master/example_images/gazebo_light.png)
![Skybox](https://github.com/ands/lightmapper/raw/master/example_images/gazebo_skybox.png)
![Shadows and Reflections](https://github.com/ands/lightmapper/raw/master/example_images/gazebo_shadows_reflections.png)

# Thanks
- To Ignacio Castaño and Jonathan Blow for blogging about their lightmapping system for The Witness.
- To Hugo Elias who documented his radiosity light mapper and acceleration technique a long time ago.
- To Sean Barrett for his library design philosophy.
- To Nyamyam Ltd. for letting me use their engine and assets during my master's thesis for which I started my work on this library.
- To Apoorva Joshi for trying to find a graphics driver performance anomaly.
- To Teh_Bucket for this public domain gazebo model on OpenGameArt.org.
- To everybody else that I forgot :)
