#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/ioctl.h>

#include <linux/fb.h>
#include <sys/mman.h>

#include "bcm_host.h"

#include "GLES2/gl2.h"
#include "EGL/egl.h"
#include "EGL/eglext.h"

float shader_time=0;
//int refresh_delayed=0;
unsigned char *framebuffer;
unsigned char *fb0;
int screen_size, screen_size2;
int xres, yres;

typedef struct
{
   uint32_t screen_width;
   uint32_t screen_height;
// OpenGL|ES objects
   EGLDisplay display;
   EGLSurface surface;
   EGLContext context;

   GLuint tex;
   GLuint tex_fb;
   GLuint vshader;
   GLuint fshader;
   GLuint program;
   GLuint buf;
// shader attribs
   GLuint attr_vertex, unif_res, unif_res2, unif_time, unif_col, unif_tex;
} CUBE_STATE_T;

static CUBE_STATE_T _state, *state=&_state;

struct timeval curr_time, start_time, interval, last_refresh, time_since_refresh;

#define check() assert(glGetError() == 0)

int
timeval_subtract (result, x, y)
  struct timeval *result, *x, *y;
  {
    /* Perform the carry for the later subtraction by updating y. */
    if (x->tv_usec < y->tv_usec) {
      int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
      y->tv_usec -= 1000000 * nsec;
      y->tv_sec += nsec;
    }
    if (x->tv_usec - y->tv_usec > 1000000) {
      int nsec = (x->tv_usec - y->tv_usec) / 1000000;
      y->tv_usec += 1000000 * nsec;
      y->tv_sec -= nsec;
    }
  
    /* Compute the time remaining to wait.
    tv_usec is certainly positive. */
    result->tv_sec = x->tv_sec - y->tv_sec;
    result->tv_usec = x->tv_usec - y->tv_usec;
  
    /* Return 1 if result is negative. */
    return x->tv_sec < y->tv_sec;
  }

static void showlog(GLuint shader)
{
   // Prints the compile log for a shader
   GLint loglen;
   GLchar *error_message;
   
   //glGetShaderiv (shader, GL_INFO_LOG_LENGTH, &loglen);
   glGetProgramiv (shader, GL_INFO_LOG_LENGTH, &loglen);

   error_message = calloc (loglen, sizeof (GLchar));

   //glGetShaderInfoLog(shader,loglen,NULL,error_message);
   glGetProgramInfoLog(shader,loglen,NULL,error_message);
   printf("%d:shader:\n%s\n", shader, error_message);
}

/***********************************************************
 * Name: init_ogl
 *
 * Arguments:
 *       CUBE_STATE_T *state - holds OGLES model info
 *
 * Description: Sets the display, OpenGL|ES context and screen stuff
 *
 * Returns: void
 *
 ***********************************************************/
   DISPMANX_RESOURCE_HANDLE_T screen_resource;
   DISPMANX_DISPLAY_HANDLE_T dispman_display;
   VC_RECT_T dst_rect;
   int fbfd=0;
