
// this is CPU scene renderer into ANSI framebuffer
#include "asciiid_render.h"
#include "sprite.h"

#include <stdint.h>
#include <malloc.h>
#include <stdio.h>
#include "terrain.h"
#include "mesh.h"
#include "matrix.h"
#include "fast_rand.h"
// #include "sprite.h"

#define _USE_MATH_DEFINES
#include <math.h>
#include <float.h>
#include <string.h>

#define DBL

static bool global_refl_mode = false;
extern Sprite* player_sprite;

template <typename Sample>
inline void Bresenham(Sample* buf, int w, int h, int from[3], int to[3])
{
	int sx = to[0] - from[0];
	int sy = to[1] - from[1];

	if (sx == 0 && sy==0)
		return;

	int sz = to[2] - from[2];

	int ax = sx >= 0 ? sx : -sx;
	int ay = sy >= 0 ? sy : -sy;

	if (ax >= ay)
	{
		float n = +1.0f / sx;
		// horizontal domain

		if (from[0] > to[0])
		{
			int* swap = from;
			from = to;
			to = swap;
		}

		int	x0 = (std::max(0, from[0]) + 1) & ~1; // round up start x, so we won't produce out of domain samples
		int	x1 = std::min(w, to[0]);

		for (int x = x0; x < x1; x+=2)
		{
			float a = x - from[0] + 0.5f;
			int y = (int)floor((a * sy)*n + from[1] + 0.5f);
			if (y >= 0 && y < h)
			{
				float z = (a * sz) * n + from[2];
				Sample* ptr = buf + w * y + x;
				if (ptr->DepthTest_RO(z))
					ptr->spare |= 4;
				ptr++;
				if (ptr->DepthTest_RO(z))
					ptr->spare |= 4;
			}
		}
	}
	else
	{
		float n = 1.0f / sy;
		// vertical domain

		if (from[1] > to[1])
		{
			int* swap = from;
			from = to;
			to = swap;
		}

		int y0 = std::max(0, from[1]);
		int y1 = std::min(h, to[1]);

		for (int y = y0; y < y1; y++)
		{
			int a = y - from[1];
			int x = (int)floor((a * sx) * n + from[0] + 0.5f);
			if (x >= 0 && x < w)
			{
				float z = (a * sz)*n + from[2];
				Sample* ptr = buf + w * y + x;
				if (ptr->DepthTest_RO(z))
					ptr->spare |= 4;
			}
		}
	}
}

template <typename Sample, typename Shader>
inline void Rasterize(Sample* buf, int w, int h, Shader* s, const int* v[3])
{
	// each v[i] must point to 4 ints: {x,y,z,f} where f should indicate culling bits (can be 0)
	// shader must implement: bool Shader::Fill(Sample* s, int bc[3])
	// where bc contains 3 barycentric weights which are normalized to 0x8000 (use '>>15' after averaging)
	// Sample must implement bool DepthTest(int z, int divisor);
	// it must return true if z/divisor passes depth test on this sample
	// if test passes, it should write new z/d to sample's depth (if something like depth write mask is enabled)

	// produces samples between buffer cells 
	#define BC_A(a,b,c) (2*(((b)[0] - (a)[0]) * ((c)[1] - (a)[1]) - ((b)[1] - (a)[1]) * ((c)[0] - (a)[0])))

	// produces samples at centers of buffer cells 
	#define BC_P(a,b,c) (((b)[0] - (a)[0]) * (2*(c)[1]+1 - 2*(a)[1]) - ((b)[1] - (a)[1]) * (2*(c)[0]+1 - 2*(a)[0]))

	if ((v[0][3] & v[1][3] & v[2][3]) == 0)
	{
		int area = BC_A(v[0],v[1],v[2]);
		if (area > 0)
		{
			assert(area < 0x10000);
			float normalizer = (1.0f - FLT_EPSILON) / area;

			// canvas intersection with triangle bbox
			int left = std::max(0, std::min(v[0][0], std::min(v[1][0], v[2][0])));
			int right = std::min(w, std::max(v[0][0], std::max(v[1][0], v[2][0])));
			int bottom = std::max(0, std::min(v[0][1], std::min(v[1][1], v[2][1])));
			int top = std::min(h, std::max(v[0][1], std::max(v[1][1], v[2][1])));

			Sample* col = buf + bottom * w + left;
			for (int y = bottom; y < top; y++, col+=w)
			{
				Sample* row = col;
				for (int x = left; x < right; x++, row++)
				{
					int p[2] = { x,y };

					int bc[3] =
					{
						BC_P(v[1], v[2], p),
						BC_P(v[2], v[0], p),
						BC_P(v[0], v[1], p)
					};

					// outside
					if (bc[0] < 0 || bc[1] < 0 || bc[2] < 0)
					//if ((bc[0] | bc[1] | bc[2]) < 0)
						continue;

					/*
					if (area > 0)
					{
						if (bc[0] < 0 || bc[1] < 0 || bc[2] < 0)
							continue;
					}
					else
					{
						if (bc[0] > 0 || bc[1] > 0 || bc[2] > 0)
							continue;
					}
					*/

					// edge pairing
					if (bc[0] == 0 && v[1][0] <= v[2][0] ||
						bc[1] == 0 && v[2][0] <= v[0][0] ||
						bc[2] == 0 && v[0][0] <= v[1][0])
					{
						continue;
					}

					assert(bc[0] + bc[1] + bc[2] == area);

					float nbc[3] =
					{
						bc[0] * normalizer,
						bc[1] * normalizer,
						bc[2] * normalizer
					};

					float z = nbc[0] * v[0][2] + nbc[1] * v[1][2] + nbc[2] * v[2][2];
					//float z = (bc[0] * v[0][2] + bc[1] * v[1][2] + bc[2] * v[2][2]) * normalizer;

					s->Blend(row,z,nbc);
					/*
					if (row->DepthTest_RW(z))
					{
						if (global_refl_mode)
							s->Refl(row, nbc);
						else
							s->Fill(row, nbc);
					}
					*/
				}
			}
		}
	}
	#undef BC
}



struct Sample
{
	uint16_t visual;
	uint8_t diffuse;
	uint8_t spare;   // refl, patch xy parity etc..., direct color bit (meshes): visual has just 565 color?
	float height;

	/*
	inline bool DepthTest_RW(float z)
	{
		if (height > z)
			return false;
		spare &= ~0x4; // clear lines
		height = z;
		return true;
	}
	*/

	inline bool DepthTest_RO(float z)
	{
		if (height > z)
		{
			int a = 0;
		}
		return height <= z;
	}
};

struct SampleBuffer
{
	int w, h; // make 2x +2 bigger than terminal buffer
	Sample* ptr;
};

struct Renderer
{
	Renderer()
	{
		memset(this, 0, sizeof(Renderer));
	}

	~Renderer()
	{
		if (sample_buffer.ptr)
			free(sample_buffer.ptr);
	}

	SampleBuffer sample_buffer; // render surface

	uint8_t* buffer;
	int buffer_size; // ansi_buffer allocation size in cells (minimize reallocs)

	static void RenderPatch(Patch* p, int x, int y, int view_flags, void* cookie /*Renderer*/);
	static void RenderMesh(Mesh* m, double* tm, void* cookie /*Renderer*/);
	static void RenderFace(float coords[9], uint8_t colors[12], uint32_t visual, void* cookie /*Renderer*/);
	
	// unstatic -> needs R/W access to sample_buffer.ptr[].height for depth testing!
	void RenderSprite(AnsiCell* ptr, int width, int height, Sprite* s, bool refl, int anim, int frame, int angle, int pos[3]);

	// transform
	double mul[6]; // 3x2 rot part
	double add[3]; // post rotated and rounded translation
	float yaw,pos[3];
	float water;
	float light[4];
	bool int_flag;

	double viewinst_tm[16];
	const double* inst_tm;

	int patch_uv[HEIGHT_CELLS][2]; // constant
};

