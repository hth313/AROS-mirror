/*
    (C) 1995-98 AROS - The Amiga Research OS
    $Id$

    Desc: Driver for using gfxhidd for gfx output
    Lang: english
*/

#define AROS_ALMOST_COMPATIBLE 1

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <clib/macros.h>

#include <proto/exec.h>
#include <graphics/rastport.h>
#include <graphics/gfxbase.h>
#include <graphics/text.h>
#include <graphics/view.h>
#include <graphics/layers.h>
#include <graphics/clip.h>
#include <graphics/gfxmacros.h>
#include <graphics/regions.h>

#include <proto/graphics.h>
#include <proto/arossupport.h>
#include <proto/utility.h>
#include <proto/oop.h>
#include <oop/oop.h>
#include <utility/tagitem.h>
#include <aros/asmcall.h>

#include <intuition/intuition.h>

#include <hidd/graphics.h>

#include "graphics_intern.h"
#include "graphics_internal.h"
#include "intregions.h"

#define SDEBUG 0
#define DEBUG 0
#include <aros/debug.h>

/* Default font for the HIDD driver */
#include "default_font.c"

#define PEN_BITS    8
#define NUM_COLORS  (1L << PEN_BITS)
#define PEN_MASK    (NUM_COLORS - 1)



/* Storage for bitmap object */
#define BM_OBJ(bitmap)	  ((Object *)(bitmap)->Planes[0])

#define BM_PIXTAB(bitmap) ((HIDDT_Pixel *)(bitmap)->Planes[2])


/* Rastport flag that tells whether or not the driver has been inited */

#define RPF_DRIVER_INITED (1L << 15)

#define AROS_PALETTE_SIZE 256
#define AROS_PALETTE_MEMSIZE (sizeof (HIDDT_Pixel) * AROS_PALETTE_SIZE)

static VOID setbitmapfast(struct BitMap *bm, LONG x_start, LONG y_start, LONG xsize, LONG ysize, ULONG pen);
static Object *fontbm_to_hiddbm(struct TextFont *font, struct GfxBase *GfxBase);
static LONG fillrect_pendrmd(struct RastPort *tp
	, LONG x1, LONG y1
	, LONG x2, LONG y2
	, HIDDT_Pixel pix
	, ULONG drmd
	, struct GfxBase *GfxBase);

static AttrBase HiddBitMapAttrBase	= 0;
static AttrBase HiddGCAttrBase		= 0;
static AttrBase HiddGfxModeAttrBase	= 0;
static AttrBase HiddPixFmtAttrBase	= 0; 

static struct ABDescr attrbases[] = {
    { IID_Hidd_BitMap,	&HiddBitMapAttrBase	},
    { IID_Hidd_GC,	&HiddGCAttrBase		},
    { IID_Hidd_GfxMode,	&HiddGfxModeAttrBase	},
    { IID_Hidd_PixFmt,	&HiddPixFmtAttrBase	},
    { NULL, 0UL }
};

#define PIXELBUF_SIZE 200000
#define NUMPIX (PIXELBUF_SIZE / 4)

#define NUMLUTPIX (PIXELBUF_SIZE)

/* This buffer is used for planar-to-chunky-converion */
static ULONG *pixel_buf;
static struct SignalSemaphore pixbuf_sema;

#define LOCK_PIXBUF ObtainSemaphore(&pixbuf_sema);
#define ULOCK_PIXBUF ReleaseSemaphore(&pixbuf_sema);


struct ETextFont
{
    struct TextFont	etf_Font;
};

struct gfx_driverdata * GetDriverData (struct RastPort * rp)
{
    return (struct gfx_driverdata *) rp->longreserved[0];
}


/* SetDriverData() should only be used when cloning RastPorts           */
/* For other purposes just change the values directly in the struct.	*/

void SetDriverData (struct RastPort * rp, struct gfx_driverdata * DriverData)
{
    rp->longreserved[0] = (IPTR) DriverData;
}


/* InitDriverData() just allocates memory for the struct. To use e.g.   */
/* AreaPtrns, UpdateAreaPtrn() has to allocate the memory for the       */
/* Pattern itself (and free previously used memory!)                    */

struct gfx_driverdata * InitDriverData (struct RastPort * rp, struct GfxBase * GfxBase)
{
    struct gfx_driverdata * dd;
    EnterFunc(bug("InitDriverData(rp=%p)\n", rp));

    /* Does this rastport have a bitmap ? */
    if (rp->BitMap)
    {
        D(bug("Got bitmap\n"));
        /* Displayable ? (== rastport of a screen) */
	if (rp->BitMap->Flags & BMF_AROS_HIDD)
	{
            D(bug("Has HIDD bitmap (displayable)\n"));

	    /* We can use this rastport. Allocate driverdata */
    	    dd = AllocMem (sizeof(struct gfx_driverdata), MEMF_CLEAR);
    	    if (dd)
    	    {
	        struct shared_driverdata *sdd;
		struct TagItem gc_tags[] = {
		    { aHidd_GC_BitMap, 	0UL},
		    { TAG_DONE, 	0UL} 
		};
		
		
		D(bug("Got driverdata\n"));
		sdd = SDD(GfxBase);
		
		/* Create a GC for it */
		gc_tags[0].ti_Data = (IPTR)BM_OBJ(rp->BitMap);
		
		dd->dd_GC = HIDD_Gfx_NewGC(sdd->gfxhidd, gc_tags);
		if (dd->dd_GC)
		{
	
		    D(bug("Got GC HIDD object\n"));
    		    dd->dd_RastPort = rp;
    		    SetDriverData(rp, dd);
    		    rp->Flags |= RPF_DRIVER_INITED;

		    ReturnPtr("InitDriverData", struct gfx_driverdata *, dd);
	        }
		
		FreeMem(dd, sizeof (struct gfx_driverdata));
	
    	    }
	}
    }

    ReturnPtr("InitDriverData", struct gfx_driverdata *, NULL);
}

void DeinitDriverData (struct RastPort * rp, struct GfxBase * GfxBase)
{
    struct gfx_driverdata * dd;
    struct shared_driverdata *sdd;
    
    EnterFunc(bug("DeInitDriverData(rp=%p)\n", rp));
		
    sdd = SDD(GfxBase);

    dd = (struct gfx_driverdata *) rp->longreserved[0];
    rp->longreserved[0] = 0;
    
    HIDD_Gfx_DisposeGC(sdd->gfxhidd, dd->dd_GC);

    FreeMem (dd, sizeof(struct gfx_driverdata));
    rp->Flags &= ~RPF_DRIVER_INITED;
    
    ReturnVoid("DeinitDriverData");
}

BOOL CorrectDriverData (struct RastPort * rp, struct GfxBase * GfxBase)
{
    BOOL retval = TRUE;
    struct gfx_driverdata * dd, * old;
    

    
    if (!rp)
    {
	retval = FALSE;
    }
    else
    {
	old = GetDriverData(rp);
	if (!old)
	{
	    old = InitDriverData(rp, GfxBase);

/* stegerg: ???? would have returned TRUE even if old == NULL
	    if (old)
	    	retval = TRUE;
*/
	    if (!old) retval = FALSE;
	}
	else if (rp != old->dd_RastPort)
	{
	    /* stegerg: cloned rastport?	    
	    ** Make sure to clear driverdata pointer and flag
	    ** in case InitDriverData fail
	    */
	    rp->longreserved[0] = 0;
	    rp->Flags &= ~RPF_DRIVER_INITED;
	    
	    dd = InitDriverData(rp, GfxBase);

/* stegerg: ???? would have returned TRUE even if dd = NULL
	    if (dd)
	   	 retval = TRUE;
*/

	    if (!dd) retval = FALSE;

	}
    }
    
    return retval;
}

BOOL init_romfonts(struct GfxBase *GfxBase)
{
    struct TextFont *tf;
    
    
    tf = AllocMem( sizeof (struct TextFont), MEMF_ANY);
    if (tf)
    {
    	/* Copy the const font struct into allocated mem */
	CopyMem((APTR)&topaz8_tf, tf, sizeof (struct TextFont));
	
	AddFont(tf);
	GfxBase->DefaultFont = tf;
	
	return TRUE;
    }
    return FALSE;
}

int driver_init (struct GfxBase * GfxBase)
{

    EnterFunc(bug("driver_init()\n"));
    
    /* Initialize the semaphore used for the chunky buffer */
    InitSemaphore(&pixbuf_sema);
    
    
    /* Allocate memory for driver data */
    SDD(GfxBase) = (struct shared_driverdata *)AllocMem(sizeof (struct shared_driverdata), MEMF_ANY|MEMF_CLEAR);
    if ( SDD(GfxBase) )
    {
        /* Open the OOP library */
	SDD(GfxBase)->oopbase = OpenLibrary(AROSOOP_NAME, 0);
	if ( SDD(GfxBase)->oopbase )
	{
	    /* Init the needed attrbases */
	    
	    if (ObtainAttrBases(attrbases))
	    {
		/* Init the driver's defaultfont */
		if (init_romfonts(GfxBase))
		{
		    pixel_buf=AllocMem(PIXELBUF_SIZE,MEMF_ANY);
		    if (pixel_buf)
			ReturnInt("driver_init", int, TRUE);
		}	
		ReleaseAttrBases(attrbases);
	    }
	    CloseLibrary( SDD(GfxBase)->oopbase );
	}
	FreeMem( SDD(GfxBase), sizeof (struct shared_driverdata));
	
    }
    ReturnInt("driver_init", int, FALSE);
}

int driver_open (struct GfxBase * GfxBase)
{
    return TRUE;
}

void driver_close (struct GfxBase * GfxBase)
{
    return;
}

void driver_expunge (struct GfxBase * GfxBase)
{

    ReleaseAttrBases(attrbases);
    
    if ( SDD(GfxBase) )
    {
	    
	/* Try to free some other stuff */
	if ( SDD(GfxBase)->planarbm_cache )
	    delete_object_cache( SDD(GfxBase)->planarbm_cache, GfxBase );
	    
	if ( SDD(GfxBase)->gc_cache )
	    delete_object_cache( SDD(GfxBase)->gc_cache, GfxBase );
	    
	if ( SDD(GfxBase)->gfxhidd )
	    DisposeObject( SDD(GfxBase)->gfxhidd );
	    
        if ( SDD(GfxBase)->oopbase )
	     CloseLibrary( SDD(GfxBase)->oopbase );
	     
	     
	FreeMem( SDD(GfxBase), sizeof (struct shared_driverdata) );
    }
    return;
}

/* Called after DOS is up & running */
BOOL driver_LateGfxInit (APTR data, struct GfxBase *GfxBase)
{

    /* Supplied data is really the librarybase of a HIDD */
    STRPTR gfxhiddname = (STRPTR)data;
    struct TagItem tags[] = { {TAG_DONE, 0UL} };    
    
    EnterFunc(bug("driver_LateGfxInit(gfxhiddname=%s)\n", gfxhiddname));
    
    /* Create a new GfxHidd object */
	
    SDD(GfxBase)->gfxhidd = NewObject(NULL, gfxhiddname, tags);
    D(bug("driver_LateGfxInit: gfxhidd=%p\n", SDD(GfxBase)->gfxhidd));
	
    if (NULL != SDD(GfxBase)->gfxhidd) {
    
    	struct TagItem gc_create_tags[] = { { TAG_DONE, 0UL } };
	
	/* Create a GC object cache */
	SDD(GfxBase)->gc_cache = create_object_cache(NULL, IID_Hidd_GC, gc_create_tags, GfxBase);
	if (NULL != SDD(GfxBase)->gc_cache) {

#warning Update tags and class ID when we have the AmiBM class finished	
	    struct TagItem bm_create_tags[] = { { TAG_DONE, 0UL } };
	    
	    SDD(GfxBase)->planarbm_cache
	    	= create_object_cache(NULL, CLID_Hidd_PlanarBM, bm_create_tags, GfxBase);
		
	    if (NULL != SDD(GfxBase)->planarbm_cache) {

	    	ReturnBool("driver_LateGfxInit", TRUE);
	    }
	    delete_object_cache(SDD(GfxBase)->gc_cache, GfxBase);
	}
	DisposeObject(SDD(GfxBase)->gfxhidd);
	SDD(GfxBase)->gfxhidd = NULL;
	    
    }
	
    
    ReturnBool("driver_LateGfxInit", FALSE);

}


static VOID clipagainstbitmap(struct BitMap *bm, LONG *x1, LONG *y1, LONG *x2, LONG *y2, struct GfxBase *GfxBase)
{
    ULONG width  = GetBitMapAttr(bm, BMA_WIDTH);
    ULONG height = GetBitMapAttr(bm, BMA_HEIGHT);
    
    /* Clip against bitmap bounds  */
	    
    if (*x1 < 0)  *x1 = 0;
    if (*y1 < 0)  *y1 = 0;

    if (*x2 >= width)  *x2 = width  - 1;
    if (*y2 >= height) *y2 = height - 1; 
    
    return;
}

void driver_SetABPenDrMd (struct RastPort * rp, ULONG apen, ULONG bpen,
	ULONG drmd, struct GfxBase * GfxBase)
{
    struct gfx_driverdata *dd;
    if (!CorrectDriverData (rp, GfxBase))
    	return;
	
	
    dd = GetDriverData(rp);
    if (dd)
    {
    	struct TagItem gc_tags[] = {
    		{ aHidd_GC_Foreground,	0},
    		{ aHidd_GC_Background,	0},
		{ aHidd_GC_ColorExpansionMode, 0UL},
		{ aHidd_GC_DrawMode, vHidd_GC_DrawMode_Copy},
		{ TAG_DONE,	0}
    	};
#warning Handle offscreen bitmaps

	gc_tags[0].ti_Data = BM_PIXTAB(rp->BitMap)[apen & PEN_MASK];
	gc_tags[1].ti_Data = BM_PIXTAB(rp->BitMap)[bpen & PEN_MASK];
	
	if (drmd & JAM2)
	{
	    gc_tags[2].ti_Data = vHidd_GC_ColExp_Opaque;
	}	
	else if (drmd & COMPLEMENT)
	{
	    gc_tags[3].ti_Data = vHidd_GC_DrawMode_Invert;
	}
	else if ((drmd & (~INVERSVID)) == JAM1)
	{
	    gc_tags[2].ti_Data = vHidd_GC_ColExp_Transparent;
	}
	
    	SetAttrs(dd->dd_GC, gc_tags);
	
    }
    return;
	
    
}

void driver_SetAPen (struct RastPort * rp, ULONG pen,
		    struct GfxBase * GfxBase)
{
    struct gfx_driverdata *dd;

/*    EnterFunc(bug("driver_SetAPen(rp=%p, pen=%d)\n", rp, pen));
*/    if (!CorrectDriverData (rp, GfxBase))
    	return;

    dd = GetDriverData(rp);
    if (dd)
    {
        struct TagItem col_tags[]= {
		{ aHidd_GC_Foreground, 0},
		{ TAG_DONE,	0UL}
	};
	
#warning Handle offscreen bitmaps
	col_tags[0].ti_Data = BM_PIXTAB(rp->BitMap)[pen & PEN_MASK];

	SetAttrs( dd->dd_GC, col_tags );
	
    }
    return;
/*    ReturnVoid("driver_SetAPen");
*/
}

void driver_SetBPen (struct RastPort * rp, ULONG pen,
		    struct GfxBase * GfxBase)
{
    struct gfx_driverdata *dd;
    
    if (CorrectDriverData (rp, GfxBase))
    {
    	
        struct TagItem col_tags[]= {
		{ aHidd_GC_Background, 0},
		{ TAG_DONE,	0UL}
	};

#warning Handle offscreen bitmaps
	col_tags[0].ti_Data = BM_PIXTAB(rp->BitMap)[pen & PEN_MASK];
	
	dd = GetDriverData(rp);
	if (dd)
	{
	    SetAttrs( dd->dd_GC, col_tags );
	}
    }
}

void driver_SetOutlinePen (struct RastPort * rp, ULONG pen,
		    struct GfxBase * GfxBase)
{
}

void driver_SetDrMd (struct RastPort * rp, ULONG mode,
		    struct GfxBase * GfxBase)
{
    struct TagItem drmd_tags[] =
    {
	{ aHidd_GC_ColorExpansionMode, 0UL },
	{ aHidd_GC_DrawMode,	vHidd_GC_DrawMode_Copy },
	{ TAG_DONE, 0UL }
    };
    struct gfx_driverdata *dd;
    
    if (!CorrectDriverData (rp, GfxBase))
    	return;
	
	
    if (mode & JAM2)
    {
    	drmd_tags[0].ti_Data = vHidd_GC_ColExp_Opaque;
    }	
    else if (mode & COMPLEMENT)
    {
	drmd_tags[1].ti_Data = vHidd_GC_DrawMode_Invert;
    }
    else if ((mode & (~INVERSVID)) == JAM1)
    {
    	drmd_tags[0].ti_Data = vHidd_GC_ColExp_Transparent;
    }

#warning Handle INVERSVID by swapping apen and bpen ?

    dd = GetDriverData(rp);
    if (dd)
    {
    	SetAttrs(dd->dd_GC, drmd_tags);
    }
    return;
    	
}



static VOID setbitmappixel(struct BitMap *bm
	, LONG x, LONG y
	, ULONG pen
	, UBYTE depth
	, UBYTE plane_mask)
{
    UBYTE i;
    ULONG idx;
    UBYTE mask, clr_mask;
    ULONG penmask;

    idx = COORD_TO_BYTEIDX(x, y, bm->BytesPerRow);

    mask = XCOORD_TO_MASK( x );
    clr_mask = ~mask;
    
    penmask = 1;
    for (i = 0; i < depth; i ++)
    {

	if ((1L << i) & plane_mask)
	{
            UBYTE *plane = bm->Planes[i];
	
	    if ((plane != NULL) && (plane != (PLANEPTR)-1))
	    {
		if ((penmask & pen) != 0)
		    plane[idx] |=  mask;
		else
		    plane[idx] &=  clr_mask;
            }

	}
	penmask <<= 1;
	
    }
    return;
}

static ULONG getbitmappixel(struct BitMap *bm
	, LONG x
	, LONG y
	, UBYTE depth
	, UBYTE plane_mask)
{
    UBYTE i;
    ULONG idx;

    ULONG mask;
    ULONG pen = 0L;
    
    idx = COORD_TO_BYTEIDX(x, y, bm->BytesPerRow);
    mask = XCOORD_TO_MASK( x );
    
    for (i = depth - 1; depth; i -- , depth -- )
    {
        pen <<= 1; /* stegerg: moved to here, was inside if!? */

        if ((1L << i) & plane_mask)
	{
	    UBYTE *plane = bm->Planes[i];
	
	    if (plane == (PLANEPTR)-1)
	    {
	        pen |= 1;
	    }
	    else if (plane != NULL)
	    {
		if ((plane[idx] & mask) != 0)
		    pen |= 1;
	    }
	}

    }
    return pen;
}


enum { SB_SINGLEMASK, SB_PREPOSTMASK, SB_FULL };
static VOID setbitmapfast(struct BitMap *bm, LONG x_start, LONG y_start, LONG xsize, LONG ysize, ULONG pen)
{
    LONG num_whole;
    UBYTE sb_type;
    
    UBYTE plane;
    UBYTE pre_pixels_to_set,
    	  post_pixels_to_set,
	  pre_and_post; /* number pixels to clear in pre and post byte */
 
    UBYTE prebyte_mask, postbyte_mask;
    
/*    kprintf("x_start: %d, y_start: %d, xsize: %d, ysize: %d, pen: %d\n",
    	x_start, y_start, xsize, ysize, pen);
*/	

    pre_pixels_to_set  = (7 - (x_start & 0x07)) + 1;
    post_pixels_to_set = ((x_start + xsize - 1) & 0x07) + 1;
    

    pre_and_post = pre_pixels_to_set + post_pixels_to_set;
    
    if (pre_and_post > xsize)
    {
	UBYTE start_bit, stop_bit;
	/* Check whether the pixels are kept within a byte */
	sb_type = SB_SINGLEMASK;
    	pre_pixels_to_set  = MIN(pre_pixels_to_set,  xsize);
	
	/* Mask out the needed bits */
	start_bit =  7 - (x_start & 0x07) + 1;
	stop_bit = 7 - ((x_start + xsize - 1) & 0x07);

/* kprintf("start_bit: %d, stop_bit: %d\n", start_bit, stop_bit);
*/	
	prebyte_mask = ((1L << start_bit) - 1) - ((1L << stop_bit) - 1) ;
/* kprintf("prebyte_mask: %d\n", prebyte_mask);

kprintf("SB_SINGLE\n");
*/
    }
    else if (pre_and_post == xsize)
    {
    	/* We have bytes within to neighbour pixels */
	sb_type = SB_PREPOSTMASK;
	prebyte_mask  = 0xFF >> (8 - pre_pixels_to_set);
	postbyte_mask = 0xFF << (8 - post_pixels_to_set);
    
/* kprintf("SB_PREPOSTMASK\n");
*/
    }
    else
    {

	/* Say we want to clear two pixels in last byte. We want the mask
	MSB 00000011 LSB
	*/
	sb_type = SB_FULL;
	prebyte_mask = 0xFF >> (8 - pre_pixels_to_set);
    
	/* Say we want to set two pixels in last byte. We want the mask
	MSB 11000000 LSB
	*/
	postbyte_mask = 0xFF << (8 - post_pixels_to_set);
	
        	/* We have at least one whole byte of pixels */
	num_whole = xsize - pre_pixels_to_set - post_pixels_to_set;
	num_whole >>= 3; /* number of bytes */
	
/* kprintf("SB_FULL\n");
*/
    }
	
/*
kprintf("pre_pixels_to_set: %d, post_pixels_to_set: %d, numwhole: %d\n"
	, pre_pixels_to_set, post_pixels_to_set, num_whole);
    
kprintf("prebyte_mask: %d, postbyte_mask: %d, numwhole: %d\n", prebyte_mask, postbyte_mask, num_whole);
*/    
    for (plane = 0; plane < GetBitMapAttr(bm, BMA_DEPTH); plane ++)
    {
    
        LONG y;
	UBYTE pixvals;
	UBYTE prepixels_set, prepixels_clr;
	UBYTE postpixels_set, postpixels_clr;
    	UBYTE *curbyte = ((UBYTE *)bm->Planes[plane]) + COORD_TO_BYTEIDX(x_start, y_start, bm->BytesPerRow);
	
	
	/* Set or clear current bit of pixval ? */
	if (pen & (1L << plane))
	    pixvals = 0xFF;
	else
	    pixvals = 0x00;
	
	/* Set the pre and postpixels */
	switch (sb_type)
	{
	    case SB_FULL:
		prepixels_set  = (pixvals & prebyte_mask);
		postpixels_set = (pixvals & postbyte_mask);
	

		prepixels_clr  = (pixvals & prebyte_mask)  | (~prebyte_mask);
		postpixels_clr = (pixvals & postbyte_mask) | (~postbyte_mask);

		for (y = 0; y < ysize; y ++)
		{
		    LONG x;
		    UBYTE *ptr = curbyte;
	    
		    *ptr |= prepixels_set;
		    *ptr ++ &= prepixels_clr;
	    
		    for (x = 0; x < num_whole; x ++)
		    {
			*ptr ++ = pixvals;
		    }
		    /* Clear the last nonwhole byte */
		    *ptr |= postpixels_set;
		    *ptr ++ &= postpixels_clr;
	    
		    /* Go to next line */
		    curbyte += bm->BytesPerRow;
		}
		break;
		
	    case SB_PREPOSTMASK:
	
		prepixels_set  = (pixvals & prebyte_mask);
		postpixels_set = (pixvals & postbyte_mask);
	

		prepixels_clr  = (pixvals & prebyte_mask)  | (~prebyte_mask);
		postpixels_clr = (pixvals & postbyte_mask) | (~postbyte_mask);

		for (y = 0; y < ysize; y ++)
		{
		    UBYTE *ptr = curbyte;
	    
		    *ptr |= prepixels_set;
		    *ptr ++ &= prepixels_clr;
	    
		    /* Clear the last nonwhole byte */
		    *ptr |= postpixels_set;
		    *ptr ++ &= postpixels_clr;
	    
		    /* Go to next line */
		    curbyte += bm->BytesPerRow;
		}
		break;
		
	    case SB_SINGLEMASK:
	
		prepixels_set  = (pixvals & prebyte_mask);
		prepixels_clr  = (pixvals & prebyte_mask) | (~prebyte_mask);

		for (y = 0; y < ysize; y ++)
		{
		    UBYTE *ptr = curbyte;
	    
		    *ptr |= prepixels_set;
		    *ptr ++ &= prepixels_clr;
	    
		    /* Go to next line */
		    curbyte += bm->BytesPerRow;
		}
		break;
		
	} /* switch() */
    }
    return;
    
}




