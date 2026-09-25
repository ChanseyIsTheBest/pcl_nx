/* display.c -- offscreen game target and scaled presentation
 *
 * Derived from POINPY NX's display.c (portrait/rotation removed, scaling modes
 * added). This software may be modified and distributed under the terms of the
 * MIT license. See the LICENSE file for details.
 */

#include <stdlib.h>
#include <string.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <switch.h>

#include "config.h"
#include "display.h"
#include "util.h"

#define MAX_TARGET_DIM 4096

static GLuint s_fbo = 0;
static GLuint s_tex = 0;
static GLuint s_depth = 0;
static GLuint s_prog = 0;
static GLuint s_vbo = 0;
static GLint  s_a_pos = -1, s_a_uv = -1, s_u_tex = -1;
static int    s_offscreen = 0;
static int    s_tex_w = 0, s_tex_h = 0;

// Where the game lands on the panel, in framebuffer pixels, top-left origin.
static float s_dst_x = 0, s_dst_y = 0, s_dst_w = 0, s_dst_h = 0;
static float s_scale = 1.0f; // panel pixels per game pixel

// ---------------------------------------------------------------------------
// sizing
// ---------------------------------------------------------------------------

void display_choose_size(void) {
  const int pw = screen_width, ph = screen_height;

  switch (config.scale) {
    case SCALE_SHARP: {
      int kx = (pw + GAME_BASE_W - 1) / GAME_BASE_W;
      int ky = (ph + GAME_BASE_H - 1) / GAME_BASE_H;
      int k = kx > ky ? kx : ky;
      while (k > 1 && (GAME_BASE_W * k > MAX_TARGET_DIM || GAME_BASE_H * k > MAX_TARGET_DIM)) k--;
      if (k < 1) k = 1;
      render_width = GAME_BASE_W * k;
      render_height = GAME_BASE_H * k;
      break;
    }
    case SCALE_INTEGER: {
      int kx = pw / GAME_BASE_W, ky = ph / GAME_BASE_H;
      int k = kx < ky ? kx : ky;
      if (k < 1) k = 1;
      render_width = GAME_BASE_W * k;
      render_height = GAME_BASE_H * k;
      break;
    }
    default:
      render_width = pw;
      render_height = ph;
      break;
  }

  if (config.scale == SCALE_INTEGER) {
    s_scale = 1.0f;
  } else {
    const float sx = (float)pw / (float)render_width;
    const float sy = (float)ph / (float)render_height;
    s_scale = sx < sy ? sx : sy;
  }
  s_dst_w = render_width * s_scale;
  s_dst_h = render_height * s_scale;
  s_dst_x = ((float)pw - s_dst_w) * 0.5f;
  s_dst_y = ((float)ph - s_dst_h) * 0.5f;

  render_offset_x = 0;
  render_offset_y = 0;

  debugPrintf("display: panel %dx%d, game %dx%d, scale mode %d (x%.3f), dst %.0f,%.0f %.0fx%.0f\n",
              pw, ph, render_width, render_height, config.scale, (double)s_scale,
              (double)s_dst_x, (double)s_dst_y, (double)s_dst_w, (double)s_dst_h);
}

// ---------------------------------------------------------------------------
// present shader
// ---------------------------------------------------------------------------

static const char *VS =
  "attribute vec2 a_pos;\n"
  "attribute vec2 a_uv;\n"
  "varying vec2 v_uv;\n"
  "void main() { v_uv = a_uv; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";

static const char *FS =
  "precision mediump float;\n"
  "uniform sampler2D u_tex;\n"
  "varying vec2 v_uv;\n"
  "void main() { gl_FragColor = vec4(texture2D(u_tex, v_uv).rgb, 1.0); }\n";

static GLuint compile(GLenum type, const char *src) {
  GLuint sh = glCreateShader(type);
  glShaderSource(sh, 1, &src, NULL);
  glCompileShader(sh);
  GLint ok = 0;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[512] = {0};
    glGetShaderInfoLog(sh, sizeof(log) - 1, NULL, log);
    debugPrintf("display: shader compile failed: %s\n", log);
    glDeleteShader(sh);
    return 0;
  }
  return sh;
}