static int create_auto_mat(uint8_t mat[]);
static uint8_t auto_mat[/*b*/32/*g*/ * 32/*r*/ * 32/*bg,fg,gl*/ * 3];
int auto_mat_result = create_auto_mat(auto_mat);
static int create_auto_mat(uint8_t mat[])
{
	/*
	#define FLO(x) ((int)floor(5 * x / 31.0f))
	#define REM(x) (5*x-31*flo[x])
	*/

	#define MCV 5
	#define MCV_TO_5(mcv) (((mcv) * 5 + MCV/2) / MCV)
	#define FLO(x) ((int)floor(MCV * x / 31.0f))
	#define REM(x) (MCV*x-31*flo[x])

	static const int flo[32] =
	{
		FLO(0),  FLO(1),  FLO(2),  FLO(3),
		FLO(4),  FLO(5),  FLO(6),  FLO(7),
		FLO(8),  FLO(9),  FLO(10), FLO(11),
		FLO(12), FLO(13), FLO(14), FLO(15),
		FLO(16), FLO(17), FLO(18), FLO(19),
		FLO(20), FLO(21), FLO(22), FLO(23),
		FLO(24), FLO(25), FLO(26), FLO(27),
		FLO(28), FLO(29), FLO(30), FLO(31),
	};

	static const int rem[32]=
	{
		REM(0),  REM(1),  REM(2),  REM(3),
		REM(4),  REM(5),  REM(6),  REM(7),
		REM(8),  REM(9),  REM(10), REM(11),
		REM(12), REM(13), REM(14), REM(15),
		REM(16), REM(17), REM(18), REM(19),
		REM(20), REM(21), REM(22), REM(23),
		REM(24), REM(25), REM(26), REM(27),
		REM(28), REM(29), REM(30), REM(31),
	};

	static const char glyph[] = " ..::%";

	int max_pr = 0;

	int i = 0;
	for (int b=0; b<32; b++)
	{
		int p[3];
		p[2] = rem[b];
		int B[2] = { flo[b],std::min(MCV, flo[b] + 1) };
		for (int g = 0; g < 32; g++)
		{
			p[1] = rem[g];
			int G[2] = { flo[g],std::min(MCV, flo[g] + 1) };
			for (int r = 0; r < 32; r++,i++)
			{
				p[0] = rem[r];
				int R[2] = { flo[r],std::min(MCV, flo[r] + 1) };

				float best_sd = -1;
				float best_pr;
				int best_lo;
				int best_hi;

				// check all pairs of 8 cube verts
				for (int lo = 0; lo < 7; lo++)
				{
					int v0[3] = { R[lo & 1], G[(lo & 2) >> 1], B[(lo & 4) >> 2] };

					int pv0[3]=
					{
						R[0] * 31 + p[0] - v0[0] * 31,
						G[0] * 31 + p[1] - v0[1] * 31,
						B[0] * 31 + p[2] - v0[2] * 31,
					};

					for (int hi = lo + 1; hi < 8; hi++)
					{
						int v1[3] = { R[hi & 1], G[(hi & 2) >> 1], B[(hi & 4) >> 2] };
						int v10[3] = { 31*(v1[0] - v0[0]), 31*(v1[1] - v0[1]), 31*(v1[2] - v0[2]) };

						int v10_sqrlen = v10[0] * v10[0] + v10[1] * v10[1] + v10[2] * v10[2];

						float pr = v10_sqrlen ? (v10[0] * pv0[0] + v10[1] * pv0[1] + v10[2] * pv0[2]) / (float)v10_sqrlen : 0.0f;

						// projection point
						float prp[3] = { v10[0] * pr, v10[1] * pr, v10[2] * pr };

						// dist vect
						float prv[3] = { pv0[0] - prp[0], pv0[1] - prp[1], pv0[2] - prp[2] };

						// square dist
						float sd = sqrtf(prv[0] * prv[0] + prv[1] * prv[1] + prv[2] * prv[2]);

						if (sd < best_sd || best_sd < 0)
						{
							best_sd = sd;
							best_pr = pr;
							best_lo = lo;
							best_hi = hi;
						}
					}
				}

				int idx = 3 * (r + 32 * (g + 32 * b));
				int shd = (int)floorf( best_pr * 11 + 0.5f );

				if (shd > 11)
				{
					shd = 11;
				}

				if (shd < 0)
				{
					shd = 0;
				}

				if (shd < 6)
				{
					mat[idx + 0] = 16 + MCV_TO_5(R[best_lo & 1]) + 6 * MCV_TO_5(G[(best_lo & 2) >> 1]) + 36 * MCV_TO_5(B[(best_lo & 4) >> 2]);
					mat[idx + 1] = 16 + MCV_TO_5(R[best_hi & 1]) + 6 * MCV_TO_5(G[(best_hi & 2) >> 1]) + 36 * MCV_TO_5(B[(best_hi & 4) >> 2]);
					mat[idx + 2] = glyph[shd];
				}
				else
				{
					mat[idx + 0] = 16 + MCV_TO_5(R[best_hi & 1]) + 6 * MCV_TO_5(G[(best_hi & 2) >> 1]) + 36 * MCV_TO_5(B[(best_hi & 4) >> 2]);
					mat[idx + 1] = 16 + MCV_TO_5(R[best_lo & 1]) + 6 * MCV_TO_5(G[(best_lo & 2) >> 1]) + 36 * MCV_TO_5(B[(best_lo & 4) >> 2]);
					mat[idx + 2] = glyph[11-shd];
				}
			}
		}
	}

	return 1;
}

void Renderer::RenderFace(float coords[9], uint8_t colors[12], uint32_t visual, void* cookie)
{
	struct Shader
	{
		void Blend(Sample*s, float z, float bc[3])
		{
			if (s->height < z)
			{
				if (global_refl_mode)
				{
					if (z < water + HEIGHT_SCALE / 8)
					{
						if (z > water)
							s->height = water;
						else
							s->height = z;

						int r8 = (int)floor(rgb[0][0] * bc[0] + rgb[1][0] * bc[1] + rgb[2][0] * bc[2]);
						int r5 = (r8 * 249 + 1014) >> 11;
						int g8 = (int)floor(rgb[0][1] * bc[0] + rgb[1][1] * bc[1] + rgb[2][1] * bc[2]);
						int g5 = (g8 * 249 + 1014) >> 11;
						int b8 = (int)floor(rgb[0][2] * bc[0] + rgb[1][2] * bc[1] + rgb[2][2] * bc[2]);
						int b5 = (b8 * 249 + 1014) >> 11;

						s->visual = r5 | (g5 << 5) | (b5 << 10);
						s->diffuse = diffuse;
						s->spare = (s->spare & ~0x4) | 0x8 | 0x3;
					}  
				}
				else 
				{
					if (z >= water - HEIGHT_SCALE / 8)
					{
						if (z < water)
							s->height = water;
						else
							s->height = z;

						int r8 = (int)floor(rgb[0][0] * bc[0] + rgb[1][0] * bc[1] + rgb[2][0] * bc[2]);
						int r5 = (r8 * 249 + 1014) >> 11;
						int g8 = (int)floor(rgb[0][1] * bc[0] + rgb[1][1] * bc[1] + rgb[2][1] * bc[2]);
						int g5 = (g8 * 249 + 1014) >> 11;
						int b8 = (int)floor(rgb[0][2] * bc[0] + rgb[1][2] * bc[1] + rgb[2][2] * bc[2]);
						int b5 = (b8 * 249 + 1014) >> 11;

						s->visual = r5 | (g5 << 5) | (b5 << 10);
						s->diffuse = diffuse;
						s->spare = (s->spare & ~(0x3|0x4)) | 0x8 | 0x1;
					}
				}
			}
		}

		/*
		void Refl(Sample* s, float bc[3]) const
		{
			if (s->height < water)
			{
				int r8 = (int)floor(rgb[0][0] * bc[0] + rgb[1][0] * bc[1] + rgb[2][0] * bc[2]);
				int r5 = (r8 * 249 + 1014) >> 11;
				int g8 = (int)floor(rgb[0][1] * bc[0] + rgb[1][1] * bc[1] + rgb[2][1] * bc[2]);
				int g5 = (g8 * 249 + 1014) >> 11;
				int b8 = (int)floor(rgb[0][2] * bc[0] + rgb[1][2] * bc[1] + rgb[2][2] * bc[2]);
				int b5 = (b8 * 249 + 1014) >> 11;

				s->visual = r5 | (g5 << 5) | (b5 << 10);
				s->diffuse = diffuse;
				s->spare |= 0x8 | 0x3;
			}
		}

		void Fill(Sample* s, float bc[3]) const
		{
			if (s->height >= water)
			{
				int r8 = (int)floor(rgb[0][0] * bc[0] + rgb[1][0] * bc[1] + rgb[2][0] * bc[2]);
				int r5 = (r8 * 249 + 1014) >> 11;
				int g8 = (int)floor(rgb[0][1] * bc[0] + rgb[1][1] * bc[1] + rgb[2][1] * bc[2]);
				int g5 = (g8 * 249 + 1014) >> 11;
				int b8 = (int)floor(rgb[0][2] * bc[0] + rgb[1][2] * bc[1] + rgb[2][2] * bc[2]);
				int b5 = (b8 * 249 + 1014) >> 11;

				s->visual = r5 | (g5 << 5) | (b5 << 10);
				s->diffuse = diffuse;
				s->spare |= 0x8;
			}
			else
				s->height = -1000000;
			//	s->spare = 3;
		}
		*/

		/*
		inline void Diffuse(int dzdx, int dzdy)
		{
			float nl = (float)sqrt(dzdx * dzdx + dzdy * dzdy + HEIGHT_SCALE * HEIGHT_SCALE);
			float df = (dzdx * light[0] + dzdy * light[1] + HEIGHT_SCALE * light[2]) / nl;
			df = df * (1.0f - 0.5f*light[3]) + 0.5f*light[3];
			diffuse = df <= 0 ? 0 : (int)(df * 0xFF);
		}
		*/

		uint8_t* rgb[3]; // per vertex colors
		float water;
		float light[4];
		uint8_t diffuse; // shading experiment
	} shader;


	Renderer* r = (Renderer*)cookie;
	shader.water = r->water;

	// temporarily, let's transform verts for each face separately

	int v[3][4];

	float tmp0[4], tmp1[4], tmp2[4];

	{
		float xyzw[] = { coords[0], coords[1], coords[2], 1.0f };
		Product(r->viewinst_tm, xyzw, tmp0);
		v[0][0] = (int)floor(tmp0[0] + 0.5f);
		v[0][1] = (int)floor(tmp0[1] + 0.5f);
		v[0][2] = (int)floor(tmp0[2] + 0.5f);
		v[0][3] = 0; // clip flags
	}

	{
		float xyzw[] = { coords[3], coords[4], coords[5], 1.0f };
		Product(r->viewinst_tm, xyzw, tmp1);
		v[1][0] = (int)floor(tmp1[0] + 0.5f);
		v[1][1] = (int)floor(tmp1[1] + 0.5f);
		v[1][2] = (int)floor(tmp1[2] + 0.5f);
		v[1][3] = 0; // clip flags
	}

	{
		float xyzw[] = { coords[6], coords[7], coords[8], 1.0f };
		Product(r->viewinst_tm, xyzw, tmp2);
		v[2][0] = (int)floor(tmp2[0] + 0.5f);
		v[2][1] = (int)floor(tmp2[1] + 0.5f);
		v[2][2] = (int)floor(tmp2[2] + 0.5f);
		v[2][3] = 0; // clip flags
	}

	int w = r->sample_buffer.w;
	int h = r->sample_buffer.h;
	Sample* ptr = r->sample_buffer.ptr;

	// normal is const, could be baked into mesh
	float e1[] = { coords[3] - coords[0], coords[4] - coords[1], coords[5] - coords[2] };
	float e2[] = { coords[6] - coords[0], coords[7] - coords[1], coords[8] - coords[2] };

	float n[4] =
	{
		e1[1] * e2[2] - e1[2] * e2[1],
		e1[2] * e2[0] - e1[0] * e2[2],
		e1[0] * e2[1] - e1[1] * e2[0],
		0
	};

	float inst_n[4];
	Product(r->inst_tm, n, inst_n);
	float nn = 1.0f / sqrtf(inst_n[0] * inst_n[0] + inst_n[1] * inst_n[1] + inst_n[2] * inst_n[2]);

	float df = nn * (inst_n[0] * r->light[0] + inst_n[1] * r->light[1] + inst_n[2] * r->light[2]);

	//diffuse = 1.0;

	df = df * (1.0f - 0.5f*r->light[3]) + 0.5f*r->light[3];
	df += 0.5;
	if (df > 1)
		df = 1;
	if (df < 0)
		df = 0;

	shader.diffuse = (int)(df * 0xFF);

	if (global_refl_mode)
	{
		const int* pv[3] = { v[2],v[1],v[0] };
		shader.rgb[0] = colors + 8;
		shader.rgb[1] = colors + 4;
		shader.rgb[2] = colors + 0;

		//for (int i = 0; i < 12; i++)
		//	colors[i] = colors[i] * 3 / 4;

		Rasterize(r->sample_buffer.ptr, r->sample_buffer.w, r->sample_buffer.h, &shader, pv);
	}
	else
	{
		const int* pv[3] = { v[0],v[1],v[2] };
		shader.rgb[0] = colors + 0;
		shader.rgb[1] = colors + 4;
		shader.rgb[2] = colors + 8;
		Rasterize(r->sample_buffer.ptr, r->sample_buffer.w, r->sample_buffer.h, &shader, pv);
	}
}