static LONG fillrect_pendrmd(struct RastPort *rp
	, LONG x1, LONG y1
	, LONG x2, LONG y2
	, HIDDT_Pixel pix
	, HIDDT_DrawMode drmd
	, struct GfxBase *GfxBase) 
{
     
    struct BitMap *bm = rp->BitMap;
    struct Layer *L = rp->Layer;
    ULONG width, height;
    
    LONG pixwritten = 0;
    
    HIDDT_DrawMode old_drmd;
    HIDDT_Pixel old_fg;
    Object *gc;

    struct TagItem gc_tags[] =
    {
	{ aHidd_GC_DrawMode, drmd },
	{ aHidd_GC_Foreground, pix },
	{ TAG_DONE, 0}
    };
    
    
    if (!CorrectDriverData (rp, GfxBase))
	return 0;
	
    gc = GetDriverData(rp)->dd_GC;
	
    GetAttr(gc, aHidd_GC_DrawMode,	(IPTR *)&old_drmd);
    GetAttr(gc, aHidd_GC_Foreground,	(IPTR *)&old_fg);
    
    SetAttrs(gc, gc_tags);
	
    width  = x2 - x1 + 1;
    height = y2 - y1 + 1;
    
    if (NULL == L)
    {
        /* No layer, probably a screen */
	
	/* Just put the pixelarray directly onto the HIDD */

	
	HIDD_BM_FillRect(BM_OBJ(bm)
		, gc
		, x1, y1
		, x2, y2
	);
		

	pixwritten = width * height;
	
    }
    else
    {
        struct ClipRect *CR = L->ClipRect;
	WORD xrel = L->bounds.MinX;
        WORD yrel = L->bounds.MinY;
	struct Rectangle tofill, intersect;
	
	tofill.MinX = x1 + xrel;
	tofill.MinY = y1 + yrel;
	tofill.MaxX = x2 + xrel;
	tofill.MaxY = y2 + yrel;
	
	LockLayerRom(L);
	
	for (;NULL != CR; CR = CR->Next)
	{
	    D(bug("Cliprect (%d, %d, %d, %d), lobs=%p\n",
	    	CR->bounds.MinX, CR->bounds.MinY, CR->bounds.MaxX, CR->bounds.MaxY,
		CR->lobs));
		
	    /* Does this cliprect intersect with area to rectfill ? */
	    if (andrectrect(&CR->bounds, &tofill, &intersect))
	    {
	    	pixwritten +=   (intersect.MaxX - intersect.MinX + 1)
			      * (intersect.MaxY - intersect.MinY + 1);
	        if (NULL == CR->lobs)
		{
		    
/* Single thread the attribute setting  */
		    
		    /* Cliprect not obscured, so we may render directly into the display */
		    HIDD_BM_FillRect(BM_OBJ(bm)
		    	, gc
		    	, intersect.MinX
			, intersect.MinY
			, intersect.MaxX
			, intersect.MaxY
		    );
		}
		else
		{
		    /* Render into offscreen cliprect bitmap */
		    if (L->Flags & LAYERSIMPLE)
		    	continue;
		    else if (L->Flags & LAYERSUPER)
		    	kprintf("fillrect_pendrmd(): Superbitmap not handled yet\n");
		    else
		    {
		    
		    	/* Cliprect not obscured, so we may render directly into the display */
		    	HIDD_BM_FillRect(BM_OBJ(CR->BitMap)
				, gc
		    		, intersect.MinX - CR->bounds.MinX + ALIGN_OFFSET(CR->bounds.MinX)
				, intersect.MinY - CR->bounds.MinY
				, intersect.MaxX - CR->bounds.MinX + ALIGN_OFFSET(CR->bounds.MinX) 
				, intersect.MaxY - CR->bounds.MinY
		    	);
		    }
		    
		}
	    }
	}
	
        UnlockLayerRom(L);
    }
    
    /* Restore old GC values */
    gc_tags[0].ti_Data = (IPTR)old_drmd;
    gc_tags[1].ti_Data = (IPTR)old_fg;
    SetAttrs(gc, gc_tags);
    
	
    return pixwritten;
}

void driver_RectFill (struct RastPort * rp, LONG x1, LONG y1, LONG x2, LONG y2,
		    struct GfxBase * GfxBase)
{
    
    
    struct BitMap *bm = rp->BitMap;
    UBYTE rp_drmd;
    
    HIDDT_Pixel pix;
    HIDDT_DrawMode drmd;
    ULONG pen;

    /* Get drawmode */
    rp_drmd = GetDrMd(rp);
    
    pen = (rp_drmd & INVERSVID ? GetBPen(rp) : GetAPen(rp));
    
    /* Get rectfill pixel */
    
    pix = BM_PIXTAB(bm)[pen];
    
    
    if (rp_drmd & JAM2)
    {
    	drmd = vHidd_GC_DrawMode_Copy;
    }
    else if (rp_drmd & COMPLEMENT)
    {
    	drmd = vHidd_GC_DrawMode_Invert;
    }
    else if ((rp_drmd & (~INVERSVID)) == JAM1)
    {
    	drmd = vHidd_GC_DrawMode_Copy;
    }

    fillrect_pendrmd(rp, x1, y1, x2, y2, pix, drmd, GfxBase);
	
    ReturnVoid("driver_RectFill");
}

void driver_BltBitMapRastPort (struct BitMap   * srcBitMap,
	LONG xSrc, LONG ySrc,
	struct RastPort * destRP,
	LONG xDest, LONG yDest,
	LONG xSize, LONG ySize,
	ULONG minterm, struct GfxBase *GfxBase
)
{
    ULONG width, height;
    struct Layer *L = destRP->Layer;
    struct BitMap *bm = destRP->BitMap;
    
    EnterFunc(bug("driver_BltBitMapRastPort(%d %d %d, %d, %d, %d)\n"
    	, xSrc, ySrc, xDest, yDest, xSize, ySize));
	
    if (!CorrectDriverData(destRP, GfxBase))
    	return;
	

    width  = GetBitMapAttr(bm, BMA_WIDTH);
    height = GetBitMapAttr(bm, BMA_HEIGHT);
    
    if (NULL == L)
    {
        /* No layer, probably a screen */
	
	/* BltBitMap() will do clipping against bitmap bounds for us  */

	BltBitMap(srcBitMap
		, xSrc, ySrc
		, bm
		, xDest, yDest
		, xSize, ySize
		, minterm, 0xFF	, NULL
	);
	
	
    }
    else
    {
        struct ClipRect *CR;
	WORD xrel;
        WORD yrel;
	struct Rectangle toblit, intersect;
	
	LockLayerRom( L );
	
	xrel = L->bounds.MinX;
	yrel = L->bounds.MinY;
	
	CR = L->ClipRect;
	
	toblit.MinX = xDest + xrel;
	toblit.MinY = yDest + yrel;
	toblit.MaxX = (xDest + xSize - 1) + xrel;
	toblit.MaxY = (yDest + ySize - 1) + yrel;
	
	
	for (;NULL != CR; CR = CR->Next)
	{
	    D(bug("Cliprect (%d, %d, %d, %d), lobs=%p\n",
	    	CR->bounds.MinX, CR->bounds.MinY, CR->bounds.MaxX, CR->bounds.MaxY,
		CR->lobs));
		
	
		
	    /* Does this cliprect intersect with area to blit ? */
	    if (andrectrect(&CR->bounds, &toblit, &intersect))
	    {
	        ULONG xoffset = intersect.MinX - toblit.MinX;
		ULONG yoffset = intersect.MinY - toblit.MinY;
		
	        if (NULL == CR->lobs)
		{
		    D(bug("non-obscured cliprect, intersect= (%d,%d,%d,%d)\n"
		    	, intersect.MinX
			, intersect.MinY
			, intersect.MaxX
			, intersect.MaxY
		    ));
		    
		    /* Cliprect not obscured, so we may render directly into the display */
		    BltBitMap(srcBitMap
		    	, xSrc + xoffset, ySrc + yoffset
			, bm
		    	, intersect.MinX
			, intersect.MinY
			, intersect.MaxX - intersect.MinX + 1
			, intersect.MaxY - intersect.MinY + 1
			, minterm, destRP->Mask, NULL
		    );
		}
		else
		{
		    /* Render into offscreen cliprect bitmap */
		    if (L->Flags & LAYERSIMPLE)
		    	continue;
		    else if (L->Flags & LAYERSUPER)
		    	kprintf("driver_BltBitMapRastPort(): Superbitmap not handled yet\n");
		    else
		    {
			BltBitMap(srcBitMap
		    		, xSrc + xoffset, ySrc + yoffset
				, CR->BitMap
		    		, intersect.MinX - CR->bounds.MinX + ALIGN_OFFSET(CR->bounds.MinX)
				, intersect.MinY - CR->bounds.MinY
				, intersect.MaxX - intersect.MinX + 1
				, intersect.MaxY - intersect.MinY + 1
				, minterm, destRP->Mask, NULL
		   	);
		    }
		    
		}
	    }
	} /* while */
	
	UnlockLayerRom( L );
	

    }
	
	
    ReturnVoid("driver_BltBitMapRastPort");
}


BOOL driver_MoveRaster (struct RastPort * rp, LONG dx, LONG dy,
	LONG x1, LONG y1, LONG x2, LONG y2, BOOL UpdateDamageList,
	BOOL hasClipRegion,
	struct GfxBase * GfxBase)
{
  LONG xCorr1, xCorr2, yCorr1, yCorr2, absdx, absdy, xBlockSize, yBlockSize;
  struct BitMap * bm;
  struct Layer * L = rp->Layer;
  ULONG AllocFlag;
  
  if (!CorrectDriverData (rp, GfxBase))
    return FALSE;
    
  if (0 == dx && 0 == dy)
    return TRUE;
  
  if (dx >= 0)
    absdx = dx;
  else
    absdx = -dx;
    
  if (dy >= 0)
    absdy = dy;
  else
    absdy = -dy;
  
  xBlockSize = x2 - x1 + 1 - absdx;  
  yBlockSize = y2 - y1 + 1 - absdy;
  /* 
    It is assumed that (x1,y1)-(x2,y2) describe a valid rectangle
    within the rastport. So any corrections have to be done by
    ScrollRaster() and ScrollRasterBF() 
  */
  /*
    There are four different cases to consider:
    1) rastport without layer (a screen's rastport)
    2) rastport with simple-refresh layer 
    3) rastport with smart -refresh layer
    4) rastport with superbitmap layer
    
    All cases need a TmpRas. If it's not there it will be allocated by
    this function.
  */
  
  /* Only if the rastport has an incomplete list of rectangles
     (because of a clipregion) then it is necessary to
     clear the bitmap when allocating it 
  */
  if (TRUE == hasClipRegion)
    AllocFlag = BMF_CLEAR;
  else
    AllocFlag = 0;
  
  bm = AllocBitMap(xBlockSize,
                   yBlockSize,
                   GetBitMapAttr(rp->BitMap,BMA_DEPTH),
                   AllocFlag,
                   rp->BitMap);
  if (NULL == bm)
    return FALSE;

  if (dx < 0) 
  {
    /* move it to the right */
    xCorr1 = 0;
    xCorr2 = dx;
  }
  else
  {
    /* move it to the left */
    xCorr1 = dx;
    xCorr2 = 0;
  }
  
  if (dy < 0)
  {
    /* move it down */
    yCorr1 = 0;
    yCorr2 = dy;
  }
  else
  {
    /* move it up */
    yCorr1 = dy;
    yCorr2 = 0;
  }
  
  /* must not change x2,x1,y2,y1 here!!! */
  /* scroll the stuff */

  if (NULL == L)
  {
    /* no layer, so this is a screen */
    /* 
       I am copying all the data into the temporary bitmap and then
       copy them back to the old location
    */
    BltBitMap(rp->BitMap,
              x1+xCorr1,
              y1+yCorr1,
              bm,
              0,
              0,
              xBlockSize,
              yBlockSize,
              0x0c0, /* copy */
              0xff,
              NULL );        
    
    /* copy it to the new location */
    BltBitMap(bm,
              0,
              0,
              rp->BitMap,
              x1-xCorr2,
              y1-yCorr2,
              xBlockSize,
              yBlockSize,
              0x0c0,
              0x0ff,
              NULL );
    /* no need to worry about damage lists here */
  }
  else
  {
    LONG xleft, xright, yup, ydown;
    /* rastport with layer - yuk! */
    /* 
       I will walk through all the layer's cliprects and copy the
       necessary parts to the temporary bitmap and if it's a simple
       layer I will also collect those parts that come out of a 
       hidden cliprect and add them to a region. Then I will manipulate
       the region and move it by (dx,dy) towards (0,0) and then 
       subtract all areas within the rectangle that are in hidden
       cliprects when copying the data back into the layer's cliprects. 
       The result of this will be a region corresponding to the damage 
       created by scrolling areas that were hidden into visible areas.
       I will add this region to the damage list and set the 
       LAYERREFRESH flag.
    */
    struct ClipRect * CR = L->ClipRect;
    struct Region * R;
    struct Rectangle Rect;
    struct Rectangle ScrollRect;
     
    LockLayerRom(L);
     
    if (0 != (L->Flags & LAYERSIMPLE) &&
        TRUE == UpdateDamageList)
    {
      /* 
         This region should also contain the current damage list in that
         area.
         Region R = damagelist AND scrollarea
      */
      if (NULL == (R = copyregion(L->DamageList, GfxBase)))
      { 
        /* not enough memory */
        DisposeRegion(R);
        UnlockLayerRom(L);
        goto failexit;
      }
      
      ScrollRect.MinX = x1;
      ScrollRect.MinY = y1;
      ScrollRect.MaxX = x2;
      ScrollRect.MaxY = y2;
      
      /* this cannot fail */
      AndRectRegion(R, &ScrollRect);
      /* Now Region R contains the damage that is already there before
         the area has been scrolled at all.
       */

      /* This area must also be cleared from the current damage list. But
         I do this later as I might run out of memory here somewhere in
         between...
      */    
    }  
    
    /* adapt x1,x2,y1,y2 to the cliprect coordinates */

    /* (xleft,yup)(xright,ydown) defines the rectangle to copy out of */

    xleft  = x1 + L->bounds.MinX + xCorr1;
    xright = x2 + L->bounds.MinX - xCorr2; /* stegerg: was + xCorr2; */
    yup    = y1 + L->bounds.MinY + yCorr1;
    ydown  = y2 + L->bounds.MinY - yCorr2; /* stegerg: was + yCorr2; */

    /* First read all data out of the source area */    
    while (NULL != CR)
    {
      /* Is this a CR to be concerned about at all??? */
      if (!(CR->bounds.MinX > xright ||
            CR->bounds.MaxX < xleft  ||
            CR->bounds.MinY > ydown  ||
            CR->bounds.MaxY < yup))
      {
        /* that is one to be at least partly concerned about */
        /* 
           first determine the area to copy out of this cliprect
           into the destination bitmap
        */
        if (CR->bounds.MinX > xleft)
          Rect.MinX = CR->bounds.MinX;
        else
          Rect.MinX = xleft;
          
        if (CR->bounds.MaxX < xright)
          Rect.MaxX = CR->bounds.MaxX;
        else
          Rect.MaxX = xright;
          
        if (CR->bounds.MinY > yup)
          Rect.MinY = CR->bounds.MinY;
        else
          Rect.MinY = yup;
          
        if (CR->bounds.MaxY < ydown)
          Rect.MaxY = CR->bounds.MaxY;
        else
          Rect.MaxY = ydown;
          
        /* If this cliprect is hidden and belongs to a simple layer then
           I add the Rect to the Region, otherwise I do a blit into the
           temporary bitmap instead 
        */
        if (NULL != CR->lobs && 
            0 != (L->Flags & LAYERSIMPLE) &&
            TRUE == UpdateDamageList)
        { 
	  /* stegerg: damage Region coords are relative to layer!! */
	  struct Rectangle Rect2;

	  Rect2.MinX = Rect.MinX - L->bounds.MinX;
	  Rect2.MinY = Rect.MinY - L->bounds.MinY;
	  Rect2.MaxX = Rect.MaxX - L->bounds.MinX;
	  Rect2.MaxY = Rect.MaxY - L->bounds.MinY;

	  D(bug("adding (%d,%d) - (%d,%d)\n",Rect2.MinX,Rect2.MinY,Rect2.MaxX,Rect2.MaxY));
	  
          if (FALSE == OrRectRegion(R, &Rect2))
          {
            /* there's still time to undo this operation */
            DisposeRegion(R);
            UnlockLayerRom(L);
            goto failexit;
          }
          /* also clear that area in the destination bitmap */
          BltBitMap(bm,
                    0,
                    0,
                    bm,
                    Rect.MinX - xleft,
                    Rect.MinY - yup,
                    Rect.MaxX - Rect.MinX + 1,
                    Rect.MaxY - Rect.MinY + 1,
                    0x000, /* clear */
                    0xff,
                    NULL);
        }
        else
        {
          /* Do the blit into the temporary bitmap */
          /* several cases:
             - non hidden area of superbitmap layer, simple layer, smart layer
                -> can all be treated equally
             - hidden area of a superbitmap layer
             - hidden area of simple layer 
          */
          if (NULL != CR->lobs)
          {
            /* Hidden */
            if (0 == (L->Flags & LAYERSUPER))
            {
              /* a smart layer */
              BltBitMap(CR->BitMap,
                        Rect.MinX - CR->bounds.MinX + ALIGN_OFFSET(CR->bounds.MinX),
                        Rect.MinY - CR->bounds.MinY,
                        bm,
                        Rect.MinX - xleft,
                        Rect.MinY - yup,
                        Rect.MaxX-Rect.MinX+1,
                        Rect.MaxY-Rect.MinY+1,
                        0x0c0,
                        0xFF,
                        NULL);
            }
            else
            {
              /* a superbitmap layer */
              BltBitMap(CR->BitMap,
                        Rect.MinX - CR->bounds.MinX - L->Scroll_X,
                        Rect.MinY - CR->bounds.MinY - L->Scroll_Y,
                        bm,
                        Rect.MinX - xleft,
                        Rect.MinY - yup,
                        Rect.MaxX-Rect.MinX+1,
                        Rect.MaxY-Rect.MinY+1,
                        0x0c0,
                        0xFF,
                        NULL);
            } /* if .. else .. */
          }
          else
          {
            /* copy it out of the rastport's bitmap */
            BltBitMap(rp->BitMap,
                      Rect.MinX,
                      Rect.MinY,
                      bm,
                      Rect.MinX - xleft,
                      Rect.MinY - yup,
                      Rect.MaxX - Rect.MinX + 1,
                      Rect.MaxY - Rect.MinY + 1,
                      0x0c0,
                      0xff,
                      NULL);            
          } /* if .. else  .. */
        } /* if .. else .. */
      } /* if (ClipRect is to be considered at all) */  

      CR = CR->Next;
    } /* while */

    /* (xleft,yup)(xright,ydown) defines the rectangle to copy into */

    
    /* Move the region, if it's a simple layer  */
    if (0 != (L->Flags & LAYERSIMPLE) &&
        TRUE == UpdateDamageList)
    {
      /* the following could possibly lead to problems when the region
         R is moved to negative coordinates. Maybe and maybe not.
      */
      R->bounds.MinX -= dx;
      R->bounds.MinY -= dy;
      R->bounds.MaxX -= dx;
      R->bounds.MaxY -= dy;

      /* make this region valid again */
      AndRectRegion(R, &ScrollRect);

    }

    xleft  -= dx;
    xright -= dx;
    yup    -= dy;
    ydown  -= dy;
    
    /* now copy from the bitmap bm into the destination */
    CR = L->ClipRect;
    while (NULL != CR)
    {
      /* Is this a CR to be concerned about at all??? */
      if (!(CR->bounds.MinX > xright ||
            CR->bounds.MaxX < xleft  ||
            CR->bounds.MinY > ydown  ||
            CR->bounds.MaxY < yup))
      {
        struct Rectangle Rect;
        /* that is one to be at least partly concerned about */
        /* 
           first determine the area to copy into from the
           temporary bitmap
        */
        if (CR->bounds.MinX > xleft)
          Rect.MinX = CR->bounds.MinX;
        else
          Rect.MinX = xleft;
          
        if (CR->bounds.MaxX < xright)
          Rect.MaxX = CR->bounds.MaxX;
        else
          Rect.MaxX = xright;
          
        if (CR->bounds.MinY > yup)
          Rect.MinY = CR->bounds.MinY;
        else
          Rect.MinY = yup;
          
        if (CR->bounds.MaxY < ydown)
          Rect.MaxY = CR->bounds.MaxY;
        else
          Rect.MaxY = ydown;
          
        /* 
           If this cliprect is hidden and belongs to a simple layer then
           I subtract the Rect from the Region, otherwise I do a blit 
           from the temporary bitmap instead 
        */
        if (NULL != CR->lobs && 
            0 != (L->Flags & LAYERSIMPLE) &&
            TRUE == UpdateDamageList)
        {
	  /* stegerg: damage Region coords are relative to layer!! */
	  struct Rectangle Rect2;
	  
	  Rect2.MinX = Rect.MinX - L->bounds.MinX;
	  Rect2.MinY = Rect.MinY - L->bounds.MinY;
	  Rect2.MaxX = Rect.MaxX - L->bounds.MinX;
	  Rect2.MaxY = Rect.MaxY - L->bounds.MinY;
	  
	  D(bug("clearing (%d,%d) - (%d,%d)\n",Rect2.MinX,Rect2.MinY,Rect2.MaxX,Rect2.MaxY));
          ClearRectRegion(R, &Rect2);
        }
        else
        {
          /* Do the blit from the temporary bitmap */
          /* several cases:
             - non hidden area of superbitmap layer, simple layer, smart layer
                -> can all be treated equally
             - hidden area of a superbitmap layer
             - hidden area of simple layer 
          */
          if (NULL != CR->lobs)
          {
            /* Hidden */
            if (0 == (L->Flags & LAYERSUPER))
            {
              /* a smart layer */
              BltBitMap(bm,
                        Rect.MinX - xleft,
                        Rect.MinY - yup,
                        CR->BitMap,
                        Rect.MinX - CR->bounds.MinX + ALIGN_OFFSET(CR->bounds.MinX),
                        Rect.MinY - CR->bounds.MinY,
                        Rect.MaxX-Rect.MinX+1,
                        Rect.MaxY-Rect.MinY+1,
                        0x0c0,
                        0xff,
                        NULL);
                        
            }
            else
            {
              /* a superbitmap layer */
              BltBitMap(bm,
                        Rect.MinX - xleft,
                        Rect.MinY - yup,
                        CR->BitMap,
                        Rect.MinX - L->bounds.MinX - L->Scroll_X,
                        Rect.MinY - L->bounds.MinY - L->Scroll_Y,
                        Rect.MaxX - Rect.MinX + 1,
                        Rect.MaxY - Rect.MinY + 1,
                        0x0c0,
                        0xff,
                        NULL);
            }
          }
          else
          {
            /* blit it into the bitmap of the screen */
            BltBitMap(bm,
                      Rect.MinX - xleft,
                      Rect.MinY - yup,
                      rp->BitMap,
                      Rect.MinX,
                      Rect.MinY,
                      Rect.MaxX - Rect.MinX + 1,
                      Rect.MaxY - Rect.MinY + 1,
                      0x0c0,
                      0xff,
                      NULL);
          }  
          
        } /* if .. else .. */
        
      }
      CR = CR->Next;
    }
    
    
    if (0 != (L->Flags & LAYERSIMPLE) && TRUE == UpdateDamageList)
    {
      /* Add the damagelist to the layer's damage list and set the
         LAYERREFRESH flag, but of course only if it's necessary */

      /* first clear the damage lists of the scrolled area */
      ClearRectRegion(L->DamageList, &ScrollRect);

      if (NULL != R->RegionRectangle)
      {
        OrRegionRegion(R, L->DamageList);
        L->Flags |= LAYERREFRESH;
      }
      DisposeRegion(R);
    }
    
    UnlockLayerRom(L); 
  }  

failexit:
  FreeBitMap(bm);  
  
