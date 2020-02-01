#include <malloc.h>
#include <string.h>
#include <assert.h>
#include "urdo.h"

#if 0
// todo: switch to file storage
// - each command record: {[cmd_id][DATA][rec_size]}
// - stream contains group open/close:  ....{open}{}{}{open}{}{}{}<-undo|redo->{}{}{close}{}{close}....
// - we just need to track current stack depth

struct URDO
{
	enum CMD
	{
		CMD_GROUP_OPEN,
		CMD_GROUP_CLOSE,
		CMD_PATCH_UPDATE,
		CMD_PATCH_DIAG
	} cmd;

	// followed by data and record size in bytes (int)

	void Do(bool un);
	static URDO* Alloc(CMD c);
	void Free();
};

// note: no need to have PurgeUndo()

static void PurgeRedo()
{
	// truncate file at current pos
	// write 'closes' for all open groups
}

void URDO_Purge()
{
	// reset stack & file size
	// move file pointer to 0 and flush
}

bool URDO_CanUndo()
{
	// true if we're not at the file head
}

bool URDO_CanRedo()
{
	// true if we're not at the file tail
}

size_t URDO_Bytes()
{
	// file size (keep track)
}

void URDO_Undo(int max_depth)
{
}

void URDO_Redo(int max_depth)
{
}

void URDO_Open()
{
	// write CMD
	// write sizeof(REC)
}

void URDO_Close()
{
	// write CMD
	// write sizeof(REC)
}

void URDO_Patch(Patch* p)
{
	// write CMD
	// write Patch*
	// write GetTerrainHeightMap(p)
	// write GetTerrainDiag(p)
	// write sizeof(REC)
}

void URDO_Diag(Patch* p)
{
	// write CMD
	// write Patch*
	// write GetTerrainDiag(p)
	// write sizeof(REC)
}

#endif

struct URDO
{
	URDO* next;
	URDO* prev;

	enum CMD
	{
		CMD_GROUP,
		CMD_PATCH_CREATE,
		CMD_PATCH_UPDATE_HEIGHT,
		CMD_PATCH_UPDATE_VISUAL,
		CMD_PATCH_DIAG,
		CMD_INST_CREATE,
	} cmd;

	void Do(bool un);
	static URDO* Alloc(CMD c);
	void Free();
};

struct URDO_Group : URDO
{
	URDO* group_head;
	URDO* group_tail;

	static void Open();
	static void Close();
	void Do(bool un);
};

struct URDO_PatchCreate : URDO
{
	int cx, cy;
	Terrain* terrain;
	Patch* patch;
	bool attached;

	static void Delete(Terrain* t, Patch* p);
	static Patch* Create(Terrain* t, int x, int y, int z);

	void Do(bool un);
};

struct URDO_PatchUpdateHeight : URDO
{
	Patch* patch;
	uint16_t height[HEIGHT_CELLS + 1][HEIGHT_CELLS + 1];
	uint16_t diag;

	static void Open(Patch* p); // alloc and copy original
	void Do(bool un);
};

struct URDO_PatchUpdateVisual : URDO
{
	Patch* patch;
	uint16_t visual[VISUAL_CELLS][VISUAL_CELLS];

	static void Open(Patch* p); // alloc and copy original
	void Do(bool un);
};

struct URDO_PatchDiag : URDO
{
	Patch* patch;
	uint16_t diag;

	static void Open(Patch* p);
	void Do(bool un);
};

struct URDO_InstCreate : URDO
{
	int mode;

	int flags;
	Inst* inst;

	union
	{
		struct
		{
			Mesh* mesh;
			double tm[16];
		};

		struct
		{
			World* w;
			Sprite* s;
			float pos[3];
			float yaw;
			int anim;
			int frame;
			int reps[4];
		};
	};