void Renderer::RenderMesh(Mesh* m, double* tm, void* cookie)
{
	Renderer* r = (Renderer*)cookie;
	double view_tm[16]=
	{
		r->mul[0] * HEIGHT_CELLS, r->mul[1] * HEIGHT_CELLS, 0.0, 0.0,
		r->mul[2] * HEIGHT_CELLS, r->mul[3] * HEIGHT_CELLS, 0.0, 0.0,
		r->mul[4], r->mul[5], global_refl_mode ? -1.0 : 1.0, 0.0,
		r->add[0], r->add[1], r->add[2], 1.0
	};

	r->inst_tm = tm;
	MatProduct(view_tm, tm, r->viewinst_tm);
	QueryMesh(m, Renderer::RenderFace, r);

	// transform verts int integer coords
	// ...

	// given interpolated RGB -> round to 555, store it in visual
	// copy to diffuse to diffuse
	// mark mash 'auto-material' as 0x8 flag in spare

	// in post pass:
	// if sample has 0x8 flag
	//   multiply rgb by diffuse (into 888 bg=fg)
	// apply color mixing with neighbours
	// if at least 1 sample have mesh bit in spare
	// - round mixed bg rgb to R5G5B5 and use auto_material[32K] -> {bg,fg,gl}
	// else apply gridlines etc.
}

