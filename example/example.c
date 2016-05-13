#include <stdio.h>
#include "glad/glad.h"
#include "GLFW/glfw3.h"

#define LIGHTMAPPER_IMPLEMENTATION
#include "../lightmapper.h"

/*static matrix4_t firstPersonCameraView(GLFWwindow *window, bool capture, size_t frame)
{
	static vec3_t cameraPosition(0.0f, 8.0f, 60.0f);
	static float cameraRotationX = 0.0f, cameraRotationY = 0.0f;
	static double lastMouseX = 0, lastMouseY = 0;

	double mouseX, mouseY;
	glfwGetCursorPos(window, &mouseX, &mouseY);
	double mouseDx = mouseX - lastMouseX, mouseDy = mouseY - lastMouseY;
	lastMouseX = mouseX; lastMouseY = mouseY;
	if (capture && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS)
	{
		cameraRotationX += (float)mouseDy * -0.2f;
		cameraRotationY += (float)mouseDx * -0.2f;
	}

	vec3_t movement(0.0f, 0.0f, 0.0f);
	if (capture)
	{
		if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) movement += vec3_t(0.0f, 0.0f, -1.0f);
		if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) movement += vec3_t(0.0f, 0.0f, 1.0f);
		if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) movement += vec3_t(-1.0f, 0.0f, 0.0f);
		if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) movement += vec3_t(1.0f, 0.0f, 0.0f);
		if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) movement += vec3_t(0.0f, -1.0f, 0.0f);
		if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) movement += vec3_t(0.0f, 1.0f, 0.0f);
	}

	matrix4_t rotationYX = rotation(cameraRotationY, vec3_t(0.0f, 1.0f, 0.0f)) * rotation(cameraRotationX, vec3_t(1.0f, 0.0f, 0.0f));
	float speed = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) ? 1.0f : 0.1f;
	cameraPosition += transformPosition(&rotationYX, movement) * speed;

	return transpose(&rotationYX) * translation(-cameraPosition); // inverse(translation(cameraPosition) * rotationYX);
}*/

static void error_callback(int error, const char *description)
{
	fprintf(stderr, "Error: %s\n", description);
}

static void APIENTRY debug_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *message, const void *userParam)
{
#ifndef GL_DEBUG_SEVERITY_NOTIFICATION
#define GL_DEBUG_SEVERITY_NOTIFICATION 0x826B
#endif
	if (severity != GL_DEBUG_SEVERITY_NOTIFICATION)
		fprintf(stderr, "Error: %s\n", message);
}

/*static void bake()
{
	lm_context *lightmapper = lmCreate(
		baking->hemisphereSize, baking->zNear, baking->zFar,
		scene->lighting.sky.color.r, scene->lighting.sky.color.g, scene->lighting.sky.color.b);
	if (!lightmapper)
	{
		fprintf(stderr, "Error: Could not initialize lightmapper.\n");
		return false;
	}
	
	lmSetTargetLightmap(lightmapper, lightmap, w, h, c);

	lmSetGeometry(lightmapper,
		(float*)(segment->frames[frame].matrices + piece->matrixIndex),
		(lm_type)positionAttrib->type, positions, segment->header->vertexSize,
		(lm_type)texcoordAttrib->type, texcoords, segment->header->vertexSize,
		piece->indexCount, LM_UNSIGNED_SHORT, segment->indices + piece->indexStart);

	int vp[4];
	matrix4_t view, projection;
	while (lmBegin(lightmapper, vp, view.m[0], projection.m[0]))
	{
		glViewport(vp[0], vp[1], vp[2], vp[3]);
		drawScene(scene, rendering, RENDER_LIGHTMAP, &view, &projection, frame);
		//if (lmProgress(lightmapper) % 9 == 0 && lightmapper->meshPosition.hemisphere.side == 0)
		//	printf("\r%d/%d    ", lmProgress(lightmapper), piece->indexCount);
		lmEnd(lightmapper);
	}
	//printf("\r%d/%d    \n", lmProgress(lightmapper), piece->indexCount);
	
	lmDestroy(lightmapper);

	// postprocess
	lmImageSmooth(temp[0], temp[1], w, h, c);
	if (baking->downsample)
	{
		lmImageSmooth(temp[1], temp[0], w, h, c);
		lmImageSmooth(temp[0], temp[1], w, h, c);
		lmImageDownsample(temp[1], temp[0], w, h, c);
		w /= 2; h /= 2;
		lmImageDilate(temp[0], temp[1], w, h, c);
	}
	lmImageDilate(temp[1], temp[0], w, h, c);
	lmImageDilate(temp[0], temp[1], w, h, c);
	lmImageDilate(temp[1], temp[0], w, h, c);
	lmImageDilate(temp[0], temp[1], w, h, c);
	lmImageDilate(temp[1], temp[0], w, h, c);
	lmImagePower(temp[0], w, h, c, 1.0f / 2.2f, 7);

	// set texture
	if (w != lightmap->w || h != lightmap->h || c != lightmap->c)
	{
		lightmap->w = w; lightmap->h = h; lightmap->c = c;
		lightmap->data = allocate(allocator, w * h * c); // doesn't free previous data
	}
	lmImageFtoUB(temp[0], (uint8_t*)lightmap->data, w, h, c, 1.0f);
	
	// TODO: upload lightmap here
}*/

int main(int argc, char* argv[])
{
	glfwSetErrorCallback(error_callback);
	if (!glfwInit())
		return 1;
	glfwWindowHint(GLFW_RED_BITS, 8);
	glfwWindowHint(GLFW_GREEN_BITS, 8);
	glfwWindowHint(GLFW_BLUE_BITS, 8);
	glfwWindowHint(GLFW_ALPHA_BITS, 8);
	glfwWindowHint(GLFW_DEPTH_BITS, 32);
	glfwWindowHint(GLFW_STENCIL_BITS, GLFW_DONT_CARE);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);
	glfwWindowHint(GLFW_SAMPLES, 4);
	GLFWwindow *window = glfwCreateWindow(1280, 800, "Lightmapper Example", NULL, NULL);
	if (!window)
		return 1;
	glfwMakeContextCurrent(window);
	gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
	glfwSwapInterval(1);

	// debug callback
	typedef void (APIENTRYP PFNGLDEBUGMESSAGECALLBACKPROC) (GLDEBUGPROC callback, const void *userParam);
	PFNGLDEBUGMESSAGECALLBACKPROC glDebugMessageCallback = (PFNGLDEBUGMESSAGECALLBACKPROC)glfwGetProcAddress("glDebugMessageCallback");
	if (glDebugMessageCallback)
		glDebugMessageCallback(debug_callback, NULL);
	else
		printf("No debug message callback :(\n");

	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();
		//if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
		//	bake();

		//matrix4_t view = firstPersonCameraView(window, !gui.active, frame);
		//matrix4_t projection = perspective(45.0f, (float)vp[2] / (float)vp[3], 1.0f, 1000.0f);
		// render scene here
		glfwSwapBuffers(window);
	}

	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}