	static void Delete(Inst* i);
	static Inst* Create(Mesh* m, int flags, double tm[16]);
	static Inst* Create(World* w, Sprite* s, int flags, float pos[3], float yaw, int anim, int frame, int reps[4]);

	void Do(bool un);
};

static size_t bytes = 0;
static URDO* undo = 0;
static URDO* redo = 0;

static int group_open = 0;
static int stack_depth = 0;
static URDO_Group* stack[64];

void URDO::Do(bool un)
{
	switch (cmd)
	{
		case CMD_GROUP: ((URDO_Group*)this)->Do(un); break;
		case CMD_PATCH_CREATE: ((URDO_PatchCreate*)this)->Do(un); break;
		case CMD_PATCH_UPDATE_HEIGHT: ((URDO_PatchUpdateHeight*)this)->Do(un); break;
		case CMD_PATCH_UPDATE_VISUAL: ((URDO_PatchUpdateVisual*)this)->Do(un); break;
		case CMD_PATCH_DIAG: ((URDO_PatchDiag*)this)->Do(un); break;
		case CMD_INST_CREATE: ((URDO_InstCreate*)this)->Do(un); break;
		default:
			assert(0);
	}
}

void URDO::Free()
{
	switch (cmd)
	{
		case CMD_GROUP: 
		{
			URDO* u = ((URDO_Group*)this)->group_head;
			while (u)
			{
				URDO* n = u->next;
				u->Free();
				u = n;
			}

			bytes -= sizeof(URDO_Group);
			break;
		}
		case CMD_PATCH_CREATE:
		{
			URDO_PatchCreate* pc = (URDO_PatchCreate*)this;
			if (!pc->attached)
				bytes -= TerrainDispose(pc->patch);
			bytes -= sizeof(URDO_PatchCreate);
			break;
		}
		case CMD_PATCH_UPDATE_HEIGHT: 
			bytes -= sizeof(URDO_PatchUpdateHeight); 
			break;
		case CMD_PATCH_UPDATE_VISUAL: 
			bytes -= sizeof(URDO_PatchUpdateVisual); 
			break;
		case CMD_PATCH_DIAG:
			bytes -= sizeof(URDO_PatchDiag);
			break;

		case CMD_INST_CREATE:
			bytes -= sizeof(URDO_InstCreate);
			break;

		default:
			assert(0);
	}

	free(this);
}

URDO* URDO::Alloc(CMD c)
{
	size_t s = 0;
	switch (c)
	{
		case CMD_GROUP: s = sizeof(URDO_Group); break;
		case CMD_PATCH_CREATE: s = sizeof(URDO_PatchCreate); break;
		case CMD_PATCH_UPDATE_HEIGHT: s = sizeof(URDO_PatchUpdateHeight); break;
		case CMD_PATCH_UPDATE_VISUAL: s = sizeof(URDO_PatchUpdateVisual); break;
		case CMD_PATCH_DIAG: s = sizeof(URDO_PatchDiag); break;
		case CMD_INST_CREATE: s = sizeof(URDO_InstCreate); break;
		default:
			assert(0);
	}

	URDO* urdo = (URDO*)malloc(s);
	bytes += s;

	if (stack_depth)
	{
		URDO_Group* p = stack[stack_depth - 1];
		if (!p->group_head)
			p->group_head = urdo;
		p->group_tail = urdo;
	}

	urdo->prev = undo;
	urdo->next = 0;
	urdo->cmd = c;
	if (undo)
		undo->next = urdo;
	undo = urdo;

	return urdo;
}

static void PurgeUndo()
{
	while (1)
	{
		URDO* head = redo;
		if (head)
			head->prev = 0;

		while (undo)
		{
			URDO* prev = undo->prev;
			undo->Free();
			undo = prev;
		}

		if (stack_depth)
		{
			URDO_Group* g = stack[--stack_depth];
			g->group_head = head;
			if (!head)
			{
				undo = g->prev;
				redo = g->next;
				
				g->group_tail = 0;

				g->Free();
				if (redo)
					redo->prev = undo;
				if (undo)
					undo->next = redo;
			}
			else
			{
				redo = g;
				undo = g->prev;
			}
		}
		else
			break;
	}
}

