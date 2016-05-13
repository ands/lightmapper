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
```
git clone --recursive https://github.com/ands/lightmapper.git
cd lightmapper/example
cmake .
make
./example
```

# Example usage
```
lm_context *ctx = lmCreate(64, 0.001f, 100.0f, 1.0f, 1.0f, 1.0f); // rendering resolution/quality, zNear, zFar, sky/clear color
if (!ctx)
{
	printf("Could not initialize lightmapper.\n");
	exit(-1);
}
for (int b = 0; b < bounces; b++)
{
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
			lmEnd(&ctx);
		}
	
		// TODO: call lmImage* here to postprocess the lightmap (smoothing and dilation recommended!).
	}
	// TODO: upload all lightmaps to the GPU now to use them for indirect lighting during the next bounce.
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

# Example images
![Sphere Light](https://github.com/ands/lightmapper/raw/master/example_images/gazebo_light.png)
![Skybox](https://github.com/ands/lightmapper/raw/master/example_images/gazebo_skybox.png)
![Shadows and Reflections](https://github.com/ands/lightmapper/raw/master/example_images/gazebo_shadows_reflections.png)

# Thanks
- To Ignacio Castaño and Jonathan Blow for blogging about their lightmapping system for The Witness.
- To Sean Barrett for his library design philosophy.
- To Nyamyam Ltd. for letting me use their engine and assets during my master's thesis for which I started my work on this library.
- To Apoorva Joshi for trying to find a graphics driver performance anomaly.
- To Teh_Bucket for this public domain gazebo model on OpenGameArt.org.
- To everybody who I forgot :)
