#define _USE_MATH_DEFINES
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>
#include "glad/glad.h"
#include "GLFW/glfw3.h"

#define LIGHTMAPPER_IMPLEMENTATION
#define LM_DEBUG_INTERPOLATION
#include "../lightmapper.h"

#define YO_IMPLEMENTATION
#define YO_NOIMG
#include "yocto_obj.h"

typedef struct
{
	GLuint program;
	GLint u_lightmap;
	GLint u_projection;
	GLint u_view;
	GLuint vao, vbo;

	lm_vec3 *positions;
	lm_vec2 *texcoords;
	int vertices;

	float *data;
	GLuint texture;
	int w, h;
} lm_scene;

static int initScene(lm_scene *scene);
static void drawScene(lm_scene *scene, float *view, float *projection);
static void destroyScene(lm_scene *scene);


#define TA_DEBUG

typedef struct
{
	int Aindex;
	short w, x, h, hflip;
	//       C           -
	//     * |  *        | h
	//   *   |     *     |
	// B-----+--------A  -
	// '--x--'
	// '-------w------'
} ta_entry;

static int ta_entry_cmp(const void *a, const void *b)
{
	ta_entry *ea = (ta_entry*)a;
	ta_entry *eb = (ta_entry*)b;
	int dh = eb->h - ea->h;
	int dw = eb->w - ea->w;
	return dh != 0 ? dh : dw;
}

#ifdef TA_DEBUG
static void ta_line(unsigned char *data, int w, int h,
                    int x0, int y0, int x1, int y1,
                    unsigned char r, unsigned char g, unsigned char b)
{
	int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
	int dy = abs(y1 - y0), sy = y0 < y1 ? 1 : -1; 
	int err = (dx > dy ? dx : -dy) / 2, e2;
	for(;;)
	{
		unsigned char *p = data + (y0 * w + x0) * 3;
		p[0] = r; p[1] = g; p[2] = b;

		if (x0 == x1 && y0 == y1) break;
		e2 = err;
		if (e2 > -dx) { err -= dy; x0 += sx; }
		if (e2 <  dy) { err += dx; y0 += sy; }
	}
}
#endif

static void ta_wave_surge(int *wave, int x0, int y0, int x1, int y1)
{
	int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
	int dy = abs(y1 - y0), sy = y0 < y1 ? 1 : -1; 
	int err = (dx > dy ? dx : -dy) / 2, e2;
	for(;;)
	{
		wave[y0] = x0;
		if (x0 == x1 && y0 == y1) break;
		e2 = err;
		if (e2 > -dx) { err -= dy; x0 += sx; }
		if (e2 <  dy) { err += dx; y0 += sy; }
	}
}

static int ta_max(int a, int b) { return a > b ? a : b; }
static int ta_min(int a, int b) { return a > b ? b : a; }

static int ta_wave_wash_up(int *wave, int height, int y0, int x1, int y1, int spacing)
{
	int x0 = 0;
	int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
	int dy = abs(y1 - y0), sy = y0 < y1 ? 1 : -1; 
	int err = (dx > dy ? dx : -dy) / 2, e2;
	int x = 0;
	for(;;)
	{
		int xDistance = wave[y0] - x0 - x; //wave[y0] - x0 - x;
		for (int y = ta_max(y0 - spacing, 0); y <= ta_min(y0 + spacing, height - 1); y++)
			xDistance = ta_max(wave[y] - x0 - x, xDistance);
		if (xDistance >= 0)
			x += xDistance;
		if (x0 == x1 && y0 == y1) break;
		e2 = err;
		if (e2 > -dx) { err -= dy; x0 += sx; }
		if (e2 <  dy) { err += dx; y0 += sy; }
	}
	return x;
}