static void PurgeRedo()
{
	while (1)
	{
		URDO* tail = undo;
		if (tail)
			tail->next = 0;

		while (redo)
		{
			URDO* next = redo->next;
			redo->Free();
			redo = next;
		}

		if (stack_depth)
		{
			URDO_Group* g = stack[--stack_depth];
			g->group_tail = tail;
			if (!tail)
			{
				redo = g->next;
				undo = g->prev;

				g->group_head = 0;

				g->Free();
				if (undo)
					undo->next = redo;
				if (redo)
					redo->prev = undo;
			}
			else
			{
				undo = g;
				redo = g->next;
			}
		}
		else
			break;
	}
}

void URDO_Purge()
{
	assert(!group_open);

	PurgeUndo();
	PurgeRedo();
}

bool URDO_CanUndo()
{
	if (group_open)
		return false;
	if (undo)
		return true;
	int d = stack_depth;
	while (d)
	{
		if (stack[--d]->prev)
			return true;
	}
	return false;
}

bool URDO_CanRedo()
{
	if (group_open)
		return false;
	if (redo)
		return true;
	int d = stack_depth;
	while (d)
	{
		if (stack[--d]->next)
			return true;
	}
	return false;
}

size_t URDO_Bytes()
{
	return bytes;
}

void URDO_Undo(int max_depth)
{
	assert(!group_open);

	while (!undo && stack_depth)
	{
		URDO_Group* g = stack[--stack_depth];
		undo = g->prev;
		redo = g;
	}

	while (undo && undo->cmd == URDO::CMD_GROUP && stack_depth < max_depth)
	{
		URDO_Group* g = (URDO_Group*)undo;
		stack[stack_depth++] = g;
		undo = g->group_tail;
		redo = 0;
	}

	if (stack_depth <= max_depth && undo)
	{
		undo->Do(true);
		redo = undo;
		undo = undo->prev;
		return;
	}

	while (stack_depth > max_depth)
	{
		while (undo)
		{
			undo->Do(true);
			redo = undo;
			undo = undo->prev;
		}

		URDO_Group* g = stack[--stack_depth];
		undo = g->prev;
		redo = g;
	}
}

void URDO_Redo(int max_depth)
{
	assert(!group_open);

	while (!redo && stack_depth)
	{
		URDO_Group* g = stack[--stack_depth];
		redo = g->next;
		undo = g;
	}

	while (redo && redo->cmd == URDO::CMD_GROUP && stack_depth < max_depth)
	{
		URDO_Group* g = (URDO_Group*)redo;
		stack[stack_depth++] = g;
		redo = g->group_head;
		undo = 0;
	}

	if (stack_depth <= max_depth && redo)
	{
		redo->Do(false);
		undo = redo;
		redo = redo->next;
		return;
	}

	while (stack_depth > max_depth)
	{
		while (redo)
		{
			redo->Do(true);
			undo = redo;
			redo = redo->next;
		}

		URDO_Group* g = stack[--stack_depth];
		redo = g->next;
		undo = g;
	}
}

void URDO_Open()
{
	assert(group_open<64);
	URDO_Group::Open();
}

void URDO_Close()
{
	assert(group_open>0);
	URDO_Group::Close();
}

Inst* URDO_Create(Mesh* m, int flags, double tm[16])
{
	assert(group_open < 64);

	return URDO_InstCreate::Create(m,flags,tm);
}

Inst* URDO_Create(World* w, Sprite* s, int flags, float pos[3], float yaw, int anim, int frame, int reps[4])
{
	assert(group_open < 64);

	return URDO_InstCreate::Create(w,s,flags,pos,yaw,anim,frame,reps);
}