static void init_ogl(CUBE_STATE_T *state)
{
   static EGL_DISPMANX_WINDOW_T nativewindow;
   int32_t success = 0;
   EGLBoolean result;
   EGLint num_config;

   DISPMANX_ELEMENT_HANDLE_T dispman_element;
   DISPMANX_UPDATE_HANDLE_T dispman_update;
   VC_RECT_T src_rect;

   uint32_t image_prt;

   static const EGLint attribute_list[] =
   {
      EGL_RED_SIZE, 8,
      EGL_GREEN_SIZE, 8,
      EGL_BLUE_SIZE, 8,
      EGL_ALPHA_SIZE, 8,
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_NONE
   };
   
   static const EGLint context_attributes[] = 
   {
      EGL_CONTEXT_CLIENT_VERSION, 2,
      EGL_NONE
   };
   EGLConfig config;

   // get an EGL display connection
   state->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   assert(state->display!=EGL_NO_DISPLAY);
   check();

   // initialize the EGL display connection
   result = eglInitialize(state->display, NULL, NULL);
   assert(EGL_FALSE != result);
   check();

   // get an appropriate EGL frame buffer configuration
   result = eglChooseConfig(state->display, attribute_list, &config, 1, &num_config);
   assert(EGL_FALSE != result);
   check();

   // get an appropriate EGL frame buffer configuration
   result = eglBindAPI(EGL_OPENGL_ES_API);
   assert(EGL_FALSE != result);
   check();

   // create an EGL rendering context
   state->context = eglCreateContext(state->display, config, EGL_NO_CONTEXT, context_attributes);
   assert(state->context!=EGL_NO_CONTEXT);
   check();

   // create an EGL window surface
   success = graphics_get_display_size(0 /* LCD */, &state->screen_width, &state->screen_height);
   assert( success >= 0 );

   dst_rect.x = 0;
   dst_rect.y = 0;
   dst_rect.width = state->screen_width;
   dst_rect.height = state->screen_height;
      
   src_rect.x = 0;
   src_rect.y = 0;
   src_rect.width = state->screen_width << 16;
   src_rect.height = state->screen_height << 16;        

   dispman_display = vc_dispmanx_display_open( 0 /* LCD */);
   dispman_update = vc_dispmanx_update_start( 0 );


   fbfd=open("/dev/fb0",O_RDWR);

   struct fb_var_screeninfo vinfo;
   ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo);
   struct fb_fix_screeninfo finfo;
   ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo);

   xres=vinfo.xres; 
   yres=vinfo.yres; 
   screen_size = finfo.smem_len;

   printf("%d",screen_size);

   //screen_size2 = state->screen_width*state->screen_height*2;

   framebuffer=malloc(screen_size);


   fb0=(unsigned char*)mmap(0,screen_size, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
   memcpy(framebuffer,fb0, screen_size);
   //memset(framebuffer,0xFF, screen_size);
   //memset(fb0,0xFF, screen_size);
   //framebuffer=fb0;
   
   //memset(framebuffer,0xff, state->screen_width*state->screen_height);

   //screen_resource = vc_dispmanx_resource_create(VC_IMAGE_RGB565, state->screen_width, state->screen_height, &image_prt);
   //vc_dispmanx_snapshot(dispman_display, screen_resource, 0);
   //vc_dispmanx_resource_read_data(screen_resource, &dst_rect, framebuffer, state->screen_width*2); 
         
   dispman_element = vc_dispmanx_element_add ( dispman_update, dispman_display,
      0/*layer*/, &dst_rect, 0/*src*/,
      &src_rect, DISPMANX_PROTECTION_NONE, 0 /*alpha*/, 0/*clamp*/, 0/*transform*/);
   
   nativewindow.element = dispman_element;
   nativewindow.width = state->screen_width;
   nativewindow.height = state->screen_height;
   vc_dispmanx_update_submit_sync( dispman_update );
      
   check();

   state->surface = eglCreateWindowSurface( state->display, config, &nativewindow, NULL );
   assert(state->surface != EGL_NO_SURFACE);
   check();

   // connect the context to the surface
   result = eglMakeCurrent(state->display, state->surface, state->surface, state->context);
   assert(EGL_FALSE != result);
   check();

   // Set background color and clear buffers
   glClearColor(0.15f, 0.25f, 0.35f, 1.0f);
   glClear( GL_COLOR_BUFFER_BIT );

   check();
}

char *
load_file (char *filename)
{
  FILE *f;
  int size;
  char *data;

  f = fopen (filename, "rb");
  fseek (f, 0, SEEK_END);
  size = ftell (f);
  fseek (f, 0, SEEK_SET);

  data = malloc (size + 1);
  if (fread (data, size, 1, f) < 1)
    {
      fprintf (stderr, "problem reading file %s\n", filename);
      free (data);
      return NULL;
    }
  fclose(f);

  data[size] = '\0';

  return data;
}