static int ta_pack(lm_vec3 *p, int count, int width, int height, int spacing, float scale, lm_vec2 *uv)
{
	ta_entry *entries = LM_CALLOC(count / 3, sizeof(ta_entry));
	for (int i = 0; i < count / 3; i++)
	{
		lm_vec3 tp[3], tv[3];
		tp[0] = lm_scale3(p[i * 3 + 0], scale);
		tp[1] = lm_scale3(p[i * 3 + 1], scale);
		tp[2] = lm_scale3(p[i * 3 + 2], scale);
		tv[0] = lm_sub3(tp[1], tp[0]);
		tv[1] = lm_sub3(tp[2], tp[1]);
		tv[2] = lm_sub3(tp[0], tp[2]);
		float tvlsq[3] = { lm_length3sq(tv[0]), lm_length3sq(tv[1]), lm_length3sq(tv[2]) };
		
		// find long edge
		int maxi;        float maxl = tvlsq[0]; maxi = 0;
		if (tvlsq[1] > maxl) { maxl = tvlsq[1]; maxi = 1; }
		if (tvlsq[2] > maxl) { maxl = tvlsq[2]; maxi = 2; }
		int nexti = (maxi + 1) % 3;
		
		// measure triangle
		float w = sqrtf(maxl);
		float x = -lm_dot3(tv[maxi], tv[nexti]) / w;
		float h = lm_length3(lm_sub3(lm_add3(tv[maxi], tv[nexti]), lm_scale3(lm_normalize3(tv[maxi]), w - x)));

		// store entry
		ta_entry *e = entries + i;
		e->Aindex = i * 3 + maxi;
		e->w = (int)ceilf(w);
		e->x = (int)ceilf(x);
		e->h = (int)ceilf(h);
		e->hflip = 0;
	}
	qsort(entries, count / 3, sizeof(ta_entry), ta_entry_cmp);
	
	lm_vec2 uvScale = lm_v2(1.0f / width, 1.0f / height);

#ifdef TA_DEBUG
	unsigned char *data = LM_CALLOC(width * height, 3);
#endif
	
	int y = spacing;
	int ch = entries[0].h;
	int processed;
	bool flipped = false;
	int *wave = LM_CALLOC(height, sizeof(int));
	for (int i = 0; i < height; i++)
		wave[i] = spacing;
	int prevSlopeSpacing = 0;
	for (processed = 0; processed < count / 3; processed++)
	{
		ta_entry *e = entries + processed;
		if (flipped)
		{
			int yf = y + ch - e->h;
			int x      = ta_wave_wash_up(wave, height, yf + e->h, e->x       , yf, spacing);
			int xHflip = ta_wave_wash_up(wave, height, yf + e->h, e->w - e->x, yf, spacing);
			if (xHflip < x || (xHflip == x && e->x > e->w / 2))
			{
				x = xHflip;
				e->hflip = 1;
				e->x = e->w - e->x;
			}
			if (x + e->w + spacing > width)
			{
				x = spacing;
				y += ch + spacing + 1;
				yf = y;
				ch = e->h;
				if (y + e->h + spacing > height)
					break;
			}
			ta_wave_surge(wave, x + e->x + spacing + 1, yf, x + e->w + spacing + 1, yf + e->h);

#ifdef TA_DEBUG
			ta_line(data, width, height, x       , yf + e->h, x + e->w, yf + e->h, 255, 255, 255);
			ta_line(data, width, height, x       , yf + e->h, x + e->x, yf       , 255, 255, 255);
			ta_line(data, width, height, x + e->x, yf       , x + e->w, yf + e->h, 255, 255, 255);
#endif
			
			// calc & store UVs
			int tri = e->Aindex - (e->Aindex % 3);
			int Ai = e->Aindex;
			int Bi = tri + ((e->Aindex + 1) % 3);
			int Ci = tri + ((e->Aindex + 2) % 3);
			if (e->hflip) LM_SWAP(int, Ai, Bi);
			uv[Ai] = lm_mul2(lm_v2i(x + e->w, yf + e->h), uvScale);
			uv[Bi] = lm_mul2(lm_v2i(x       , yf + e->h), uvScale);
			uv[Ci] = lm_mul2(lm_v2i(x + e->x, yf       ), uvScale);
		}
		else
		{
			int x      = ta_wave_wash_up(wave, height, y, e->x       , y + e->h, spacing);
			int xHflip = ta_wave_wash_up(wave, height, y, e->w - e->x, y + e->h, spacing);
			if (xHflip < x || (xHflip == x && e->x > e->w / 2))
			{
				x = xHflip;
				e->hflip = 1;
				e->x = e->w - e->x;
			}
			if (x + e->w + spacing > width)
			{
				x = spacing;
				y += ch + spacing + 1;
				ch = e->h;
				if (y + e->h + spacing > height)
					break;
			}
			ta_wave_surge(wave, x + e->x + spacing + 1, y + e->h, x + e->w + spacing + 1, y);

#ifdef TA_DEBUG
			ta_line(data, width, height, x       , y       , x + e->w, y       , 255, 255, 255);
			ta_line(data, width, height, x       , y       , x + e->x, y + e->h, 255, 255, 255);
			ta_line(data, width, height, x + e->x, y + e->h, x + e->w, y       , 255, 255, 255);
#endif
			
			// calc & store UVs
			int tri = e->Aindex - (e->Aindex % 3);
			int Ai = e->Aindex;
			int Bi = tri + ((e->Aindex + 1) % 3);
			int Ci = tri + ((e->Aindex + 2) % 3);
			if (e->hflip) LM_SWAP(int, Ai, Bi);
			uv[Ai] = lm_mul2(lm_v2i(x + e->w, y       ), uvScale);
			uv[Bi] = lm_mul2(lm_v2i(x       , y       ), uvScale);
			uv[Ci] = lm_mul2(lm_v2i(x + e->x, y + e->h), uvScale);
		}
		flipped = !flipped;
	}

#ifdef TA_DEBUG
	for (int i = 0; i < height; i++)
	{
		int x = wave[i] - spacing;
		while (x < width)
			data[(i * width + x++) * 3] = 255;
	}
	if (lmImageSaveTGAub("debug_triangle_packing.tga", data, width, height, 3))
		printf("Saved debug_triangle_packing.tga\n");
	LM_FREE(data);
#endif

	LM_FREE(wave);
	LM_FREE(entries);
	
	return processed * 3;
}