void URDO_Delete(Inst* i)
{
	assert(group_open < 64);
	URDO_InstCreate::Delete(i);
}

Patch* URDO_Create(Terrain* t, int x, int y, int z)
{
	assert(group_open < 64);
	return URDO_PatchCreate::Create(t,x,y,z);
}

void URDO_Delete(Terrain* t, Patch* p)
{
	assert(group_open < 64);
	URDO_PatchCreate::Delete(t,p);
}

void URDO_Patch(Patch* p, bool visual)
{
	if (visual)
		URDO_PatchUpdateVisual::Open(p);
	else
		URDO_PatchUpdateHeight::Open(p);
}

void URDO_Diag(Patch* p)
{
	URDO_PatchDiag::Open(p);
}

void URDO_Group::Open()
{
	if (!group_open)
		PurgeRedo();
	group_open++;

	URDO_Group* g = (URDO_Group*)Alloc(CMD_GROUP);

	stack[stack_depth++] = g;

	g->group_head = 0;
	g->group_tail = 0;

	undo = 0;
	redo = 0;
}

void URDO_Group::Close()
{
	group_open--;

	// here we swap global with group lists
	URDO_Group* g = stack[--stack_depth];

	if (!g->group_head)
	{
		// delete empty group
		undo = g->prev;
		if (g->prev)
			g->prev->next = 0;
		g->Free();
		return;
	}

	g->group_tail = undo;
	undo = g;
}

void URDO_Group::Do(bool un)
{
	if (un)
	{
		URDO* urdo = group_tail;
		while (urdo)
		{
			urdo->Do(true);
			urdo = urdo->prev;
		}
	}
	else
	{
		URDO* urdo = group_head;
		while (urdo)
		{
			urdo->Do(false);
			urdo = urdo->next;
		}
	}
}

void URDO_PatchUpdateHeight::Open(Patch* p)
{
	if (!group_open)
		PurgeRedo();

	URDO_PatchUpdateHeight* urdo = (URDO_PatchUpdateHeight*)Alloc(CMD_PATCH_UPDATE_HEIGHT);

	urdo->patch = p;
	memcpy(urdo->height, GetTerrainHeightMap(p), sizeof(uint16_t)*(HEIGHT_CELLS+1)*(HEIGHT_CELLS+1));
	urdo->diag = GetTerrainDiag(p);
}

void URDO_PatchUpdateVisual::Open(Patch* p)
{
	if (!group_open)
		PurgeRedo();

	URDO_PatchUpdateVisual* urdo = (URDO_PatchUpdateVisual*)Alloc(CMD_PATCH_UPDATE_VISUAL);

	urdo->patch = p;
	memcpy(urdo->visual, GetTerrainVisualMap(p), sizeof(uint16_t)*VISUAL_CELLS*VISUAL_CELLS);
}

void URDO_PatchUpdateHeight::Do(bool un)
{
	uint16_t* t = GetTerrainHeightMap(patch);
	uint16_t* u = (uint16_t*)height;
	for (int i = 0; i < (HEIGHT_CELLS + 1)*(HEIGHT_CELLS + 1); i++)
	{
		uint16_t s = t[i];
		t[i] = u[i];
		u[i] = s;
	}

	uint16_t d = diag;
	diag = GetTerrainDiag(patch);

	UpdateTerrainHeightMap(patch);
	SetTerrainDiag(patch,d);
}

void URDO_PatchUpdateVisual::Do(bool un)
{
	uint16_t* t = GetTerrainVisualMap(patch);
	uint16_t* u = (uint16_t*)visual;
	for (int i = 0; i < VISUAL_CELLS*VISUAL_CELLS; i++)
	{
		uint16_t s = t[i];
		t[i] = u[i];
		u[i] = s;
	}

	UpdateTerrainVisualMap(patch);
}