static int build_program(void) {
  GLuint vs = compile(GL_VERTEX_SHADER, VS);
  GLuint fs = compile(GL_FRAGMENT_SHADER, FS);
  if (!vs || !fs) return 0;

  s_prog = glCreateProgram();
  glAttachShader(s_prog, vs);
  glAttachShader(s_prog, fs);
  glLinkProgram(s_prog);
  glDeleteShader(vs);
  glDeleteShader(fs);

  GLint ok = 0;
  glGetProgramiv(s_prog, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[512] = {0};
    glGetProgramInfoLog(s_prog, sizeof(log) - 1, NULL, log);
    debugPrintf("display: program link failed: %s\n", log);
    glDeleteProgram(s_prog);
    s_prog = 0;
    return 0;
  }

  s_a_pos = glGetAttribLocation(s_prog, "a_pos");
  s_a_uv  = glGetAttribLocation(s_prog, "a_uv");
  s_u_tex = glGetUniformLocation(s_prog, "u_tex");
  glGenBuffers(1, &s_vbo);
  return 1;
}

// ---------------------------------------------------------------------------
// render target
// ---------------------------------------------------------------------------

static void destroy_target(void) {
  if (s_fbo)   { glDeleteFramebuffers(1, &s_fbo); s_fbo = 0; }
  if (s_tex)   { glDeleteTextures(1, &s_tex); s_tex = 0; }
  if (s_depth) { glDeleteRenderbuffers(1, &s_depth); s_depth = 0; }
}

static int create_target(void) {
  destroy_target();
  s_tex_w = render_width;
  s_tex_h = render_height;

  // 1:1 presents want exact texels; any real scaling wants bilinear.
  const GLint filter = (s_scale > 0.999f && s_scale < 1.001f) ? GL_NEAREST : GL_LINEAR;

  glGenTextures(1, &s_tex);
  glBindTexture(GL_TEXTURE_2D, s_tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, s_tex_w, s_tex_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glGenRenderbuffers(1, &s_depth);
  glBindRenderbuffer(GL_RENDERBUFFER, s_depth);
  while (glGetError() != GL_NO_ERROR) { } // drain, so the probe below is honest
  // The runner expects a depth+stencil screen buffer (it asked for D24S8).
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8_OES, s_tex_w, s_tex_h);
  const int packed = (glGetError() == GL_NO_ERROR);
  if (!packed) {
    debugPrintf("display: no packed depth/stencil, falling back to DEPTH_COMPONENT16\n");
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, s_tex_w, s_tex_h);
  }

  glGenFramebuffers(1, &s_fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, s_fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_tex, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, s_depth);
  if (packed)
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, s_depth);

  const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    debugPrintf("display: framebuffer incomplete (0x%04x)\n", status);
    destroy_target();
    return 0;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, s_fbo);
  glViewport(0, 0, s_tex_w, s_tex_h);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  debugPrintf("display: offscreen target %dx%d fbo=%u tex=%u%s\n",
              s_tex_w, s_tex_h, s_fbo, s_tex, packed ? "" : " (depth only)");
  return 1;
}

static void fallback_to_panel(void) {
  // Direct-to-panel: the runner's window is the panel itself.
  s_offscreen = 0;
  render_width = screen_width;
  render_height = screen_height;
  s_scale = 1.0f;
  s_dst_x = s_dst_y = 0.0f;
  s_dst_w = (float)screen_width;
  s_dst_h = (float)screen_height;
}

int display_init(void) {
  display_choose_size();
  if (!build_program()) {
    debugPrintf("display: present shader unavailable, rendering direct to panel\n");
    fallback_to_panel();
    return 0;
  }
  if (!create_target()) {
    fallback_to_panel();
    return 0;
  }
  s_offscreen = 1;
  return 1;
}

void display_shutdown(void) {
  destroy_target();
  if (s_vbo)  { glDeleteBuffers(1, &s_vbo); s_vbo = 0; }
  if (s_prog) { glDeleteProgram(s_prog); s_prog = 0; }
  s_offscreen = 0;
}

GLuint display_game_fbo(void) { return s_offscreen ? s_fbo : 0; }
int display_is_offscreen(void) { return s_offscreen; }

// ---------------------------------------------------------------------------
// present
// ---------------------------------------------------------------------------

