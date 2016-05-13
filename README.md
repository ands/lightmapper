# lightmapper
A C/C++ single-file library for lightmap baking by using your existing OpenGL renderer.


While the example application demonstrates the baking of ambient occlusion only, the library can also be used to precompute global illumination for static scene geometry by doing multiple bounces.

Any light shapes and emissive surfaces are supported as long as you draw them using their emissive light colors.

You may want to use [thekla_atlas](https://github.com/Thekla/thekla_atlas) or [uvatlas](https://uvatlas.codeplex.com/) to prepare your geometry for lightmapping.

![Lightmapper Example Screenshot](https://github.com/ands/lightmapper/raw/master/example/demo.png)