  return TRUE;
  
}

void driver_DrawEllipse (struct RastPort * rp, LONG center_x, LONG center_y, LONG rx, LONG ry,
		struct GfxBase * GfxBase)
{

    LONG   x = rx, y = 0;     /* ellipse points */

    /* intermediate terms to speed up loop */
    LONG t1 = rx * rx, t2 = t1 << 1, t3 = t2 << 1;
    LONG t4 = ry * ry, t5 = t4 << 1, t6 = t5 << 1;
    LONG t7 = rx * t5, t8 = t7 << 1, t9 = 0L;
    LONG d1 = t2 - t7 + (t4 >> 1);    /* error terms */
    LONG d2 = (t1 >> 1) - t8 + t5;
    EnterFunc(bug("driver_DrawEllipse()"));
    
    if (!CorrectDriverData (rp, GfxBase))
    	ReturnVoid("driver_DrawEllipse (No driverdata)");

    while (d2 < 0)                  /* til slope = -1 */
    {
        /* draw 4 points using symmetry */
        WritePixel(rp, center_x + x, center_y + y);
        WritePixel(rp, center_x + x, center_y - y);
        WritePixel(rp, center_x - x, center_y + y);
        WritePixel(rp, center_x - x, center_y - y);
    
        y++;            /* always move up here */
        t9 = t9 + t3;
        if (d1 < 0)     /* move straight up */
        {
            d1 = d1 + t9 + t2;
            d2 = d2 + t9;
        }
        else            /* move up and left */
        {
            x--;
            t8 = t8 - t6;
            d1 = d1 + t9 + t2 - t8;
            d2 = d2 + t9 + t5 - t8;
        }
    }

    do                              /* rest of top right quadrant */
    {
        /* draw 4 points using symmetry */
        WritePixel(rp, center_x + x, center_y + y);
        WritePixel(rp, center_x + x, center_y - y);
        WritePixel(rp, center_x - x, center_y + y);
        WritePixel(rp, center_x - x, center_y - y);
    
        x--;            /* always move left here */
        t8 = t8 - t6;
        if (d2 < 0)     /* move up and left */
        {
            y++;
            t9 = t9 + t3;
            d2 = d2 + t9 + t5 - t8;
        }
        else            /* move straight left */
        {
            d2 = d2 + t5 - t8;
        }
    } while (x >= 0);

	
    ReturnVoid("driver_DrawEllipse");
}


void blit_glyph_fast(struct RastPort *rp, Object *fontbm, WORD xsrc
	, WORD destx, WORD desty, UWORD width, UWORD height)
{
    struct Layer *L = rp->Layer;
    struct BitMap *bm = rp->BitMap;
    
    Object *gc;
    
    
    EnterFunc(bug("blit_glyph_fast(%d, %d, %d, %d, %d)\n"
    	, xsrc, destx, desty, width, height));
	
	
    if (!CorrectDriverData(rp, GfxBase))
    	ReturnVoid("blit_glyph_fast");
	
    gc = GetDriverData(rp)->dd_GC;
    
    
    
    if (NULL == L)
    {
        /* No layer, probably a screen */
	HIDD_BM_BlitColorExpansion( BM_OBJ(bm)
		, gc
		, fontbm
		, xsrc, 0
		, destx, desty
		, width, height
	);
		
	
	
    }
    else
    {
    	/* Window rastport, we need to clip the operation */
	
        struct ClipRect *CR;
	WORD xrel;
        WORD yrel;
	struct Rectangle toblit, intersect;
	
	
	LockLayerRom( L );
	
	CR = L->ClipRect;
	xrel = L->bounds.MinX;
	yrel = L->bounds.MinY;
	
	toblit.MinX = xrel + destx;
	toblit.MinY = yrel + desty;
	toblit.MaxX = xrel + (destx + width  - 1);
	toblit.MaxY = yrel + (desty + height - 1);
	
	for (;NULL != CR; CR = CR->Next)
	{
	    D(bug("Cliprect (%d, %d, %d, %d), lobs=%p\n",
	    	CR->bounds.MinX, CR->bounds.MinY, CR->bounds.MaxX, CR->bounds.MaxY,
		CR->lobs));
		
	    /* Does this cliprect intersect with area to blit ? */
	    if (andrectrect(&CR->bounds, &toblit, &intersect))
	    {
		    
		
	        if (NULL == CR->lobs)
		{
		
		    HIDD_BM_BlitColorExpansion(BM_OBJ(bm)
		    	, gc
		    	, fontbm
			, intersect.MinX - toblit.MinX + xsrc	/* srcX		*/
			, intersect.MinY - toblit.MinY		/* srcY		*/
			, intersect.MinX, intersect.MinY	/* destX, destY	*/
			, intersect.MaxX - intersect.MinX + 1	/* width	*/
			, intersect.MaxY - intersect.MinY + 1	/* height	*/
		    );

		}
		else
		{
		    if (L->Flags & LAYERSIMPLE)
		    	continue;
		    else if (L->Flags & LAYERSUPER)
		    	kprintf("blit_glyph_fast(): Superbitmap not handled yet\n");
		    else
		    {


			HIDD_BM_BlitColorExpansion(BM_OBJ(CR->BitMap)
				, gc
				, fontbm
				, intersect.MinX - toblit.MinX + xsrc	/* srcX		*/
				, intersect.MinY - toblit.MinY		/* srcY		*/
				, intersect.MinX - CR->bounds.MinX + ALIGN_OFFSET(CR->bounds.MinX)
				, intersect.MinY - CR->bounds.MinY	/* destY	*/
				, intersect.MaxX - intersect.MinX + 1	/* width	*/
				, intersect.MaxY - intersect.MinY + 1	/* height	*/
			);
			
		    }

		}
		
	    } /* if (cliprect intersects with area we want to draw to) */
	    
	} /* while (cliprects to examine) */
	
	UnlockLayerRom( L );
	
    } /* if (not screen rastport) */


    ReturnVoid("blit_glyph_fast");
}
    
    

#define NUMCHARS(tf) ((tf->tf_HiChar - tf->tf_LoChar) + 2)
#define CTF(x) ((struct ColorTextFont *)x)

void driver_Text (struct RastPort * rp, STRPTR string, LONG len,
		struct GfxBase * GfxBase)
{

#warning Does not handle color textfonts nor drawingmodes
    WORD  render_y;
    struct TextFont *tf;
    WORD current_x;
    struct tfe_hashnode *hn;
    Object *fontbm = NULL;
    
    if (!CorrectDriverData (rp, GfxBase))
    	return;

    /* does this rastport have a layer. If yes, lock the layer it.*/
    if (NULL != rp->Layer)
      LockLayerRom(rp->Layer);	
    
    tf = rp->Font;
    
    /* Check if font has character data as a HIDD bitmap */
    hn = tfe_hashlookup(tf, GfxBase);
    if (NULL != hn)
    {
	if (NULL == hn->font_bitmap)
	{
	    hn->font_bitmap = fontbm_to_hiddbm(tf, GfxBase);
	}
    }
    else
    {
    	hn = tfe_hashnode_create(GfxBase);
	if (NULL != hn)
	{
	    
	    hn->font_bitmap = fontbm_to_hiddbm(tf, GfxBase);
	    tfe_hashadd(hn, tf, NULL, GfxBase);
	}
    }

    if (NULL != hn)
	fontbm = hn->font_bitmap;
    
    if (NULL == fontbm)
    {
    	kprintf("FONT HAS NO HIDD BITMAP ! Won't render text\n");
	return;
    }


    /* Render along font's baseline */
    render_y = rp->cp_y - tf->tf_Baseline;
    current_x = rp->cp_x;
    
    while ( len -- )
    {
	ULONG charloc;
	WORD render_x;
	ULONG idx;
	
	if (*string < tf->tf_LoChar || *string > tf->tf_HiChar )
	{
	     /* A character which there is no glyph for. We just
	        draw the last glyph in the font
	     */
	     idx = tf->tf_HiChar - tf->tf_LoChar;
	}
	else
	{
	    idx = *string - tf->tf_LoChar;
	}
	
	
		
	charloc = ((ULONG *)tf->tf_CharLoc)[idx];
	
	if (tf->tf_Flags & FPF_PROPORTIONAL)
	{
	    render_x = current_x + ((WORD *)tf->tf_CharKern)[idx];
	}
	else
	    render_x = current_x;	/* Monospace */
	    
	if (tf->tf_Style & FSF_COLORFONT)
	{
#warning Handle color fonts	
	}
	else
	{
	    WORD xoffset;
	    xoffset = charloc >> 16;
	
//	    kprintf("F ");  
	    blit_glyph_fast(rp
		, fontbm
		, xoffset
		, render_x, render_y
		, charloc & 0xFFFF
		, tf->tf_YSize
	    );
	}
	
	if (tf->tf_Flags & FPF_PROPORTIONAL)
	    current_x += ((WORD *)tf->tf_CharSpace)[idx];
	else
	    current_x += tf->tf_XSize; /* Add glyph width */
	
	string ++;
    } /* for (each character to render) */
    
    Move(rp, current_x, rp->cp_y);
    
    if (NULL != rp->Layer)
      UnlockLayerRom(rp->Layer);
    
    return;

}

WORD driver_TextLength (struct RastPort * rp, STRPTR string, ULONG len,
		    struct GfxBase * GfxBase)
{
    struct TextFont *tf = rp->Font;
    WORD strlen = 0;

    while (len --)
    {
	
	if (tf->tf_Flags & FPF_PROPORTIONAL)
	{
	    WORD idx;
	
	    if ( *string < tf->tf_LoChar || *string > tf->tf_HiChar)
	    {
		idx = NUMCHARS(tf) - 1; /* Last glyph is the default glyph */
	    }
	    else
	    {
		idx = *string - tf->tf_LoChar;
	    }
	    strlen += ((WORD *)tf->tf_CharSpace)[idx];
	}
	else
	{
	    strlen += tf->tf_XSize;
	}
	
	string ++;
	
    }
    return strlen;
}

void driver_Move (struct RastPort * rp, LONG x, LONG y,
		    struct GfxBase * GfxBase)
{
    return;
}


static inline LONG get_pen_from_hidd(struct BitMap *bm, LONG x, LONG y)
{
    UBYTE pen;
    HIDDT_PixelLUT pixlut = { AROS_PALETTE_SIZE, BM_PIXTAB(bm) };

    HIDD_BM_GetImageLUT(BM_OBJ(bm), &pen, 1, x, y, 1, 1, &pixlut);

    return (LONG)pen;
}


ULONG driver_ReadPixel (struct RastPort * rp, LONG x, LONG y,
		    struct GfxBase * GfxBase)
{
 
  struct gfx_driverdata *dd;

  struct Layer * L = rp -> Layer;
  BYTE Mask, pen_Mask;
  LONG count;
  struct BitMap * bm = rp->BitMap;
  ULONG Width, Height;
  ULONG i;
  BYTE * Plane;
  LONG penno;
  BOOL found_offscreen = FALSE;
  
  if(!CorrectDriverData (rp, GfxBase))
	return ((ULONG)-1L);
  dd = GetDriverData(rp);
  
  Width = GetBitMapAttr(bm, BMA_WIDTH);  
  Height = GetBitMapAttr(bm, BMA_HEIGHT);

  /* do we have a layer?? */
  if (NULL == L)
  {  
    /* is this pixel inside the rastport? 
       all pixels with coordinate less than zero are useless */
    if (x < 0 || x  > Width || 
        y < 0 || y  > Height)
    {
      /* no, it's not ! I refuse to do anything :-))  */
      return -1;
    }
  }
  else
  {
    /* No one may access the layer while I am doing this here! */
    LockLayerRom(L);
  }
  
  Width = WIDTH_TO_BYTES(Width);

  /* does this rastport have a layer? */
  if (NULL != L)
  {
    /* 
       more difficult part here as the layer might be partially 
       hidden.
       The coordinate (x,y) is relative to the layer.
    */
    struct ClipRect * CR = L -> ClipRect;
    WORD XRel = L->bounds.MinX;
    WORD YRel = L->bounds.MinY;
    
    /* Is this pixel inside the layer at all ?? */
    if (x > (L->bounds.MaxX - XRel + 1) ||
        y > (L->bounds.MaxY - YRel + 1)   )
    {
      /* ah, no it is not. So we exit. */
      penno = -1;
      goto exit;
    }
    /* No one may interrupt me while I'm working with this layer */

    /* search the list of ClipRects. If the cliprect where the pixel
       goes into does not have an entry in lobs, we can directly
       draw it to the bitmap, otherwise we have to draw it into the
       bitmap of the cliprect. 
    */
    while ((NULL != CR) && (!found_offscreen))
    {
      if (x >= (CR->bounds.MinX - XRel) &&
          x <= (CR->bounds.MaxX - XRel) &&
          y >= (CR->bounds.MinY - YRel) &&  
          y <= (CR->bounds.MaxY - YRel)    )
      {
        /* that's our cliprect!! */
        /* if it is not hidden, then we treat it as if we were
           directly drawing to the BitMap  
        */
        LONG Offset;
        if (NULL == CR->lobs)
        {

          i = COORD_TO_BYTEIDX(x + XRel, y + YRel, Width);
          Mask = XCOORD_TO_MASK(x + XRel);
	  if (bm->Flags & BMF_AROS_HIDD)
	  {
	    penno = get_pen_from_hidd(bm, x + XRel, y + YRel);
	    goto exit;
	  }
        } 
        else
        {
	   
	  /* Cannot get pen from obscured simple refresh CRs */
	  if (L->Flags & LAYERSIMPLE)
	  	goto fail_exit;
		
          /* we have to draw into the BitMap of the ClipRect, which
             will be shown once the layer moves... 
           */
	   
           
          bm = CR -> BitMap;
           
          Width = GetBitMapAttr(bm, BMA_WIDTH);
          /* Calculate the Width of the bitplane in bytes */
	  
	  Width = WIDTH_TO_BYTES(Width);
           
          if (0 == (L->Flags & LAYERSUPER))
          { 
	    ULONG val;
            /* Smart refresh */

            Offset = ALIGN_OFFSET(CR->bounds.MinX);


            val = get_pen_from_hidd(bm
	    	, x - (CR->bounds.MinX - XRel) + Offset
		, y - (CR->bounds.MinY - YRel)
	    );
	    return val;

#if 0          
            Offset = ALIGN_OFFSET(CR->bounds.MinX);
            i = COORD_TO_BYTEIDX( x - (CR->bounds.MinX - XRel) + Offset
	    			, y - (CR->bounds.MinY - YRel)
				, Width
	    );   
                /* Offset: optimization for blitting!! */
            Mask = XCOORD_TO_MASK(Offset + x - (CR->bounds.MinX - XRel));
#endif	    
          }
          else
          {
            /* with superbitmap */
	    found_offscreen = TRUE;
            i =  COORD_TO_BYTEIDX(x - L->Scroll_X, y - L->Scroll_Y, Width);
            Mask = XCOORD_TO_MASK(x - L->Scroll_X);
          }
          
        }       
        break;
        
      } /* if */      
      /* examine the next cliprect */
      CR = CR->Next;
    } /* while */   
  } /* if */
  else /* NULL == L */
  { /* this is probably a screen, it doesn't have layer!!! */

    if (bm->Flags & BMF_AROS_HIDD)
    {
    	ULONG val;
        /* no need to unlock the layer!!!! */
        val = get_pen_from_hidd(bm, x, y);
	return val;	
    }
    i = COORD_TO_BYTEIDX(x, y, Width);
    Mask = XCOORD_TO_MASK(x);
  }

 /* get the pen for this rastport */
 penno = -1L;
 
 if (found_offscreen)
 {
 
   

     pen_Mask = 1;
     penno = 0;

     /* read the pixel from all bitplanes */
     for (count = 0; count < GetBitMapAttr(bm, BMA_DEPTH); count++)
     {
       Plane = bm->Planes[count];
       /* are we supposed to clear this pixel or set it in this bitplane */
       if (0 != (Plane[i] & Mask))
       {
         /* in this bitplane the pixel is  set */
         penno |= pen_Mask;                
       } /* if */

       pen_Mask = pen_Mask << 1;
     } /* for */
    
  
 } /* if (found inside offsecreen cliprect) */
  /* if there was a layer I have to unlock it now */

exit:

  if (NULL != L) 
    UnlockLayerRom(L);
    
  return penno;

fail_exit:

  if (NULL != L) 
    UnlockLayerRom(L);
    
  return -1;

}

LONG driver_WritePixel (struct RastPort * rp, LONG x, LONG y,
		    struct GfxBase * GfxBase)
{

  struct Layer * L = rp -> Layer;
  ULONG pen;
  BYTE Mask, pen_Mask, CLR_Mask;
  LONG count;
  struct BitMap * bm = rp->BitMap;
  ULONG Width, Height;
  ULONG i;
  BYTE * Plane; 
  BOOL found_offscreen = FALSE;
  
  Object *gc;
  

  if(!CorrectDriverData (rp, GfxBase))
	return  -1L;
	
  gc = GetDriverData(rp)->dd_GC;

  Width = GetBitMapAttr(bm, BMA_WIDTH);  
  Height = GetBitMapAttr(bm, BMA_HEIGHT);
  
  /* 
     Is there a layer. If not then it cannot be a regular window!!
  */
  if (NULL == L)
  {  
    /* is this pixel inside the rastport? */
    if (x < 0 || x  > Width || 
        y < 0 || y  > Height)
    {
      /* no, it's not ! I refuse to do anything :-))  */
	return -1L;
    }
  }
  else
  {
    /* No one may access the layer while I am doing this here! */
    LockLayerRom(L);
  }
  
  Width = WIDTH_TO_BYTES(Width);