static void init_shaders(CUBE_STATE_T *state, const GLchar *fshader_source)
{
   static const GLfloat vertex_data[] = {
        -1.0,-1.0,1.0,1.0,
        1.0,-1.0,1.0,1.0,
        1.0,1.0,1.0,1.0,
        -1.0,1.0,1.0,1.0
   };
   const GLchar *vshader_source =
              "attribute vec4 vertex;"
              "void main(void) {"
              " gl_Position = vertex;"
              "}";

        state->vshader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(state->vshader, 1, &vshader_source, 0);
        glCompileShader(state->vshader);
        check();

        state->fshader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(state->fshader, 1, &fshader_source, 0);
        glCompileShader(state->fshader);
        check();

	//GLint status = GL_FALSE;
	//glGetShaderiv (state->fshader, GL_COMPILE_STATUS, &status);
	
	
	//check();
	//assert(glGetError() == 0);

        // fragment shader
        state->program = glCreateProgram();
        glAttachShader(state->program, state->vshader);
        glAttachShader(state->program, state->fshader);
        glLinkProgram(state->program);
        check();

	GLint status = GL_FALSE;
	glGetProgramiv (state->program, GL_LINK_STATUS, &status);

        if (status==GL_FALSE){
            printf("Shader compile error:\n");
            showlog(state->program);
	}
	else{
            printf("Shader compiled succesfully\n");
	}
        check();

        state->attr_vertex = glGetAttribLocation(state->program, "vertex");
        state->unif_res  = glGetUniformLocation(state->program, "resolution");
        state->unif_res2  = glGetUniformLocation(state->program, "iResolution");
        state->unif_time  = glGetUniformLocation(state->program, "iGlobalTime");
        state->unif_time  = glGetUniformLocation(state->program, "time");
        state->unif_col  = glGetUniformLocation(state->program, "led_color");
        state->unif_tex  = glGetUniformLocation(state->program, "tex");
   
        glClearColor ( 0.0, 1.0, 1.0, 1.0 );
        
        glGenBuffers(1, &state->buf);

	glGenTextures(1,&state->tex);
	glBindTexture(GL_TEXTURE_2D,state->tex);

	glTexImage2D(GL_TEXTURE_2D,0,GL_RGB, xres, yres, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, framebuffer);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        // Prepare viewport
        glViewport ( 0, 0, state->screen_width, state->screen_height );
        check();
        
        // Upload vertex data to a buffer
        glBindBuffer(GL_ARRAY_BUFFER, state->buf);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertex_data),
                             vertex_data, GL_STATIC_DRAW);
        glVertexAttribPointer(state->attr_vertex, 4, GL_FLOAT, 0, 16, 0);
        glEnableVertexAttribArray(state->attr_vertex);
        check();
}


        
static void draw_triangles(CUBE_STATE_T *state, GLfloat width, GLfloat height, int refresh)
{
        // Now render to the main frame buffer
        glBindFramebuffer(GL_FRAMEBUFFER,0);
        // Clear the background (not really necessary I suppose)
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
	if(refresh){
		glTexSubImage2D(GL_TEXTURE_2D,0,0,0,xres,yres,GL_RGB,GL_UNSIGNED_SHORT_5_6_5,framebuffer);
	}
        check();

        glBindBuffer(GL_ARRAY_BUFFER, state->buf);
        check();
        glUseProgram ( state->program );
        check();
	glBindTexture(GL_TEXTURE_2D,state->tex);
        check();
        glUniform2f(state->unif_res, width, height);
        glUniform3f(state->unif_res2, width, height,1);
        glUniform1f(state->unif_time, shader_time);
        glUniform3f(state->unif_col, .6,.3,0.);
        glUniform1i(state->unif_tex, 0);
        check();
        
        glDrawArrays ( GL_TRIANGLE_FAN, 0, 4 );
	if(glGetError()==0x505){
		printf("Out of Memory\n");
		abort();
	}

        glBindBuffer(GL_ARRAY_BUFFER, 0);

        glFlush();
        glFinish();
        check();
        
        eglSwapBuffers(state->display, state->surface);
        check();
}

//==============================================================================

int main (int argc, char * argv[])
{
   int terminate = 0, refresh=1;
   GLfloat width, height;
   bcm_host_init();

   // Clear application state
   memset( state, 0, sizeof( *state ) );

   //load fragment shader from file
   if (optind != argc - 1)
     {
       fprintf (stderr, "No shaderfile specified. Aborting.\n");
       exit (-1);
     }

   char * frag_code = load_file (argv[optind]);
   if (!frag_code)
     {
       fprintf (stderr, "Failed to load Shaderfile. Aborting.\n");
       exit (-1);
     }
      
   // Start OGLES
   init_ogl(state);
   init_shaders(state, frag_code);
   width = state->screen_width;
   height = state->screen_height;
      
   gettimeofday(&start_time, NULL);
   last_refresh=start_time;

   while (!terminate)
   {

      gettimeofday(&curr_time, NULL);
      timeval_subtract(&interval, &curr_time, &start_time);
      shader_time=interval.tv_sec+interval.tv_usec/1000000.;
      /*
      timeval_subtract(&time_since_refresh, &curr_time, &last_refresh);
      if(time_since_refresh.tv_usec>125000){
   	  //memcpy(framebuffer,fb0, screen_size);
          refresh=1;
          last_refresh=curr_time;
      }
      */
      memcpy(framebuffer,fb0, screen_size);
      draw_triangles(state, width, height, refresh);
      //refresh=0;
   }
   return 0;
}