void URDO_PatchDiag::Open(Patch* p)
{
	if (!group_open)
		PurgeRedo();

	URDO_PatchDiag* urdo = (URDO_PatchDiag*)Alloc(CMD_PATCH_DIAG);

	urdo->patch = p;
	urdo->diag = GetTerrainDiag(p);
}

void URDO_PatchDiag::Do(bool un)
{
	uint16_t d = diag;
	diag = GetTerrainDiag(patch);
	SetTerrainDiag(patch, d);
}

void URDO_PatchCreate::Delete(Terrain* t, Patch* p)
{
	if (!group_open)
		PurgeRedo();

	URDO_PatchCreate* urdo = (URDO_PatchCreate*)Alloc(CMD_PATCH_CREATE);

	urdo->terrain = t;
	urdo->patch = p;

	bytes += TerrainDetach(t,p,&urdo->cx, &urdo->cy);
	urdo->attached = false;
}

Patch* URDO_PatchCreate::Create(Terrain* t, int x, int y, int z)
{
	if (!group_open)
		PurgeRedo();

	URDO_PatchCreate* urdo = (URDO_PatchCreate*)Alloc(CMD_PATCH_CREATE);

	urdo->terrain = t;
	urdo->patch = AddTerrainPatch(t,x,y,z);
	urdo->attached = true;
	urdo->cx = x;
	urdo->cy = y;

	return urdo->patch;
}

void URDO_PatchCreate::Do(bool un)
{
	if (attached)
	{
		bytes += TerrainDetach(terrain, patch, &cx, &cy);
		attached = false;
	}
	else
	{
		bytes -= TerrainAttach(terrain, patch, cx, cy);
		attached = true;
	}
}

void URDO_InstCreate::Delete(Inst* i)
{
	if (!group_open)
		PurgeRedo();

	URDO_InstCreate* urdo = (URDO_InstCreate*)Alloc(CMD_INST_CREATE);

	urdo->mesh = GetInstMesh(i);
	urdo->flags = GetInstFlags(i);
	GetInstTM(i, urdo->tm);

	DeleteInst(i);
	urdo->inst = 0;
}

Inst* URDO_InstCreate::Create(Mesh* m, int flags, double tm[16])
{
	if (!group_open)
		PurgeRedo();

	URDO_InstCreate* urdo = (URDO_InstCreate*)Alloc(CMD_INST_CREATE);

	urdo->mode = 0; // mesh
	urdo->mesh = 0;
	urdo->flags = 0;
	memset(urdo->tm, 0, sizeof(double[16]));
	urdo->inst = CreateInst(m,flags,tm,0);

	return urdo->inst;
}

Inst* URDO_InstCreate::Create(World* w, Sprite* s, int flags, float pos[3], float yaw, int anim, int frame, int reps[4])
{
	if (!group_open)
		PurgeRedo();

	URDO_InstCreate* urdo = (URDO_InstCreate*)Alloc(CMD_INST_CREATE);

	urdo->mode = 1; // sprite
	urdo->w = w;
	urdo->flags = 0;
	urdo->inst = CreateInst(w,s,flags,pos,yaw,anim,frame,reps,0);

	return urdo->inst;
}


void URDO_InstCreate::Do(bool un)
{
	if (mode == 0)
	{
		if (!inst)
		{
			inst = CreateInst(mesh, flags, tm);
		}
		else
		{
			mesh = GetInstMesh(inst);
			flags = GetInstFlags(inst);
			GetInstTM(inst, tm);
			DeleteInst(inst);
			inst = 0;
		}

		RebuildWorld(GetMeshWorld(mesh));
	}
	else
	{
		if (!inst)
		{
			inst = CreateInst(w,s,flags, pos,yaw,anim,frame,reps,0);
		}
		else
		{
			s = GetInstSprite(inst, pos,&yaw,&anim,&frame,reps);
			flags = GetInstFlags(inst);
			DeleteInst(inst);
			inst = 0;
		}

		RebuildWorld(w);
	}
}