  /* does this rastport have a layer? */
  if (NULL != L)
  {
    /* more difficult part here as the layer might be partially 
       hidden.
       The coordinate x,y is relative to the layer.
    */
    struct ClipRect * CR = L -> ClipRect;
    WORD YRel = L->bounds.MinY - L->Scroll_Y;
    WORD XRel = L->bounds.MinX - L->Scroll_X;

    /* Is this pixel inside the layer ?? */
    if (x > (L->bounds.MaxX - XRel + 1) ||
        y > (L->bounds.MaxY - YRel + 1)   )
    {
      /* ah, no it is not. So we exit */
	goto fail_exit;
    }
    
    /* search the list of ClipRects. If the cliprect where the pixel
       goes into does not have an entry in lobs, we can directly
       draw it to the bitmap, otherwise we have to draw it into the
       bitmap of the cliprect. 
    */
    while ((NULL != CR) && (!found_offscreen)) 
    {
      if (x >= (CR->bounds.MinX - XRel) &&
          x <= (CR->bounds.MaxX - XRel) &&
          y >= (CR->bounds.MinY - YRel) &&  
          y <= (CR->bounds.MaxY - YRel)    )
      {
        /* that's our cliprect!! */
        /* if it is not hidden, then we treat it as if we were
           directly drawing to the BitMap  
        */
        LONG Offset;
        if (NULL == CR->lobs)
        {

          /* this ClipRect is not hidden! */
	  i = COORD_TO_BYTEIDX(x + XRel, y + YRel, Width);
          Mask = XCOORD_TO_MASK(x + XRel);

          /* and let the driver set the pixel to the X-Window also,
             but this Pixel has a relative position!! */
          if (bm->Flags & BMF_AROS_HIDD)
	  {
            HIDD_BM_DrawPixel ( BM_OBJ(bm), gc, x+XRel, y+YRel);
	   } 
        } 
        else
        {
	  /* For simple layers there is no offscreen bitmap
	     to render into
	 */
	  if (L->Flags & LAYERSIMPLE)
	  	goto fail_exit;
		
          /* we have to draw into the BitMap of the ClipRect, which
             will be shown once the layer moves... 
           */
	   
	  
          bm = CR -> BitMap;
           
          Width = GetBitMapAttr(bm, BMA_WIDTH);
          /* Calculate the Width of the bitplane in bytes */
	  
	  Width = WIDTH_TO_BYTES(Width);
           
          if (0 == (L->Flags & LAYERSUPER))
          { 
            /* Smart refresh */

            Offset = ALIGN_OFFSET(CR->bounds.MinX);
	    
	    HIDD_BM_DrawPixel ( BM_OBJ(bm)
	    	, gc
	    	, x - (CR->bounds.MinX - XRel) + Offset
		, y - (CR->bounds.MinY - YRel)
	    );

#if 0	    
            Offset = ALIGN_OFFSET(CR->bounds.MinX);
          
            i = COORD_TO_BYTEIDX( x - (CR->bounds.MinX - XRel) + Offset
	    			, y - (CR->bounds.MinY - YRel)
				, Width
	    );   
                /* Offset: optimization for blitting!! */
            Mask = XCOORD_TO_MASK(Offset + x - (CR->bounds.MinX - XRel) );

#endif
          }
          else
          {
 	    found_offscreen = TRUE;
            /* with superbitmap */
            i = COORD_TO_BYTEIDX(x - L->Scroll_X, y - L->Scroll_Y, Width);
            Mask = XCOORD_TO_MASK(x - L->Scroll_X);
          }
          
        }       
        break;
        
      } /* if */      
      /* examine the next cliprect */
      CR = CR->Next;
    } /* while */
       
  } /* if */
  else
  { /* this is probably something like a screen */
  
    i = COORD_TO_BYTEIDX(x, y, Width);
    Mask = XCOORD_TO_MASK( x );

    /* and let the driver set the pixel to the X-Window also */
    if (bm->Flags & BMF_AROS_HIDD)
    {
      HIDD_BM_DrawPixel( BM_OBJ(bm), gc, x, y);
    }

  }

  /* We have allready tested if this is a LAYERSIMPLE layer */
  if (found_offscreen)
  {

	/* get the pen for this rastport */
	pen = GetAPen(rp);

	pen_Mask = 1;
	CLR_Mask = ~Mask;
  
    /* we use brute force and write the pixel to
       all bitplanes, setting the bitplanes where the pen is
       '1' and clearing the other ones */
	for (count = 0; count < GetBitMapAttr(bm, BMA_DEPTH); count++)
	{
	  Plane = bm->Planes[count];

	  /* are we supposed to clear this pixel or set it in this bitplane */
	  if (0 != (pen_Mask & pen))
	  {
	    /* in this bitplane we're setting it */
	    Plane[i] |= Mask;          
	  } 
	  else
	  {
	    /* and here we clear it */
            Plane[i] &= CLR_Mask;
          }
	  pen_Mask = pen_Mask << 1;
	} /* for */
	
  
  } /* if (bitmap found as offscreen bitmap) */


  /* if there was a layer I have to unlock it now */
  if (NULL != L) 
    UnlockLayerRom(L);

  return 0;

fail_exit:
  if (NULL != L)
    UnlockLayerRom(L);
  
  return -1L;
}

void driver_PolyDraw (struct RastPort * rp, LONG count, WORD * coords,
		    struct GfxBase * GfxBase)
{
    int i;                              /* Loop variable */

    for(i = 0; i < count-1; i++)
    {
	Move(rp, coords[2*i], coords[2*i+1]);
	Draw(rp, coords[2*i+2], coords[2*i+3]);
    }
}

void driver_SetRast (struct RastPort * rp, ULONG color,
		    struct GfxBase * GfxBase)
{

    
    /* We have to use layers to perform clipping */
    struct Layer *L = rp->Layer;
    struct BitMap *bm = rp->BitMap;
    
    HIDDT_DrawMode old_drmd;
    HIDDT_Pixel old_fg;
    
    Object *gc;
    
    struct TagItem gc_tags[] = {
	{ aHidd_GC_DrawMode, vHidd_GC_DrawMode_Copy},
	{ aHidd_GC_Foreground, 0 },
	{ TAG_DONE, 0}
    };

    EnterFunc(bug("driver_SetRast(rp=%p, color=%u)\n", rp, color));
    
    if (!CorrectDriverData(rp, GfxBase))
    	return;
    
    gc = GetDriverData(rp)->dd_GC;
    
    GetAttr(gc, aHidd_GC_DrawMode,	&old_drmd);
    GetAttr(gc, aHidd_GC_Foreground ,	&old_fg);
    
    if (NULL != L)
    {
    
        /* Layered rastport, we have to clip this operation. */
    	/* Window rastport, we need to clip this operation */
	
        struct ClipRect *CR;
	struct Rectangle intersect;
	
	LockLayerRom( L );
	
	CR = L->ClipRect;
	
	for (;NULL != CR; CR = CR->Next)
	{
	    D(bug("Cliprect (%d, %d, %d, %d), lobs=%p\n",
	    	CR->bounds.MinX, CR->bounds.MinY, CR->bounds.MaxX, CR->bounds.MaxY,
		CR->lobs));
		
	    if (andrectrect(&CR->bounds, &L->bounds, &intersect))
	    {
		
	        if (NULL == CR->lobs)
		{
		    
		    
		    /* Write to the HIDD */
		    gc_tags[1].ti_Data = BM_PIXTAB(rp->BitMap)[color & PEN_MASK];

		    SetAttrs(gc, gc_tags);
		    HIDD_BM_FillRect(BM_OBJ(bm)
		    	, gc
		    	, intersect.MinX, intersect.MinY
			, intersect.MaxX, intersect.MaxY
		    );
	
		}
		else
		{
		    if (L->Flags & LAYERSIMPLE)
		    	continue;
		    else if (L->Flags & LAYERSUPER)
		    	kprintf("driver_SetRast(): Superbitmap not handled yet\n");
		    else
		    {
			gc_tags[1].ti_Data = BM_PIXTAB(rp->BitMap)[color & PEN_MASK];
			
		    	SetAttrs(gc, gc_tags);
		    	HIDD_BM_FillRect(BM_OBJ(CR->BitMap)
				, gc
		    		, intersect.MinX - CR->bounds.MinX + ALIGN_OFFSET(CR->bounds.MinX)
				, intersect.MinY - CR->bounds.MinY
				, intersect.MaxX - intersect.MinX + 1
				, intersect.MaxY - intersect.MinY + 1
		    	);
		    }
		    
		} /* if (intersecton inside hidden cliprect) */

		
	    } /* if (cliprect intersects with area we want to draw to) */
	    
	} /* while (cliprects to examine) */
	
	UnlockLayerRom( L );
	
	
    }
    else
    {
    	ULONG width, height;
	
	GetAttr(BM_OBJ(bm), aHidd_BitMap_Width,  &width);
	GetAttr(BM_OBJ(bm), aHidd_BitMap_Height, &height);
	HIDD_BM_FillRect(BM_OBJ(bm)
		, gc
		, 0, 0
		, width - 1, height - 1
	);

    }
    
    gc_tags[0].ti_Data = (IPTR)old_drmd;
    gc_tags[1].ti_Data = (IPTR)old_fg;
    SetAttrs(gc, gc_tags);

    ReturnVoid("driver_SetRast");

}

void driver_SetFont (struct RastPort * rp, struct TextFont * font,
		    struct GfxBase * GfxBase)
{
    CorrectDriverData (rp, GfxBase);
}


struct TextFont * driver_OpenFont (struct TextAttr * ta,
	struct GfxBase * GfxBase)
{
    struct TextFont *tf, *best_so_far = NULL;
    WORD bestmatch = 0;
   
    
    if (!ta->ta_Name)
	return NULL;
	
    /* Search for font in the fontlist */
    Forbid();
    ForeachNode(&GfxBase->TextFonts, tf)
    {
	if (0 == strcmp(tf->tf_Message.mn_Node.ln_Name, ta->ta_Name))
	{
	    UWORD match;
	    struct TagItem *tags = NULL;
	    struct TextAttr match_ta =
	    {
	    	tf->tf_Message.mn_Node.ln_Name,
		tf->tf_YSize,
		tf->tf_Style,
		tf->tf_Flags
	    };
	    
	    if (ExtendFont(tf, NULL))
	    {
	        tags = ((struct TextFontExtension *)tf->tf_Extension)->tfe_Tags;
		
	    }
	    else
	    	tags = NULL;
	    
	    match = WeighTAMatch(ta, &match_ta, tags);
	    if (match > bestmatch)
	    {
	    	bestmatch = match;
		best_so_far = tf;
	    }
	}
    }
    Permit();

    
    return best_so_far;
}

void driver_CloseFont (struct TextFont * tf, struct GfxBase * GfxBase)
{
    /* Nobody using the font anymore ? */
    if (    tf->tf_Accessors == 0
         && (tf->tf_Flags & FPF_ROMFONT) == 0) /* Don't free ROM fonts */
    {
        Forbid();
	
	Remove((struct Node *)tf);
	
	Permit();
	
	/* Free font data */
	
	/* !!! NOTE. FreeXXX functions has to match AllocXXX in
	   workbench/libs/diskfont/diskfont_io.c
	*/

	if (tf->tf_Style & FSF_COLORFONT)
	{
	    UWORD i;
	    struct ColorFontColors *cfc;
			
	    for (i = 0; i < 8; i ++)
	    {
		if (CTF(tf)->ctf_CharData[i])
		    FreeVec(CTF(tf)->ctf_CharData[i]);
	    }
	    
	    cfc = CTF(tf)->ctf_ColorFontColors;
	    if (cfc)
	    {
		if (cfc->cfc_ColorTable)
		    FreeVec(cfc->cfc_ColorTable);
				
		FreeVec(cfc);
	    }

	}
	else
	{
	    /* Not a colortextfont, only one plane */
	    FreeVec(tf->tf_CharData);
	}
	StripFont(tf);
	
	if (tf->tf_CharSpace)
	    FreeVec(tf->tf_CharSpace);
	    
	if (tf->tf_CharKern)
	    FreeVec(tf->tf_CharKern);
	    
	/* All fonts have a tf_CharLoc allocated */    
	FreeVec(tf->tf_CharLoc); 
	
	FreeVec(tf->tf_Message.mn_Node.ln_Name);
	FreeVec(tf);
	
    }
    return;
}

BOOL driver_ExtendFont(struct TextFont *tf, struct tfe_hashnode *hn, struct GfxBase *GfxBase)
{
    if (NULL != hn->font_bitmap)
    	return TRUE;
	
    hn->font_bitmap = fontbm_to_hiddbm(tf, GfxBase);
    if (NULL != hn->font_bitmap)
	return TRUE;
	    
    return FALSE;
}

void driver_StripFont(struct TextFont *tf, struct tfe_hashnode *hn, struct GfxBase *GfxBase)
{
    if (NULL != hn->font_bitmap)
    {
    	DisposeObject(hn->font_bitmap);
    }
    return;
}

int driver_InitRastPort (struct RastPort * rp, struct GfxBase * GfxBase)
{

   /* Do nothing */
   
/*    if (!rp->BitMap)
    {
	rp->BitMap = AllocMem (sizeof (struct BitMap), MEMF_CLEAR|MEMF_ANY);
	
	if (!rp->BitMap)
	{
	    return FALSE;
	}
    }

*/
/*    if(!GetDriverData(rp))
	InitDriverData (rp, GfxBase);
    else
	CorrectDriverData(rp, GfxBase);

*/

    return TRUE;
}

int driver_CloneRastPort (struct RastPort * newRP, struct RastPort * oldRP,
			struct GfxBase * GfxBase)
{
    /* Let CorrectDriverData() have a bitmap to use for the GC */
    newRP->BitMap = oldRP->BitMap;
    
    /* Creates a new GC. Hmmm, a general Copy method would've been nice */
    
    if (!CorrectDriverData (newRP, GfxBase))
    	return FALSE;
	
    /* copy rastports attributes */
    SetFont(newRP, oldRP->Font);
    SetABPenDrMd(newRP, GetAPen(oldRP), GetBPen(oldRP), GetDrMd(oldRP));
    Move(newRP, oldRP->cp_x, oldRP->cp_y);
    
#warning Some attributes not copied    
    return TRUE;
}

void driver_DeinitRastPort (struct RastPort * rp, struct GfxBase * GfxBase)
{
    D(bug("driver_DeInitRP()\n"));

    if ( rp->Flags & RPF_DRIVER_INITED )
    {
    	D(bug("RP inited, rp=%p, %flags=%d=\n", rp, rp->Flags));
		 
        if (GetDriverData(rp)->dd_RastPort == rp) 
	{
	    D(bug("Calling DeInitDriverData\n"));
	    DeinitDriverData (rp, GfxBase);
	}
    }
    return;
}

void driver_InitView(struct View * View, struct GfxBase * GfxBase)
{
  /* To Do */
  View->DxOffset = 0;
  View->DyOffset = 0;
} /* driver_InitView */

void driver_InitVPort(struct ViewPort * ViewPort, struct GfxBase * GfxBase)
{
  /* To Do (maybe even an unnecessary function) */
} /* driver_InitVPort */

ULONG driver_SetWriteMask (struct RastPort * rp, ULONG mask,
			struct GfxBase * GfxBase)
{

    CorrectDriverData (rp, GfxBase);

#warning TODO

    /* For now we do not support bit masking */
    return FALSE;
    
}

void driver_WaitTOF (struct GfxBase * GfxBase)
{
}

void driver_LoadRGB4 (struct ViewPort * vp, UWORD * colors, LONG count,
	    struct GfxBase * GfxBase)
{
    LONG t;

    for (t = 0; t < count; t ++ )
    {
	driver_SetRGB32 (vp, t
	    , (colors[t] & 0x0F00) << 20
	    , (colors[t] & 0x00F0) << 24
	    , (colors[t] & 0x000F) << 28
	    , GfxBase
	);
        
    }
} /* driver_LoadRGB4 */

void driver_LoadRGB32(struct ViewPort * vp, const ULONG * table,
	    struct GfxBase * GfxBase)
{
    LONG t;
    
    EnterFunc(bug("driver_LoadRGB32(vp=%p, table=%p)\n", vp, table));
    
    
    while (*table)
    {
        ULONG count, first;
	
	count = (*table) >> 16;
	first = *table & 0xFFFF;

	table ++;

	for (t=0; t<count; t++)
	{
	    driver_SetRGB32 (vp, t + first
		, table[0]
		, table[1]
		, table[2]
		, GfxBase
	    );

	    table += 3;
	}

    } /* while (*table) */
    ReturnVoid("driver_LoadRGB32");

} /* driver_LoadRGB32 */

struct BitMap * driver_AllocBitMap (ULONG sizex, ULONG sizey, ULONG depth,
	ULONG flags, struct BitMap * friend, struct GfxBase * GfxBase)
{
    struct BitMap * nbm;
    
    
    EnterFunc(bug("driver_AllocBitMap(sizex=%d, sizey=%d, depth=%d, flags=%d, friend=%p)\n",
    	sizex, sizey, depth, flags, friend));

    nbm = AllocMem (sizeof (struct BitMap), MEMF_ANY|MEMF_CLEAR);
    if (nbm)
    {
    	
	    Object *bm_obj;
	    Object *gfxhidd;
	
	    struct TagItem bm_tags[] =
	    {
		{ aHidd_BitMap_Width,		0	},
		{ aHidd_BitMap_Height,		0	},
		{ aHidd_BitMap_Depth,		0	},
		{ aHidd_BitMap_Displayable,	0	},
		{ aHidd_BitMap_Friend,		0	},
		{ TAG_DONE, 0 }
	    };
	    
	    
	
	    D(bug("BitMap struct allocated\n"));
	
	    /* Insert supplied values */
	    bm_tags[0].ti_Data = sizex;
	    bm_tags[1].ti_Data = sizey;
	    bm_tags[2].ti_Data = depth;
	    bm_tags[3].ti_Data = ((flags & BMF_DISPLAYABLE) ? TRUE : FALSE);
	    
	    if (NULL != friend)
	    {
		if (friend->Flags & BMF_AROS_HIDD)
		    bm_tags[4].ti_Data = (IPTR)BM_OBJ(friend);
	    }

	
	    gfxhidd  = SDD(GfxBase)->gfxhidd;
	    D(bug("Gfxhidd: %p\n", gfxhidd));

	    /* Create HIDD bitmap object */
	    if (gfxhidd)
	    {
		bm_obj = HIDD_Gfx_NewBitMap(gfxhidd, bm_tags);
		D(bug("bitmap object: %p\n", bm_obj));
		
		

		if (bm_obj)
		{
		    Object *pf;
		    ULONG graphtype;
		    /* 	It is possible that the HIDD had to allocate
		   	a larger depth than that supplied, so
		   	we should get back the correct depth.
		   	This is because layers.library might
		   	want to allocate offscreen bimaps to
		   	store obscured areas, and then those
		   	offscreen bitmaps should be of the same depth as
		   	the onscreen ones.
		    */
		   
		    pf = HIDD_BM_GetPixelFormat(bm_obj, vHidd_PixFmt_Native);
		   
		    GetAttr(pf, aHidd_PixFmt_Depth, &depth);
		    GetAttr(pf, aHidd_PixFmt_GraphType, &graphtype);
	    	    
		    /* Store it in plane array */
		    BM_OBJ(nbm) = bm_obj;
		    nbm->Rows   = sizey;
		    nbm->BytesPerRow = WIDTH_TO_BYTES(sizex);
		    nbm->Depth  = depth;
		    nbm->Flags  = flags | BMF_AROS_HIDD;
		    
		    /* If this is a displayable bitmap, create a color table for it */

		    if (flags & BMF_DISPLAYABLE) {
		    	BM_PIXTAB(nbm) = AllocMem(AROS_PALETTE_MEMSIZE, MEMF_ANY);
			if (NULL != BM_PIXTAB(nbm)) {
			
			    /* Set this palette to all black by default */
			    
			    HIDDT_Color col;
			    HIDDT_Pixel black_pixel;
			    ULONG i;
			    
			    col.red	= 0;
			    col.green	= 0;
			    col.blue	= 0;
			    col.alpha	= 0;
			    
			    if (vHidd_GT_Palette == graphtype) {
			    
				ULONG numcolors;
			    
				numcolors = 1L << depth;
				if (numcolors > AROS_PALETTE_SIZE)
				    numcolors = AROS_PALETTE_SIZE;
				    
				/* Set palette to all black */
			    	for (i = 0; i < numcolors; i ++) {
				    HIDD_BM_SetColors(BM_OBJ(nbm), &col, i, 1);
				    BM_PIXTAB(nbm)[i] = col.pixval;
				   
				}
				
			    } else if (vHidd_GT_TrueColor == graphtype) {
			    
				/* Get index for first blackpixel */
			    
			    	black_pixel = HIDD_BM_MapColor(BM_OBJ(nbm), &col);
			    
			    	for (i = 0; i < AROS_PALETTE_SIZE; i ++) {
			            BM_PIXTAB(nbm)[i] = black_pixel;
			    	}
			    }
				
			    ReturnPtr("driver_AllocBitMap", struct BitMap *, nbm);
			    
			}
		    }
		    else
		    {
		    	if (friend)
			{
#warning Here we assume that the friend bitmap is a HIDD bitmap		    
			    /* We got a friend bitmap. We inherit its colormap
			       !!! NOTE !!! If this is used after the friend bitmap is freed
			       it means trouble, as the colortab mem
			       will no longer be valid
			    */

			    BM_PIXTAB(nbm) = BM_PIXTAB(friend);
			    ReturnPtr("driver_AllocBitMap", struct BitMap *, nbm);
			    
			}
		    }
		    
		    DisposeObject(bm_obj);
	    
	    
		} /* if (bitmap object allocated) */
		
	    } /* if (gfxhidd) */
	    
	    
	
	FreeMem(nbm, sizeof (struct BitMap));
	
    }

    ReturnPtr("driver_AllocBitMap", struct BitMap *, NULL);
}


/* Minterms and GC drawmodes are in opposite order */
#define MINTERM_TO_GCDRMD(minterm) 	\
((  	  ((minterm & 0x80) >> 3)	\
	| ((minterm & 0x40) >> 1)	\
	| ((minterm & 0x20) << 1)	\
	| ((minterm & 0x10) << 3) )  >> 4 )



struct blit_info
{
    struct BitMap *bitmap;
    ULONG minterm;
    ULONG planemask;
    UBYTE bmdepth;
    ULONG bmwidth;
    
};

#define BI(x) ((struct blit_info *)x)
static VOID bitmap_to_buf(APTR src_info
	, LONG x_src, LONG y_src
	, LONG x_dest, LONG y_dest
	, LONG width, LONG height
	, ULONG *bufptr
	, struct BitMap *dest_bm) /* destination HIDD bitmap */
{

    LONG y;
    HIDDT_Pixel *coltab = BM_PIXTAB(dest_bm);
    
    /* Fill buffer with pixels from bitmap */
    for (y = 0; y < height; y ++)
    {
	LONG x;
	    
	for (x = 0; x < width; x ++)
	{
	    UBYTE pen;
	    
	    pen = getbitmappixel(BI(src_info)->bitmap
		, x + x_src
		, y + y_src
		, BI(src_info)->bmdepth
		, BI(src_info)->planemask);
		
		
		
	    *bufptr ++ = coltab[pen];
//	    kprintf("(%d, %d) pen=%d buf=%d\n", x, y, pen, coltab[pen]);
			

	}
	
    }

}