void display_present(void) {
  if (!s_offscreen) return;

  // --- save the state the runner cares about -------------------------------
  GLint old_prog = 0, old_abuf = 0, old_tex = 0, old_active = 0, old_vp[4];
  const GLboolean blend_on   = glIsEnabled(GL_BLEND);
  const GLboolean depth_on   = glIsEnabled(GL_DEPTH_TEST);
  const GLboolean cull_on    = glIsEnabled(GL_CULL_FACE);
  const GLboolean scissor_on = glIsEnabled(GL_SCISSOR_TEST);
  const GLboolean stencil_on = glIsEnabled(GL_STENCIL_TEST);
  GLboolean old_mask[4];
  glGetBooleanv(GL_COLOR_WRITEMASK, old_mask);
  glGetIntegerv(GL_CURRENT_PROGRAM, &old_prog);
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &old_abuf);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &old_active);
  glActiveTexture(GL_TEXTURE0);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_tex);
  glGetIntegerv(GL_VIEWPORT, old_vp);

  GLint va0_on = 0, va1_on = 0;
  if (s_a_pos >= 0) glGetVertexAttribiv((GLuint)s_a_pos, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &va0_on);
  if (s_a_uv  >= 0) glGetVertexAttribiv((GLuint)s_a_uv,  GL_VERTEX_ATTRIB_ARRAY_ENABLED, &va1_on);

  // --- draw ----------------------------------------------------------------
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  if (scissor_on) glDisable(GL_SCISSOR_TEST);
  if (stencil_on) glDisable(GL_STENCIL_TEST);
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glViewport(0, 0, screen_width, screen_height);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  const float x0 = (s_dst_x / (float)screen_width) * 2.0f - 1.0f;
  const float x1 = ((s_dst_x + s_dst_w) / (float)screen_width) * 2.0f - 1.0f;
  const float y1 = 1.0f - (s_dst_y / (float)screen_height) * 2.0f;
  const float y0 = 1.0f - ((s_dst_y + s_dst_h) / (float)screen_height) * 2.0f;

  // Triangle strip BL, BR, TL, TR. The texture already holds the image the GL
  // way up, so the UVs are the identity.
  const float verts[] = {
    x0, y0, 0.0f, 0.0f,
    x1, y0, 1.0f, 0.0f,
    x0, y1, 0.0f, 1.0f,
    x1, y1, 1.0f, 1.0f,
  };

  glUseProgram(s_prog);
  glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
  if (s_a_pos >= 0) {
    glEnableVertexAttribArray((GLuint)s_a_pos);
    glVertexAttribPointer((GLuint)s_a_pos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
  }
  if (s_a_uv >= 0) {
    glEnableVertexAttribArray((GLuint)s_a_uv);
    glVertexAttribPointer((GLuint)s_a_uv, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
  }
  glBindTexture(GL_TEXTURE_2D, s_tex);
  if (s_u_tex >= 0) glUniform1i(s_u_tex, 0);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  // --- restore -------------------------------------------------------------
  if (s_a_pos >= 0 && !va0_on) glDisableVertexAttribArray((GLuint)s_a_pos);
  if (s_a_uv  >= 0 && !va1_on) glDisableVertexAttribArray((GLuint)s_a_uv);
  glColorMask(old_mask[0], old_mask[1], old_mask[2], old_mask[3]);
  glBindBuffer(GL_ARRAY_BUFFER, (GLuint)old_abuf);
  glBindTexture(GL_TEXTURE_2D, (GLuint)old_tex);
  glActiveTexture((GLenum)old_active);
  glUseProgram((GLuint)old_prog);
  glViewport(old_vp[0], old_vp[1], old_vp[2], old_vp[3]);
  if (blend_on)   glEnable(GL_BLEND);
  if (depth_on)   glEnable(GL_DEPTH_TEST);
  if (cull_on)    glEnable(GL_CULL_FACE);
  if (scissor_on) glEnable(GL_SCISSOR_TEST);
  if (stencil_on) glEnable(GL_STENCIL_TEST);

  // Hand the runner its render target back for the next frame.
  glBindFramebuffer(GL_FRAMEBUFFER, s_fbo);
}

// ---------------------------------------------------------------------------
// coordinate mapping
// ---------------------------------------------------------------------------

void display_screen_to_game(float sx, float sy, float *gx, float *gy) {
  *gx = (sx - s_dst_x) / s_scale;
  *gy = (sy - s_dst_y) / s_scale;
}

void display_game_to_screen(float gx, float gy, float *sx, float *sy) {
  *sx = s_dst_x + gx * s_scale;
  *sy = s_dst_y + gy * s_scale;
}