// we could easily make it template of <Sample,Shader>
void Renderer::RenderPatch(Patch* p, int x, int y, int view_flags, void* cookie /*Renderer*/)
{
	struct Shader
	{
		void Blend(Sample*s, float z, float bc[3])
		{
			if (s->height < z)
			{
				if (global_refl_mode)
				{
					if (z < water + HEIGHT_SCALE / 8)
					{
						if (z > water)
							s->height = water;
						else
							s->height = z;

						int u = (int)floor(uv[0] * bc[0] + uv[2] * bc[1] + uv[4] * bc[2]);
						int v = (int)floor(uv[1] * bc[0] + uv[3] * bc[1] + uv[5] * bc[2]);

						/*
						if (u >= VISUAL_CELLS || v >= VISUAL_CELLS)
						{
							// detect overflow
							s->visual = 2;
						}
						else
						*/
						{
							s->visual = map[v * VISUAL_CELLS + u];
							s->diffuse = diffuse;
							s->spare |= parity | 0x3;
							s->spare &= ~(0x4|0x8); // clear mesh and lines
						}
					}
				}
				else
				{
					if (z >= water - HEIGHT_SCALE / 8)
					{
						if (z < water)
							s->height = water;
						else
							s->height = z;

						int u = (int)floor(uv[0] * bc[0] + uv[2] * bc[1] + uv[4] * bc[2]);
						int v = (int)floor(uv[1] * bc[0] + uv[3] * bc[1] + uv[5] * bc[2]);

						/*
						if (u >= VISUAL_CELLS || v >= VISUAL_CELLS)
						{
							// detect overflow
							s->visual = 2;
						}
						else
						*/
						{
							s->visual = map[v * VISUAL_CELLS + u];
							s->diffuse = diffuse;
							s->spare = (s->spare & ~(0x8|0x3|0x4)) | parity; // clear refl, mesh and line, then add parity
						}
					}
				}
			}
		}

		/*
		void Refl(Sample* s, float bc[3]) const
		{
			if (s->height < water)
			{
				int u = (int)floor(uv[0] * bc[0] + uv[2] * bc[1] + uv[4] * bc[2]);
				int v = (int)floor(uv[1] * bc[0] + uv[3] * bc[1] + uv[5] * bc[2]);

				if (u >= VISUAL_CELLS || v >= VISUAL_CELLS)
				{
					// detect overflow
					s->visual = 2;
				}
				else
				{
					s->visual = map[v * VISUAL_CELLS + u];
					s->diffuse = diffuse;
					s->spare |= parity;
				}
			}
		}

		void Fill(Sample* s, float bc[3]) const
		{
			if (s->height >= water)
			{
				int u = (int)floor(uv[0] * bc[0] + uv[2] * bc[1] + uv[4] * bc[2]);
				int v = (int)floor(uv[1] * bc[0] + uv[3] * bc[1] + uv[5] * bc[2]);

				if (u >= VISUAL_CELLS || v >= VISUAL_CELLS)
				{
					// detect overflow
					s->visual = 2;
				}
				else
				{
					s->visual = map[v * VISUAL_CELLS + u];
					s->diffuse = diffuse;
					s->spare |= parity;
				}
			}
			else
				s->height = -1000000;
			//else
			//	s->spare = 3;
		}
		*/

		inline void Diffuse(int dzdx, int dzdy)
		{
			float nl = (float)sqrt(dzdx * dzdx + dzdy * dzdy + HEIGHT_SCALE * HEIGHT_SCALE);
			float df = (dzdx * light[0] + dzdy * light[1] + HEIGHT_SCALE * light[2]) / nl;
			df = df * (1.0f - 0.5f*light[3]) + 0.5f*light[3];
			diffuse = df <= 0 ? 0 : (int)(df * 0xFF);
		}

		int* uv; // points to array of 6 ints (u0,v0,u1,v1,u2,v2) each is equal to 0 or VISUAL_CELLS
		uint16_t* map; // points to array of VISUAL_CELLS x VISUAL_CELLS ushorts
		float water;
		float light[4];
		uint8_t diffuse; // shading experiment
		uint8_t parity;
	} shader;

	Renderer* r = (Renderer*)cookie;

	double* mul = r->mul;

	int iadd[2] = { (int)r->add[0], (int)r->add[1] };
	double* add = r->add;

	int w = r->sample_buffer.w;
	int h = r->sample_buffer.h;
	Sample* ptr = r->sample_buffer.ptr;

	uint16_t* hmap = GetTerrainHeightMap(p);
	
	uint16_t* hm = hmap;

	// transform patch verts xy+dx+dy, together with hmap into this array
	int xyzf[HEIGHT_CELLS + 1][HEIGHT_CELLS + 1][4];

	for (int dy = 0; dy <= HEIGHT_CELLS; dy++)
	{
		int vy = y * HEIGHT_CELLS + dy * VISUAL_CELLS;

		for (int dx = 0; dx <= HEIGHT_CELLS; dx++)
		{
			int vx = x * HEIGHT_CELLS + dx * VISUAL_CELLS;
			int vz = *(hm++);

			if (global_refl_mode)
			{
				if (r->int_flag)
				{
					int tx = (int)floor(mul[0] * vx + mul[2] * vy + 0.5 + add[0]);
					int ty = (int)floor(mul[1] * vx + mul[3] * vy + mul[5] * vz + 0.5 + add[1]);

					xyzf[dy][dx][0] = tx;
					xyzf[dy][dx][1] = ty;
					xyzf[dy][dx][2] = (int)(2 * r->water) - vz;

					// todo: if patch is known to fully fit in screen, set f=0 
					// otherwise we need to check if / which screen edges cull each vertex
					xyzf[dy][dx][3] = (tx < 0) | ((tx > w) << 1) | ((ty < 0) << 2) | ((ty > h) << 3);
				}
				else
				{
					int tx = (int)floor(mul[0] * vx + mul[2] * vy + 0.5) + iadd[0];
					int ty = (int)floor(mul[1] * vx + mul[3] * vy + mul[5] * vz + 0.5) + iadd[1];

					xyzf[dy][dx][0] = tx;
					xyzf[dy][dx][1] = ty;
					xyzf[dy][dx][2] = (int)(2 * r->water) - vz;

					// todo: if patch is known to fully fit in screen, set f=0 
					// otherwise we need to check if / which screen edges cull each vertex
					xyzf[dy][dx][3] = (tx < 0) | ((tx > w) << 1) | ((ty < 0) << 2) | ((ty > h) << 3);
				}
			}
			else
			{
				// transform 
				if (r->int_flag)
				{
					int tx = (int)floor(mul[0] * vx + mul[2] * vy + 0.5 + add[0]);
					int ty = (int)floor(mul[1] * vx + mul[3] * vy + mul[5] * vz + 0.5 + add[1]);

					xyzf[dy][dx][0] = tx;
					xyzf[dy][dx][1] = ty;
					xyzf[dy][dx][2] = vz;

					// todo: if patch is known to fully fit in screen, set f=0 
					// otherwise we need to check if / which screen edges cull each vertex
					xyzf[dy][dx][3] = (tx < 0) | ((tx > w) << 1) | ((ty < 0) << 2) | ((ty > h) << 3);
				}
				else
				{
					int tx = (int)floor(mul[0] * vx + mul[2] * vy + 0.5) + iadd[0];
					int ty = (int)floor(mul[1] * vx + mul[3] * vy + mul[5] * vz + 0.5) + iadd[1];

					xyzf[dy][dx][0] = tx;
					xyzf[dy][dx][1] = ty;
					xyzf[dy][dx][2] = vz;

					// todo: if patch is known to fully fit in screen, set f=0 
					// otherwise we need to check if / which screen edges cull each vertex
					xyzf[dy][dx][3] = (tx < 0) | ((tx > w) << 1) | ((ty < 0) << 2) | ((ty > h) << 3);
				}
			}
		}
	}

	uint16_t  diag = GetTerrainDiag(p);

	// 2 parity bits for drawing lines around patches
	// 0 - no patch rendered here
	// 1 - odd
	// 2 - even
	// 3 - under water
	shader.parity = (((x^y)/VISUAL_CELLS) & 1) + 1; 
	shader.water = r->water;
	shader.map = GetTerrainVisualMap(p);

	shader.light[0] = r->light[0];
	shader.light[1] = r->light[1];
	shader.light[2] = r->light[2];
	shader.light[3] = r->light[3];

	/*
	shader.light[0] = 0;
	shader.light[1] = 0;
	shader.light[2] = 1;
	*/

//	if (shader.parity == 1)
//		return;

	hm = hmap;

	const int (*uv)[2] = r->patch_uv;

	for (int dy = 0; dy < HEIGHT_CELLS; dy++, hm++)
	{
		for (int dx = 0; dx < HEIGHT_CELLS; dx++,diag>>=1, hm++)
		{
			//if (!(diag & 1))
			if (diag & 1)
			{
				// .
				// |\
				// |_\
				// '  '
				// lower triangle

				// terrain should keep diffuse map with timestamp of light modification it was updated to
				// then if current light timestamp is different than in terrain we need to update diffuse (into terrain)
				// now we should simply use diffuse from terrain
				// note: if terrain is being modified, we should clear its timestamp or immediately update diffuse
				if (global_refl_mode)
				{
					//done
					int lo_uv[] = { uv[dx][0],uv[dy][1], uv[dx][1],uv[dy][0], uv[dx][0],uv[dy][0] };
					const int* lo[3] = { xyzf[dy + 1][dx], xyzf[dy][dx + 1], xyzf[dy][dx] };
					shader.uv = lo_uv;
					shader.Diffuse(-xyzf[dy][dx][2] + xyzf[dy][dx + 1][2], -xyzf[dy][dx][2] + xyzf[dy + 1][dx][2]);
					Rasterize(ptr, w, h, &shader, lo);
				}
				else
				{
					int lo_uv[] = { uv[dx][0],uv[dy][0], uv[dx][1],uv[dy][0], uv[dx][0],uv[dy][1] };
					const int* lo[3] = { xyzf[dy][dx], xyzf[dy][dx + 1], xyzf[dy + 1][dx] };
					shader.uv = lo_uv;
					shader.Diffuse(xyzf[dy][dx][2] - xyzf[dy][dx + 1][2], xyzf[dy][dx][2] - xyzf[dy + 1][dx][2]);
					Rasterize(ptr, w, h, &shader, lo);
				}

				// .__.
				//  \ |
				//   \|
				//    '
				// upper triangle
				if (global_refl_mode)
				{
					//done
					int up_uv[] = { uv[dx][1],uv[dy][0], uv[dx][0],uv[dy][1], uv[dx][1],uv[dy][1] };
					const int* up[3] = { xyzf[dy][dx + 1], xyzf[dy + 1][dx], xyzf[dy + 1][dx + 1] };
					shader.uv = up_uv;
					shader.Diffuse(-xyzf[dy + 1][dx][2] + xyzf[dy + 1][dx + 1][2], -xyzf[dy][dx + 1][2] + xyzf[dy + 1][dx + 1][2]);
					Rasterize(ptr, w, h, &shader, up);
				}
				else
				{
					int up_uv[] = { uv[dx][1],uv[dy][1], uv[dx][0],uv[dy][1], uv[dx][1],uv[dy][0] };
					const int* up[3] = { xyzf[dy + 1][dx + 1], xyzf[dy + 1][dx], xyzf[dy][dx + 1] };
					shader.uv = up_uv;
					shader.Diffuse(xyzf[dy + 1][dx][2] - xyzf[dy + 1][dx + 1][2], xyzf[dy][dx + 1][2] - xyzf[dy + 1][dx + 1][2]);
					Rasterize(ptr, w, h, &shader, up);
				}
			}
			else
			{
				// lower triangle
				//    .
				//   /|
				//  /_|
				// '  '
				if (global_refl_mode)
				{
					// done
					int lo_uv[] = { uv[dx][0],uv[dy][0], uv[dx][1],uv[dy][1], uv[dx][1],uv[dy][0] };
					const int* lo[3] = { xyzf[dy][dx], xyzf[dy + 1][dx + 1], xyzf[dy][dx + 1] };
					shader.uv = lo_uv;
					shader.Diffuse(-xyzf[dy][dx][2] + xyzf[dy][dx + 1][2], -xyzf[dy][dx + 1][2] + xyzf[dy + 1][dx + 1][2]);
					Rasterize(ptr, w, h, &shader, lo);
				}
				else
				{
					int lo_uv[] = { uv[dx][1],uv[dy][0], uv[dx][1],uv[dy][1], uv[dx][0],uv[dy][0] };
					const int* lo[3] = { xyzf[dy][dx + 1], xyzf[dy + 1][dx + 1], xyzf[dy][dx] };
					shader.uv = lo_uv;
					shader.Diffuse(xyzf[dy][dx][2] - xyzf[dy][dx + 1][2], xyzf[dy][dx + 1][2] - xyzf[dy + 1][dx + 1][2]);
					Rasterize(ptr, w, h, &shader, lo);
				}


				// upper triangle
				// .__.
				// | / 
				// |/  
				// '
				if (global_refl_mode)
				{
					//done
					int up_uv[] = { uv[dx][1],uv[dy][1], uv[dx][0],uv[dy][0], uv[dx][0],uv[dy][1] };
					const int* up[3] = { xyzf[dy + 1][dx + 1], xyzf[dy][dx], xyzf[dy + 1][dx]  };
					shader.uv = up_uv;
					shader.Diffuse(-xyzf[dy + 1][dx][2] + xyzf[dy + 1][dx + 1][2], -xyzf[dy][dx][2] + xyzf[dy + 1][dx][2]);
					Rasterize(ptr, w, h, &shader, up);
				}
				else
				{
					int up_uv[] = { uv[dx][0],uv[dy][1], uv[dx][0],uv[dy][0], uv[dx][1],uv[dy][1] };
					const int* up[3] = { xyzf[dy + 1][dx], xyzf[dy][dx], xyzf[dy + 1][dx + 1] };
					shader.uv = up_uv;
					shader.Diffuse(xyzf[dy + 1][dx][2] - xyzf[dy + 1][dx + 1][2], xyzf[dy][dx][2] - xyzf[dy + 1][dx][2]);
					Rasterize(ptr, w, h, &shader, up);
				}
			}
		}
	}


	if (!global_refl_mode) // disabled on reflections
	{
		// grid lines thru middle of patch?
		int mid = (HEIGHT_CELLS + 1) / 2;

		for (int lin = 0; lin <= HEIGHT_CELLS; lin++)
		{
			xyzf[lin][mid][2] += HEIGHT_SCALE / 2;
			if (mid != lin)
				xyzf[mid][lin][2] += HEIGHT_SCALE / 2;
		}

		for (int lin = 0; lin < HEIGHT_CELLS; lin++)
		{
			Bresenham(ptr, w, h, xyzf[lin][mid], xyzf[lin + 1][mid]);
			Bresenham(ptr, w, h, xyzf[mid][lin], xyzf[mid][lin + 1]);
		}
	}
}