static VOID buf_to_bitmap(APTR dest_info
	, LONG x_src, LONG y_src
	, LONG x_dest, LONG y_dest
	, ULONG width, ULONG height
	, UBYTE *bufptr
	, struct BitMap *src_bm
)
{
	
    if (BI(dest_info)->minterm ==  0x00C0)
    {
	LONG y;
	for (y = 0; y < height; y ++)
	{
	    LONG x;
	    for (x = 0; x < width; x ++)
	    {
		setbitmappixel(BI(dest_info)->bitmap
		    	, x + x_dest
			, y + y_dest
			, *bufptr ++, BI(dest_info)->bmdepth, BI(dest_info)->planemask
		);


	    }
		
	}

    }
    else
    {
	LONG y;
	    
	for (y = 0; y < height; y ++)
	{
	    LONG x;
		
	    for (x = 0; x < width; x ++)
	    {
		ULONG src = *bufptr ++ , dest = 0;
		ULONG minterm = BI(dest_info)->minterm;

		/* Set the pixel using correct minterm */

		dest = getbitmappixel(BI(dest_info)->bitmap
			, x + x_dest
			, y + y_dest
			, BI(dest_info)->bmdepth
			, BI(dest_info)->planemask
		);

#warning Do reverse coltab lookup	    	
		if (minterm & 0x0010) dest  = ~src & ~dest;
		if (minterm & 0x0020) dest |= ~src & dest;
		if (minterm & 0x0040) dest |=  src & ~dest;
		if (minterm & 0x0080) dest |= src & dest;
		    
		setbitmappixel(BI(dest_info)->bitmap
			, x + x_dest
			, y + y_dest
			, dest, BI(dest_info)->bmdepth
			, BI(dest_info)->planemask
		);

	    }
		
	}
	    
    }
    return;

}

/* General functions for moving blocks of data to or from HIDDs, be it pixelarrays
  or bitmaps. They use a callback-function to get data from amiga/put data to amiga bitmaps/pixelarrays
  
*/	
static VOID amiga2hidd_fast(APTR src_info
	, Object *hidd_gc
	, LONG x_src , LONG	y_src
	, struct BitMap 	*hidd_bm
	, LONG x_dest, LONG y_dest
	, ULONG xsize, ULONG ysize
	, VOID (*fillbuf_hook)()
)
{
    
    
    ULONG tocopy_w,
    	  tocopy_h;
	  
    LONG pixels_left_to_process = xsize * ysize;
	  
    LONG current_x, current_y, next_x, next_y;

    
    next_x = 0;
    next_y = 0;

LOCK_PIXBUF    
    while (pixels_left_to_process)
    {

	/* Get some more pixels from the HIDD */

	current_x = next_x;
	current_y = next_y;
	
	if (NUMPIX < xsize)
	{
	   /* buffer can't hold a single horizontal line, and must 
	      divide each line into several copy-operations */
	    tocopy_w = xsize - current_x;
	    if (tocopy_w > NUMPIX)
	    {
	        /* Not quite finished with current horizontal pixel line */
	    	tocopy_w = NUMPIX;
		next_x += NUMPIX;
	    }
	    else
	    {	/* Start at a new line */
	    	next_x = 0;
		next_y ++;
	    }
	    tocopy_h = 1;
	    
    	}
	else /* We can copy one or several whole horizontal lines at a time */
	{
	    tocopy_h = MIN(NUMPIX / xsize, ysize - current_y);

	    tocopy_w = xsize;

	    next_x = 0;
	    next_y += tocopy_h;
	    
	
	}


	/* Get data */
	fillbuf_hook(src_info
		, current_x + x_src
		, current_y + y_src
		, current_x + x_dest
		, current_y + y_dest
		, tocopy_w, tocopy_h
		, pixel_buf
		, hidd_bm
	);
	
	/* Put it to the HIDD */
	D(bug("Putting box\n"));

	HIDD_BM_PutImage(BM_OBJ(hidd_bm)
		, hidd_gc
		, (UBYTE*)pixel_buf
		, tocopy_w * sizeof (HIDDT_Pixel)
		, x_dest + current_x
		, y_dest + current_y
		, tocopy_w, tocopy_h
		, vHidd_PixFmt_Native32
	);

	D(bug("Box put\n"));

	pixels_left_to_process -= (tocopy_w * tocopy_h);
	
	
    } /* while (pixels left to copy) */
    
ULOCK_PIXBUF    
    
    return;
    
}
	

static VOID hidd2amiga_fast(struct BitMap *hidd_bm
	, LONG x_src , LONG y_src
	, APTR dest_info
	, LONG x_dest, LONG y_dest
	, ULONG xsize, ULONG ysize
	, VOID (*putbuf_hook)()
)
{

    ULONG tocopy_w, tocopy_h;
    
    LONG pixels_left_to_process = xsize * ysize;
    ULONG current_x, current_y, next_x, next_y;
    
    HIDDT_PixelLUT pixlut = { AROS_PALETTE_SIZE, BM_PIXTAB(hidd_bm) };
    
    next_x = 0;
    next_y = 0;
    
LOCK_PIXBUF    

    while (pixels_left_to_process)
    {
	
	current_x = next_x;
	current_y = next_y;
	
	if (NUMLUTPIX < xsize)
	{
	   /* buffer cant hold a single horizontal line, and must 
	      divide each line into copies */
	    tocopy_w = xsize - current_x;
	    if (tocopy_w > NUMLUTPIX)
	    {
	        /* Not quite finished with current horizontal pixel line */
	    	tocopy_w = NUMLUTPIX;
		next_x += NUMLUTPIX;
	    }
	    else
	    {	/* Start at a new line */
	    
	    	next_x = 0;
		next_y ++;
	    }
	    tocopy_h = 1;
	    
    	}
    	else
    	{
	    tocopy_h = MIN(NUMLUTPIX / xsize, ysize - current_y);
	    tocopy_w = xsize;

	    next_x = 0;
	    next_y += tocopy_h;
	    
    	}
	
	
	/* Get some more pixels from the HIDD */
	HIDD_BM_GetImageLUT(BM_OBJ(hidd_bm)
		, (UBYTE *)pixel_buf
		, tocopy_w
		, x_src + current_x
		, y_src + current_y
		, tocopy_w, tocopy_h
		, &pixlut);


	/*  Write pixels to the destination */
	putbuf_hook(dest_info
		, current_x + x_src
		, current_y + y_src
		, current_x + x_dest
		, current_y + y_dest
		, tocopy_w, tocopy_h
		, (UBYTE *)pixel_buf
		, hidd_bm
	);
	
	pixels_left_to_process -= (tocopy_w * tocopy_h);

    }
    
ULOCK_PIXBUF
    
    return;
    
}


LONG driver_BltBitMap (struct BitMap * srcBitMap, LONG xSrc,
	LONG ySrc, struct BitMap * destBitMap, LONG xDest,
	LONG yDest, LONG xSize, LONG ySize, ULONG minterm,
	ULONG mask, PLANEPTR tempA, struct GfxBase * GfxBase)
{
    LONG planecnt = 0;
    
    ULONG wSrc, wDest;
    ULONG x;
    ULONG depth;
    
    Object *tmp_gc;

    EnterFunc(bug("driver_BltBitMap()\n"));
	

/* kprintf("BltBitMap(%p, %d, %d, %p, %d, %d, %d, %d, %x)\n"
		,srcBitMap, xSrc, ySrc, destBitMap, xDest, yDest, xSize, ySize, minterm);

		
kprintf("Amiga to Amiga, wSrc=%d, wDest=%d\n",
		wSrc, wDest);
*/	
    wSrc  = GetBitMapAttr( srcBitMap, BMA_WIDTH);
    wDest = GetBitMapAttr(destBitMap, BMA_WIDTH);

    /* Clip all blits */

    depth = GetBitMapAttr ( srcBitMap, BMA_DEPTH);
    x     = GetBitMapAttr (destBitMap, BMA_DEPTH);
    if (x < depth)
	depth = x;

    /* Clip X and Y */
    if (xSrc < 0)
    {
	xDest += -xSrc;
	xSize -= -xSrc;
	xSrc = 0;
    }

    if (ySrc < 0)
    {
	yDest += -ySrc;
	ySize -= -ySrc;
	ySrc = 0;
    }

    /* Clip width and height for source and dest */
    if (ySrc + ySize > srcBitMap->Rows)
    {
	ySize = srcBitMap->Rows - ySrc;
    }

    if (yDest + ySize > destBitMap->Rows)
    {
	ySize = destBitMap->Rows - yDest;
    }

    if (xSrc + xSize >= wSrc)
    {
	xSize = wSrc - xSrc;
    }
        
    if (xDest + xSize >= wDest)
    {
    	xSize = wDest - xDest;
    }

    /* If the size is illegal or we need not copy anything, return */
    if (ySize <= 0 || xSize <= 0 || !mask)
	return 0;


    /* Create a temproary GC to use for the bitmaps. Alternatively
       we could share a precreated GC, but this could cause
       deadlocks when locking it
    */
    tmp_gc = obtain_cache_object(SDD(GfxBase)->gc_cache, GfxBase);
    if (NULL == tmp_gc)
    	return 0;
    
    planecnt = depth;
    
    

/*    kprintf("driver_BltBitMap(%p, %d, %d, %p, %d, %d, %d, %d, %d, %d)\n",
    	srcBitMap, xSrc, ySrc, destBitMap, xDest, yDest, xSize, ySize, minterm, mask);
*/    
    /* The posibble cases:
	1) both src and dest is HIDD bitmaps
	2) src is HIDD bitmap, dest is amigabitmap.
     	3) srcBitMap is amiga bitmap, dest is HIDD bitmap.
	
	The case where both src & dest is amiga bitmap is handled
	by BltBitMap() itself.
    */
    	
    
    if (srcBitMap->Flags & BMF_AROS_HIDD)
    {
        Object *src_bm = (Object *)BM_OBJ(srcBitMap);
	
    	if (destBitMap->Flags & BMF_AROS_HIDD)
	{
	    Object *dst_bm = (Object *)BM_OBJ(destBitMap);
	    
	    /* Case 1. */
	    switch (minterm)
	    {
	    	case 0x00:  { /* Clear dest */
 		    struct TagItem tags[] =
		    {
		    	{aHidd_GC_Foreground,	0UL},
			{aHidd_GC_DrawMode,	vHidd_GC_DrawMode_Copy},
			{TAG_DONE, 0UL}
		    };

		    tags[0].ti_Data = BM_PIXTAB(destBitMap)[0];

		    SetAttrs(tmp_gc, tags);
		    HIDD_BM_FillRect(dst_bm
		    	, tmp_gc
		    	, xDest, yDest
			, xDest + xSize - 1
			, yDest + ySize - 1
		    );
			
		    break; }
		

		default: {
		    struct TagItem drmd_tags[] =
		    {
		    	{ aHidd_GC_DrawMode,	0UL},
			{ TAG_DONE, 0UL}
		    };
		    
		    
		    drmd_tags[0].ti_Data = MINTERM_TO_GCDRMD(minterm);

/* kprintf("drawmode %d for minterm %x\n", drmd_tags[0].ti_Data, minterm);

kprintf("Locking BM\n");
*/
/* kprintf("Setting attrs\n");
*/
		    SetAttrs(tmp_gc, drmd_tags);
/* kprintf("copy HIDD to HIDD\n");
*/
		    HIDD_BM_CopyBox( src_bm
		    	, tmp_gc
		    	, xSrc, ySrc
			, dst_bm
			, xDest, yDest
			, xSize, ySize
		    );
		    break;  }
		    
	    } /* switch */
	    
	}
	else
	{
	    /* Case 2. */
	    switch (minterm)
	    {
	        case 0: /* Clear Amiga bitmap */
/* kprintf("clear amiga bitmap\n");
*/		    setbitmapfast(destBitMap, xDest, yDest, xSize, ySize, 0);
		    
		    break;
		    
		default: {

		    struct blit_info bi;
		    struct TagItem drmd_tags[] =
		    {
		    	{ aHidd_GC_DrawMode,	0UL},
			{ TAG_DONE, 0UL}
		    };
		    bi.bitmap	 = destBitMap;
		    bi.minterm	 = minterm;
		    bi.planemask = mask;
		    
		    bi.bmdepth	= GetBitMapAttr(destBitMap, BMA_DEPTH);
		    bi.bmwidth	= GetBitMapAttr(destBitMap, BMA_WIDTH);
		    
/* kprintf("copy HIDD to Amiga:\n");

*/
		    SetAttrs(tmp_gc, drmd_tags);
		    hidd2amiga_fast( srcBitMap
		    	, xSrc, ySrc
			, (APTR) &bi
			, xDest, yDest
			, xSize, ySize
			, buf_to_bitmap
		    );
			
		    break; }
		
	    } /* switch (minterm) */
	    
	    
	}
    }
    else
    {
	Object *dst_bm = (Object *)BM_OBJ(destBitMap);
        
	/* Case 3. */
	switch (minterm)
	{

	    case 0: {/* Clear the destination */

 		    struct TagItem tags[] =
		    {
		    	{aHidd_GC_Foreground,	0UL},
			{aHidd_GC_DrawMode,	vHidd_GC_DrawMode_Copy},
			{TAG_DONE, 0UL}
		    };

#warning Handle offscreen bitmaps
		    tags[0].ti_Data = BM_PIXTAB(destBitMap)[0];

		    SetAttrs(tmp_gc, tags);

/* kprintf("clear HIDD bitmap\n"); 
*/		    
		HIDD_BM_FillRect(dst_bm
		    , tmp_gc
		    , xDest, yDest
		    , xDest + xSize - 1
		    , yDest + ySize - 1
		);

	    
	    break; }
	    

	    default: {
		struct TagItem drmd_tags[] =
		{
		    { aHidd_GC_DrawMode,	0UL},
		    { TAG_DONE, 0UL}
		};

		struct blit_info bi;
		drmd_tags[0].ti_Data = MINTERM_TO_GCDRMD(minterm);
		
		SetAttrs(tmp_gc, drmd_tags);

/* kprintf("copy Amiga to HIDD\n"); 
*/
		bi.bitmap	= srcBitMap;
		bi.minterm	= minterm;
		bi.planemask	= mask;

		bi.bmdepth	= GetBitMapAttr(srcBitMap, BMA_DEPTH);
		bi.bmwidth	= GetBitMapAttr(srcBitMap, BMA_WIDTH);

	    	amiga2hidd_fast( (APTR) &bi
			, tmp_gc
			, xSrc, ySrc
			, destBitMap
			, xDest, yDest
			, xSize, ySize
			, bitmap_to_buf
			
		);

			
	    break; }
	}
    
    }
    
    release_cache_object(SDD(GfxBase)->gc_cache, tmp_gc, GfxBase);

    ReturnInt("driver_BltBitMap", LONG, planecnt);
}

void driver_FreeBitMap (struct BitMap * bm, struct GfxBase * GfxBase)
{
    Object *gfxhidd = SDD(GfxBase)->gfxhidd;
    
    HIDD_Gfx_DisposeBitMap(gfxhidd, (Object *)BM_OBJ(bm));
    
    if (bm->Flags & BMF_DISPLAYABLE)
    {
    	FreeMem(BM_PIXTAB(bm), AROS_PALETTE_MEMSIZE);
    }
    FreeMem(bm, sizeof (struct BitMap));
}


void driver_SetRGB32 (struct ViewPort * vp, ULONG color,
	    ULONG red, ULONG green, ULONG blue,
	    struct GfxBase * GfxBase)
{
    struct BitMap *bm;
   HIDDT_Color hidd_col;
   Object *pf;
   ULONG graphtype;
   
   EnterFunc(bug("driver_SetRGB32(vp=%p, color=%d, r=%x, g=%x, b=%x)\n",
   		vp, color, red, green, blue));
		
   /* Get bitmap object */
   bm = vp->RasInfo->BitMap;
   
   D(bug("Bitmap obj: %p\n", bm_obj));
   
   /* HIDDT_Color entries are UWORD */
   hidd_col.red   = red   >> 16;
   hidd_col.green = green >> 16 ;
   hidd_col.blue  = blue  >> 16;
   
   pf = HIDD_BM_GetPixelFormat(BM_OBJ(bm), vHidd_PixFmt_Native);
   
   GetAttr(pf, aHidd_PixFmt_GraphType, &graphtype);
   
   
   if (vHidd_GT_Palette == graphtype) {
   	HIDD_BM_SetColors(BM_OBJ(bm), &hidd_col, color, 1);
	BM_PIXTAB(bm)[color] = hidd_col.pixval;
	
   } else if (vHidd_GT_TrueColor == graphtype) {
   	BM_PIXTAB(bm)[color] = HIDD_BM_MapColor(BM_OBJ(bm), &hidd_col);
   }
   
   ReturnVoid("driver_SetRGB32");
   

} /* driver_SetRGB32 */

void driver_SetRGB4 (struct ViewPort * vp, ULONG color,
	    ULONG red, ULONG green, ULONG blue,
	    struct GfxBase * GfxBase)
{
 	driver_SetRGB32 (vp, color
	    , (ULONG)(red<<28)
	    , (ULONG)(green<<28)
	    , (ULONG)(blue<<28)
	    , GfxBase
	);
}


static LONG write_pixels_8(struct RastPort *rp, UBYTE *array
	, ULONG modulo
	, LONG xstart, LONG ystart
	, LONG xstop, LONG ystop
	, HIDDT_PixelLUT *pixlut
	, struct GfxBase *GfxBase)

{
	
    struct BitMap *bm = rp->BitMap;
    struct Layer *L = rp->Layer;
    ULONG array_width, array_height;
    
    LONG pixwritten = 0;
    
    Object *gc;
    HIDDT_DrawMode old_drmd;

    struct TagItem gc_tags[] =
    {
	{ aHidd_GC_DrawMode, vHidd_GC_DrawMode_Copy},
	{ TAG_DONE, 0}
    };
    
    
    if (!CorrectDriverData (rp, GfxBase))
	return 0;
	
    gc = GetDriverData(rp)->dd_GC;
    
    GetAttr(gc, aHidd_GC_DrawMode, &old_drmd);
    SetAttrs(gc, gc_tags);
    
    array_width  = modulo;
    array_height = ystop - ystart + 1;
    
    
    if (NULL == L)
    {
        /* No layer, probably a screen */
	
	/* Just put the pixelarray directly onto the HIDD */

	
	HIDD_BM_PutImageLUT(BM_OBJ(bm)
		, gc
		, array
		, array_width
		, xstart, ystart
		, array_width, array_height
		, pixlut
	);

	pixwritten += array_width * array_height;
	
    }
    else
    {
    	/* Window rastport, we need to clip th operation */
	
        struct ClipRect *CR;
	WORD xrel;
        WORD yrel;
	struct Rectangle towrite, intersect;
	
	LockLayerRom( L );
	
	CR = L->ClipRect;
	xrel = L->bounds.MinX;
	yrel = L->bounds.MinY;
	
	towrite.MinX = xstart + xrel;
	towrite.MinY = ystart + yrel;
	towrite.MaxX = xstop  + xrel;
	towrite.MaxY = ystop  + yrel;
	
	for (;NULL != CR; CR = CR->Next)
	{
	    D(bug("Cliprect (%d, %d, %d, %d), lobs=%p\n",
	    	CR->bounds.MinX, CR->bounds.MinY, CR->bounds.MaxX, CR->bounds.MaxY,
		CR->lobs));
		
	    /* Does this cliprect intersect with area to blit ? */
	    if (andrectrect(&CR->bounds, &towrite, &intersect))
	    {
	        ULONG inter_width  = intersect.MaxX - intersect.MinX + 1;
		ULONG inter_height = intersect.MaxY - intersect.MinY + 1;
		LONG array_rel_x = intersect.MinX - towrite.MinX;
		LONG array_rel_y = intersect.MinY - towrite.MinY;
		
	        if (NULL == CR->lobs)
		{
		    
		    /* Write to the HIDD */
		    
		    HIDD_BM_PutImageLUT(BM_OBJ(bm)
		    	, gc
		    	, array + CHUNKY8_COORD_TO_BYTEIDX(array_rel_x, array_rel_y, array_width)
			, array_width
			, intersect.MinX, intersect.MinY
			, inter_width, inter_height
			, pixlut
		    );
			
		}
		else
		{
		    /* This is the tricky one: render into offscreen cliprect bitmap */
		    if (L->Flags & LAYERSIMPLE)
		    	continue;
		    else if (L->Flags & LAYERSUPER)
		    	kprintf("driver_WritePixelArray8(): Superbitmap not handled yet\n");
		    else
		    {

		    	LONG cr_rel_x	= intersect.MinX - CR->bounds.MinX + ALIGN_OFFSET(CR->bounds.MinX);
		    	LONG cr_rel_y	= intersect.MinY - CR->bounds.MinY;
		    


			HIDD_BM_PutImageLUT(BM_OBJ(CR->BitMap)
			    , gc
			    , array + CHUNKY8_COORD_TO_BYTEIDX(array_rel_x, array_rel_y, array_width)
			    , array_width
			    , cr_rel_x, cr_rel_y
			    , inter_width, inter_height
			    , pixlut
		        );
			
		    } /* If (SMARTREFRESH cliprect) */
		    
		    
		}   /* if (intersecton inside hidden cliprect) */
		
	        pixwritten += inter_width * inter_height;

		
	    } /* if (cliprect intersects with area we want to draw to) */
	    
	} /* while (cliprects to examine) */
	
	UnlockLayerRom( L );
	
    } /* if (not screen rastport) */
    
    /* Reset to preserved drawmode */
    gc_tags[0].ti_Data = old_drmd;
    SetAttrs(gc, gc_tags);
    
    return pixwritten;
}	

LONG driver_WritePixelArray8 (struct RastPort * rp, ULONG xstart,
	    ULONG ystart, ULONG xstop, ULONG ystop, UBYTE * array,
	    struct RastPort * temprp, struct GfxBase * GfxBase)
{

    LONG pixwritten;
    
    HIDDT_PixelLUT pixlut = { AROS_PALETTE_SIZE, BM_PIXTAB(rp->BitMap) };
    