static void injectDirectLighting(lm_scene *scene)
{
	for (int i = 400; i < 600; i++)
	{
		for (int j = 200; j < 400; j++)
		{
			scene->data[(i * scene->w + j) * 4 + 0] = 0.0f;
			scene->data[(i * scene->w + j) * 4 + 1] = 0.5f;
			scene->data[(i * scene->w + j) * 4 + 2] = 1.0f;
			scene->data[(i * scene->w + j) * 4 + 3] = 1.0f;
		}
	}
}

static float sky[] = { 1.0f, 0.8f, 0.3f };
static float bakeScale = 0.01f;

static int bake(lm_scene *scene)
{
	lm_context *ctx = lmCreate(
		128,                    // hemisphere resolution (power of two, max=512)
		0.0001f, 100.0f,        // zNear, zFar of hemisphere cameras
		bakeScale * sky[0], bakeScale * sky[1], bakeScale * sky[2], // background color (white for ambient occlusion)
		2, 0.01f);              // lightmap interpolation & threshold (small differences are interpolated rather than sampled)
	                            // check debug_interpolation.tga for an overview of sampled (red) vs interpolated (green) pixels.
	if (!ctx)
	{
		fprintf(stderr, "Error: Could not initialize lightmapper.\n");
		return 0;
	}
	
	double bakeStart = glfwGetTime();

	float *data = scene->data;
	int w = scene->w, h = scene->h;

	memset(data, 0, sizeof(float) * w * h * 4); // clear lightmap before rendering to it.
	lmSetTargetLightmap(ctx, data, w, h, 4);

	lmSetGeometry(ctx, NULL,
		LM_FLOAT, scene->positions, 0,
		LM_FLOAT, scene->texcoords, 0,
		scene->vertices, LM_NONE, 0);

	int vp[4];
	float view[16], projection[16];
	double lastUpdateTime = 0.0;
	while (lmBegin(ctx, vp, view, projection))
	{
		// render to lightmapper framebuffer
		glViewport(vp[0], vp[1], vp[2], vp[3]);
		drawScene(scene, view, projection);

		// display progress every second (printf is expensive)
		double time = glfwGetTime();
		if (time - lastUpdateTime > 1.0)
		{
			lastUpdateTime = time;
			printf("\r%6.2f%%", lmProgress(ctx) * 100.0f);
			fflush(stdout);
		}

		lmEnd(ctx);
	}
	printf("\rFinished baking %d triangles.\n", scene->vertices / 3);
	printf("Baking took %.1fs.\n", (float)(glfwGetTime() - bakeStart));

	// postprocess texture
	float *temp = calloc(w * h * 4, sizeof(float));
	lmImageSmooth(data, temp, w, h, 4);
	lmImageDilate(temp, data, w, h, 4);
	for (int i = 0; i < 16; i++)
	{
		lmImageDilate(data, temp, w, h, 4);
		lmImageDilate(temp, data, w, h, 4);
	}
	lmImagePower(data, w, h, 4, 1.0f / 2.2f, 0x7); // gamma correct color channels
	free(temp);

	injectDirectLighting(scene);

	// save result to a file
	if (lmImageSaveTGAf("result.tga", data, w, h, 4, 1.0f))
		printf("Saved result.tga\n");

	// upload result
	glBindTexture(GL_TEXTURE_2D, scene->texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_FLOAT, data);

	lmDestroy(ctx);
	return 1;
}

