#include <stdio.h>
#define _USE_MATH_DEFINES
#include <math.h>
#include "glad/glad.h"
#include "GLFW/glfw3.h"

#define LIGHTMAPPER_IMPLEMENTATION
#include "../lightmapper.h"

typedef struct {
	float p[3];
	float t[2];
} vertex_t;
static int loadSimpleObjFile(const char *filename, vertex_t **vertices, unsigned int *vertexCount, unsigned short **indices, unsigned int *indexCount);

static GLuint loadProgram(const char *vp, const char *fp);

static void fpsCameraViewMatrix(GLFWwindow *window, float *view);
static void perspectiveMatrix(float *out, float fovy, float aspect, float zNear, float zFar);

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

typedef struct
{
	GLuint program;
	GLint u_lightmap;
	GLint u_projection;
	GLint u_view;
	
	GLuint lightmap;
	GLuint vao, vbo, ibo;
	vertex_t *vertices;
	unsigned short *indices;
	unsigned int vertexCount, indexCount;
} scene_t;

static void drawScene(scene_t *scene, float *view, float *projection)
{
	glEnable(GL_DEPTH_TEST);

	glUseProgram(scene->program);
	glUniform1i(scene->u_lightmap, 0);
	glUniformMatrix4fv(scene->u_projection, 1, GL_FALSE, projection);
	glUniformMatrix4fv(scene->u_view, 1, GL_FALSE, view);

	glBindTexture(GL_TEXTURE_2D, scene->lightmap);

	glBindVertexArray(scene->vao);
	glDrawElements(GL_TRIANGLES, scene->indexCount, GL_UNSIGNED_SHORT, 0);
	glBindVertexArray(0);
}

static int bake(GLuint lightmap, int w, int h, vertex_t *vertices, unsigned short *indices, unsigned int indexCount, scene_t *scene)
{
	lm_context *lightmapper = lmCreate(
		32, 0.001f, 100.0f,
		1.0f, 1.0f, 1.0f);
	if (!lightmapper)
	{
		fprintf(stderr, "Error: Could not initialize lightmapper.\n");
		return 0;
	}
	
	float *data = calloc(w * h * 4, sizeof(float));
	lmSetTargetLightmap(lightmapper, data, w, h, 4);

	lmSetGeometry(lightmapper, NULL,
		LM_FLOAT, (unsigned char*)vertices + offsetof(vertex_t, p), sizeof(vertex_t),
		LM_FLOAT, (unsigned char*)vertices + offsetof(vertex_t, t), sizeof(vertex_t),
		indexCount, LM_UNSIGNED_SHORT, indices);

	int vp[4];
	float view[16], projection[16];
	while (lmBegin(lightmapper, vp, view, projection))
	{
		glViewport(vp[0], vp[1], vp[2], vp[3]);
		drawScene(scene, &view, &projection);
		if (lmProgress(lightmapper) % 9 == 0 && lightmapper->meshPosition.hemisphere.side == 0) // TODO
			printf("\r%d/%d    ", lmProgress(lightmapper), indexCount);
		lmEnd(lightmapper);
	}
	printf("\r%d/%d    \n", lmProgress(lightmapper), indexCount);
	
	lmDestroy(lightmapper);

	// postprocess
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

	// save result to a file
	lmImageSaveTGAf("result.tga", data, w, h, 4, 1.0f);

	// upload result
	glBindTexture(GL_TEXTURE_2D, lightmap);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_FLOAT, data);
	free(data);

	return 1;
}

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

	scene_t scene = {0};
	if (!loadSimpleObjFile("gazebo.obj", &scene.vertices, &scene.vertexCount, &scene.indices, &scene.indexCount))
	{
		fprintf(stderr, "Error loading obj file\n");
		exit(1);
	}

	glGenVertexArrays(1, &scene.vao);
	glBindVertexArray(scene.vao);
	
	glGenBuffers(1, &scene.vbo);
	glBindBuffer(GL_ARRAY_BUFFER, scene.vbo);
	glBufferData(GL_ARRAY_BUFFER, scene.vertexCount * sizeof(vertex_t), scene.vertices, GL_STATIC_DRAW);

	glGenBuffers(1, &scene.ibo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, scene.ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, scene.indexCount * sizeof(unsigned short), scene.indices, GL_STATIC_DRAW);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (void*)offsetof(vertex_t, p));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (void*)offsetof(vertex_t, t));

	glGenTextures(1, &scene.lightmap);
	glBindTexture(GL_TEXTURE_2D, scene.lightmap);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	unsigned char emissive[] = { 0, 0, 0, 255 };
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, emissive);

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

	scene.program = loadProgram(vp, fp);
	if (!scene.program)
	{
		fprintf(stderr, "Error loading shader\n");
		exit(1);
	}
	scene.u_view = glGetUniformLocation(scene.program, "u_view");
	scene.u_projection = glGetUniformLocation(scene.program, "u_projection");
	scene.u_lightmap = glGetUniformLocation(scene.program, "u_lightmap");

	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();
		if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
			bake(scene.lightmap, 654, 654, scene.vertices, scene.indices, scene.indexCount, &scene);

		int w, h;
		glfwGetFramebufferSize(window, &w, &h);
		glViewport(0, 0, w, h);

		float view[16], projection[16];
		fpsCameraViewMatrix(window, view);
		perspectiveMatrix(projection, 45.0f, (float)w / (float)h, 0.01f, 100.0f);
		
		glClearColor(0.6f, 0.8f, 1.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		drawScene(&scene, view, projection);

		glfwSwapBuffers(window);
	}

	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}