    EnterFunc(bug("driver_WritePixelArray8(%p, %d, %d, %d, %d)\n",
    	rp, xstart, ystart, xstop, ystop));
	
  
    pixwritten = write_pixels_8(rp, array
    	, xstop - xstart + 1 /* modulo */
	, xstart, ystart
	, xstop, ystop
	, &pixlut
	, GfxBase);

    ReturnInt("driver_WritePixelArray8", LONG, pixwritten);
    
} /* driver_WritePixelArray8 */


LONG driver_ReadPixelArray8 (struct RastPort * rp, ULONG xstart,
	    ULONG ystart, ULONG xstop, ULONG ystop, UBYTE * array,
	    struct RastPort * temprp, struct GfxBase * GfxBase)
{
    struct gfx_driverdata *dd;
    struct BitMap *bm = rp->BitMap;
    struct Layer *L = rp->Layer;
    ULONG array_width, array_height;
    HIDDT_PixelLUT pixlut = { AROS_PALETTE_SIZE, BM_PIXTAB(rp->BitMap) };
    
    LONG pixread = 0;
    
    EnterFunc(bug("driver_ReadPixelArray8(%p, %d, %d, %d, %d)\n",
    	rp, xstart, ystart, xstop, ystop));
    
    if (!CorrectDriverData (rp, GfxBase))
	return 0;
	
    dd = GetDriverData(rp);
    
    array_width  = xstop - xstart + 1;
    array_height = ystop - ystart + 1;

    if (NULL == L)
    {
        /* No layer, probably a screen */
	
	/* Just get the pixelarray directly from the HIDD */
	HIDD_BM_GetImageLUT(BM_OBJ(bm)
		, array
		, array_width
		, xstart, ystart
		, array_width, array_height
		, &pixlut
	);
	
	pixread += array_width * array_height;
	
    }
    else
    {
    	/* Window rastport, we need to clip th operation */
	
        struct ClipRect *CR;
	WORD xrel;
        WORD yrel;
	struct Rectangle toread, intersect;
	
	LockLayerRom( L );
	
	CR = L->ClipRect;
	xrel = L->bounds.MinX;
	yrel = L->bounds.MinY;
	
	toread.MinX = xstart + xrel;
	toread.MinY = ystart + yrel;
	toread.MaxX = xstop  + xrel;
	toread.MaxY = ystop  + yrel;
	
	for (;NULL != CR; CR = CR->Next)
	{
	    D(bug("Cliprect (%d, %d, %d, %d), lobs=%p\n",
	    	CR->bounds.MinX, CR->bounds.MinY, CR->bounds.MaxX, CR->bounds.MaxY,
		CR->lobs));
		
	    /* Does this cliprect intersect with area to blit ? */
	    if (andrectrect(&CR->bounds, &toread, &intersect))
	    {
	        ULONG inter_width  = intersect.MaxX - intersect.MinX + 1;
		ULONG inter_height = intersect.MaxY - intersect.MinY + 1;
		LONG array_rel_x = intersect.MinX - toread.MinX;
		LONG array_rel_y = intersect.MinY - toread.MinY;
		
	        if (NULL == CR->lobs)
		{
		    
		    /* Read from the HIDD */

		    HIDD_BM_GetImageLUT(BM_OBJ(bm)
		    	, array + COORD_TO_BYTEIDX(array_rel_x, array_rel_y, array_width)
			, array_width
			, intersect.MinX, intersect.MinY
			, inter_width, inter_height
			, &pixlut
		    );
			

		}
		else
		{
		    /* This is the tricky one: render into offscreen cliprect bitmap */
		    if (L->Flags & LAYERSIMPLE)
		    	continue;
		    else if (L->Flags & LAYERSUPER)
		    	kprintf("driver_ReadPixelArray8(): Superbitmap not handled yet\n");
		    else
		    {
		    	LONG cr_rel_x = intersect.MinX - CR->bounds.MinX + ALIGN_OFFSET(CR->bounds.MinX);
		    	LONG cr_rel_y = intersect.MinY - CR->bounds.MinY;


			HIDD_BM_GetImageLUT(BM_OBJ(CR->BitMap)
			    , array + COORD_TO_BYTEIDX(array_rel_x, array_rel_y, array_width)
			    , array_width
			    , cr_rel_x, cr_rel_y
			    , inter_width, inter_height
			    , &pixlut
		        );
			




		    } /* if (SMARTREFRESH cliprect) */
		    
		} /* if (intersecton inside hidden cliprect) */
		
	        pixread += inter_width * inter_height;

		
	    } /* if (cliprect intersects with area we want to draw to) */
	    
	} /* while (cliprects to examine) */
	
	UnlockLayerRom( L );
	
    } /* if (not screen rastport) */
	
    ReturnInt("driver_ReadPixelArray8", LONG, pixread);
    
} /* driver_WritePixelArray8 */


struct template_info
{
    PLANEPTR source;
    LONG x_src;
    LONG modulo;
    BOOL invertsrc;
};

VOID template_to_buf(struct template_info *ti
	, LONG x_src, LONG y_src
	, LONG x_dest, LONG y_dest
	, ULONG xsize, ULONG ysize
	, ULONG *buf
	, struct BitMap *dest_bm)
{
    UBYTE *srcptr;
    LONG x, y;
    
    EnterFunc(bug("template_to_buf(%p, %d, %d, %d, %d, %p)\n"
    			, ti, x_src, y_src, xsize, ysize, buf));
    /* Find the exact startbyte */
    srcptr = ti->source + XCOORD_TO_BYTEIDX(x_src) + (ti->modulo * y_src);
    
    D(bug("offset (in bytes): %d, modulo=%d, y_src=%d, x_src=%d\n"
    	, XCOORD_TO_BYTEIDX(x_src) + (ti->modulo * y_src), ti->modulo, y_src, x_src ));
    /* Find the exact startbit */
    
    x_src &= 0x07;
    
    
    D(bug("new x_src: %d, srcptr=%p\n", x_src, srcptr));

    for (y = 0; y < ysize; y ++)
    {
	UBYTE *byteptr = srcptr;
    	for (x = 0; x < xsize; x ++)
	{
	    UBYTE mask = XCOORD_TO_MASK(x + x_src);
	    BOOL is_set = ((*byteptr & mask) ? TRUE : FALSE);
	    
	    if (ti->invertsrc)
	    {
	    	is_set = ((is_set == TRUE) ? FALSE : TRUE);
	    }
	    
	    if (is_set)
		*buf = 1UL;
	    else
		*buf = 0UL;
/* D(bug("%d", *buf));
*/	    buf ++;

	    /* Last pixel in this byte ? */
	    if (((x + x_src) & 0x07) == 0x07)
	    {
	    	byteptr ++;
	    }
		
	}
/*	D(bug("\n"));
*/	srcptr += ti->modulo;
    }
    
    D(bug("srcptr is %p\n", srcptr));
    ReturnVoid("template_to_buf");
}

VOID driver_BltTemplate(PLANEPTR source, LONG xSrc, LONG srcMod, struct RastPort * destRP,
	LONG xDest, LONG yDest, LONG xSize, LONG ySize, struct GfxBase *GfxBase)
{
    struct Layer *L = destRP->Layer;
    struct BitMap *bm = destRP->BitMap;
    ULONG width, height;
    Object *gc;
    struct BitMap template_bm;
    
    struct template_info ti;
    
    struct TagItem bm_tags[] = 
    {
    	{ aHidd_BitMap_Width,	xSize },
	{ aHidd_BitMap_Height,	ySize },
	{ aHidd_BitMap_Depth,	1 },
	{ aHidd_BitMap_Displayable, FALSE },
	{ TAG_DONE, 0UL }
    };
    
    struct TagItem gc_tags[] = {
    	{ aHidd_GC_DrawMode,	vHidd_GC_DrawMode_Copy },
	{ TAG_DONE, 0UL }
    };
    HIDDT_DrawMode old_drmd;

    EnterFunc(bug("driver_BltTemplate(%d, %d, %d, %d, %d, %d)\n"
    	, xSrc, srcMod, xDest, yDest, xSize, ySize));
	
    if (!CorrectDriverData(destRP, GfxBase))
    	ReturnVoid("driver_BltTemplate");
	
    gc = GetDriverData(destRP)->dd_GC;
    

    width  = GetBitMapAttr(bm, BMA_WIDTH);
    height = GetBitMapAttr(bm, BMA_HEIGHT);
    
    BM_PIXTAB(&template_bm)	= NULL;
    template_bm.Rows		= height;
    template_bm.BytesPerRow	= WIDTH_TO_BYTES(width);
    template_bm.Depth		= 1;
    template_bm.Flags		= BMF_AROS_HIDD;
    
    
    /* Create an offscreen HIDD bitmap of depth 1 to use in color expansion */
    BM_OBJ(&template_bm) = HIDD_Gfx_NewBitMap(SDD(GfxBase)->gfxhidd, bm_tags);
    if (!BM_OBJ(&template_bm))
    	ReturnVoid("driver_BltTemplate");
	
    /* Copy contents from Amiga bitmap to the offscreen HIDD bitmap */
    ti.source	 = source;
    ti.modulo	 = srcMod;
    ti.invertsrc = ((GetDrMd(destRP) & INVERSVID) ? TRUE : FALSE);

D(bug("Copying template to HIDD offscreen bitmap\n"));


    /* Preserve state */
    GetAttr(gc, aHidd_GC_DrawMode, &old_drmd);
    SetAttrs(gc, gc_tags);

    amiga2hidd_fast( (APTR)&ti
    	, gc
    	, xSrc, 0
	, &template_bm
	, 0, 0
	, xSize, ySize
	, template_to_buf
    );
    
    /* Reset to preserved state */
    gc_tags[0].ti_Data = old_drmd;
    SetAttrs(gc, gc_tags);



    if (NULL == L)
    {
        /* No layer, probably a screen */
	
	/* Blit with color expansion */
	HIDD_BM_BlitColorExpansion( BM_OBJ(bm)
		, gc
		, BM_OBJ(&template_bm)
		, 0, 0
		, xDest, yDest
		, xSize, ySize);
	
    }
    else
    {
    	/* Window rastport, we need to clip the operation */
	
        struct ClipRect *CR;
	WORD xrel;
        WORD yrel;
	struct Rectangle toblit, intersect;
	
	LockLayerRom(L);
	
	CR = L->ClipRect;
	xrel = L->bounds.MinX;
	yrel = L->bounds.MinY;
	
	toblit.MinX = xDest + xrel;
	toblit.MinY = yDest + yrel;
	toblit.MaxX = (xDest + xSize - 1) + xrel;
	toblit.MaxY = (yDest + ySize - 1) + yrel;
	
	for (;NULL != CR; CR = CR->Next)
	{
	    D(bug("Cliprect (%d, %d, %d, %d), lobs=%p\n",
	    	CR->bounds.MinX, CR->bounds.MinY, CR->bounds.MaxX, CR->bounds.MaxY,
		CR->lobs));
		
	    /* Does this cliprect intersect with area to blit ? */
	    if (andrectrect(&CR->bounds, &toblit, &intersect))
	    {
		    
		
	        if (NULL == CR->lobs)
		{
		    LONG  clipped_xsrc, clipped_ysrc;
		    clipped_xsrc = /* xsrc = 0 + */ intersect.MinX - toblit.MinX;
		    clipped_ysrc = /* ysrc = 0 + */ intersect.MinY - toblit.MinY;
		    
		    
		    HIDD_BM_BlitColorExpansion( BM_OBJ(bm)
		    	, gc
			, BM_OBJ(&template_bm)
			, clipped_xsrc, clipped_ysrc
			, intersect.MinX
			, intersect.MinY
			, intersect.MaxX - intersect.MinX + 1
			, intersect.MaxY - intersect.MinY + 1
		    );
		    
		}
		else
		{

		    if (L->Flags & LAYERSIMPLE)
		    	continue;
		    else if (L->Flags & LAYERSUPER)
		    	kprintf("driver_BltTemplate(): Superbitmap not handled yet\n");
		    else
		    {

		    	LONG clipped_xsrc, clipped_ysrc;

			clipped_xsrc = /* xsrc = 0 + */ intersect.MinX - toblit.MinX;
			clipped_ysrc = /* ysrc = 0 + */ intersect.MinY - toblit.MinY;
			
			
		    	HIDD_BM_BlitColorExpansion( BM_OBJ(CR->BitMap)
				, gc
				, BM_OBJ(&template_bm)
				, clipped_xsrc, clipped_ysrc
				, intersect.MinX - CR->bounds.MinX + ALIGN_OFFSET(CR->bounds.MinX)
				, intersect.MinY - CR->bounds.MinY
				, intersect.MaxX - intersect.MinX + 1
				, intersect.MaxY - intersect.MinY + 1
		    	);

		    }

		}
		
	    } /* if (cliprect intersects with area we want to draw to) */
	    
	} /* while (cliprects to examine) */
	
	UnlockLayerRom( L );
	
    } /* if (not screen rastport) */
    
    
    HIDD_Gfx_DisposeBitMap(SDD(GfxBase)->gfxhidd, BM_OBJ(&template_bm));
	
    ReturnVoid("driver_BltTemplate");
}


struct pattern_info
{
    PLANEPTR mask;
    struct RastPort *rp;
    LONG mask_bpr; /* Butes per row */
    
    LONG orig_xmin;
    LONG orig_ymin;
    
    UBYTE dest_depth;
    
    struct GfxBase * gfxbase;
    
};

#define GfxBase (pi->gfxbase)
static VOID pattern_to_buf(struct pattern_info *pi
	, LONG x_src, LONG y_src
	, LONG x_dest, LONG y_dest
	, ULONG xsize, ULONG ysize
	, ULONG *buf
	, struct BitMap *dest_bm)
{

    /* x_src, y_src is the coordinates into the layer. */
    LONG y;
    struct RastPort *rp = pi->rp;
    ULONG apen = GetAPen(rp);
    ULONG bpen = GetBPen(rp);
    UBYTE *apt = (UBYTE *)rp->AreaPtrn;
    HIDDT_Pixel *coltab = BM_PIXTAB(dest_bm);
    
    LONG mask_x, mask_y;
    
    if (pi->mask)
    {
    	mask_x = x_src - pi->orig_xmin;
	mask_y = y_src - pi->orig_ymin;
    }
    

    EnterFunc(bug("pattern_to_buf(%p, %d, %d, %d, %d, %d, %d, %p)\n"
    			, pi, x_src, y_src, x_dest, y_dest, xsize, ysize, buf ));
			

    HIDD_BM_GetImage(BM_OBJ(dest_bm)
    	, (UBYTE *)buf
	, xsize * sizeof (HIDDT_Pixel)
	, x_dest, y_dest
	, xsize, ysize
	, vHidd_PixFmt_Native32
    );

    
    for (y = 0; y < ysize; y ++)
    {
        LONG x;
	
	for (x = 0; x < xsize; x ++)
	{
	    ULONG set_pixel;
	    ULONG pixval;
	    
	    /* Mask supplied ? */
	    if (pi->mask)
	    {
		ULONG idx, mask;


		idx = COORD_TO_BYTEIDX(x + mask_x, y + mask_y, pi->mask_bpr);
		mask = XCOORD_TO_MASK(x + mask_x);
		 
		set_pixel = pi->mask[idx] & mask;
		 
	    }
	    else
	        set_pixel = 1UL;
		
		
	    if (set_pixel)
	    {
	   
	
		if (apt)
		{
		
		   set_pixel = pattern_pen(rp, x + x_src, y + y_src, apen, bpen, &pixval, GfxBase);
		   if (set_pixel)
		   {
		   	D(bug(" s"));
		    	*buf = coltab[pixval];
		   }
		    
		}
		else
		    *buf = apen;
	    
	    } /* if (pixel should be set */
	    

	    D(bug("(%d, %d): %d", x, y, *buf));
	    buf ++;
	    
	} /* for (each column) */
	
    } /* for (each row) */
    
    
    ReturnVoid("pattern_to_buf");
}


#undef GfxBase


VOID driver_BltPattern(struct RastPort *rp, PLANEPTR mask, LONG xMin, LONG yMin,
		LONG xMax, LONG yMax, ULONG byteCnt, struct GfxBase *GfxBase)
{

    struct Layer *L = rp->Layer;
    struct BitMap *bm = rp->BitMap;
    ULONG width, height;
    
    struct pattern_info pi;
    
    Object *gc;
    HIDDT_DrawMode old_drmd;
    struct TagItem gc_tags[] = {
	{ aHidd_GC_DrawMode,	vHidd_GC_DrawMode_Copy	},
	{ TAG_DONE, 0UL }
    };
    
    
    EnterFunc(bug("driver_BltPattern(%d, %d, %d, %d, %d)\n"
    	, xMin, yMin, xMax, yMax, byteCnt));
	
/*kprintf("Entering %s\n",__FUNCTION__);*/
	
    if (!CorrectDriverData(rp, GfxBase))
    	ReturnVoid("driver_BltPattern");

    pi.mask	= mask;
    pi.rp	= rp;
    pi.gfxbase	= GfxBase;
    pi.mask_bpr = byteCnt;
    pi.dest_depth	= GetBitMapAttr(rp->BitMap, BMA_DEPTH);
    
	
    gc = GetDriverData(rp)->dd_GC;
    
    GetAttr(gc, aHidd_GC_DrawMode, &old_drmd);
    SetAttrs(gc, gc_tags);
    
    width  = xMax - xMin + 1;
    height = yMax - yMin + 1;
    
    
    if (NULL == L)
    {
        /* No layer, probably a screen */

	pi.orig_xmin = xMin;
	pi.orig_ymin = yMin;
	
	amiga2hidd_fast( (APTR) &pi
		, gc
		, xMin, yMin
		, bm
		, xMin, yMin
		, width
		, height
		, pattern_to_buf
	);
	
	
    }
    else
    {
    	/* Window rastport, we need to clip the operation */
	
        struct ClipRect *CR;
	WORD xrel;
        WORD yrel;
	struct Rectangle toblit, intersect;
	
	LockLayerRom( L );
	
	CR = L->ClipRect;
	xrel = L->bounds.MinX;
	yrel = L->bounds.MinY;
	
	toblit.MinX = xMin + xrel;
	toblit.MinY = yMin + yrel;
	toblit.MaxX = xMax + xrel;
	toblit.MaxY = yMax + yrel;
	
	for (;NULL != CR; CR = CR->Next)
	{
	    D(bug("Cliprect (%d, %d, %d, %d), lobs=%p\n",
	    	CR->bounds.MinX, CR->bounds.MinY, CR->bounds.MaxX, CR->bounds.MaxY,
		CR->lobs));
		
	    /* Does this cliprect intersect with area to blit ? */
	    if (andrectrect(&CR->bounds, &toblit, &intersect))
	    {
		    
		pi.orig_xmin = xMin;
		pi.orig_ymin = yMin;
		
	        if (NULL == CR->lobs)
		{
		

/*kprintf("VISIBLE!\n");
kprintf("xMin: %d, yMin: %d, xMax: %d, yMax: %d\n",xMin,yMin,xMax,yMax);
kprintf("intersect: %d/%d - %d/%d\n",intersect.MinX, intersect.MinY,
                                     intersect.MaxX, intersect.MaxY);		

kprintf("source coords: %d/%d\n\n",intersect.MinX-xrel, intersect.MinY-yrel);*/

		    amiga2hidd_fast( (APTR) &pi
		    	, gc
		        , intersect.MinX - xrel
		        , intersect.MinY - yrel
			, bm
			, intersect.MinX
			, intersect.MinY
			, intersect.MaxX - intersect.MinX + 1
			, intersect.MaxY - intersect.MinY + 1
			, pattern_to_buf
		    );
		}
		else
		{
		    if (L->Flags & LAYERSIMPLE)
		    	continue;
		    else if (L->Flags & LAYERSUPER)
		    	kprintf("driver_BltPattern(): Superbitmap not handled yet\n");
		    else
		    {



/*kprintf("INVISIBLE! CR->BitMap = %x\n",CR->BitMap);
kprintf("xMin: %d, yMin: %d, xMax: %d, yMax: %d\n",xMin,yMin,xMax,yMax);
kprintf("intersect: %d/%d - %d/%d\n",intersect.MinX, intersect.MinY,
                                     intersect.MaxX, intersect.MaxY);*/

		    	amiga2hidd_fast( (APTR) &pi
				, gc
				, intersect.MinX - xrel
				, intersect.MinY - yrel
				, CR->BitMap
				, intersect.MinX - CR->bounds.MinX + ALIGN_OFFSET(CR->bounds.MinX)
				, intersect.MinY - CR->bounds.MinY
				, intersect.MaxX - intersect.MinX + 1
				, intersect.MaxY - intersect.MinY + 1
				, pattern_to_buf
			);

			
		    }

		}
		
	    } /* if (cliprect intersects with area we want to draw to) */
	    
	} /* while (cliprects to examine) */
	
	UnlockLayerRom( L );
	
    } /* if (not screen rastport) */
    
    gc_tags[0].ti_Data = old_drmd;
    SetAttrs(gc, gc_tags);


    ReturnVoid("driver_BltPattern");
}



VOID driver_WriteChunkyPixels(struct RastPort * rp, ULONG xstart, ULONG ystart,
		ULONG xstop, ULONG ystop, UBYTE * array,
		LONG bytesperrow, struct GfxBase *GfxBase)
{
    driver_WritePixelArray8(rp 
		, xstart, ystart
		, xstop, ystop
		, array, NULL
		, GfxBase
    );

}

LONG driver_ReadPixelLine8 (struct RastPort * rp, ULONG xstart,
			    ULONG ystart, ULONG width,
			    UBYTE * array, struct RastPort * temprp,
			    struct GfxBase *GfxBase)
{
    /* We are lazy, and waste som cycles to be able to reuse what we've
       allready done
     */
    
    return driver_ReadPixelArray8(rp
    	, xstart, ystart
	, xstart + width - 1, 1
	, array, temprp
	, GfxBase
    );
}		    


LONG driver_WritePixelLine8 (struct RastPort * rp, ULONG xstart,
			    ULONG ystart, ULONG width,
			    UBYTE * array, struct RastPort * temprp,
			    struct GfxBase *GfxBase)
{
    /* We are lazy, and waste som cycles to be able to reuse what we've
       allready done
     */
    return driver_WritePixelArray8(rp
    	, xstart, ystart
	, xstart + width - 1, 1
	, array, temprp
	, GfxBase
    );
}


struct layerhookmsg
{
    struct Layer *Layer;
/*  struct Rectangle rect; (replaced by the next line!) */
    WORD MinX, MinY, MaxX, MaxY;
    LONG OffsetX, OffsetY;
};