static void error_callback(int error, const char *description)
{
	fprintf(stderr, "Error: %s\n", description);
}

static void fpsCameraViewMatrix(GLFWwindow *window, float *view);
static void perspectiveMatrix(float *out, float fovy, float aspect, float zNear, float zFar);

int main(int argc, char* argv[])
{
	glfwSetErrorCallback(error_callback);
	if (!glfwInit()) return 1;
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
	GLFWwindow *window = glfwCreateWindow(1024, 768, "Lightmapping Example", NULL, NULL);
	if (!window) return 1;
	glfwMakeContextCurrent(window);
	gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
	glfwSwapInterval(1);

	lm_scene scene = {0};
	if (!initScene(&scene))
	{
		fprintf(stderr, "Could not initialize scene.\n");
		return 1;
	}

	printf("Ambient Occlusion Baking Example.\n");
	printf("Use your mouse and the W, A, S, D, E, Q keys to navigate.\n");
	printf("Press SPACE to start baking one light bounce!\n");
	printf("This will take a few seconds and bake a lightmap illuminated by:\n");
	printf("1. The mesh itself (initially black)\n");
	printf("2. A white sky (1.0f, 1.0f, 1.0f)\n");

	while (!glfwWindowShouldClose(window))
	{
		printf("A\n");
		glfwPollEvents();
		printf("B\n");
		if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
			bake(&scene);
		printf("C\n");
		int w, h;
		glfwGetFramebufferSize(window, &w, &h);
		glViewport(0, 0, w, h);
		printf("D\n");
		// camera for glfw window
		float view[16], projection[16];
		fpsCameraViewMatrix(window, view);
		perspectiveMatrix(projection, 45.0f, (float)w / (float)h, 0.01f, 100.0f);
		printf("E\n");
		// draw to screen with a blueish sky
		glClearColor(sky[0], sky[1], sky[2], 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		drawScene(&scene, view, projection);
		printf("F\n");

		glfwSwapBuffers(window);
	}

	destroyScene(&scene);
	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}

// helpers ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static GLuint loadProgram(const char *vp, const char *fp, const char **attributes, int attributeCount);

static int initScene(lm_scene *scene)
{
	// load obj
	yo_scene *yo = yo_load_obj("city.obj", true, false);
	if (!yo || !yo->nshapes)
	{
		fprintf(stderr, "Error loading obj file\n");
		return 0;
	}

	scene->vertices = 0;
	for (int i = 0; i < yo->nshapes; i++)
		scene->vertices += yo->shapes[i].nelems * 3;
	
	scene->positions = LM_CALLOC(scene->vertices, sizeof(lm_vec3));
	scene->texcoords = LM_CALLOC(scene->vertices, sizeof(lm_vec2));
	size_t positionsSize = scene->vertices * sizeof(lm_vec3);
	size_t texcoordsSize = scene->vertices * sizeof(lm_vec2);
	
	int n = 0;
	for (int i = 0; i < yo->nshapes; i++)
	{
		yo_shape *shape = yo->shapes + i;
		for (int j = 0; j < shape->nelems * 3; j++)
			scene->positions[n + j] = *(lm_vec3*)&shape->pos[shape->elem[j] * 3];
		n += shape->nelems * 3;
	}
	yo_free_scene(yo);
	
	// create lightmap texture atlas
	scene->w = 1024;
	scene->h = 1024;
	int processed = ta_pack(scene->positions, scene->vertices, scene->w, scene->h, 2, 136.0f/*375.0f*/, scene->texcoords);
	if (processed < scene->vertices)
	{
		fprintf(stderr, "Could not pack all triangles into the lightmap! (%d/%d)\n", processed / 3, scene->vertices / 3);
		return 0;
	}
	
	// upload geometry to opengl
	glGenVertexArrays(1, &scene->vao);
	glBindVertexArray(scene->vao);

	glGenBuffers(1, &scene->vbo);
	glBindBuffer(GL_ARRAY_BUFFER, scene->vbo);
	glBufferData(GL_ARRAY_BUFFER, positionsSize + texcoordsSize, NULL, GL_STATIC_DRAW);
	unsigned char *buffer = (unsigned char*)glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
	assert(buffer);
	memcpy(buffer                , scene->positions, positionsSize);
	memcpy(buffer + positionsSize, scene->texcoords, texcoordsSize);
	glUnmapBuffer(GL_ARRAY_BUFFER);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, (void*)positionsSize);

	// create black lightmap textures
	scene->data = calloc(scene->w * scene->h * 4, sizeof(float));
	injectDirectLighting(scene);

	glGenTextures(1, &scene->texture);
	glBindTexture(GL_TEXTURE_2D, scene->texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, scene->w, scene->h, 0, GL_RGBA, GL_FLOAT, scene->data);

	// load shader
	const char *vp =
		"#version 150 core\n"
		"in vec3 a_position;\n"
		"in vec2 a_texcoord;\n"
		"uniform mat4 u_view;\n"
		"uniform mat4 u_projection;\n"
		"out vec2 v_texcoord;\n"

		"void main()\n"
		"{\n"
		"gl_Position = u_projection * (u_view * vec4(a_position, 1.0));\n"
		"v_texcoord = a_texcoord;\n"
		"}\n";

	const char *fp =
		"#version 150 core\n"
		"in vec2 v_texcoord;\n"
		"uniform sampler2D u_lightmap;\n"
		"out vec4 o_color;\n"

		"void main()\n"
		"{\n"
		"o_color = vec4(texture(u_lightmap, v_texcoord).rgb, gl_FrontFacing ? 1.0 : 0.0);\n"
		"}\n";

	const char *attribs[] =
	{
		"a_position",
		"a_texcoord"
	};

	scene->program = loadProgram(vp, fp, attribs, 2);
	if (!scene->program)
	{
		fprintf(stderr, "Error loading shader\n");
		return 0;
	}
	scene->u_view = glGetUniformLocation(scene->program, "u_view");
	scene->u_projection = glGetUniformLocation(scene->program, "u_projection");
	scene->u_lightmap = glGetUniformLocation(scene->program, "u_lightmap");

	return 1;
}