static int loadSimpleObjFile(const char *filename, vertex_t **vertices, unsigned int *vertexCount, unsigned short **indices, unsigned int *indexCount)
{
	FILE *file = fopen(filename, "rt");
	if (!file)
		return 0;
	char line[1024];

	// first pass
	unsigned int np = 0, nn = 0, nt = 0, nf = 0;
	while (!feof(file))
	{
		fgets(line, 1024, file);
		if (line[0] == '#') continue;
		if (line[0] == 'v')
		{
			if (line[1] == ' ') { np++; continue; }
			if (line[1] == 'n') { nn++; continue; }
			if (line[1] == 't') { nt++; continue; }
			assert(!"unknown vertex attribute");
		}
		if (line[0] == 'f') { nf++; continue; }
		assert(!"unknown identifier");
	}
	assert(np && np == nn && np == nt && nf); // only supports obj files without separately indexed vertex attributes

	// allocate memory
	*vertexCount = np;
	*vertices = calloc(np, sizeof(vertex_t));
	*indexCount = nf * 3;
	*indices = calloc(nf * 3, sizeof(unsigned short));

	// second pass
	fseek(file, 0, SEEK_SET);
	unsigned int cp = 0, cn = 0, ct = 0, cf = 0;
	while (!feof(file))
	{
		fgets(line, 1024, file);
		if (line[0] == '#') continue;
		if (line[0] == 'v')
		{
			if (line[1] == ' ') { float *p = (*vertices)[cp++].p; char *e1, *e2; p[0] = (float)strtod(line + 2, &e1); p[1] = (float)strtod(e1, &e2); p[2] = (float)strtod(e2, 0); continue; }
			if (line[1] == 'n') { /*float *n = (*vertices)[cn++].n; char *e1, *e2; n[0] = (float)strtod(line + 3, &e1); n[1] = (float)strtod(e1, &e2); n[2] = (float)strtod(e2, 0);*/ continue; }
			if (line[1] == 't') { float *t = (*vertices)[ct++].t; char *e1;      t[0] = (float)strtod(line + 3, &e1); t[1] = (float)strtod(e1, 0);                                continue; }
			assert(!"unknown vertex attribute");
		}
		if (line[0] == 'f')
		{
			unsigned short *tri = (*indices) + cf;
			cf += 3;
			char *e1, *e2, *e3 = line + 1;
			for (int i = 0; i < 3; i++)
			{
				unsigned long pi = strtoul(e3 + 1, &e1, 10);
				assert(e1[0] == '/');
				unsigned long ti = strtoul(e1 + 1, &e2, 10);
				assert(e2[0] == '/');
				unsigned long ni = strtoul(e2 + 1, &e3, 10);
				assert(pi == ti && pi == ni);
				tri[i] = (unsigned short)(pi - 1);
			}
			continue;
		}
		assert(!"unknown identifier");
	}

	fclose(file);
	return 1;
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
static GLuint loadProgram(const char *vp, const char *fp)
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
	angle *= (float)M_PI / 180.0f;
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
	float f = 1.0f / tanf(fovy * (float)M_PI / 360.0f);
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