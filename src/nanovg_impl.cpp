#define GLAD_GL_IMPLEMENTATION
#include <glad/gl.h>               // GL prototypes for the backend

// Define this before including NanoVG headers
#define NANOVG_GL3_IMPLEMENTATION

// Include NanoVG with clean paths (thanks to CMake include directories)
#include "nanovg.h"
#include "nanovg_gl.h"
#include "nanovg_gl_utils.h"