static void drawScene(lm_scene *scene, float *view, float *projection)
{
	glEnable(GL_DEPTH_TEST);

	glUseProgram(scene->program);
	glUniform1i(scene->u_lightmap, 0);
	glUniformMatrix4fv(scene->u_projection, 1, GL_FALSE, projection);
	glUniformMatrix4fv(scene->u_view, 1, GL_FALSE, view);

	glBindTexture(GL_TEXTURE_2D, scene->texture);
	glBindVertexArray(scene->vao);
	glDrawArrays(GL_TRIANGLES, 0, scene->vertices);
}

static void destroyScene(lm_scene *scene)
{
	glDeleteVertexArrays(1, &scene->vao);
	glDeleteBuffers(1, &scene->vbo);
	glDeleteTextures(1, &scene->texture);
	glDeleteProgram(scene->program);
	free(scene->positions);
	free(scene->texcoords);
	free(scene->data);
}

static GLuint loadShader(GLenum type, const char *source)
{
	GLuint shader = glCreateShader(type);
	if (shader == 0)
	{
		fprintf(stderr, "Could not create shader!\n");
		return 0;
	}
	glShaderSource(shader, 1, &source, NULL);
	glCompileShader(shader);
	GLint compiled;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
	if (!compiled)
	{
		fprintf(stderr, "Could not compile shader!\n");
		GLint infoLen = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
		if (infoLen)
		{
			char* infoLog = (char*)malloc(infoLen);
			glGetShaderInfoLog(shader, infoLen, NULL, infoLog);
			fprintf(stderr, "%s\n", infoLog);
			free(infoLog);
		}
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}
static GLuint loadProgram(const char *vp, const char *fp, const char **attributes, int attributeCount)
{
	GLuint vertexShader = loadShader(GL_VERTEX_SHADER, vp);
	if (!vertexShader)
		return 0;
	GLuint fragmentShader = loadShader(GL_FRAGMENT_SHADER, fp);
	if (!fragmentShader)
	{
		glDeleteShader(vertexShader);
		return 0;
	}

	GLuint program = glCreateProgram();
	if (program == 0)
	{
		fprintf(stderr, "Could not create program!\n");
		return 0;
	}
	glAttachShader(program, vertexShader);
	glAttachShader(program, fragmentShader);
    
    for (int i = 0; i < attributeCount; i++)
        glBindAttribLocation(program, i, attributes[i]);
    
	glLinkProgram(program);
	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);
	GLint linked;
	glGetProgramiv(program, GL_LINK_STATUS, &linked);
	if (!linked)
	{
		fprintf(stderr, "Could not link program!\n");
		GLint infoLen = 0;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLen);
		if (infoLen)
		{
			char* infoLog = (char*)malloc(sizeof(char) * infoLen);
			glGetProgramInfoLog(program, infoLen, NULL, infoLog);
			fprintf(stderr, "%s\n", infoLog);
			free(infoLog);
		}
		glDeleteProgram(program);
		return 0;
	}
	return program;
}