void Renderer::RenderSprite(AnsiCell* ptr, int width, int height, Sprite* s, bool refl, int anim, int frame, int angle, int pos[3])
{
	// intersect frame with screen buffer
	int i = frame + angle * s->anim[anim].length;
	if (refl)
		i += s->anim[anim].length * s->angles;

	Sprite::Frame* f = s->atlas + s->anim[anim].frame_idx[i];

	int dx = f->ref[0] / 2;
	int dy = f->ref[1] / 2;

	int left   = pos[0] - dx;
	int right  = left + f->width;
	int bottom = pos[1] - dy;
	int top    = bottom + f->height;

	left = std::max(0, left);
	right = std::min(width, right);

	if (left >= right)
		return;

	bottom = std::max(0, bottom);
	top = std::min(height, top);

	if (bottom >= top)
		return;

	int sample_xy = 2 + 2 * (2 + 2 * width + 2);
	int sample_dx = 2;
	int sample_dy = 2 * (2 + 2 * width + 2);
	int sample_ofs[4] = { 0, 1, 2 + 2 * width + 2, 2 + 2 * width + 2 + 1 };

	//static const float height_scale = HEIGHT_SCALE / 1.5; // WHY?????  HS*DBL/ZOOM ?

	static const float ds = 2.0 * (/*zoom*/ 1.0 * /*scale*/ 3.0) / VISUAL_CELLS * 0.5 /*we're not dbl_wh*/;
	static const float dz_dy = HEIGHT_SCALE / (cos(30 * M_PI / 180) * HEIGHT_CELLS * ds);

	for (int y = bottom; y < top; y++)
	{
		int fy = y - pos[1] + dy;
		for (int x = left; x < right; x++)
		{
			int fx = x - pos[0] + dx;
			AnsiCell* dst = ptr + x + width * y;
			const AnsiCell* src = f->cell + fx + fy * f->width;

			int depth_passed = 0;

			Sample* s00 = sample_buffer.ptr + sample_xy + x * sample_dx + y * sample_dy;
			Sample* s01 = s00 + 1;
			Sample* s10 = s00 + 2 + 2 * width + 2;
			Sample* s11 = s10 + 1;

			// spare is in full blocks, ref in half!
			float height = (2 * src->spare + f->ref[2]) * 0.5 * dz_dy + pos[2]; // *height_scale + pos[2]; // transform!

			if (src->bk != 255)
			{
				if (src->fg != 255)
				{
					// check if at least 2/4 samples passes depth test, update all 4
					// ...

					if (!refl && height >= water || refl && height <= water)
					{
						if (height >= s00->height)
							depth_passed++;
						if (height >= s01->height)
							depth_passed++;
						if (height >= s10->height)
							depth_passed++;
						if (height >= s11->height)
							depth_passed++;
					}

					if (depth_passed >= 3)
					{
						*dst = *src;
						s00->height = height;
						s01->height = height;
						s10->height = height;
						s11->height = height;
					}
				}
				else
				{
					// check if at least 1/2 bk sample passes depth test, update both
					// ...

					if (!refl && height >= water || refl && height <= water)
					{
						if (height >= s00->height)
							depth_passed++;
						if (height >= s01->height)
							depth_passed++;
						if (height >= s10->height)
							depth_passed++;
						if (height >= s11->height)
							depth_passed++;
					}

					if (depth_passed >= 3)
					{
						if (dst->gl == 0xDC && src->gl == 0xDF || dst->gl == 0xDD && src->gl == 0xDE ||
							dst->gl == 0xDF && src->gl == 0xDC || dst->gl == 0xDE && src->gl == 0xDD)
						{
							dst->fg = src->bk;
						}
						else
						{
							dst->bk = src->bk;
							dst->gl = src->gl;
						}

						s00->height = height;
						s01->height = height;
						s10->height = height;
						s11->height = height;
					}
				}
			}
			else
			{
				if (src->fg != 255)
				{
					// check if at least 1/2 fg samples passes depth test, update both
					// ...
					if (!refl && height >= water || refl && height <= water)
					{
						if (height >= s00->height)
							depth_passed++;
						if (height >= s01->height)
							depth_passed++;
						if (height >= s10->height)
							depth_passed++;
						if (height >= s11->height)
							depth_passed++;
					}

					if (depth_passed >= 3)
					{
						if (dst->gl == 0xDC && src->gl == 0xDF || dst->gl == 0xDD && src->gl == 0xDE ||
							dst->gl == 0xDF && src->gl == 0xDC || dst->gl == 0xDE && src->gl == 0xDD)
						{
							dst->bk = src->fg;
						}
						else
						{
							dst->fg = src->fg;
							dst->gl = src->gl;
						}

						s00->height = height;
						s01->height = height;
						s10->height = height;
						s11->height = height;
					}
				}
			}
		}
	}
}