VOID calllayerhook(struct Hook *h, struct RastPort *rp, struct layerhookmsg *msg)
{
    struct BitMap *bm = rp->BitMap;
    Object *gc;
    
    if (!CorrectDriverData(rp, GfxBase)) 
    	return;
	
	
    gc = GetDriverData(rp)->dd_GC;
    


    if(h == LAYERS_BACKFILL)
    {

        /* Use default backfill */
	if (bm->Flags & BMF_AROS_HIDD)
	{
	     HIDDT_DrawMode old_drmd;
	     HIDDT_Pixel old_fg;
	     
	     struct TagItem gc_tags[] =
	     {
	     	{aHidd_GC_Foreground, 0UL},
		{aHidd_GC_DrawMode,	  vHidd_GC_DrawMode_Copy},
		{TAG_DONE, 0UL}
	     };
	     
	     GetAttr(gc, aHidd_GC_DrawMode,   &old_drmd);
	     GetAttr(gc, aHidd_GC_Foreground, &old_fg);
	     


	     gc_tags[0].ti_Data = BM_PIXTAB(rp->BitMap)[0];

	     SetAttrs(gc, gc_tags);
		    
	     /* Cliprect not obscured, so we may render directly into the display */


	     HIDD_BM_FillRect(BM_OBJ(bm)
	     	, gc
		, msg->MinX, msg->MinY
		, msg->MaxX, msg->MaxY
	     );
	     
	     gc_tags[0].ti_Data = old_fg;
	     gc_tags[1].ti_Data = old_drmd;
	     
	     SetAttrs(gc, gc_tags);
	     
	}
	else
	{
	    setbitmapfast(rp->BitMap
	    	, msg->MinX, msg->MinY
		, msg->MaxX - msg->MinX + 1
		, msg->MaxY - msg->MinY + 1
		, 0
	    );
	}
    }

    else if(h != LAYERS_NOBACKFILL)
    {
	/* Call user specified hook */
	AROS_UFC3(void, h->h_Entry,
	    AROS_UFCA(struct Hook *,         h,    A0),
	    AROS_UFCA(struct RastPort *,     rp,   A2),
	    AROS_UFCA(struct layerhookmsg *, msg, A1)
	);
    }
}

void driver_EraseRect (struct RastPort * rp, LONG x1, LONG y1, LONG x2, LONG y2,
		    struct GfxBase * GfxBase)
{

    ULONG width, height;
    struct Layer *L = rp->Layer;
    struct BitMap *bm = rp->BitMap;
    
    
    EnterFunc(bug("driver_EraseRect(%d, %d, %d, %d)\n", x1, y1, x2, y2));
    if (!CorrectDriverData(rp, GfxBase))
    	ReturnVoid("driver_EraseRect(No driverdata)");

    
    width  = GetBitMapAttr(bm, BMA_WIDTH);
    height = GetBitMapAttr(bm, BMA_HEIGHT);
    
    if (NULL == L)
    {
	struct layerhookmsg msg;
	
	clipagainstbitmap(bm, &x1, &y1, &x2, &y2, GfxBase);
	
	
	/* Use the layerinfo hook */
	msg.Layer = NULL;
	msg.MinX = x1;
	msg.MinY = y1;
	msg.MaxX = x2;
	msg.MaxY = y2;
	
	/* Hook should not use these */
	msg.OffsetX = 0;
	msg.OffsetY = 0;
	
#warning TODO
/*  From where do I get the layerinfo->BlankHook ?
    Maybe put it into rp->LongReserved[1] in
    OpenScreen() ? (Or really inside intui_OpenScreen() )
	calllayerhook(struct Hook *???, rp, &msg);
*/
	
    }
    else
    {
        struct ClipRect *CR;
	WORD xrel;
        WORD yrel;
	struct Rectangle toerase, intersect;
	struct layerhookmsg msg;
	
	struct RastPort *fakeRP = NULL;
	
	LockLayerRom( L );
	
	CR = L->ClipRect;
	xrel = L->bounds.MinX;
	yrel = L->bounds.MinY;
	
	toerase.MinX = x1 + xrel;
	toerase.MinY = y1 + yrel;
	toerase.MaxX = x2 + xrel;
	toerase.MaxY = y2 + yrel;
	
	msg.Layer = L;
	
#warning TODO
/* What should these be ? */	
	msg.OffsetX = 0;
	msg.OffsetY = 0;
	
    
	for (;NULL != CR; CR = CR->Next)
	{
		
	    /* Does this cliprect intersect with area to erase ? */
	    if (andrectrect(&CR->bounds, &toerase, &intersect))
	    {
	    
	        if (NULL == CR->lobs)
		{
		    
		    msg.MinX = intersect.MinX;
		    msg.MinY = intersect.MinY;
		    msg.MaxX = intersect.MaxX;
		    msg.MaxY = intersect.MaxY;


		    calllayerhook(L->BackFill, rp, &msg);
		}
		else
		{
		    if (L->Flags & LAYERSIMPLE)
		    	continue;
		    else if (L->Flags & LAYERSUPER)
		    	kprintf("driver_EraseRect(): Superbitmap not handled yet\n");
		    else
		    {
			if (NULL == fakeRP)
			    fakeRP = CreateRastPort();
		    	if (NULL == fakeRP)
		    	    continue;
		    
		    	fakeRP->BitMap = CR->BitMap;

		    	msg.MinX = intersect.MinX - CR->bounds.MinX + ALIGN_OFFSET(CR->bounds.MinX);
		    	msg.MinY = intersect.MinY - CR->bounds.MinY;
		    	msg.MaxX = msg.MinX + (intersect.MaxX - intersect.MinX);
		    	msg.MaxY = msg.MinY + (intersect.MaxY - intersect.MinY);

		    	calllayerhook(L->BackFill, fakeRP /* rp */, &msg);

		    }

		}

	    }
	    
	}
	
	if (NULL != fakeRP)
	    FreeRastPort(fakeRP);
	    
	UnlockLayerRom( L );
	

    }
    ReturnVoid("driver_EraseRect");

}


/*********** BltMaskBitMapRastPort() ***************************/

struct bltmask_info
{
    PLANEPTR mask;
    LONG mask_xmin;
    LONG mask_ymin;
    ULONG mask_bpr;
    struct BitMap *srcbm;
    struct GfxBase *GfxBase;
};


static VOID bltmask_to_buf(struct bltmask_info *bmi
	, LONG x_src, LONG y_src
	, LONG x_dest, LONG y_dest
	, ULONG xsize, ULONG ysize
	, ULONG *buf
	, struct BitMap *dest_bm)
{	
    /* x_src, y_src is the coordinates int the layer. */
    LONG y;
    UBYTE src_depth;
    HIDDT_Pixel *coltab = BM_PIXTAB(dest_bm);

    EnterFunc(bug("bltmask_to_buf(%p, %d, %d, %d, %d, %p)\n"
    			, bmi, x_src, y_src, xsize, ysize, buf ));

    src_depth = GetBitMapAttr(bmi->srcbm, BMA_DEPTH);
    
    /* We must get the data from the destination bitmap */
    HIDD_BM_GetImage(BM_OBJ(dest_bm)
    	, (UBYTE *)buf
	, xsize * sizeof (HIDDT_Pixel)
	, x_dest, y_dest
	, xsize, ysize
	, vHidd_PixFmt_Native32
    );
			
    
    for (y = 0; y < ysize; y ++)
    {
        LONG x;
	
	for (x = 0; x < xsize; x ++)
	{
	    ULONG set_pixel;
	    
	    ULONG idx, mask;
	    idx = COORD_TO_BYTEIDX(x + bmi->mask_xmin, y + bmi->mask_ymin, bmi->mask_bpr);
	    mask = XCOORD_TO_MASK(x + bmi->mask_xmin);
		 
	    set_pixel = bmi->mask[idx] & mask;
		
	    if (set_pixel)
	    {
		  UBYTE pen;
		  
		  pen = getbitmappixel(bmi->srcbm
		  	, x + x_src
			, y + y_src
			, src_depth
			, 0xFF
		   );
		   
		  *buf = coltab[pen];
	    }
	    
	    buf ++;
	    
	} /* for (each column) */
	
    } /* for (each row) */

    
    ReturnVoid("bltmask_to_buf");
}


#define APPLY_MINTERM(pen, src, dest, minterm) \
	pen = 0;	\
	if ((minterm) & 0x0010)	pen = (~(src) & ~(dest));	\
	if ((minterm) & 0x0020)	pen = (~(src) &  (dest));	\
	if ((minterm) & 0x0040)	pen = ( (src) & ~(dest));	\
	if ((minterm) & 0x0080)	pen = ( (src) &  (dest));
	

VOID driver_BltMaskBitMapRastPort(struct BitMap *srcBitMap
    		, LONG xSrc, LONG ySrc
		, struct RastPort *destRP
		, LONG xDest, LONG yDest
		, ULONG xSize, ULONG ySize
		, ULONG minterm
		, PLANEPTR bltMask
		, struct GfxBase *GfxBase )
{
    ULONG width, height;
    struct Layer *L = destRP->Layer;
    struct BitMap *bm = destRP->BitMap;
    struct bltmask_info bmi;
    
    HIDDT_DrawMode old_drmd;
    Object *gc;
    
    struct TagItem gc_tags[] =
    {
	{ aHidd_GC_DrawMode, 0UL },
	{ TAG_DONE, 0UL }
    };
    
    
    EnterFunc(bug("driver_BltMaskBitMapRastPort(%d, %d, %d, %d, %d, %d)\n"
    		, xSrc, ySrc, xDest, yDest, xSize, ySize));

    if (!CorrectDriverData(destRP, GfxBase))
    	ReturnVoid("driver_BltMaskBitMapRastPort");
    
    gc = GetDriverData(destRP)->dd_GC;
    
    gc_tags[0].ti_Data = MINTERM_TO_GCDRMD(minterm);
    
    GetAttr(gc, aHidd_GC_DrawMode, &old_drmd);
    SetAttrs(gc, gc_tags);

    bmi.mask	= bltMask;
    bmi.srcbm	= srcBitMap;
    bmi.GfxBase	= GfxBase;
    
    bmi.mask_bpr = WIDTH_TO_WORDS(xSize);
    
    /* Set minterm bitmap object */
    
    if (NULL == L)
    {
        /* No layer, probably a screen */
	
	bmi.mask_xmin = 0;
	bmi.mask_ymin = 0;
	
	amiga2hidd_fast( (APTR) &bmi
		, gc
		, xSrc, ySrc
		, bm
		, xDest, yDest
		, xSize
		, ySize
		, bltmask_to_buf
	);
	
    }
    else
    {
        struct ClipRect *CR;
	WORD xrel;
        WORD yrel;
	struct Rectangle toblit, intersect;
	
	LockLayerRom( L );
	
	CR = L->ClipRect;
	xrel = L->bounds.MinX;
	yrel = L->bounds.MinY;
	
	toblit.MinX = xDest + xrel;
	toblit.MinY = yDest + yrel;
	toblit.MaxX = (xDest + xSize - 1) + xrel;
	toblit.MaxY = (yDest + ySize - 1) + yrel;
	
	
	for (;NULL != CR; CR = CR->Next)
	{
	    D(bug("Cliprect (%d, %d, %d, %d), lobs=%p\n",
	    	CR->bounds.MinX, CR->bounds.MinY, CR->bounds.MaxX, CR->bounds.MaxY,
		CR->lobs));
		
	    /* Does this cliprect intersect with area to blit ? */
	    if (andrectrect(&CR->bounds, &toblit, &intersect))
	    {
	        ULONG xoffset = intersect.MinX - toblit.MinX;
		ULONG yoffset = intersect.MinY - toblit.MinY;
		
		bmi.mask_xmin = xoffset;
		bmi.mask_ymin = yoffset;
	    
	        if (NULL == CR->lobs)
		{
		    
		    /* Cliprect not obscured, so we may render directly into the display */
		    amiga2hidd_fast( (APTR) &bmi
		    	, gc
			, xSrc + xoffset, ySrc + yoffset
			, bm
			, intersect.MinX, intersect.MinY
			, width
			, height
			, bltmask_to_buf
		     );
		}
		else
		{
		    /* Render into offscreen cliprect bitmap */
		    if (L->Flags & LAYERSIMPLE)
		    	continue;
		    else if (L->Flags & LAYERSUPER)
		    	kprintf("driver_BltMaskBitMapRastPort(): Superbitmap not handled yet\n");
		    else
		    {
		    
		    
		    
		    	amiga2hidd_fast( (APTR) &bmi
				, gc
				, xSrc + xoffset, ySrc + yoffset
				, CR->BitMap
		    		, intersect.MinX - CR->bounds.MinX + ALIGN_OFFSET(CR->bounds.MinX)
				, intersect.MinY - CR->bounds.MinY
				, width
				, height
				, bltmask_to_buf
		     	);

		    }
		}
	    }
	}
	UnlockLayerRom( L );
	
    }
    
    /* Reset to told drawmode value */
    gc_tags[0].ti_Data = (IPTR)old_drmd;
    SetAttrs(gc, gc_tags);
	

    ReturnVoid("driver_BltMaskBitMapRastPort");
}


static Object *fontbm_to_hiddbm(struct TextFont *font, struct GfxBase *GfxBase)
{
    ULONG width, height;
    Object *bm_obj;
    Object *tmp_gc;
    /* Caclulate sizes for the font bitmap */
    struct TagItem bm_tags[] = {
	{ aHidd_BitMap_Width,		0	},
	{ aHidd_BitMap_Height,		0	},
	{ aHidd_BitMap_Displayable,	FALSE	},
	{ aHidd_BitMap_Depth,		1	},
	{ TAG_DONE,	0UL }
    };
    
    tmp_gc = obtain_cache_object(SDD(GfxBase)->gc_cache, GfxBase);
    if (NULL == tmp_gc)
    	return NULL;

    width  = font->tf_Modulo * 8;
    height = font->tf_YSize;
    
    bm_tags[0].ti_Data = width;
    bm_tags[1].ti_Data = height;
	    
#warning Handle color textfonts
	    
    bm_obj = HIDD_Gfx_NewBitMap(SDD(GfxBase)->gfxhidd, bm_tags);
    if (NULL != bm_obj)
    {
    	struct template_info ti;
    	struct BitMap bm;
	struct TagItem gc_tags[] = {
	    { aHidd_GC_DrawMode,	vHidd_GC_DrawMode_Copy },
	    { TAG_DONE, 0UL }
	};
	
	BM_OBJ(&bm)	= bm_obj;
	BM_PIXTAB(&bm)	= NULL;
	bm.Rows		= height;
	bm.BytesPerRow	= WIDTH_TO_BYTES(width);
	bm.Depth	= 1;
	bm.Flags	= BMF_AROS_HIDD;
	
	ti.source	= font->tf_CharData;
	ti.x_src	= 0;
	ti.modulo	= font->tf_Modulo;
	ti.invertsrc	= FALSE;
		
    	/* Copy the character data into the bitmap */
	SetAttrs(tmp_gc, gc_tags);
	
	amiga2hidd_fast((APTR)&ti
		, tmp_gc
		, 0, 0
		, &bm
		, 0, 0
		, width, height
		, template_to_buf
	);
		
    }
    
    release_cache_object(SDD(GfxBase)->gc_cache, tmp_gc, GfxBase);
    
    return bm_obj;
}



/***********************************************/
/* CYBERGFX CALLS                            ***/

#include "cybergraphics_intern.h"

#include <proto/cybergraphics.h>
#include <cybergraphx/cybergraphics.h>

static HIDDT_StdPixFmt cyber2hidd_pixfmt(UWORD cpf, struct Library *CyberGfxBase);
static UWORD hidd2cyber_pixfmt(HIDDT_StdPixFmt stdpf, struct Library *CyberGfxBase);

LONG driver_WriteLUTPixelArray(APTR srcrect, 
	UWORD srcx, UWORD srcy,
	UWORD srcmod, struct RastPort *rp, APTR ctable,
	UWORD destx, UWORD desty,
	UWORD sizex, UWORD sizey,
	UBYTE ctabformat,
	struct Library *CyberGfxBase)
{
    ULONG depth;
    
    HIDDT_PixelLUT pixlut;
    HIDDT_Pixel pixtab[256];
    
    HIDDT_Color col;
    ULONG i;
    
    LONG pixwritten = 0;
    UBYTE *buf;
    
    
    pixlut.entries	= 256;
    pixlut.pixels	= pixtab;
    
    depth = GetBitMapAttr(rp->BitMap, BMA_DEPTH);
    
    /* This call does only support bitmaps with depth > 8. Use WritePixelArray8
       for other bitmaps
    */
    
    if (depth <= 8) {
    	kprintf("!!! TRYING TO USE WriteLUTPixelArray() ON BITMAP WITH DEPTH < 8\n");
    	return 0;
    }
	
    /* Curently only one format is supported */
    if (CTABFMT_XRGB8 != ctabformat) {
    	kprintf("!!! WriteLUTPixelArray() CALLED WITH UNSUPPORTED CTAB FORMAT %d\n"
		, ctabformat);
    	return 0;
    }
    col.alpha	= 0;
	
    /* Convert the coltab into native pixels */
    for (i = 0; i < 256; i ++) {
    	register ULONG rgb = ((ULONG *)ctable)[i];
    	col.red		= (HIDDT_ColComp)((rgb & 0x00FF0000) >> 8);
	col.green	= (HIDDT_ColComp)(rgb & 0x0000FF00);
	col.blue	= (HIDDT_ColComp)((rgb & 0x000000FF) << 8);
	
	pixtab[i] = HIDD_BM_MapColor(BM_OBJ(rp->BitMap), &col);
    }
    
    buf = (UBYTE *)srcrect;
    
    buf += CHUNKY8_COORD_TO_BYTEIDX(srcx, srcy, srcmod);
    
    pixwritten = write_pixels_8(rp
    	, buf
	, srcmod
	, destx, desty
	, destx + sizex - 1, desty + sizey - 1
	, &pixlut
	, GfxBase);
    
    
    /* Now blit the colors onto the screen */
    
    return pixwritten;
}


LONG driver_WritePixelArray(APTR src, UWORD srcx, UWORD srcy
	, UWORD srcmod, struct RastPort *rp, UWORD destx, UWORD desty
	, UWORD width, UWORD height, UBYTE srcformat, struct Library *CyberGfxBase)
{
     
    struct BitMap *bm = rp->BitMap;
    struct Layer *L = rp->Layer;
//    ULONG array_width, array_height;
    
    HIDDT_StdPixFmt srcfmt_hidd;
    
    ULONG start_offset;
    UBYTE * array = (UBYTE *)src;
    
    LONG pixwritten = 0;
    Object *gc;

    
    start_offset = COORD_TO_BYTEIDX(srcx, srcy, srcmod);
    array += start_offset;
    
    
    if (!CorrectDriverData (rp, GfxBase))
	return 0;
	
    gc = GetDriverData(rp)->dd_GC;
	
    if (RECTFMT_LUT8 == srcformat)
    {
	HIDDT_PixelLUT pixlut = { 256, BM_PIXTAB(rp->BitMap) };
	
    	pixwritten = write_pixels_8(rp
		, array, srcmod
		, destx, desty
		, destx + width - 1, desty + height - 1
		, &pixlut
		, GfxBase);
		
	return pixwritten;
    }
    
    if (RECTFMT_GREY8 == srcformat)
    {
    	kprintf("!!! RECTFMT_GREY8 not yet handled in driver_WritePixelArray\n");
	return 0;
    }
    
    switch (srcformat)
    {
	case RECTFMT_RGB:
	   srcfmt_hidd = vHidd_PixFmt_RGB24;
	   break;
	
	case RECTFMT_RGBA:
	    srcfmt_hidd = vHidd_PixFmt_RGBA32;
	    break;
	
	case RECTFMT_ARGB:
	    srcfmt_hidd = vHidd_PixFmt_ARGB32;
	    break;
	
    }
    
    if (NULL == L)
    {
        /* No layer, probably a screen */
	
	/* Just put the pixelarray directly onto the HIDD */
	
	HIDD_BM_PutImage(BM_OBJ(bm)
		, gc
		, array
		, srcmod
		, destx, desty
		, width, height
		, srcfmt_hidd
	);

	pixwritten += width * height;
    }
    else
    {
    	/* Window rastport, we need to clip the operation */
	
        struct ClipRect *CR;
	WORD xrel;
        WORD yrel;
	struct Rectangle towrite, intersect;
	
	LockLayerRom( L );
	
	CR = L->ClipRect;
	xrel = L->bounds.MinX;
	yrel = L->bounds.MinY;
	
	towrite.MinX = destx + xrel;
	towrite.MinY = desty + yrel;
	towrite.MaxX = (destx + width  - 1) + xrel;
	towrite.MaxY = (desty + height - 1) + yrel;
	
	for (;NULL != CR; CR = CR->Next)
	{
		
	    /* Does this cliprect intersect with area to blit ? */
	    if (andrectrect(&CR->bounds, &towrite, &intersect))
	    {
	        ULONG inter_width  = intersect.MaxX - intersect.MinX + 1;
		ULONG inter_height = intersect.MaxY - intersect.MinY + 1;
		LONG array_rel_x = intersect.MinX - towrite.MinX;
		LONG array_rel_y = intersect.MinY - towrite.MinY;
		
	        if (NULL == CR->lobs)
		{
		    
		    /* Write to the HIDD */
		    
		    HIDD_BM_PutImage(BM_OBJ(bm)
		    	, gc
		    	, array + COORD_TO_BYTEIDX(array_rel_x, array_rel_y, srcmod)
			, srcmod
			, intersect.MinX, intersect.MinY
			, inter_width, inter_height
			, srcfmt_hidd
		    );
			
		}
		else
		{
		    /* This is the tricky one: render into offscreen cliprect bitmap */
		    if (L->Flags & LAYERSIMPLE)
		    	continue;
		    else if (L->Flags & LAYERSUPER)
		    	kprintf("driver_WritePixelArray(): Superbitmap not handled yet\n");
		    else
		    {

		    	LONG cr_rel_x	= intersect.MinX - CR->bounds.MinX + ALIGN_OFFSET(CR->bounds.MinX);
		    	LONG cr_rel_y	= intersect.MinY - CR->bounds.MinY;
	

			HIDD_BM_PutImage(BM_OBJ(CR->BitMap)
			    , gc
			    , array + COORD_TO_BYTEIDX(array_rel_x, array_rel_y, srcmod)
			    , srcmod
			    , cr_rel_x, cr_rel_y
			    , inter_width, inter_height
			    , srcfmt_hidd
		        );
			

		    } /* If (SMARTREFRESH cliprect) */
		    
		    
		}   /* if (intersecton inside hidden cliprect) */
		
	        pixwritten += inter_width * inter_height;

	    } /* if (cliprect intersects with area we want to draw to) */
	    
	} /* while (cliprects to examine) */
	
	UnlockLayerRom( L );
	
    } /* if (not screen rastport) */
    return pixwritten;
}