#define LME_PI 3.14159265358979323846f

static void multiplyMatrices(float *out, float *a, float *b)
{
	for (int y = 0; y < 4; y++)
		for (int x = 0; x < 4; x++)
			out[y * 4 + x] = a[x] * b[y * 4] + a[4 + x] * b[y * 4 + 1] + a[8 + x] * b[y * 4 + 2] + a[12 + x] * b[y * 4 + 3];
}
static void translationMatrix(float *out, float x, float y, float z)
{
	out[ 0] = 1.0f; out[ 1] = 0.0f; out[ 2] = 0.0f; out[ 3] = 0.0f;
	out[ 4] = 0.0f; out[ 5] = 1.0f; out[ 6] = 0.0f; out[ 7] = 0.0f;
	out[ 8] = 0.0f; out[ 9] = 0.0f; out[10] = 1.0f; out[11] = 0.0f;
	out[12] = x;    out[13] = y;    out[14] = z;    out[15] = 1.0f;
}
static void rotationMatrix(float *out, float angle, float x, float y, float z)
{
	angle *= LME_PI / 180.0f;
	float c = cosf(angle), s = sinf(angle), c2 = 1.0f - c;
	out[ 0] = x*x*c2 + c;   out[ 1] = y*x*c2 + z*s; out[ 2] = x*z*c2 - y*s; out[ 3] = 0.0f;
	out[ 4] = x*y*c2 - z*s; out[ 5] = y*y*c2 + c;   out[ 6] = y*z*c2 + x*s; out[ 7] = 0.0f;
	out[ 8] = x*z*c2 + y*s; out[ 9] = y*z*c2 - x*s; out[10] = z*z*c2 + c;   out[11] = 0.0f;
	out[12] = 0.0f;         out[13] = 0.0f;         out[14] = 0.0f;         out[15] = 1.0f;
}
static void transformPosition(float *out, float *m, float *p)
{
	float d = 1.0f / (m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15]);
	out[2] =     d * (m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]);
	out[1] =     d * (m[1] * p[0] + m[5] * p[1] + m[ 9] * p[2] + m[13]);
	out[0] =     d * (m[0] * p[0] + m[4] * p[1] + m[ 8] * p[2] + m[12]);
}
static void transposeMatrix(float *out, float *m)
{
	out[ 0] = m[0]; out[ 1] = m[4]; out[ 2] = m[ 8]; out[ 3] = m[12];
	out[ 4] = m[1]; out[ 5] = m[5]; out[ 6] = m[ 9]; out[ 7] = m[13];
	out[ 8] = m[2]; out[ 9] = m[6]; out[10] = m[10]; out[11] = m[14];
	out[12] = m[3]; out[13] = m[7]; out[14] = m[11]; out[15] = m[15];
}
static void perspectiveMatrix(float *out, float fovy, float aspect, float zNear, float zFar)
{
	float f = 1.0f / tanf(fovy * LME_PI / 360.0f);
	float izFN = 1.0f / (zNear - zFar);
	out[ 0] = f / aspect; out[ 1] = 0.0f; out[ 2] = 0.0f;                       out[ 3] = 0.0f;
	out[ 4] = 0.0f;       out[ 5] = f;    out[ 6] = 0.0f;                       out[ 7] = 0.0f;
	out[ 8] = 0.0f;       out[ 9] = 0.0f; out[10] = (zFar + zNear) * izFN;      out[11] = -1.0f;
	out[12] = 0.0f;       out[13] = 0.0f; out[14] = 2.0f * zFar * zNear * izFN; out[15] = 0.0f;
}