bool Render(Terrain* t, World* w, float water, float zoom, float yaw, float pos[3], float lt[4], int width, int height, AnsiCell* ptr, float player_dir, int player_stp)
{
	AnsiCell* out_ptr = ptr;
	static Renderer r;

#ifdef DBL
	float scale = 3.0;
#else
	float scale = 1.5;
#endif

	zoom *= scale;

#ifdef DBL
	int dw = 4+2*width;
	int dh = 4+2*height;
#else
	int dw = 1 + width + 1;
	int dh = 1 + height + 1;
#endif

	float ds = 2*zoom / VISUAL_CELLS;

	if (!r.sample_buffer.ptr)
	{
		r.int_flag = true;
		for (int uv=0; uv<HEIGHT_CELLS; uv++)
		{
			r.patch_uv[uv][0] = uv * VISUAL_CELLS / HEIGHT_CELLS;
			r.patch_uv[uv][1] = (uv+1) * VISUAL_CELLS / HEIGHT_CELLS;
		};


		r.sample_buffer.w = dw;
		r.sample_buffer.h = dh;
		r.sample_buffer.ptr = (Sample*)malloc(dw*dh * sizeof(Sample) * 2); // upper half is clear cache

		for (int cl = dw * dh; cl < 2*dw*dh; cl++)
		{
			r.sample_buffer.ptr[cl].height = -1000000;
			r.sample_buffer.ptr[cl].spare = 0x8;
			r.sample_buffer.ptr[cl].diffuse = 0xFF;
			r.sample_buffer.ptr[cl].visual = 0xC | (0xC << 5) | (0x1B << 10);
		}
	}
	else
	if (r.sample_buffer.w != dw || r.sample_buffer.h != dh)
	{
		r.int_flag = true;
		r.sample_buffer.w = dw;
		r.sample_buffer.h = dh;
		free(r.sample_buffer.ptr);
		r.sample_buffer.ptr = (Sample*)malloc(dw*dh * sizeof(Sample) * 2); // upper half is clear cache

		for (int cl = dw * dh; cl < 2 * dw*dh; cl++)
		{
			r.sample_buffer.ptr[cl].height = -1000000;
			r.sample_buffer.ptr[cl].spare = 0x8;
			r.sample_buffer.ptr[cl].diffuse = 0xFF;
			r.sample_buffer.ptr[cl].visual = 0xC | (0xC << 5) | (0x1B << 10);
		}
	}
	else
	{
		if (pos[0] != r.pos[0] || pos[1] != r.pos[1] || pos[2] != r.pos[2])
		{
			r.int_flag = true;
		}

		if (yaw != r.yaw)
		{
			r.int_flag = false;
		}
	}


	r.pos[0] = pos[0];
	r.pos[1] = pos[1];
	r.pos[2] = pos[2];
	r.yaw = yaw;

	r.light[0] = lt[0];
	r.light[1] = lt[1];
	r.light[2] = lt[2];
	r.light[3] = lt[3];

	// memset(r.sample_buffer.ptr, 0x00, dw*dh * sizeof(Sample));
	memcpy(r.sample_buffer.ptr, r.sample_buffer.ptr + dw * dh, dw*dh * sizeof(Sample));

	static const double sin30 = sin(M_PI*30.0/180.0); 
	static const double cos30 = cos(M_PI*30.0/180.0);

	/*
	static int frame = 0;
	frame++;
	if (frame == 200)
		frame = 0;
	water += HEIGHT_SCALE * 5 * sinf(frame*M_PI*0.01);
	*/

	// water integerificator (there's 4 instead of 2 because reflection goes 2x faster than water)
	int water_i = (int)floor(water / (HEIGHT_SCALE / (4 * ds * cos30)));
	water = (float)(water_i * (HEIGHT_SCALE / (4 * ds * cos30)));

	r.water = water;

	double a = yaw * M_PI / 180.0;
	double sinyaw = sin(a);
	double cosyaw = cos(a);

	double tm[16];
	tm[0] = +cosyaw *ds;
	tm[1] = -sinyaw * sin30*ds;
	tm[2] = 0;
	tm[3] = 0;
	tm[4] = +sinyaw * ds;
	tm[5] = +cosyaw * sin30*ds;
	tm[6] = 0;
	tm[7] = 0;
	tm[8] = 0;
	tm[9] = +cos30/HEIGHT_SCALE*ds*HEIGHT_CELLS;
	tm[10] = 1.0; //+2./0xffff;
	tm[11] = 0;
	//tm[12] = dw*0.5 - (pos[0] * tm[0] + pos[1] * tm[4] + pos[2] * tm[8]) * HEIGHT_CELLS;
	//tm[13] = dh*0.5 - (pos[0] * tm[1] + pos[1] * tm[5] + pos[2] * tm[9]) * HEIGHT_CELLS;
	tm[12] = dw*0.5 - (pos[0] * tm[0] * HEIGHT_CELLS + pos[1] * tm[4] * HEIGHT_CELLS + pos[2] * tm[8]);
	tm[13] = dh*0.5 - (pos[0] * tm[1] * HEIGHT_CELLS + pos[1] * tm[5] * HEIGHT_CELLS + pos[2] * tm[9]);
	tm[14] = 0.0; //-1.0;
	tm[15] = 1.0;

	r.mul[0] = tm[0];
	r.mul[1] = tm[1];
	r.mul[2] = tm[4];
	r.mul[3] = tm[5];
	r.mul[4] = 0;
	r.mul[5] = tm[9];

	// if yaw didn't change, make it INTEGRAL (and EVEN in case of DBL)
	r.add[0] = tm[12];
	r.add[1] = tm[13] + 0.5;
	r.add[2] = tm[14];

	if (r.int_flag)
	{
		int x = (int)floor(r.add[0] + 0.5);
		int y = (int)floor(r.add[1] + 0.5);

		#ifdef DBL
		x &= ~1;
		y &= ~1;
		#endif

		r.add[0] = (double)x;
		r.add[1] = (double)y;
	}

	int planes = 5;
	int view_flags = 0xAA; // should contain only bits that face viewing direction

	double clip_world[5][4];

	double clip_left[4] =   { 1, 0, 0, 1 };
	double clip_right[4] =  {-1, 0, 0, 1 };
	double clip_bottom[4] = { 0, 1, 0, 1 };
	double clip_top[4] =    { 0,-1, 0, 1 };
	double clip_water[4] =  { 0, 0, 1, -((r.water-1)*2.0/0xffff - 1.0) };

	// easier to use another transform for clipping
	{
		// somehow it works
		double clip_tm[16];
		clip_tm[0] = +cosyaw / (0.5 * dw) * ds * HEIGHT_CELLS;
		clip_tm[1] = -sinyaw*sin30 / (0.5 * dh) * ds * HEIGHT_CELLS;
		clip_tm[2] = 0;
		clip_tm[3] = 0;
		clip_tm[4] = +sinyaw / (0.5 * dw) * ds * HEIGHT_CELLS;
		clip_tm[5] = +cosyaw*sin30 / (0.5 * dh) * ds * HEIGHT_CELLS;
		clip_tm[6] = 0;
		clip_tm[7] = 0;
		clip_tm[8] = 0;
		clip_tm[9] = +cos30 / HEIGHT_SCALE / (0.5 * dh) * ds * HEIGHT_CELLS;
		clip_tm[10] = +2. / 0xffff;
		clip_tm[11] = 0;
		clip_tm[12] = -(pos[0] * clip_tm[0] + pos[1] * clip_tm[4] + pos[2] * clip_tm[8]);
		clip_tm[13] = -(pos[0] * clip_tm[1] + pos[1] * clip_tm[5] + pos[2] * clip_tm[9]);
		clip_tm[14] = -1.0;
		clip_tm[15] = 1.0;

		TransposeProduct(clip_tm, clip_left, clip_world[0]);
		TransposeProduct(clip_tm, clip_right, clip_world[1]);
		TransposeProduct(clip_tm, clip_bottom, clip_world[2]);
		TransposeProduct(clip_tm, clip_top, clip_world[3]);
		TransposeProduct(clip_tm, clip_water, clip_world[4]);
	}

	QueryTerrain(t, planes, clip_world, view_flags, Renderer::RenderPatch, &r);
	QueryWorld(w, planes, clip_world, Renderer::RenderMesh, &r);


	// player shadow

	double inv_tm[16];
	Invert(tm, inv_tm);

	void* GetMaterialArr();
	Material* matlib = (Material*)GetMaterialArr();

	int sh_x = (dw/2 + 1) & ~1;
	for (int y = 0; y < dh; y++)
	{
		for (int x = sh_x-10; x <= sh_x+10; x++)
		{
			Sample* s = r.sample_buffer.ptr + x + y * dw;
			if (abs(s->height - pos[2]) <= 64)
			{
				double screen_space[] = { x+.5,y+.5,s->height,1.0 };
				double world_space[4];

				Product(inv_tm, screen_space, world_space);
				double dx = world_space[0]/HEIGHT_CELLS - pos[0];
				double dy = world_space[1]/HEIGHT_CELLS - pos[1];
				if (dx*dx + dy*dy <= 2.00)
				{
					if (s->spare & 0x8)
					{
						s->diffuse = s->diffuse * 200 / 255;
					}
					else
					{
						int mat = s->visual & 0xFF;
						int shd = (s->visual >> 8) & 0x7F;

						int r = (matlib[mat].shade[1][shd].bg[0] * 249 + 1014) >> 11;
						int g = (matlib[mat].shade[1][shd].bg[1] * 249 + 1014) >> 11;
						int b = (matlib[mat].shade[1][shd].bg[2] * 249 + 1014) >> 11;
						s->visual = r | (g << 5) | (b << 10);

						// if this is terrain sample, convert it to rgb first
						// s->visual = ;
						s->spare |= 0x8;
						s->spare &= ~4;
						s->diffuse = 230;
					}
				}
			}
		}
	}

	////////////////////
	// REFL

	// once again for reflections
	tm[8] = -tm[8];
	tm[9] = -tm[9];
	tm[10] = -tm[10]; // let them simply go below 0 :)

	//tm[12] = dw*0.5 - (pos[0] * tm[0] + pos[1] * tm[4] + ((2 * water / HEIGHT_CELLS) - pos[2]) * tm[8]) * HEIGHT_CELLS;
	//tm[13] = dh*0.5 - (pos[0] * tm[1] + pos[1] * tm[5] + ((2 * water / HEIGHT_CELLS) - pos[2]) * tm[9]) * HEIGHT_CELLS;
	tm[12] = dw*0.5 - (pos[0] * tm[0] * HEIGHT_CELLS + pos[1] * tm[4] * HEIGHT_CELLS + ((2 * water) - pos[2]) * tm[8]);
	tm[13] = dh*0.5 - (pos[0] * tm[1] * HEIGHT_CELLS + pos[1] * tm[5] * HEIGHT_CELLS + ((2 * water) - pos[2]) * tm[9]);
	tm[14] = 2*r.water;

	r.mul[0] = tm[0];
	r.mul[1] = tm[1];
	r.mul[2] = tm[4];
	r.mul[3] = tm[5];
	r.mul[4] = 0;
	r.mul[5] = tm[9];

	// if yaw didn't change, make it INTEGRAL (and EVEN in case of DBL)
	r.add[0] = tm[12];
	r.add[1] = tm[13] + 0.5;
	r.add[2] = tm[14];

	if (r.int_flag)
	{
		int x = (int)floor(r.add[0] + 0.5);
		int y = (int)floor(r.add[1] + 0.5);

		#ifdef DBL
		x &= ~1;
		y &= ~1;
		#endif

		r.add[0] = (double)x;
		r.add[1] = (double)y;
	}


	clip_water[2] = -1; // was +1
	clip_water[3] = +((r.water+1)*-2.0 / 0xffff + 1.0); // was -((r.water-1)*2.0/0xffff - 1.0)

	{
		// somehow it works
		double clip_tm[16];
		clip_tm[0] = +cosyaw / (0.5 * dw) * ds * HEIGHT_CELLS;
		clip_tm[1] = -sinyaw * sin30 / (0.5 * dh) * ds * HEIGHT_CELLS;
		clip_tm[2] = 0;
		clip_tm[3] = 0;
		clip_tm[4] = +sinyaw / (0.5 * dw) * ds * HEIGHT_CELLS;
		clip_tm[5] = +cosyaw * sin30 / (0.5 * dh) * ds * HEIGHT_CELLS;
		clip_tm[6] = 0;
		clip_tm[7] = 0;
		clip_tm[8] = 0;
		clip_tm[9] = -cos30 / HEIGHT_SCALE / (0.5 * dh) * ds * HEIGHT_CELLS;
		clip_tm[10] = -2. / 0xffff;
		clip_tm[11] = 0;
		clip_tm[12] = -(pos[0] * clip_tm[0] + pos[1] * clip_tm[4] + (2 * r.water - pos[2]) * clip_tm[8]);
		clip_tm[13] = -(pos[0] * clip_tm[1] + pos[1] * clip_tm[5] + (2 * r.water - pos[2]) * clip_tm[9]);
		clip_tm[14] = +1.0;
		clip_tm[15] = 1.0;

		TransposeProduct(clip_tm, clip_left, clip_world[0]);
		TransposeProduct(clip_tm, clip_right, clip_world[1]);
		TransposeProduct(clip_tm, clip_bottom, clip_world[2]);
		TransposeProduct(clip_tm, clip_top, clip_world[3]);
		TransposeProduct(clip_tm, clip_water, clip_world[4]);
	}

	global_refl_mode = true;
	QueryTerrain(t, planes, clip_world, view_flags, Renderer::RenderPatch, &r);
	QueryWorld(w, planes, clip_world, Renderer::RenderMesh, &r);
	global_refl_mode = false;

	Sample* src = r.sample_buffer.ptr + 2 + 2 * dw;
	for (int y = 0; y < height; y++)
	{
		for (int x = 0; x < width; x++, ptr++)
		{
			// given interpolated RGB -> round to 555, store it in visual
			// copy to diffuse to diffuse
			// mark mash 'auto-material' as 0x8 flag in spare

			// in post pass:
			// if sample has 0x8 flag
			//   multiply rgb by diffuse (into 888 bg=fg)
			// apply color mixing with neighbours
			// if at least 1 sample have mesh bit in spare
			// - round mixed bg rgb to R5G5B5 and use auto_material[32K] -> {bg,fg,gl}
			// else apply gridlines etc.


#ifdef DBL

// average 4 backgrounds
// mask 11 (something rendered)
			int spr[4] = { src[0].spare & 11, src[1].spare & 11, src[dw].spare & 11, src[dw + 1].spare & 11 };
			int mat[4] = { src[0].visual & 0x00FF , src[1].visual & 0x00FF, src[dw].visual & 0x00FF, src[dw + 1].visual & 0x00FF };
			int dif[4] = { src[0].diffuse , src[1].diffuse, src[dw].diffuse, src[dw + 1].diffuse };
			int vis[4] = { src[0].visual, src[1].visual, src[dw].visual, src[dw + 1].visual };

			// TODO:
			// every material must have 16x16 map and uses visual shade to select Y and lighting to select X
			// animated materials additionaly pre shifts and wraps visual shade by current time scaled by material's 'speed'

			int elv = 0; // (src[0].visual >> 15) & 0x0001;

			/*
			int shd = 0; // (src[0].visual >> 8) & 0x007F;

			int gl = matlib[mat[0]].shade[1][shd].gl;
			int bg[3] = { 0,0,0 };
			int fg[3] = { 0,0,0 };
			for (int i = 0; i < 4; i++)
			{
				bg[0] += matlib[mat[i]].shade[1][shd].bg[0] * dif[i];
				bg[1] += matlib[mat[i]].shade[1][shd].bg[1] * dif[i];
				bg[2] += matlib[mat[i]].shade[1][shd].bg[2] * dif[i];
				fg[0] += matlib[mat[i]].shade[1][shd].fg[0] * dif[i];
				fg[1] += matlib[mat[i]].shade[1][shd].fg[1] * dif[i];
				fg[2] += matlib[mat[i]].shade[1][shd].fg[2] * dif[i];
			}
			*/

			int shd = (dif[0] + dif[1] + dif[2] + dif[3] + 17 * 2) / (17 * 4); // 17: FF->F, 4: avr
			int gl = matlib[mat[0]].shade[1][shd].gl;

			int bg[3] = { 0,0,0 }; // 4
			int fg[3] = { 0,0,0 };

			int half_h[2][2] = { {0,1},{2,3} };
			int half_v[2][2] = { {0,2},{1,3} };
			int bg_h[2][3] = { { 0,0,0 },{ 0,0,0 } }; // 0+1 \ 2+3 
			int bg_v[2][3] = { { 0,0,0 },{ 0,0,0 } }; // 0+2 | 1+3

			bool use_auto_mat = false;

			int err_h = 0;
			int err_v = 0;

			// if cell contains both refl and non-refl terrain enable auto-mat
			bool has_refl = (spr[0] & 3) == 3 || (spr[1] & 3) == 3 || (spr[2] & 3) == 3 || (spr[3] & 3) == 3;
			bool has_norm = (spr[0] & 3) == 1 || (spr[1] & 3) == 1 || (spr[2] & 3) == 1 || (spr[3] & 3) == 1;
			if (has_refl && has_norm)
			{
				use_auto_mat = true;
			}

			for (int m = 0; m < 2; m++)
			{
				for (int i = 0; i < 4; i++)
				{
					//if (spr[i])
					{
						if (spr[i] & 0x8)
						{
							int r = ((vis[i] & 0x1F) * 527 + 23) >> 6;
							int g = (((vis[i] >> 5) & 0x1F) * 527 + 23) >> 6;
							int b = (((vis[i] >> 10) & 0x1F) * 527 + 23) >> 6;

							if ((spr[i] & 0x3) == 3)
							{
								r = r * dif[i] / 400;
								g = g * dif[i] / 400;
								b = b * dif[i] / 400;
							}
							else
							{
								r = r * dif[i] / 255;
								g = g * dif[i] / 255;
								b = b * dif[i] / 255;
							}

							if (i == 0 || i == 1)
							{
								if (m)
								{
									err_h += abs(bg_h[0][0] - 4 * r);
									err_h += abs(bg_h[0][1] - 4 * g);
									err_h += abs(bg_h[0][2] - 4 * b);
								}
								else
								{
									bg_h[0][0] += 2 * r;
									bg_h[0][1] += 2 * g;
									bg_h[0][2] += 2 * b;
								}
							}
							if (i == 2 || i == 3)
							{
								if (m)
								{
									err_h += abs(bg_h[1][0] - 4 * r);
									err_h += abs(bg_h[1][1] - 4 * g);
									err_h += abs(bg_h[1][2] - 4 * b);
								}
								else
								{
									bg_h[1][0] += 2 * r;
									bg_h[1][1] += 2 * g;
									bg_h[1][2] += 2 * b;
								}
							}

							if (i == 0 || i == 2)
							{
								if (m)
								{
									err_v += abs(bg_v[0][0] - 4 * r);
									err_v += abs(bg_v[0][1] - 4 * g);
									err_v += abs(bg_v[0][2] - 4 * b);
								}
								else
								{
									bg_v[0][0] += 2 * r;
									bg_v[0][1] += 2 * g;
									bg_v[0][2] += 2 * b;
								}
							}
							if (i == 1 || i == 3)
							{
								if (m)
								{
									err_v += abs(bg_v[1][0] - 4 * r);
									err_v += abs(bg_v[1][1] - 4 * g);
									err_v += abs(bg_v[1][2] - 4 * b);
								}
								else
								{
									bg_v[1][0] += 2 * r;
									bg_v[1][1] += 2 * g;
									bg_v[1][2] += 2 * b;
								}
							}

							if (!m)
							{
								bg[0] += r;
								bg[1] += g;
								bg[2] += b;
								use_auto_mat = true;
							}
						}
						else
						{
							int r = matlib[mat[i]].shade[1][shd].bg[0];
							int g = matlib[mat[i]].shade[1][shd].bg[1];
							int b = matlib[mat[i]].shade[1][shd].bg[2];

							if ((spr[i] & 0x3) == 3)
							{
								r = r * 255 / 400;
								g = g * 255 / 400;
								b = b * 255 / 400;
							}

							if (i == 0 || i == 1)
							{
								if (m)
								{
									err_h += abs(bg_h[0][0] - 4 * r);
									err_h += abs(bg_h[0][1] - 4 * g);
									err_h += abs(bg_h[0][2] - 4 * b);
								}
								else
								{
									bg_h[0][0] += 2 * r;
									bg_h[0][1] += 2 * g;
									bg_h[0][2] += 2 * b;
								}
							}
							if (i == 2 || i == 3)
							{
								if (m)
								{
									err_h += abs(bg_h[1][0] - 4 * r);
									err_h += abs(bg_h[1][1] - 4 * g);
									err_h += abs(bg_h[1][2] - 4 * b);
								}
								else
								{
									bg_h[1][0] += 2*r;
									bg_h[1][1] += 2*g;
									bg_h[1][2] += 2*b;
								}
							}

							if (i == 0 || i == 2)
							{
								if (m)
								{
									err_v += abs(bg_v[0][0] - 4 * r);
									err_v += abs(bg_v[0][1] - 4 * g);
									err_v += abs(bg_v[0][2] - 4 * b);
								}
								else
								{
									bg_v[0][0] += 2*r;
									bg_v[0][1] += 2*g;
									bg_v[0][2] += 2*b;
								}
							}
							if (i == 1 || i == 3)
							{
								if (m)
								{
									err_v += abs(bg_v[1][0] - 4 * r);
									err_v += abs(bg_v[1][1] - 4 * g);
									err_v += abs(bg_v[1][2] - 4 * b);
								}
								else
								{
									bg_v[1][0] += 2*r;
									bg_v[1][1] += 2*g;
									bg_v[1][2] += 2*b;
								}
							}

							if (!m)
							{
								bg[0] += r;
								bg[1] += g;
								bg[2] += b;

								if ((spr[i] & 0x3) == 0x3)
								{
									fg[0] += matlib[mat[i]].shade[1][shd].fg[0] * 255 / 400;
									fg[1] += matlib[mat[i]].shade[1][shd].fg[1] * 255 / 400;
									fg[2] += matlib[mat[i]].shade[1][shd].fg[2] * 255 / 400;
								}
								else
								{
									fg[0] += matlib[mat[i]].shade[1][shd].fg[0];
									fg[1] += matlib[mat[i]].shade[1][shd].fg[1];
									fg[2] += matlib[mat[i]].shade[1][shd].fg[2];
								}
							}
						}
					}
				}
			}

			if (use_auto_mat)
			{
				// WORKS REALY WELL! 
				bool vh_near = true;

				if (err_h * 1000 < err_v * 999)
				{
					vh_near = false;
					// _FG_
					//  BK
					ptr->gl = 0xDF;

					int auto_mat_lo = 3 * (bg_h[0][0] / 33 + 32 * (bg_h[0][1] / 33) + 32 * 32 * (bg_h[0][2] / 33));
					int auto_mat_hi = 3 * (bg_h[1][0] / 33 + 32 * (bg_h[1][1] / 33) + 32 * 32 * (bg_h[1][2] / 33));

					ptr->bk = auto_mat[auto_mat_lo + 0];
					ptr->fg = auto_mat[auto_mat_hi + 0];
				}
				else
				if (err_v * 1000 < err_h * 999)
				{
					vh_near = false;
					// B|F
					// K|G
					ptr->gl = 0xDE;

					int auto_mat_lt = 3 * (bg_v[0][0] / 33 + 32 * (bg_v[0][1] / 33) + 32 * 32 * (bg_v[0][2] / 33));
					int auto_mat_rt = 3 * (bg_v[1][0] / 33 + 32 * (bg_v[1][1] / 33) + 32 * 32 * (bg_v[1][2] / 33));

					ptr->bk = auto_mat[auto_mat_lt + 0];
					ptr->fg = auto_mat[auto_mat_rt + 0];
				}

				
				if (ptr->bk == ptr->fg || vh_near)
				{
					// avr4
					int auto_mat_idx = 3 * (bg[0] / 33 + 32 * (bg[1] / 33) + 32 * 32 * (bg[2] / 33));
					ptr->gl = auto_mat[auto_mat_idx + 2];
					ptr->bk = auto_mat[auto_mat_idx + 0];
					ptr->fg = auto_mat[auto_mat_idx + 1];
					ptr->spare = 0xFF;
				}
			}
			else
			{
				int bk_rgb[3] =
				{
					(bg[0] + 102) / 204,
					(bg[1] + 102) / 204,
					(bg[2] + 102) / 204
				};

				ptr->gl = gl;
				ptr->bk = 16 + bk_rgb[0] + bk_rgb[1] * 6 + bk_rgb[2] * 36;
				ptr->fg = 16 + (((fg[0] + 102) / 204) + (((fg[1] + 102) / 204) * 6) + (((fg[2] + 102) / 204) * 36));
				ptr->spare = 0xFF;

				// collect line bits
				int linecase = ((src[0].spare & 0x4) >> 2) | ((src[1].spare & 0x4) >> 1) | (src[dw].spare & 0x4) | ((src[dw + 1].spare & 0x4) << 1);

				static const int linecase_glyph[] = { 0, ',', ',', ',', '`', ';', ';', ';', '`', ';', ';', ';', '`', ';', ';', ';' };
				if (linecase)
					ptr->gl = linecase_glyph[linecase];

				// silhouette repetitoire:  _-/\| (should not be used by materials?)
				float z_hi = src[dw].height + src[dw + 1].height;
				float z_lo = src[0].height + src[1].height;
				float z_pr = src[-dw].height + src[1 - dw].height;

				float minus = z_lo - z_hi;
				float under = z_pr - z_lo;

				static const float thresh = 1 * HEIGHT_SCALE;

				if (minus > under)
				{
					if (minus > thresh)
					{
						ptr->gl = 0xC4; // '-'
						bk_rgb[0] = std::max(0, bk_rgb[0] - 1);
						bk_rgb[1] = std::max(0, bk_rgb[1] - 1);
						bk_rgb[2] = std::max(0, bk_rgb[2] - 1);
						ptr->fg = 16 + bk_rgb[0] + bk_rgb[1] * 6 + bk_rgb[2] * 36;
					}
				}
				else
				{
					if (under > thresh)
					{
						ptr->gl = 0x5F; // '_'
						bk_rgb[0] = std::max(0, bk_rgb[0] - 1);
						bk_rgb[1] = std::max(0, bk_rgb[1] - 1);
						bk_rgb[2] = std::max(0, bk_rgb[2] - 1);
						ptr->fg = 16 + bk_rgb[0] + bk_rgb[1] * 6 + bk_rgb[2] * 36;
					}
				}
			}

			src += 2;


			
			#else
			
			int mat = src[0].visual & 0x00FF;
			int shd = 0; // (src[0].visual >> 8) & 0x007F;
			int elv = 0; // (src[0].visual >> 15) & 0x0001;

			// fill from material
			const MatCell* cell = &(matlib[mat].shade[1][shd]);
			const uint8_t* bg = matlib[mat].shade[1][shd].bg;
			const uint8_t* fg = matlib[mat].shade[1][shd].fg;

			ptr->gl = cell->gl;
			ptr->bk = 16 + (((bg[0] + 25) / 51) + (((bg[1] + 25) / 51) * 6) + (((bg[2] + 25) / 51) * 36));
			ptr->fg = 16 + (((fg[0] + 25) / 51) + (((fg[1] + 25) / 51) * 6) + (((fg[2] + 25) / 51) * 36));
			ptr->spare = 0xFF;

			src++;
			#endif

		}

		#ifdef DBL
		src += 4 + dw;
		#else
		src += 2;
		#endif
	}

	// so blend sprites directly to ansi

	/*
	{
		double p[3] = { pos[0],pos[1],-1 };
		double v[3] = { 0,0,-1 };
		double r[4] = { 0,0,0,1 };
		Patch* patch = HitTerrain(t, p, v, r);

		if (patch)
		{
			player_pos[2] = r[2];
		}
	}
	*/

	int ang = (int)floor((player_dir-yaw) * player_sprite->angles / 360.0f + 0.5f);
	/*
	if (ang < 0)
		ang += player_sprite->angles * (1 - ang / player_sprite->angles);
	else
	if (ang >= player_sprite->angles)
		ang -= ang / player_sprite->angles;
	*/
	ang = ang >= 0 ? ang % player_sprite->angles : (ang % player_sprite->angles + player_sprite->angles) % player_sprite->angles;


	int anim = 1;
	int fr = player_stp/8 % player_sprite->anim[anim].length;

	if (player_stp < 0)
	{
		anim = 0;
		fr = 0;
	}

	static const float dy_dz = (cos(30 * M_PI / 180) * HEIGHT_CELLS * (ds / 2/*we're not dbl_wh*/)) / HEIGHT_SCALE;

	int player_pos[3] =
	{
		width / 2,
		height / 2,
		(int)floor(pos[2]+0.5) + HEIGHT_SCALE / 4
	};

	r.RenderSprite(out_ptr, width, height, player_sprite, false, anim, fr, ang, player_pos);

	// player_pos[1] = height / 2 + (int)floor((2 * r.water - pos[2]) * dy_dz + 0.5);
	player_pos[1] = height / 2 - (int)floor(2*(pos[2]-r.water)*dy_dz + 0.5);

	// player_pos[2] = (int)floor(2 * r.water - pos[2] + 0.5);
	player_pos[2] = (int)floor(2* r.water - pos[2] + 0.5) - HEIGHT_SCALE/4;

	r.RenderSprite(out_ptr, width, height, player_sprite, true, anim, fr, ang, player_pos);



	return true;
}