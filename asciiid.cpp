// nvbug.cpp : Defines the entry point for the console application.
//

#include <stdio.h>

#define _USE_MATH_DEFINES
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>

#include <string.h>

#include "gl.h"

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h" // beta: ImGuiItemFlags_Disabled
#include "imgui_impl_opengl3.h"

#include "asciiid_platform.h"
#include "texheap.h"
#include "terrain.h"
#include "asciiid_urdo.h"

#include "matrix.h"

#include "fast_rand.h"

Terrain* terrain = 0;

struct Font
{
	static void Load(void* cookie, A3D_ImageFormat f, int w, int h, const void* data, int palsize, const void* palbuf)
	{
		Font* font = (Font*)cookie;

		glCreateTextures(GL_TEXTURE_2D, 1, &font->tex);
		int ifmt = 0;
		int fmt = 0;
		int type = 0;

		void* buf = 0;
		int bits = 0;
		int bytes = 0;

		switch (f)
		{
			case A3D_RGB8:
				ifmt = GL_RGB8; fmt = GL_RGB; type = GL_UNSIGNED_BYTE;
				break;
			case A3D_RGB16:
				ifmt = GL_RGB16; fmt = GL_RGB; type = GL_UNSIGNED_SHORT;
				break;
			case A3D_RGBA8:
				ifmt = GL_RGBA8; fmt = GL_RGBA; type = GL_UNSIGNED_BYTE;
				break;
			case A3D_RGBA16:
				ifmt = GL_RGBA16; fmt = GL_RGBA; type = GL_UNSIGNED_SHORT;
				break;
			case A3D_LUMINANCE1:
				bits = 1; bytes = 1;
				ifmt = GL_R8; fmt = GL_RED; type = GL_UNSIGNED_BYTE;
				break;
			case A3D_LUMINANCE2:
				bits = 2; bytes = 1;
				ifmt = GL_R8; fmt = GL_RED; type = GL_UNSIGNED_BYTE;
				break;
			case A3D_LUMINANCE4:
				bits = 4; bytes = 1;
				ifmt = GL_R8; fmt = GL_RED; type = GL_UNSIGNED_BYTE;
				break;
			case A3D_LUMINANCE8:
				ifmt = GL_R8; fmt = GL_RED; type = GL_UNSIGNED_BYTE;
				break;
			case A3D_LUMINANCE_ALPHA1:
				bits = 1; bytes = 2;
				ifmt = GL_RG8; fmt = GL_RG; type = GL_UNSIGNED_BYTE;
				break;
			case A3D_LUMINANCE_ALPHA2:
				bits = 2; bytes = 2;
				ifmt = GL_RG8; fmt = GL_RG; type = GL_UNSIGNED_BYTE;
				break;
			case A3D_LUMINANCE_ALPHA4:
				bits = 4; bytes = 2;
				ifmt = GL_RG8; fmt = GL_RG; type = GL_UNSIGNED_BYTE;
				break;
			case A3D_LUMINANCE_ALPHA8:
				ifmt = GL_RG8; fmt = GL_RG; type = GL_UNSIGNED_BYTE;
				break;

			case A3D_INDEX1:
				bits = 1; bytes = 4;
				ifmt = GL_RGBA8; fmt = GL_RGBA; type = GL_UNSIGNED_BYTE;
				break;
			case A3D_INDEX2:
				bits = 2; bytes = 4;
				ifmt = GL_RGBA8; fmt = GL_RGBA; type = GL_UNSIGNED_BYTE;
				break;
			case A3D_INDEX4:
				bits = 4; bytes = 4;
				ifmt = GL_RGBA8; fmt = GL_RGBA; type = GL_UNSIGNED_BYTE;
				break;
			case A3D_INDEX8:
				bits = 8; bytes = 4;
				ifmt = GL_RGBA8; fmt = GL_RGBA; type = GL_UNSIGNED_BYTE;
				break;
		}

		if (bits)
		{
			int out_row = bytes * w;
			int in_row = (((bits * w - 1) | 0x7) + 1) / 8;
			const uint8_t* in_line = (const uint8_t*)data;
			uint8_t* out_line = (uint8_t*)malloc(out_row*h);

			int d = 1;
			int q = 8 / bits - 1;
			int m = (1 << bits) - 1;
			int r = 0;
			int mul = 1;
			switch (bits)
			{
				case 1: r = 3; mul = 255; break;
				case 2: r = 2; mul = 85;  break;
				case 4: r = 1; mul = 17;  break;
			}

			if (bytes < 4)
			{
				for (int y = 0; y < h; y++)
				{
					for (int b = 0; b < out_row; b++)
					{
						int val = (in_line[b >> r] >> ((b&q)*bits)) & m;
						out_line[b] = val * mul;
					}

					in_line += in_row;
					out_line += out_row;
				}
			}
			else
			{
				// depal
				for (int y = 0; y < h; y++)
				{
					for (int b = 0; b < w; b++)
					{
						int idx = (in_line[b >> r] >> ((b&q)*bits)) & m;
						((uint32_t*)out_line)[b] = ((const uint32_t*)palbuf)[b];
					}

					in_line += in_row;
					out_line += out_row;
				}
			}
		}

		glTextureStorage2D(font->tex, 1, ifmt, w, h);
		glTextureParameteri(font->tex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTextureParameteri(font->tex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTextureParameteri(font->tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(font->tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glTextureSubImage2D(font->tex, 0, 0, 0, w, h, fmt, type, buf ? buf : data);

		if (buf)
			free(buf);
	}
	GLuint tex;
} font;

float font_size = 10;// 0.125;// 16; // so every visual cell appears as 16px
float rot_yaw = 45;
float rot_pitch = 30;//90;
bool spin_anim = false;
float pos_x = 0, pos_y = 0, pos_z = 0;
int mouse_in = 0;

int panning = 0;
int panning_x = 0;
int panning_y = 0;
double panning_dx = 0;
double panning_dy = 0;

float zoom_wheel = 0;

int spinning = 0;
int spinning_x = 0;
int spinning_y = 0;

int painting = 0; 
const float STAMP_R = 0.50;
const float STAMP_A = 1.00;
float br_radius = 10;
float br_alpha = 0.5f;
int painting_x = 0;
int painting_y = 0;
double painting_dx;
double painting_dy;
double paint_dist;

bool diag_flipped = false;
bool br_limit = false;
int probe_z = 0;

uint64_t g_Time; // in microsecs

#define QUOT(a) #a
#define DEFN(a) "#define " #a " " QUOT(a) "\n"
#define CODE(...) #__VA_ARGS__

struct RenderContext
{
	void Create()
	{
		int err = glGetError();
		glCreateBuffers(1, &vbo);
		err = glGetError();
		glNamedBufferStorage(vbo, TERRAIN_TEXHEAP_CAPACITY * sizeof(GLint[5]), 0, GL_DYNAMIC_STORAGE_BIT);
		err = glGetError();

		glCreateVertexArrays(1, &vao);
		glBindVertexArray(vao);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glVertexAttribIPointer(0, 4, GL_UNSIGNED_INT, sizeof(GLint[5]), (void*)0);
		glVertexAttribIPointer(1, 1, GL_UNSIGNED_INT, sizeof(GLint[5]), (void*)sizeof(GLint[4]));
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glEnableVertexAttribArray(0);
		glEnableVertexAttribArray(1);
		glBindVertexArray(0);

		const char* vs_src = 
		CODE(#version 450\n)
		DEFN(HEIGHT_SCALE)
		DEFN(HEIGHT_CELLS)
		DEFN(VISUAL_CELLS)
		CODE(
			layout(location = 0) in ivec4 in_xyuv;
			layout(location = 1) in int in_diag;
			out ivec4 xyuv;
			out int diag;

			void main()
			{
				xyuv = in_xyuv;
				diag = in_diag;
			}
		);

		const char* gs_src = 
		CODE(#version 450\n)
		DEFN(HEIGHT_SCALE)
		DEFN(HEIGHT_CELLS)
		DEFN(VISUAL_CELLS)
		CODE(
			layout(points) in;
			layout(triangle_strip, max_vertices = 4*HEIGHT_CELLS*HEIGHT_CELLS ) out;

			uniform vec4 br;
			uniform usampler2D z_tex;
			uniform mat4 tm;

			uniform vec3 pr; // .x=height , .y=alpha (alpha=0.5 when probing, otherwise 1.0), .z is br_limit direction (+1/-1 or 0 if disabled)


			in ivec4 xyuv[];
			in int diag[];

			out vec4 world_xyuv;
			out vec3 uvh;
			flat out vec3 normal;
			
			void main()
			{
				uint z;
				vec4 v;
				ivec2 xy;

				vec3 xyz[4];
				vec2 uv[4];

				float rvh = float(VISUAL_CELLS) / float(HEIGHT_CELLS);
				float dxy = 1.0 / float(HEIGHT_CELLS);
				ivec2 bxy = xyuv[0].xy*HEIGHT_CELLS;

				// todo: emit optimized strips
				// should allow having upto 6x6 patches -> 12 scalars * 6 strips * (6+1) cols * 2 verts = 1008 components (out of 1024)
				// currently max is 4x4 -> 12 scalars * 4*4 quads * 4 verts -> 768 components

				int rot = diag[0];
				ivec4 order[2] = ivec4[2](ivec4(0, 1, 2, 3), ivec4(1, 3, 0, 2));

				for (int y = 0; y < HEIGHT_CELLS; y++)
				{
					for (int x = 0; x < HEIGHT_CELLS; x++)
					{
						xy = ivec2(x, y + 1);
						uv[0] = (xyuv[0].zw + vec2(xy) / HEIGHT_CELLS) * VISUAL_CELLS;
						z = texelFetch(z_tex, xyuv[0].zw*(HEIGHT_CELLS+1) + xy, 0).r;
						xy = bxy + xy*VISUAL_CELLS;
						xyz[0] = vec3(xy*dxy, z);

						xy = ivec2(x, y);
						uv[1] = (xyuv[0].zw + vec2(xy) / HEIGHT_CELLS) * VISUAL_CELLS;
						z = texelFetch(z_tex, xyuv[0].zw*(HEIGHT_CELLS + 1) + xy, 0).r;
						xy = bxy + xy*VISUAL_CELLS;
						xyz[1] = vec3(xy*dxy, z);

						xy = ivec2(x + 1, y + 1);
						uv[2] = (xyuv[0].zw + vec2(xy) / HEIGHT_CELLS) * VISUAL_CELLS;
						z = texelFetch(z_tex, xyuv[0].zw*(HEIGHT_CELLS + 1) + xy, 0).r;
						xy = bxy + xy * VISUAL_CELLS;
						xyz[2] = vec3(xy*dxy, z);

						xy = ivec2(x + 1, y);
						uv[3] = (xyuv[0].zw + vec2(xy) / HEIGHT_CELLS) * VISUAL_CELLS;
						z = texelFetch(z_tex, xyuv[0].zw*(HEIGHT_CELLS + 1) + xy, 0).r;
						xy = bxy + xy * VISUAL_CELLS;
						xyz[3] = vec3(xy*dxy, z);

						if (br.w != 0.0 && br.z>0)
						{
							for (int i = 0; i < 4; i++)
							{
								vec2 d = xyz[i].xy - br.xy;
								float len = length(d);
								if (len < br.z)
								{
									float gauss = (0.5 + 0.5*cos(len/br.z*3.141592));

									int d = int(round(gauss*gauss * br.w * br.z * HEIGHT_SCALE));

									float z = xyz[i].z + d;

									if (pr.z!=0) // limit enabled
									{
										if (d > 0)
										{
											if (xyz[i].z > pr.x)
												z = xyz[i].z;
											else
											if (z > pr.x)
												z = pr.x;
										}
										else
										if (d < 0)
										{
											if (xyz[i].z < pr.x)
												z = xyz[i].z;
											else
											if (z < pr.x)
												z = pr.x;
										}
									}
									else
									{
										if (z < 0)
											z = 0;
										if (z > 0xffff)
											z = 0xffff;
									}

									xyz[i].z = z;

									// xyz[i].z += int(round(gauss*gauss * br.w * br.z * HEIGHT_SCALE));
									// xyz[i].z = clamp(xyz[i].z, 0, 0xffff);
								}
							}
						}

						vec3 norm[4];
						norm[0] = cross(xyz[1] - xyz[0], xyz[2] - xyz[0]);
						norm[1] = cross(xyz[2] - xyz[3], xyz[1] - xyz[3]);
						norm[2] = cross(xyz[3] - xyz[1], xyz[0] - xyz[1]);
						norm[3] = cross(xyz[0] - xyz[2], xyz[3] - xyz[2]);

						int r = rot & 1;

						normal = norm[2 * r];
						normal.xy *= 1.0 / HEIGHT_SCALE;

						{
							int i = order[r][0];

							world_xyuv = vec4(xyz[i].xy, uv[i]);
							uvh.xyz = xyz[i] - ivec3(xyuv[0].xy, 0);
							uvh.xyz /= vec3(rvh, rvh, HEIGHT_SCALE);

							gl_Position = tm * vec4(xyz[i], 1.0);
							EmitVertex();
						}
						{
							int i = order[r][1];

							world_xyuv = vec4(xyz[i].xy, uv[i]);
							uvh.xyz = xyz[i] - ivec3(xyuv[0].xy, 0);
							uvh.xyz /= vec3(rvh, rvh, HEIGHT_SCALE);

							gl_Position = tm * vec4(xyz[i], 1.0);
							EmitVertex();
						}
						{
							int i = order[r][2];

							world_xyuv = vec4(xyz[i].xy, uv[i]);
							uvh.xyz = xyz[i] - ivec3(xyuv[0].xy, 0);
							uvh.xyz /= vec3(rvh, rvh, HEIGHT_SCALE);

							gl_Position = tm * vec4(xyz[i], 1.0);
							EmitVertex();
						}

						normal = norm[2 * r + 1];
						normal.xy *= 1.0 / HEIGHT_SCALE;

						{
							int i = order[r][3];

							world_xyuv = vec4(xyz[i].xy, uv[i]);
							uvh.xyz = xyz[i] - ivec3(xyuv[0].xy, 0);
							uvh.xyz /= vec3(rvh, rvh, HEIGHT_SCALE);

							gl_Position = tm * vec4(xyz[i], 1.0);
							EmitVertex();
						}


						rot = rot >> 1;
						EndPrimitive();
					}
				}
			}
		);

		const char* fs_src = 
		CODE(#version 450\n)
		DEFN(HEIGHT_SCALE)
		DEFN(HEIGHT_CELLS)
		DEFN(VISUAL_CELLS)
		CODE(
			layout(location = 0) out vec4 color;

			uniform usampler2D v_tex;

			uniform vec4 br; // brush
			uniform vec3 qd; // quad diag
			uniform vec3 pr; // .x=height , .y=alpha (alpha=0.5 when probing, otherwise 1.0), .z is br_limit direction (+1/-1 or 0 if disabled)

			flat in vec3 normal;
			in vec3 uvh;
			in vec4 world_xyuv;

			float Grid(vec2 d, vec2 p, float s)
			{
				d *= s;
				p = fract(p*s + vec2(0.5));

				float r = 1.0;

				if (d.x < 0.25)
				{
					float a = clamp(-log2(d.x * 4), 0.0, 1.0);
					float m = smoothstep(0.5 - d.x, 0.5, p.x) * smoothstep(0.5 + d.x, 0.5, p.x);
					r *= mix(1.0, pow(1.0 - m, 0.5), a);
				}
				if (d.y < 0.25)
				{
					float a = clamp(-log2(d.y * 4), 0.0, 1.0);
					float m = smoothstep(0.5 - d.y, 0.5, p.y) * smoothstep(0.5 + d.y, 0.5, p.y);
					r *= mix(1.0, pow(1.0 - m, 0.5), a);
				}

				return r;
			}
			
			void main()
			{
				// sample terrain visual
				uint visual = texelFetch(v_tex, ivec2(floor(world_xyuv.zw)), 0).r;
				//visual = 12345;

				{
					/*
					uint matid = visual & 0x3F;
					uint shade = (visual >> 6) & 0xF;
					// 6 bits left for (elevation, ...)

					// sample material array
					// y=0,1 -> descent; y=2,3 -> fill; y=4,5 -> ascent
					vec4 fill_rgbx = texelFetch(m_tex, ivec3(shade, 2, matid), 0);
					vec4 fill_rgby = texelFetch(m_tex, ivec3(shade, 3, matid), 0);

					// sample font texture (pure alpha)
					vec2 chr_ofs = vec2(0, 0);  // calc fraction of char coordinate
					float glyph = texture(f_tex, vec2(fill_fg_rgbx.w, fill_bg_rgby.w) + chr_ofs).r;

					// compose glyph
					vec3 color = mix(fill_rgbx.rgb, fill_rgby.rgb, glyph);
					*/

				}

				// at the moment we assume that visual is simply RGB565 color
				color.r = float(visual & 0x1f) / 31.0;
				color.g = float((visual>>5) & 0x3f) / 63.0;
				color.b = float((visual>>11) & 0x1f) / 31.0;
				color.a = 1;

				vec3 light_pos = normalize(vec3(1, 1, 0.25));
				float light = 0.5 + 0.5*dot(light_pos, normalize(normal));

				color.rgb *= light;

				{
					// quad preview
					float d = float(VISUAL_CELLS) / float(HEIGHT_CELLS);
					if (world_xyuv.x >= qd.x && world_xyuv.x < qd.x + d &&
						world_xyuv.y >= qd.y && world_xyuv.y < qd.y + d)
					{
						//color.rb = mix(color.rb, color.rb * 0.5, qd.z);
						color.rgb = mix(color.rgb, vec3(0, 1, 0), qd.z*0.25);
					}
				}

				{
					// height probe
					if (uvh.z * HEIGHT_SCALE < pr.x)
					{
						//color.g *= (1.0 - 0.25 * pr.y);
						color.rgb = mix(color.rgb, vec3(0.25, 0.5, 0.75), 0.1 + 0.1 * pr.y);
					}
				}

				if (!gl_FrontFacing)
					color.rgb = 0.25 * (vec3(1.0) - color.rgb);

				float dx = 1.25*length(vec2(dFdxFine(uvh.x), dFdyFine(uvh.x)));
				float dy = 1.25*length(vec2(dFdxFine(uvh.y), dFdyFine(uvh.y)));

				vec2 d = vec2(dx, dy);

				float grid = 1.0;
				grid = min(grid, Grid(d*1.50, uvh.xy, 1.0 / float(HEIGHT_CELLS)));
				grid = min(grid, Grid(d*1.25, uvh.xy, 1.0));
				grid = min(grid, Grid(d*1.00, uvh.xy, float(VISUAL_CELLS) / float(HEIGHT_CELLS)));

				// color.rgb *= grid;
				color.rgb = mix(vec3(0, 0, 1), color.rgb, grid);

				// brush preview
				if (br.w != 0.0)
				{
					float abs_r = abs(br.z);
					float len = length(world_xyuv.xy - br.xy);
					float alf = (abs_r - len) / abs_r;

					float dalf = fwidth(alf);
					float silh = smoothstep(-dalf, 0, alf) * smoothstep(+dalf, 0, alf);

					alf = max(0.0, alf);

					if (br.z>0)
						color.gb *= 1.0 - alf;
					else
						color.rg *= 1.0 - alf;

					color.rgb *= 1.0 - silh*0.25;
				}
			}
		);

		err = glGetError();
		prg = glCreateProgram();

		GLenum st[3] = { GL_VERTEX_SHADER, GL_GEOMETRY_SHADER, GL_FRAGMENT_SHADER };
		const char* src[3] = { vs_src, gs_src, fs_src };
		GLuint shader[3];

		GLsizei loglen = 999;
		char logstr[1000];

		for (int i = 0; i < 3; i++)
		{
			shader[i] = glCreateShader(st[i]);
			GLint len = (GLint)strlen(src[i]);
			glShaderSource(shader[i], 1, &(src[i]), &len);
			glCompileShader(shader[i]);

			loglen = 999;
			glGetShaderInfoLog(shader[i], loglen, &loglen, logstr);
			logstr[loglen] = 0;

			if (loglen)
				printf("%s", logstr);

			glAttachShader(prg, shader[i]);
		}

		glLinkProgram(prg);

		for (int i = 0; i < 3; i++)
			glDeleteShader(shader[i]);

		loglen = 999;
		glGetProgramInfoLog(prg, loglen, &loglen, logstr);
		logstr[loglen] = 0;

		if (loglen)
			printf("%s", logstr);

		tm_loc = glGetUniformLocation(prg, "tm");
		z_tex_loc = glGetUniformLocation(prg, "z_tex");
		v_tex_loc = glGetUniformLocation(prg, "v_tex");
		br_loc = glGetUniformLocation(prg, "br");
		qd_loc = glGetUniformLocation(prg, "qd");
		pr_loc = glGetUniformLocation(prg, "pr");
	}

	void Delete()
	{
		glDeleteVertexArrays(1, &vao);
		glDeleteBuffers(1, &vbo);
		glDeleteProgram(prg);
	}

	void BeginPatches(const double* tm, const float* br, const float* qd, const float* pr)
	{
		glUseProgram(prg);

		static const float br_off[] = { 0,0,1,0 };
		if (!br)
			br = br_off;

		//glUniformMatrix4dv(tm_loc, 1, GL_FALSE, tm);
		float ftm[16];// NV bug! workaround
		for (int i = 0; i < 16; i++)
			ftm[i] = (float)tm[i];

		glUniformMatrix4fv(tm_loc, 1, GL_FALSE, ftm);
		glUniform1i(z_tex_loc, 0);
		glUniform1i(v_tex_loc, 1);
		glUniform4fv(br_loc, 1, br);
		glUniform3fv(qd_loc, 1, qd);
		glUniform3fv(pr_loc, 1, pr);
		glBindVertexArray(vao);

		head = 0;
		patches = 0;
		draws = 0;
		changes = 0;
		page_tex = 0;

		render_time = a3dGetTime();
	}

	static void RenderPatch(Patch* p, int x, int y, int view_flags, void* cookie)
	{
		RenderContext* rc = (RenderContext*)cookie;
		TexAlloc* ta = GetTerrainTexAlloc(p);

		rc->patches++;

		TexPageBuffer* buf = (TexPageBuffer*)ta->page->user;

		if (buf->size == 0)
		{
			if (rc->head)
				((TexPageBuffer*)rc->head->user)->prev = ta->page;
			buf->prev = 0;
			buf->next = rc->head;
			rc->head = ta->page;
		}

		GLint* patch = buf->data + 5 * buf->size;

		patch[0] = x;
		patch[1] = y;
		patch[2] = ta->x;
		patch[3] = ta->y;
		patch[4] = GetTerrainDiag(p);

		buf->size++;

		if (buf->size == TERRAIN_TEXHEAP_CAPACITY)
		{
			rc->draws++;
			
			if (rc->page_tex != ta->page)
			{
				rc->changes++;
				rc->page_tex = ta->page;

				for (int u=0; u<2; u++)
					glBindTextureUnit(u, rc->page_tex->tex[u]);
			}

			glNamedBufferSubData(rc->vbo, 0, sizeof(GLint[5]) * buf->size, buf->data);
			glDrawArrays(GL_POINTS, 0, buf->size);

			if (buf->prev)
				((TexPageBuffer*)buf->prev->user)->next = buf->next;
			else
				rc->head = buf->next;

			if (buf->next)
				((TexPageBuffer*)buf->next->user)->prev = buf->prev;

			buf->size = 0;
			buf->next = 0;
			buf->prev = 0;
		}

	}

	void EndPatches()
	{
		TexPage* tp = head;
		while (tp)
		{
			TexPageBuffer* buf = (TexPageBuffer*)tp->user;

			if (page_tex != tp)
			{
				changes++;
				page_tex = tp;

				for (int u=0; u<2; u++)
					glBindTextureUnit(u, page_tex->tex[u]);
			}

			draws++;
			glNamedBufferSubData(vbo, 0, sizeof(GLint[5]) * buf->size, buf->data);
			glDrawArrays(GL_POINTS, 0, buf->size);

			tp = buf->next;
			buf->size = 0;
			buf->next = 0;
			buf->prev = 0;
		}

		page_tex = 0;
		head = 0;

		for (int u = 0; u < 2; u++)
			glBindTextureUnit(u,0);

		glBindVertexArray(0);
		glUseProgram(0);

		render_time = a3dGetTime() - render_time;
	}

	GLint tm_loc; // uniform
	GLint z_tex_loc;
	GLint v_tex_loc;
	GLint br_loc;
	GLint qd_loc;
	GLint pr_loc;

	GLuint prg;
	GLuint vao;
	GLuint vbo;

	TexPage* page_tex;
	TexPage* head;

	int patches; // rendered stats
	int draws;
	int changes;
	uint64_t render_time;
};

RenderContext render_context;

void GL_APIENTRY glDebugCall(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *message, const void *userParam)
{
	static const char* source_str[] = // 0x8246 - 0x824B
	{
		"API",
		"WINDOW_SYSTEM",
		"SHADER_COMPILER",
		"THIRD_PARTY",
		"APPLICATION",
		"OTHER"
	};

	const char* src = "?";
	if (source >= 0x8246 && source <= 0x824B)
		src = source_str[source - 0x8246];

	static const char* type_str[] = // 0x824C - 0x8251
	{
		"ERROR",
		"DEPRECATED_BEHAVIOR",
		"UNDEFINED_BEHAVIOR",
		"PORTABILITY",
		"PERFORMANCE",
		"OTHER"
	};

	const char* typ = "?";
	if (type >= 0x824C && type <= 0x8251)
		typ = type_str[type - 0x824C];

	static const char* severity_str[] = // 0x9146 - 0x9148 , 0x826B
	{
		"HIGH",
		"MEDIUM",
		"LOW",
		"NOTIFICATION",
	};

	const char* sev = "?";
	if (severity >= 0x9146 && severity <= 0x9148)
		sev = severity_str[severity - 0x9146];
	else
		if (severity == 0x826B)
		{
			return;
			sev = severity_str[3];
		}

	printf("src:%s type:%s id:%d severity:%s\n%s\n\n", src, typ, id, sev, (const char*)message);
}


struct Gather
{
	int x, y; // patch aligned
	int count; // number of actually queried patches
	int size; // in patches
	int* tmp_x;
	int* tmp_y;
	Patch* patch[1];

	int GetPatchIdx(int px, int py)
	{
		int dx = px - x;
		int dy = py - y;

		int bx = dx / VISUAL_CELLS;
		int by = dy / VISUAL_CELLS;

		assert(bx >= 0 && bx < size && by >= 0 && by < size);
		return bx + by * size;
	}

	int Sample(int hx, int hy) // hx and hy are in height map samples relative to Gather::x,y
	{
		int px = hx / HEIGHT_CELLS;
		int py = hy / HEIGHT_CELLS;

		int sx = hx % HEIGHT_CELLS;
		int sy = hy % HEIGHT_CELLS;

		int idx = px + py * size;
		Patch* p = patch[idx];

		if (!p)
			return -1;

		uint16_t* map = GetTerrainHeightMap(p);

		return map[sx + sy * (HEIGHT_CELLS + 1)];
	}
};

Gather* gather = 0;

static void GatherCB(Patch* p, int x, int y, int view_flags, void* cookie)
{
	gather->count++;
	gather->patch[gather->GetPatchIdx(x, y)] = p;
}

static void StampCB(Patch* p, int x, int y, int view_flags, void* cookie)
{
	double mul = br_alpha * br_radius * HEIGHT_SCALE;
	if (fabs(mul) < 0.499)
		return;

	uint16_t lo, hi;
	GetTerrainLimits(p, &lo, &hi);
	if (hi == 0 && br_alpha < 0 || lo == 0xffff && br_alpha>0)
		return;

	URDO_Patch(p);

	double* xy = (double*)cookie;
	uint16_t* map = GetTerrainHeightMap(p);

	const static double sxy = (double)VISUAL_CELLS / (double)HEIGHT_CELLS;

	double max_r2 = 0;

	for (int i=0, hy = 0; hy <= HEIGHT_CELLS; hy++)
	{
		double dy = y + sxy * hy - xy[1];
		dy *= dy;
		for (int hx = 0; hx <= HEIGHT_CELLS; hx++, i++)
		{
			double dx = x + sxy * hx - xy[0];
			dx *= dx;

			double len = sqrt(dx + dy);
			if (len < br_radius)
			{
				double gauss = 0.5 + 0.5*cos(len / br_radius * M_PI);

				int d = (int)(round(gauss*gauss * mul));
				if (d)
					max_r2 = fmax(max_r2, dx + dy);

				int z = map[i] + d;

				if (br_limit)
				{
					if (d > 0)
					{
						if (map[i] > probe_z)
							z = map[i];
						else
						if (z > probe_z)
							z = probe_z;
					}
					else
					if (d < 0)
					{
						if (map[i] < probe_z)
							z = map[i];
						else
						if (z < probe_z)
							z = probe_z;
					}
				}
				else
				{
					if (z < 0)
						z = 0;
					if (z > 0xffff)
						z = 0xffff;
				}
				map[i] = z;
			}
		}
	}

	xy[2] = fmax(xy[2], max_r2);
	UpdateTerrainHeightMap(p);
}

void Stamp(double x, double y)
{
	// query all patches int radial range br_xyra[2] from x,y
	// get their heightmaps apply brush on height samples and update TexHeap pages 

	ImGuiIO& io = ImGui::GetIO();

	int stamp_mode = 1;
	if (io.KeysDown[A3D_LSHIFT])
		stamp_mode = 2;

	if (stamp_mode == 1)
	{
		URDO_Open();
		double xy[3] = { x,y,0 };
		QueryTerrain(terrain, x, y, br_radius * 1.001, 0x00, StampCB, xy);
		URDO_Close();
	}
	else
	{
		double mul = br_alpha * br_radius * HEIGHT_SCALE;
		if (fabs(mul) < 0.499)
			return;

		// gather
		int size = 4 * (int)ceil(br_radius / VISUAL_CELLS) + 2;
		int tmp_buf_size = sizeof(int)*(size*HEIGHT_CELLS)*(size*HEIGHT_CELLS);
		if (!gather || gather->size != size)
		{
			if (gather)
			{
				free(gather->tmp_x);
				free(gather->tmp_y);
				free(gather);
			}
			int bs = sizeof(Gather) + sizeof(Patch*)*(size*size - 1);
			gather = (Gather*)malloc(bs);
			gather->size = size;

			gather->tmp_x = (int*)malloc(tmp_buf_size);
			gather->tmp_y = (int*)malloc(tmp_buf_size);
		}

		memset(gather->patch, 0, sizeof(Patch*)*(size*size));

		gather->x = (int)floor(x / VISUAL_CELLS - 0.5 * size) * VISUAL_CELLS;
		gather->y = (int)floor(y / VISUAL_CELLS - 0.5 * size) * VISUAL_CELLS;

		gather->count=0;
		QueryTerrain(terrain, x, y, 2.0*br_radius, 0x00, GatherCB, 0);

		if (!gather->count)
			return;

		int* tmp_x = gather->tmp_x;
		memset(tmp_x, -1, tmp_buf_size);

		int r = (int)floor(br_radius * HEIGHT_CELLS / VISUAL_CELLS);
		for (int hy = 0; hy < size * HEIGHT_CELLS; hy++)
		{
			for (int hx = r; hx < size * HEIGHT_CELLS - r; hx++)
			{
				double acc = 0;
				double den = 0;

				for (int sx = hx-r; sx < hx+r; sx++)
				{
					int h = gather->Sample(sx, hy);
					if (h >= 0)
					{
						// HERE we use TRUE gaussian filter (must be separable)
						double len = (double)sx * VISUAL_CELLS / HEIGHT_CELLS + gather->x - x;
						len /= br_radius;
						double gauss = exp(-len * len * 3);

						acc += h * gauss;
						den += gauss;
					}
				}

				if (den > 0)
					tmp_x[hx + hy * size * HEIGHT_CELLS] = (uint16_t)round(acc / den);
				else
					tmp_x[hx + hy * size * HEIGHT_CELLS] = -1;
			}
		}

		int* tmp_y = gather->tmp_y;
		memset(tmp_y, -1, tmp_buf_size);

		for (int hy = r; hy < size * HEIGHT_CELLS - r; hy++)
		{
			for (int hx = r; hx < size * HEIGHT_CELLS - r; hx++)
			{
				double acc = 0;
				double den = 0;

				for (int sy = hy - r; sy < hy + r; sy++)
				{
					int h = tmp_x[hx + sy * size * HEIGHT_CELLS];
					if (h >= 0)
					{
						// HERE we use TRUE gaussian filter (must be separable)
						double len = (double)sy * VISUAL_CELLS / HEIGHT_CELLS + gather->y - y;
						len /= br_radius;
						double gauss = exp(-len*len*3);

						acc += h * gauss;
						den += gauss;
					}
				}

				if (den > 0)
					tmp_y[hx + hy * size * HEIGHT_CELLS] = (uint16_t)round(acc / den);
				else
					tmp_y[hx + hy * size * HEIGHT_CELLS] = -1;
			}
		}

		// run all patches
		URDO_Open();
		for (int py = gather->size/4; py < gather->size - gather->size / 4; py++)
		{
			for (int px = gather->size / 4; px < gather->size - gather->size / 4; px++)
			{
				Patch* p = gather->patch[px + size * py];
				if (p)
				{
					URDO_Patch(p);
					uint16_t* map = GetTerrainHeightMap(p);

					for (int sy = 0; sy <= HEIGHT_CELLS; sy++)
					{
						int hy = (HEIGHT_CELLS * py + sy);
						double dy = gather->y + hy * VISUAL_CELLS / (double)HEIGHT_CELLS - y;
						dy *= dy;
						for (int sx = 0; sx <= HEIGHT_CELLS; sx++)
						{
							int hx = (HEIGHT_CELLS * px + sx);
							double dx = gather->x + hx * VISUAL_CELLS / (double)HEIGHT_CELLS - x;
							dx *= dx;

							double len = sqrt(dx + dy);

							if (len < br_radius)
							{
								double gauss = 0.5 + 0.5*cos(len / br_radius * M_PI);
								gauss *= gauss * br_alpha;

								if (gauss < 0)
								{
									double diff = gauss * (tmp_y[hx + hy * size * HEIGHT_CELLS] - map[sx + sy * (HEIGHT_CELLS + 1)]);
									int z = (int)round(diff) + map[sx + sy * (HEIGHT_CELLS + 1)];
									if (z < 0)
										z = 0;
									if (z > 0xffff)
										z = 0xffff;

									map[sx + sy * (HEIGHT_CELLS + 1)] = z;
								}
								else
								{
									double blend = map[sx + sy * (HEIGHT_CELLS + 1)] * (1.0 - gauss);
									blend += tmp_y[hx + hy * size * HEIGHT_CELLS] * gauss;
									map[sx + sy * (HEIGHT_CELLS + 1)] = (uint16_t)round(blend);
								}
							}
						}
					}

					UpdateTerrainHeightMap(p);
				}
			}
		}
		URDO_Close();
	}
}

void my_render()
{
	ImGuiIO& io = ImGui::GetIO();

	// THINGZ
	static ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
	{
		ImGui_ImplOpenGL3_NewFrame();
		{
			// Setup time step
			ImGuiIO& io = ImGui::GetIO();
			uint64_t current_time = a3dGetTime();
			uint64_t delta = current_time - g_Time;
			io.DeltaTime = delta>0 ? delta / 1000000.0f : FLT_MIN;
			g_Time = current_time;
			// Start the frame
			ImGui::NewFrame();
		}

		ImGui::BeginMainMenuBar();
		if (ImGui::BeginMenu("Examples"))
		{
			{
				static bool s = false;
				ImGui::MenuItem("gogo", NULL, &s);
			}
			{
				static bool s = false;
				ImGui::MenuItem("zogo", NULL, &s);
			}
			{
				static bool s = false;
				ImGui::MenuItem("fogo", NULL, &s);
			}
			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();

		ImGui::Begin("Hello, world!");                          // Create a window called "Hello, world!" and append into it.

		if (ImGui::CollapsingHeader("View Control", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::SliderFloat("PITCH", &rot_pitch, +30.0f, +90.0f);

			ImGui::SliderFloat("YAW", &rot_yaw, -180.0f, +180.0f); ImGui::SameLine();
			ImGui::Checkbox("Spin", &spin_anim);

			ImGui::SliderFloat("ZOOM", &font_size, 0.16f, 16.0f);
			ImGui::SameLine();
			ImGui::Text("%dx%d", (int)round(io.DisplaySize.x/font_size), (int)round(io.DisplaySize.y / font_size));
		}

		if (ImGui::CollapsingHeader("Brush", ImGuiTreeNodeFlags_DefaultOpen))
		{
			const char* mode = "";

			if (!painting && io.KeyCtrl && io.KeyShift)
			{
				mode = "HEIGHT PROBE";
			}
			else
			if (!painting && io.KeyCtrl)
				mode = "DIAGONAL FLIP";
			else
			{
				if (io.KeyShift)
					mode = br_alpha >= 0 ? "BLURRING" : "SHARPENING";
				else
					mode = br_alpha >= 0 ? "ASCENT" : "DESCENT";
			}

			ImGui::Text("MODE (shift/ctrl): %s", mode);
			ImGui::SliderFloat("BRUSH RADIUS", &br_radius, 5.f, 100.f);
			ImGui::SliderFloat("BRUSH ALPHA", &br_alpha, -0.5f, +0.5f);


			ImGui::Checkbox("BRUSH HEIGHT LIMIT",&br_limit);
			ImGui::SameLine();

			// Arrow buttons with Repeater
			float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
			ImGui::PushButtonRepeat(true);
			if (ImGui::ArrowButton("##left", ImGuiDir_Left)) { if (probe_z>0) probe_z-=1; }
			ImGui::SameLine(0.0f, spacing);
			if (ImGui::ArrowButton("##right", ImGuiDir_Right)) { if (probe_z<0xffff) probe_z+=1; }
			ImGui::PopButtonRepeat();
			ImGui::SameLine();
			ImGui::Text("%d", probe_z);
			ImGui::Text("%s", "ctrl+shift to probe");

			// ImGui::SliderFloat("BRUSH HEIGHT", &probe_z, 0.0f, 65535.0f);
		}

		if (ImGui::CollapsingHeader("Undo / Redo", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (!URDO_CanUndo())
			{
				ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
				ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
				ImGui::Button("<<");
				ImGui::SameLine();
				ImGui::Button("<");
				ImGui::PopStyleVar();
				ImGui::PopItemFlag();
			}
			else
			{
				if (ImGui::Button("<<") || ImGui::IsItemActive() && io.MouseDownDuration[0] > .25f)
					URDO_Undo(0);
				ImGui::SameLine();
				if (ImGui::Button("<") || ImGui::IsItemActive() && io.MouseDownDuration[0] > .25f)
					URDO_Undo(1);
			}
			ImGui::SameLine();
			if (!URDO_CanRedo())
			{
				ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
				ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
				ImGui::Button(">");
				ImGui::SameLine();
				ImGui::Button(">>");
				ImGui::PopStyleVar();
				ImGui::PopItemFlag();
			}
			else
			{
				if (ImGui::Button(">") || ImGui::IsItemActive() && io.MouseDownDuration[0] > .25f)
					URDO_Redo(1);
				ImGui::SameLine();
				if (ImGui::Button(">>") || ImGui::IsItemActive() && io.MouseDownDuration[0] > .25f)
					URDO_Redo(0);
			}
			ImGui::SameLine();
			if (!URDO_CanRedo() && !URDO_CanUndo())
			{
				ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
				ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
				ImGui::Button("PURGE");
				ImGui::PopStyleVar();
				ImGui::PopItemFlag();
			}
			else
			if (ImGui::Button("PURGE"))
				URDO_Purge();
			ImGui::SameLine();
			ImGui::Text("%zu BYTES", URDO_Bytes());
		}

		if (ImGui::CollapsingHeader("Stats", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Text("PATCHES: %d, DRAWS: %d, CHANGES: %d", render_context.patches, render_context.draws, render_context.changes);
			ImGui::Text("RENDER TIME: %6jd [" /*micro*/"\xc2\xb5"/*utf8*/ "s]", render_context.render_time);
			ImGui::Text("%zu BYTES", GetTerrainBytes(terrain));
		}

		/*
		static int paint_mode=0;

		ImGui::RadioButton("VIEW POSITION", &paint_mode, 0); // or hold 'space' to interrupt current mode
		ImGui::RadioButton("PAINT", &paint_mode, 1); ImGui::SameLine();
		ImGui::RadioButton("SCULPT", &paint_mode, 2); ImGui::SameLine();
		ImGui::RadioButton("SMOOTH", &paint_mode, 3);

		if (paint_mode == 0)
		{
			ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
		}

		static int tool_mode = 0;
		ImGui::RadioButton("POINT", &tool_mode, 0); ImGui::SameLine();
		ImGui::RadioButton("LINE", &tool_mode, 1); ImGui::SameLine();
		ImGui::RadioButton("OVAL", &tool_mode, 2);

		if (paint_mode == 0)
		{
			ImGui::PopStyleVar();
			ImGui::PopItemFlag();
		}
		*/

		ImGui::End();

		static bool show_demo_window = true;
		static bool show_another_window = false;

		// 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
		if (show_demo_window)
			ImGui::ShowDemoWindow(&show_demo_window);

		/*

		// 2. Show a simple window that we create ourselves. We use a Begin/End pair to created a named window.
		{
			static float f = 0.0f;
			static int counter = 0;

			ImGui::Begin("Hello, world!");                          // Create a window called "Hello, world!" and append into it.

			ImGui::Text("This is some useful text.");               // Display some text (you can use a format strings too)
			ImGui::Checkbox("Demo Window", &show_demo_window);      // Edit bools storing our window open/close state
			ImGui::Checkbox("Another Window", &show_another_window);

			ImGui::SliderFloat("float", &f, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
			ImGui::ColorEdit3("clear color", (float*)&clear_color); // Edit 3 floats representing a color

			if (ImGui::Button("Button"))                            // Buttons return true when clicked (most widgets return true when edited/activated)
				counter++;
			ImGui::SameLine();
			ImGui::Text("counter = %d", counter);

			ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

			ImGui::Text("PATCHES: %d, DRAWS: %d, CHANGES: %d", render_context.patches, render_context.draws, render_context.changes);

			ImGui::End();
		}
		*/

		// 3. Show another simple window.
		/*
		if (show_another_window)
		{
			ImGui::Begin("Another Window", &show_another_window);   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
			ImGui::Text("Hello from another window!");
			if (ImGui::Button("Close Me"))
				show_another_window = false;
			ImGui::End();
		}
		*/
	}

	ImGui::Render();

	glViewport(0, 0, (GLsizei)io.DisplaySize.x, (GLsizei)io.DisplaySize.y);
	glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
	glClearDepth(0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	RenderContext* rc = &render_context;
	double tm[16];

	// currently we're assuming: 1 visual cell = 1 font_size

	double z_scale = 1.0 / HEIGHT_SCALE; // this is a constant, (what fraction of font_size is produced by +1 height_map)

	if (!io.MouseDown[0])
	{
		diag_flipped = false;
	}

	if (!io.MouseDown[1])
	{
		spinning = 0;
	}

	if (!io.MouseDown[2])
	{
		panning = 0;
	}

	if (!io.WantCaptureMouse)
	{
		if (zoom_wheel)
		{
			font_size *= pow(1.1, zoom_wheel);
			zoom_wheel = 0;
		}

		if (spinning)
		{
			double mdx = spinning_x - round(io.MousePos.x);
			double mdy = -(spinning_y - round(io.MousePos.y));

			rot_yaw += mdx * 0.1;
			if (rot_yaw < -180)
				rot_yaw += 360;
			if (rot_yaw > 180)
				rot_yaw -= 360;

			rot_pitch += mdy * 0.1;
			if (rot_pitch > 90)
				rot_pitch = 90;
			if (rot_pitch < 30)
				rot_pitch = 30;


			spinning_x = round(io.MousePos.x);
			spinning_y = round(io.MousePos.y);
		}
		else
		if (io.MouseDown[1])
		{
			spinning = 1;
			spinning_x = round(io.MousePos.x);
			spinning_y = round(io.MousePos.y);
		}
	}

	double rx = 0.5 * io.DisplaySize.x / font_size;
	double ry = 0.5 * io.DisplaySize.y / font_size;

	double pitch = rot_pitch * (M_PI / 180);
	double yaw = rot_yaw * (M_PI / 180);


	if (spin_anim)
	{
		rot_yaw += 0.1f;
		if (rot_yaw > 180)
			rot_yaw -= 360;
	}

	if (!io.WantCaptureMouse)
	{
		if (panning)
		{
			double mdx = panning_x - round(io.MousePos.x);
			double mdy = -(panning_y - round(io.MousePos.y)) / sin(pitch);
			pos_x = panning_dx + (mdx*cos(yaw) - mdy * sin(yaw)) / font_size;
			pos_y = panning_dy + (mdx*sin(yaw) + mdy * cos(yaw)) / font_size;

			panning_x = round(io.MousePos.x);
			panning_y = round(io.MousePos.y);

			panning_dx = pos_x;
			panning_dy = pos_y;
		}
		else
		if (io.MouseDown[2])
		{
			panning = 1;
			panning_x = round(io.MousePos.x);
			panning_y = round(io.MousePos.y);
			panning_dx = pos_x;
			panning_dy = pos_y;
		}
	}

	tm[0] = +cos(yaw)/rx;
	tm[1] = -sin(yaw)*sin(pitch)/ry;
	tm[2] = 0;
	tm[3] = 0;
	tm[4] = +sin(yaw)/rx;
	tm[5] = +cos(yaw)*sin(pitch)/ry;
	tm[6] = 0;
	tm[7] = 0;
	tm[8] = 0;
	tm[9] = +cos(pitch)*z_scale/ry;
	tm[10] = +2./0xffff;
	tm[11] = 0;
	tm[12] = -(pos_x * tm[0] + pos_y * tm[4] + pos_z * tm[8]);
	tm[13] = -(pos_x * tm[1] + pos_y * tm[5] + pos_z * tm[9]);
	tm[14] = -1.0;
	tm[15] = 1.0;

	float br_xyra[4] = { 0,0, br_radius, 0 };
	float br_quad[3] = { 0,0,0 };
	float br_probe[3] = { (float)probe_z, 1.0f, br_limit ? br_alpha : 0.0f };

	if (!io.WantCaptureMouse && mouse_in)
	{
		if (painting)
		{
			//DRAG and/or DROP
			double mdx = painting_x - round(io.MousePos.x);
			double mdy = -(painting_y - round(io.MousePos.y)) / sin(pitch);
			double dx = -(mdx*cos(yaw) - mdy * sin(yaw)) / font_size;
			double dy = -(mdx*sin(yaw) + mdy * cos(yaw)) / font_size;
			double x = painting_dx + dx;
			double y = painting_dy + dy;

			double dist = paint_dist + sqrt(dx*dx + dy * dy);

			int i = 0;
			float alpha = br_alpha;
			br_alpha *= STAMP_A;
			while (1)
			{
				double w = ((i + 1) * br_radius * STAMP_R - paint_dist) / (dist - paint_dist);

				if (w >= 1)
					break;

				double sx = painting_dx + w * dx;
				double sy = painting_dy + w * dy;

				Stamp(sx, sy);

				i++;
			}
			br_alpha = alpha;

			paint_dist = dist - i * br_radius * STAMP_R;
			painting_dx = x;
			painting_dy = y;
			painting_x = (int)round(io.MousePos.x);
			painting_y = (int)round(io.MousePos.y);

			br_xyra[0] = x;
			br_xyra[1] = y;

			if (!io.MouseDown[0])
			{
				// DROP
				float alpha = br_alpha;
				br_alpha *= pow(paint_dist / (br_radius * STAMP_R) * STAMP_A,2.0);
				Stamp(x, y);
				br_alpha = alpha;
				br_xyra[3] = 0;
				painting = 0;
				URDO_Close();
			}
			else
				br_xyra[3] = pow(paint_dist / (br_radius * STAMP_R) * STAMP_A, 2.0) * br_alpha;
		}
		else
		{
			// HOVER preview
			// all coords in world space!
			double itm[16];
			Invert(tm, itm);

			double ray_p[4];
			double ray_v[4];

			// mouse ray
			double clip_mouse[4] =
			{
				2.0 * io.MousePos.x / io.DisplaySize.x - 1.0,
				1.0 - 2.0 * io.MousePos.y / io.DisplaySize.y,
				-1.1, // bit under floor
				1
			};

			Product(itm, clip_mouse, ray_p);

			clip_mouse[2] = -1.2; // bit under bit under floor

			Product(itm, clip_mouse, ray_v);

			ray_v[0] -= ray_p[0];
			ray_v[1] -= ray_p[1];
			ray_v[2] -= ray_p[2];

			double hit[4];

			Patch* p = HitTerrain(terrain, ray_p, ray_v, hit);

			if (p)
			{
				if (io.KeyCtrl)
				{
					if (io.KeyShift)
					{
						// add here probe preview
						if (io.MouseDown[0])
						{
							// height-probe
							probe_z = (int)round(hit[2]);
							br_probe[0] = probe_z;
							br_probe[1] = 0.5;
						}
						else
						{
							// preview
							br_probe[0] = round(hit[2]);
							br_probe[1] = 0.5;
						}
					}
					else
					{
						// add here quad preview
						double qx = floor(hit[0] * HEIGHT_CELLS / VISUAL_CELLS) * VISUAL_CELLS / HEIGHT_CELLS;
						double qy = floor(hit[1] * HEIGHT_CELLS / VISUAL_CELLS) * VISUAL_CELLS / HEIGHT_CELLS;
						br_quad[0] = (float)qx;
						br_quad[1] = (float)qy;
						br_quad[2] = 1.0f;

						if (!diag_flipped && io.MouseDown[0])
						{
							// floor xy hit coords to height cells
							int hx = (int)floor(hit[0] * HEIGHT_CELLS / VISUAL_CELLS) % HEIGHT_CELLS;
							int hy = (int)floor(hit[1] * HEIGHT_CELLS / VISUAL_CELLS) % HEIGHT_CELLS;

							{
								uint16_t diag = GetTerrainDiag(p);
								diag ^= 1 << (hx + hy * HEIGHT_CELLS);

								URDO_Diag(p);
								SetTerrainDiag(p, diag);
							}

							// one per click
							diag_flipped = true;
						}
					}
				}
				else
				{
					br_xyra[0] = (float)hit[0];
					br_xyra[1] = (float)hit[1];
					br_xyra[3] = br_alpha;

					if (io.MouseDown[0])
					{
						//BEGIN
						URDO_Open();
						painting = 1;

						painting_x = round(io.MousePos.x);
						painting_y = round(io.MousePos.y);

						painting_dx = hit[0];
						painting_dy = hit[1];
						paint_dist = 0.0;

						float alpha = br_alpha;
						br_alpha *= STAMP_A;
						Stamp(hit[0], hit[1]);
						br_alpha = alpha;

						// stamped, don't apply preview to it
					}
				}
			}
		}
	}

	if (panning || spinning)
	{
		br_xyra[3] = 0;
	}

	if (io.KeysDown[A3D_LSHIFT])
	{
		br_xyra[2] = -br_xyra[2];
	}

	// 4 clip planes in clip-space

	double clip_left[4] =   { 1, 0, 0,+1 };
	double clip_right[4] =  {-1, 0, 0,+1 };
	double clip_bottom[4] = { 0, 1, 0,+1 }; 
	double clip_top[4] =    { 0,-1, 0,+1 }; // adjust by max brush descent

	double brush_extent = cos(pitch) * br_xyra[3] * br_xyra[2] / ry;

	if (br_xyra[2] > 0)
	{
		// adjust by max brush ASCENT
		if (br_xyra[3] > 0)
			clip_bottom[3] += brush_extent;

		// adjust by max brush DESCENT
		if (br_xyra[3] < 0)
			clip_top[3] -= brush_extent;
	}

	// transform them to world-space (mul by tm^-1)

	double clip_world[4][4];
	TransposeProduct(tm, clip_left, clip_world[0]);
	TransposeProduct(tm, clip_right, clip_world[1]);
	TransposeProduct(tm, clip_bottom, clip_world[2]);
	TransposeProduct(tm, clip_top, clip_world[3]);

	int planes = 4;
	int view_flags = 0xAA; // should contain only bits that face viewing direction

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_GEQUAL);
	rc->BeginPatches(tm, br_xyra, br_quad, br_probe);
	QueryTerrain(terrain, planes, clip_world, view_flags, RenderContext::RenderPatch, rc);
	//printf("rendered %d patches / %d total\n", rc.patches, GetTerrainPatches(terrain));
	rc->EndPatches();
	glDisable(GL_DEPTH_TEST);


	//glUseProgram(0); // You may want this if using this code in an OpenGL 3+ context where shaders may be bound, but prefer using the GL3+ code.
	
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

	a3dSwapBuffers();
}

void my_mouse(int x, int y, MouseInfo mi)
{
	if ((mi & 0xF) == MouseInfo::LEAVE)
	{
		mouse_in = 0;
		return;
	}
	else
	if ((mi & 0xF) == MouseInfo::ENTER)
		mouse_in = 1;

	ImGuiIO& io = ImGui::GetIO();

	io.MousePos = ImVec2((float)x, (float)y);

	switch (mi & 0xF)
	{
		case MouseInfo::WHEEL_DN:
			zoom_wheel--;
			io.MouseWheel -= 1.0;
			break;
		case MouseInfo::WHEEL_UP:
			zoom_wheel++;
			io.MouseWheel += 1.0;
			break;
		case MouseInfo::LEFT_DN:
			io.MouseDown[0] = true;
			break;
		case MouseInfo::LEFT_UP:
			io.MouseDown[0] = false;
			break;
		case MouseInfo::RIGHT_DN:
			io.MouseDown[1] = true;
			break;
		case MouseInfo::RIGHT_UP:
			io.MouseDown[1] = false;
			break;
		case MouseInfo::MIDDLE_DN:
			io.MouseDown[2] = true;
			break;
		case MouseInfo::MIDDLE_UP:
			io.MouseDown[2] = false;
			break;
	}
}

void my_resize(int w, int h)
{
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2((float)w, (float)h);
}

void my_init()
{
	font.tex = 0;
	a3dLoadImage("cp437_18x18.png", &font, Font::Load);

	g_Time = a3dGetTime();
	render_context.Create();

	glDebugMessageCallback(glDebugCall, 0/*cookie*/);

	// Setup Dear ImGui context
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = 0;
	io.BackendPlatformName = "imgui_impl_a3d";

	io.KeyMap[ImGuiKey_Tab] = A3D_TAB;
	io.KeyMap[ImGuiKey_LeftArrow] = A3D_LEFT;
	io.KeyMap[ImGuiKey_RightArrow] = A3D_RIGHT;
	io.KeyMap[ImGuiKey_UpArrow] = A3D_UP;
	io.KeyMap[ImGuiKey_DownArrow] = A3D_DOWN;
	io.KeyMap[ImGuiKey_PageUp] = A3D_PAGEUP;
	io.KeyMap[ImGuiKey_PageDown] = A3D_PAGEDOWN;
	io.KeyMap[ImGuiKey_Home] = A3D_HOME;
	io.KeyMap[ImGuiKey_End] = A3D_END;
	io.KeyMap[ImGuiKey_Insert] = A3D_INSERT;
	io.KeyMap[ImGuiKey_Delete] = A3D_DELETE;
	io.KeyMap[ImGuiKey_Backspace] = A3D_BACKSPACE;
	io.KeyMap[ImGuiKey_Space] = A3D_SPACE;
	io.KeyMap[ImGuiKey_Enter] = A3D_ENTER;
	io.KeyMap[ImGuiKey_Escape] = A3D_ESCAPE;
	io.KeyMap[ImGuiKey_A] = A3D_A;
	io.KeyMap[ImGuiKey_C] = A3D_C;
	io.KeyMap[ImGuiKey_V] = A3D_V;
	io.KeyMap[ImGuiKey_X] = A3D_X;
	io.KeyMap[ImGuiKey_Y] = A3D_Y;
	io.KeyMap[ImGuiKey_Z] = A3D_Z;

	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsClassic();

	ImGui_ImplOpenGL3_Init();

	terrain = CreateTerrain();

	// ALTERNATIVE:
	// terrain = CreateTerrain(int x, int y, int w, int h, uint16_t* data);
	// xywh coords are in patches, so data is w*4+1,h*4+1 !!!!!!!!!!!!!!!!

	const int num1 = 32;
	const int num2 = num1*num1;

	uint32_t* rnd = (uint32_t*)malloc(sizeof(uint32_t)*num2);
	int n = num2;
	for (int i = 0; i < num2; i++)
		rnd[i] = i;

	for (int i = 0; i < num2; i++)
	{
		int r = (fast_rand() + fast_rand()*(FAST_RAND_MAX+1)) % n;

		uint32_t uv = rnd[r];
		rnd[r] = rnd[--n];
		uint32_t u = uv % num1;
		uint32_t v = uv / num1;
		AddTerrainPatch(terrain, u, v, 0/*fast_rand()&0x7F*/);
	}

	free(rnd);

	pos_x = num1 * VISUAL_CELLS / 2;
	pos_y = num1 * VISUAL_CELLS / 2;
	pos_z = 0x0;

	a3dSetTitle(L"ASCIIID");

	int full[] = { -1280,0,800,600};
	//int full[] = { 0,0,1920,1080};
	a3dSetRect(full, true);

	a3dSetVisible(true);
}

void my_keyb_char(wchar_t chr)
{
	ImGuiIO& io = ImGui::GetIO();
	io.AddInputCharacter((unsigned short)chr);
}

void my_keyb_key(KeyInfo ki, bool down)
{
	ImGuiIO& io = ImGui::GetIO();
	if (ki < IM_ARRAYSIZE(io.KeysDown))
		io.KeysDown[ki] = down;
	
	io.KeysDown[A3D_ENTER] = a3dGetKeyb(A3D_ENTER) || a3dGetKeyb(A3D_NUMPAD_ENTER);
	io.KeyAlt = a3dGetKeyb(A3D_LALT) || a3dGetKeyb(A3D_RALT);
	io.KeyCtrl = a3dGetKeyb(A3D_LCTRL) || a3dGetKeyb(A3D_RCTRL);
	io.KeyShift = a3dGetKeyb(A3D_LSHIFT) || a3dGetKeyb(A3D_RSHIFT);
}

void my_keyb_focus(bool set)
{
}

void my_close()
{
	a3dClose();

	ImGui_ImplOpenGL3_Shutdown();
	ImGui::DestroyContext();

	render_context.Delete();
}

int main(int argc, char *argv[]) 
{
	PlatformInterface pi;
	pi.close = my_close;
	pi.render = my_render;
	pi.resize = my_resize;
	pi.init = my_init;
	pi.keyb_char = my_keyb_char;
	pi.keyb_key = my_keyb_key;
	pi.keyb_focus = my_keyb_focus;
	pi.mouse = my_mouse;

	GraphicsDesc gd;
	gd.color_bits = 32;
	gd.alpha_bits = 8;
	gd.depth_bits = 24;
	gd.stencil_bits = 8;
	gd.flags = (GraphicsDesc::FLAGS) (GraphicsDesc::DEBUG_CONTEXT | GraphicsDesc::DOUBLE_BUFFER);

	a3dOpen(&pi, &gd);

	return 0;
}