LONG driver_ReadPixelArray(APTR dst, UWORD destx, UWORD desty
	, UWORD dstmod, struct RastPort *rp, UWORD srcx, UWORD srcy
	, UWORD width, UWORD height, UBYTE dstformat, struct Library *CyberGfxBase)
{
     
    struct BitMap *bm = rp->BitMap;
    struct Layer *L = rp->Layer;
//    ULONG array_width, array_height;
    
    HIDDT_StdPixFmt dstfmt_hidd;
    
    ULONG start_offset;
    UBYTE * array = (UBYTE *)dst;
    
    LONG pixread = 0;
    HIDDT_DrawMode old_drmd;
    Object *gc;

    struct TagItem gc_tags[] =
    {
	{ aHidd_GC_DrawMode, vHidd_GC_DrawMode_Copy},
	{ TAG_DONE, 0}
    };
    
    
    start_offset = COORD_TO_BYTEIDX(destx, desty, dstmod);
    array += start_offset;
    
    
    if (!CorrectDriverData (rp, GfxBase))
	return 0;
	
    gc = GetDriverData(rp)->dd_GC;

   /* Preserve old drawmode */
    GetAttr(gc, aHidd_GC_DrawMode, &old_drmd);
    
    SetAttrs(gc, gc_tags);
    
    
    switch (dstformat)
    {
	case RECTFMT_RGB:
	   dstfmt_hidd = vHidd_PixFmt_RGB24;
	   break;
	
	case RECTFMT_RGBA:
	    dstfmt_hidd = vHidd_PixFmt_RGBA32;
	    break;
	
	case RECTFMT_ARGB:
	    dstfmt_hidd = vHidd_PixFmt_ARGB32;
	    break;
	
    }
    
    
    if (NULL == L)
    {
        /* No layer, probably a screen */
	
	
	HIDD_BM_GetImage(BM_OBJ(bm)
		, array
		, dstmod
		, srcx, srcy
		, width, height
		, dstfmt_hidd
	);

	pixread += width * height;
    }
    else
    {
    	/* Window rastport, we need to clip the operation */
	
        struct ClipRect *CR;
	WORD xrel;
        WORD yrel;
	struct Rectangle towrite, intersect;
	
	LockLayerRom( L );
	
	CR = L->ClipRect;
	xrel = L->bounds.MinX;
	yrel = L->bounds.MinY;
	
	towrite.MinX = srcx + xrel;
	towrite.MinY = srcy + yrel;
	towrite.MaxX = (srcx + width  - 1) + xrel;
	towrite.MaxY = (srcy + height - 1) + yrel;
	
	for (;NULL != CR; CR = CR->Next)
	{
		
	    /* Does this cliprect intersect with area to blit ? */
	    if (andrectrect(&CR->bounds, &towrite, &intersect))
	    {
	        ULONG inter_width  = intersect.MaxX - intersect.MinX + 1;
		ULONG inter_height = intersect.MaxY - intersect.MinY + 1;
		LONG array_rel_x = intersect.MinX - towrite.MinX;
		LONG array_rel_y = intersect.MinY - towrite.MinY;
		
	        if (NULL == CR->lobs)
		{
		    
		    /* Write to the HIDD */
		    
		    HIDD_BM_GetImage(BM_OBJ(bm)
		    	, array + COORD_TO_BYTEIDX(array_rel_x, array_rel_y, dstmod)
			, dstmod
			, intersect.MinX, intersect.MinY
			, inter_width, inter_height
			, dstfmt_hidd
		    );
			
		}
		else
		{
		    /* This is the tricky one: render into offscreen cliprect bitmap */
		    if (L->Flags & LAYERSIMPLE)
		    	continue;
		    else if (L->Flags & LAYERSUPER)
		    	kprintf("driver_WritePixelArray(): Superbitmap not handled yet\n");
		    else
		    {

		    	LONG cr_rel_x	= intersect.MinX - CR->bounds.MinX + ALIGN_OFFSET(CR->bounds.MinX);
		    	LONG cr_rel_y	= intersect.MinY - CR->bounds.MinY;

			HIDD_BM_GetImage(BM_OBJ(CR->BitMap)
			    , array + COORD_TO_BYTEIDX(array_rel_x, array_rel_y, dstmod)
			    , dstmod
			    , cr_rel_x, cr_rel_y
			    , inter_width, inter_height
			    , dstfmt_hidd
		        );
			

		    } /* If (SMARTREFRESH cliprect) */
		    
		    
		}   /* if (intersecton inside hidden cliprect) */
		
	        pixread += inter_width * inter_height;

	    } /* if (cliprect intersects with area we want to draw to) */
	    
	} /* while (cliprects to examine) */
	
	UnlockLayerRom( L );
	
    } /* if (not screen rastport) */
    
    /* restore old gc values */
    gc_tags[0].ti_Data = (IPTR)old_drmd;
    SetAttrs(gc, gc_tags);
    
    return pixread;
}

LONG driver_InvertPixelArray(struct RastPort *rp
	, UWORD destx, UWORD desty, UWORD width, UWORD height
	, struct Library *CyberGfxBase)
{

   return (LONG)fillrect_pendrmd(rp
   	, destx, desty
	, destx + width  - 1
	, desty + height - 1
	, 0	/* Source pen does not matter */
	, vHidd_GC_DrawMode_Invert
	, GfxBase);
}

LONG driver_FillPixelArray(struct RastPort *rp
	, UWORD destx, UWORD desty, UWORD width, UWORD height
	, ULONG pixel, struct Library *CyberGfxBase) 
{
    HIDDT_Color col;
    HIDDT_Pixel pix;
    
    col.alpha	= (HIDDT_ColComp)(pixel >> 24);
    col.red	= (HIDDT_ColComp)((pixel >> 16) & 0x000000FF);
    col.green	= (HIDDT_ColComp)((pixel >> 8) & 0x000000FF);
    col.blue	= (HIDDT_ColComp)(pixel & 0x000000FF);
    
    pix = HIDD_BM_MapColor(BM_OBJ(rp->BitMap), &col);

    return (LONG)fillrect_pendrmd(rp
	, destx, desty
	, destx + width  - 1
	, desty + height - 1
	, pix
	, vHidd_GC_DrawMode_Copy
	, GfxBase
    );
}

LONG driver_MovePixelArray(UWORD srcx, UWORD srcy, struct RastPort *rp
	, UWORD destx, UWORD desty, UWORD width, UWORD height
	, struct Library *CyberGfxBase)
{

    kprintf("driver_MovePixelArray() not implemented yet\n");
    
    return 0;
}



LONG driver_WriteRGBPixel(struct RastPort *rp, UWORD x, UWORD y
	, ULONG pixel, struct Library *CyberGfxBase)
{
    struct BitMap *bm = rp->BitMap;
    struct Layer *L = rp->Layer;
    LONG error = -1;
    
    /* Get the HIDD pixel val */
    HIDDT_Color col;
    HIDDT_Pixel pix;
    
    col.alpha	= (HIDDT_ColComp)(pixel >> 24);
    col.red	= (HIDDT_ColComp)((pixel >> 16) & 0x000000FF);
    col.green	= (HIDDT_ColComp)((pixel >> 8) & 0x000000FF);
    col.blue	= (HIDDT_ColComp)(pixel & 0x000000FF);
    
    pix = HIDD_BM_MapColor(BM_OBJ(rp->BitMap), &col);
   
    if (NULL == L)
    {
    	/* This is a screen */
	HIDD_BM_PutPixel(BM_OBJ(bm), x, y, pix);
	error = 0;
    }
    else
    {
        struct ClipRect *CR;
	LONG absx, absy;
	
	LockLayerRom( L );
	
	CR = L->ClipRect;
	
	absx = x + L->bounds.MinX;
	absy = y + L->bounds.MinY;
	
	for (;NULL != CR; CR = CR->Next)
	{
	
	    if (    absx >= CR->bounds.MinX
	         && absy >= CR->bounds.MinY
		 && absx <= CR->bounds.MaxX
		 && absy <= CR->bounds.MaxY )
	    {
		
	        if (NULL == CR->lobs)
		{
		    
		    /* Write to the HIDD */
		    HIDD_BM_PutPixel(BM_OBJ(bm), absx, absy, pix);
			
		    

		}
		else
		{
		    /* This is the tricky one: render into offscreen cliprect bitmap */
		    if (L->Flags & LAYERSIMPLE)
		    	continue;
		    else if (L->Flags & LAYERSUPER)
		    	kprintf("driver_WriteRGBPixel(): Superbitmap not handled yet\n");
		    else
		    {

			HIDD_BM_PutPixel(BM_OBJ(CR->BitMap)
				, absx - CR->bounds.MinX + ALIGN_OFFSET(CR->bounds.MinX)
				, absy - CR->bounds.MinY
				, pix
			); 


		    } /* If (SMARTREFRESH cliprect) */
		    
		    
		}   /* if (intersecton inside hidden cliprect) */
		
		error = 0;
		break;

	    } /* if (cliprect intersects with area we want to draw to) */
	    
	} /* while (cliprects to examine) */
	
	UnlockLayerRom( L );
    
    }
    
    return error;
}


ULONG driver_ReadRGBPixel(struct RastPort *rp, UWORD x, UWORD y
	, struct Library *CyberGfxBase)
{
    struct BitMap *bm = rp->BitMap;
    struct Layer *L = rp->Layer;
    ULONG retval = 0;
    
    /* Get the HIDD pixel val */
    HIDDT_Color col;
    HIDDT_Pixel pix;
    
   
    if (NULL == L)
    {
    	/* This is a screen */
	pix = HIDD_BM_GetPixel(BM_OBJ(bm), x, y);
    }
    else
    {
        struct ClipRect *CR;
	LONG absx, absy;
	
	LockLayerRom( L );
	
	CR = L->ClipRect;
	
	absx = x + L->bounds.MinX;
	absy = y + L->bounds.MinY;
	
	for (;NULL != CR; CR = CR->Next)
	{
	
	    if (    absx >= CR->bounds.MinX
	         && absy >= CR->bounds.MinY
		 && absx <= CR->bounds.MaxX
		 && absy <= CR->bounds.MaxY )
	    {
		
	        if (NULL == CR->lobs)
		{
		    
		    /* Write to the HIDD */
		    pix = HIDD_BM_GetPixel(BM_OBJ(bm), absx, absy);
			
		}
		else
		{
		    /* This is the tricky one: render into offscreen cliprect bitmap */
		    if (L->Flags & LAYERSIMPLE)
		    	continue;
		    else if (L->Flags & LAYERSUPER)
		    	kprintf("driver_WriteRGBPixel(): Superbitmap not handled yet\n");
		    else
		    {

			pix = HIDD_BM_GetPixel(BM_OBJ(CR->BitMap)
				, absx - CR->bounds.MinX + ALIGN_OFFSET(CR->bounds.MinX)
				, absy - CR->bounds.MinY
			); 


		    } /* If (SMARTREFRESH cliprect) */
		    
		    
		}   /* if (intersecton inside hidden cliprect) */
		
		break;

	    } /* if (cliprect intersects with area we want to draw to) */
	    
	} /* while (cliprects to examine) */
	
	UnlockLayerRom( L );
    
    }
    
    HIDD_BM_UnmapPixel(BM_OBJ(bm), pix, &col);
    
    retval = 	  (col.alpha << 24)
    		| (col.red   << 16)
		| (col.green << 8 )
		| (col.blue);
		
    
    return retval;
}


#warning Implement Display mode attributes in the below function
APTR driver_AllocCModeListTagList(struct TagItem *taglist, struct Library *CyberGfxBase )
{
    struct List *hiddmlist;
    struct TagItem *tag, *tstate;
    
    ULONG minwidth = 0;
    ULONG maxwidth = 0xFFFFFFFF;
    ULONG minheight = 0;
    ULONG maxheight = 0xFFFFFFFF;
    
    struct List *cybermlist = NULL;
    
    Object *gfxhidd;
    
    /* GetModeInfo tags */
    struct TagItem qgmtags[] =  {
    	{ tHidd_GfxMode_MinWidth,	0x0 },
	{ tHidd_GfxMode_MaxWidth,	0xFFFFFFFF },
	{ tHidd_GfxMode_MinHeight,	0x0 },
	{ tHidd_GfxMode_MaxHeight,	0xFFFFFFFF },
	{ tHidd_GfxMode_PixFmts,	(IPTR)NULL },
	{ TAG_DONE, 0UL }
    };
    


    UWORD *cmodelarray = NULL;
    
    gfxhidd = SDD(GfxBase)->gfxhidd;
    
    for (tstate = taglist; (tag = NextTagItem((const struct TagItem **)&tstate)); ) {
	switch (tag->ti_Tag) {
	    case CYBRMREQ_MinWidth:
	    	minwidth = (ULONG)tag->ti_Data;
		break;
		
	    case CYBRMREQ_MaxWidth:
	     	maxwidth = (ULONG)tag->ti_Data;
		break;
		
	    case CYBRMREQ_MinHeight:
	    	minheight = (ULONG)tag->ti_Data;
		break;
		
	    case CYBRMREQ_MaxHeight:
	    	maxheight = (ULONG)tag->ti_Data;
		break;
		
	    case CYBRMREQ_CModelArray:
	    	cmodelarray = (UWORD *)tag->ti_Data;
		break;
		
	    default:
	    	kprintf("!!! UNKNOWN TAG PASSED TO AllocCModeListTagList\n");
		break;
		
	
	} 	
    }
    
    /* Build taglist for the QueryGfxModes call */
    qgmtags[0].ti_Data = minwidth;
    qgmtags[1].ti_Data = maxwidth;
    qgmtags[2].ti_Data = minheight;
    qgmtags[3].ti_Data = maxheight;
    
    /* Ask the HIDD for the modes */
    hiddmlist = HIDD_Gfx_QueryGfxModes(gfxhidd, qgmtags);
    
    if (NULL != hiddmlist) {
	
	/* Allocate the exec list */
	cybermlist = AllocMem(sizeof (struct List), MEMF_CLEAR);
	if (NULL != cybermlist) {
    	    struct ModeNode *hmnode;
	    NEWLIST(cybermlist);
	    /* Convert the modeinfo into an exec list */
	
	    /* NOTE: If HIDD_GfxGetModeInfo returned != NULL, it means
	           we have at least one mode, ie. ine tHidd_GfxMode_Start/Stop pair
	    */	    
	    ForeachNode(hiddmlist, hmnode) {
	    
	    	struct CyberModeNode *cmnode;
		
		UWORD *cyberpixfmts;
		ULONG width, height, depth;
		
		
		/* Check whether the gfxmode is the correct pixel format */
		if (NULL != cmodelarray) {
		    HIDDT_StdPixFmt stdpf;
		    UWORD cyberpf;
		    BOOL found = FALSE;
			/* Get the gfxmode pixelf format */
		    GetAttr(hmnode->gfxMode, aHidd_PixFmt_StdPixFmt, &stdpf);
		
		    cyberpf = hidd2cyber_pixfmt(stdpf, CyberGfxBase);
		    if (cyberpf == (UWORD)-1)
		    	continue;	/* Unknown format */
			
		    for (cyberpixfmts = cmodelarray; *cyberpixfmts; cyberpixfmts ++) {
		    	/* See if the stdpixfmt is present in the array */
			if (*cyberpixfmts == cyberpf) {
			    found = TRUE;
			    break;
			}
			
 		    } /* for (each supplied pixelformat in the cmodelarray) */
		    
		    if (!found)
		    	continue; /* PixFmt not wanted, just continue with next node */
		
		} /* if (cmodelarray supplied in the taglist) */
		
	    	/* Allocate a cybergfx modeinfo struct */
	    
	    	cmnode = AllocMem(sizeof (struct CyberModeNode), MEMF_CLEAR);
	    	if (NULL == cmnode)
		    goto failexit;
		    
		/* Get some info from the HIDD object */
		
		GetAttr(hmnode->gfxMode, aHidd_GfxMode_Width,  &width);
		GetAttr(hmnode->gfxMode, aHidd_GfxMode_Height, &height);
		GetAttr(hmnode->gfxMode, aHidd_PixFmt_Depth,   &depth);
		
		cmnode->Width	= width;
		cmnode->Height	= height;
		cmnode->Depth	= depth;
		cmnode->DisplayTagList = NULL;
		strncpy(cmnode->ModeText, "Blah", DISPLAYNAMELEN);
		
		/* Keep track of the node */
		AddTail(cybermlist, (struct Node *)cmnode);
		
		/* Get a display ID for the mode */
	
	    } /* while (hidd modes to process( */
	
	} /* if (NULL != cybermlist) */
	
    	HIDD_Gfx_ReleaseGfxModes(gfxhidd, hiddmlist);
    } /* if (NULL != hiddmlist) */

    
    return cybermlist;
    
failexit:
    if (NULL != hiddmlist)
     	HIDD_Gfx_ReleaseGfxModes(gfxhidd, hiddmlist);
	
    if (NULL != cybermlist)
     	FreeCModeList(cybermlist);
    
    return NULL;
}


VOID driver_FreeCModeList(struct List *modeList, struct Library *CyberGfxBase)
{
    struct CyberModeNode *node, *safe;
    
    ForeachNodeSafe(modeList, node, safe) {
	Remove((struct Node *)node);
	FreeMem(node, sizeof (struct CyberModeNode));
    }
    
    FreeMem(modeList, sizeof (struct List));
}


ULONG driver_GetCyberMapAttr(struct BitMap *bitMap, ULONG attribute, struct Library *CyberGfxBase)
{
    Object *bm_obj;
    Object *pf;
    
    IPTR retval;
    
    /* We only get info about HIDD bitmaps */
    if (!(bitMap->Flags & BMF_AROS_HIDD))
    	return 0;
	
    bm_obj = BM_OBJ(bitMap);
    
    pf = HIDD_BM_GetPixelFormat(bm_obj, vHidd_PixFmt_Native);
    
    switch (attribute) {
   	case CYBRMATTR_XMOD:
	    GetAttr(bm_obj, aHidd_BitMap_BytesPerRow, &retval);
	    break;
	
   	case CYBRMATTR_BPPIX:
	    GetAttr(pf, aHidd_PixFmt_BytesPerPixel, &retval);
	    break;
	
   	case CYBRMATTR_PIXFMT: {
	    HIDDT_StdPixFmt stdpf;
	    UWORD cpf;
	    GetAttr(pf, aHidd_PixFmt_StdPixFmt, (IPTR *)&stdpf);
	    
	    /* Convert to cybergfx */
	    cpf = hidd2cyber_pixfmt(stdpf, CyberGfxBase);
	    
	    if (cpf == (UWORD)-1) {
	    	kprintf("!!! UNKNOWN PIXEL FORMAT IN GetCyberMapAttr()\n");
	    }
	    
	    retval = (IPTR)cpf;
	    break;
	    
	}
	
   	case CYBRMATTR_WIDTH:
	    GetAttr(bm_obj, aHidd_BitMap_Width, &retval);
	    break;
	
   	case CYBRMATTR_HEIGHT:
	    GetAttr(bm_obj, aHidd_BitMap_Height, &retval);
	    break;
	
   	case CYBRMATTR_DEPTH:
	    GetAttr(pf, aHidd_PixFmt_Depth, &retval);
	    break;
	
   	case CYBRMATTR_ISCYBERGFX: {
	    IPTR depth;
	    
	    GetAttr(pf, aHidd_PixFmt_Depth, &depth);
	    
	    if (depth < 8) {
	    	retval = FALSE;
	    } else {
	    /* We allways have a HIDD bitmap */
	    	retval = TRUE;
	    }
	    break; }
	
   	case CYBRMATTR_ISLINEARMEM:
	    GetAttr(bm_obj, aHidd_BitMap_IsLinearMem, &retval);
	    break;
	
	default:
	kprintf("!!! UNKNOWN ATTRIBUTE PASSED TO GetCyberMapAttr()\n");
	break;
	
	
    } /* switch (attribute) */
    
    return retval;
}


/******************************************/
/* Support stuff for cybergfx             */
/******************************************/

static UWORD hidd2cyber_pixfmt(HIDDT_StdPixFmt stdpf, struct Library *CyberGfxBase)
{
     UWORD cpf = (UWORD)-1;
     
     switch (stdpf) {
	case vHidd_PixFmt_RGB16:
	    cpf = PIXFMT_RGB16;
	    break;
	
	case vHidd_PixFmt_RGB24:
	    cpf = PIXFMT_RGB24;
	    break;
	
	case vHidd_PixFmt_ARGB32:
	    cpf = PIXFMT_ARGB32;
	    break;
	
	case vHidd_PixFmt_RGBA32:
	    cpf = PIXFMT_RGBA32;
	    break;
	
	case vHidd_PixFmt_LUT8:
	    cpf = PIXFMT_LUT8;
	    break;
	    
	default:
	    kprintf("UNKNOWN CYBERGRAPHICS PIXFMT IN cyber2hidd_pixfmt\n");
	    break;
     
    }

    return cpf;     
     
}

static HIDDT_StdPixFmt cyber2hidd_pixfmt(UWORD cpf, struct Library *CyberGfxBase)
{
    HIDDT_StdPixFmt stdpf = vHidd_PixFmt_Unknown;

    switch (cpf) {
	case PIXFMT_RGB16:
	    stdpf = vHidd_PixFmt_RGB16;
	    break;
	
	case PIXFMT_RGB24:
	    stdpf = vHidd_PixFmt_RGB24;
	    break;
	
	case PIXFMT_ARGB32:
	    stdpf = vHidd_PixFmt_ARGB32;
	    break;
	
	case PIXFMT_RGBA32:
	    stdpf = vHidd_PixFmt_RGBA32;
	    break;
	
	case PIXFMT_LUT8:
	    stdpf = vHidd_PixFmt_LUT8;
	    break;
	    
	default:
	    kprintf("UNKNOWN CYBERGRAPHICS PIXFMT IN cyber2hidd_pixfmt\n");
	    break;
	
    }
    
    return stdpf;
    
}
    