static void fpsCameraViewMatrix(GLFWwindow *window, float *view)
{
	// initial camera config
	static float position[] = { 0.0f, 0.3f, 1.5f };
	static float rotation[] = { 0.0f, 0.0f };

	// mouse look
	static double lastMouse[] = { 0.0, 0.0 };
	double mouse[2];
	glfwGetCursorPos(window, &mouse[0], &mouse[1]);
	if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS)
	{
		rotation[0] += (float)(mouse[1] - lastMouse[1]) * -0.2f;
		rotation[1] += (float)(mouse[0] - lastMouse[0]) * -0.2f;
	}
	lastMouse[0] = mouse[0];
	lastMouse[1] = mouse[1];

	float rotationY[16], rotationX[16], rotationYX[16];
	rotationMatrix(rotationX, rotation[0], 1.0f, 0.0f, 0.0f);
	rotationMatrix(rotationY, rotation[1], 0.0f, 1.0f, 0.0f);
	multiplyMatrices(rotationYX, rotationY, rotationX);

	// keyboard movement (WSADEQ)
	float speed = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) ? 0.1f : 0.01f;
	float movement[3] = {0};
	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) movement[2] -= speed;
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) movement[2] += speed;
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) movement[0] -= speed;
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) movement[0] += speed;
	if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) movement[1] -= speed;
	if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) movement[1] += speed;

	float worldMovement[3];
	transformPosition(worldMovement, rotationYX, movement);
	position[0] += worldMovement[0];
	position[1] += worldMovement[1];
	position[2] += worldMovement[2];

	// construct view matrix
	float inverseRotation[16], inverseTranslation[16];
	transposeMatrix(inverseRotation, rotationYX);
	translationMatrix(inverseTranslation, -position[0], -position[1], -position[2]);
	multiplyMatrices(view, inverseRotation, inverseTranslation); // = inverse(translation(position) * rotationYX);
}
