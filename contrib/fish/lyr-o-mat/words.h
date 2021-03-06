#define __far
#define __stdargs
#include <aros/oldprograms.h>

/*
 *  Source machine generated by GadToolsBox V1.4
 *  which is (c) Copyright 1991,92 Jaba Development
 */

#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/classes.h>
#include <intuition/classusr.h>
#include <intuition/imageclass.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <graphics/displayinfo.h>
#include <graphics/gfxbase.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>
#include <workbench/icon.h>
#include <libraries/asl.h>
#include <time.h>

#if 0
#include <clib/exec_protos.h>
#include <clib/intuition_protos.h>
#include <clib/gadtools_protos.h>
#include <clib/graphics_protos.h>
#include <clib/utility_protos.h>
#include <clib/dos_protos.h>
#include <clib/asl_protos.h>
#include <clib/icon_protos.h>
#else
#include <proto/exec.h>
#include <proto/alib.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/dos.h>
#include <proto/asl.h>
#include <proto/icon.h>
#endif

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <proto/dos.h>
//#include <dos.h>
#include <pragmas/exec_pragmas.h>
#include <pragmas/intuition_pragmas.h>
#include <pragmas/gadtools_pragmas.h>
#include <pragmas/graphics_pragmas.h>
#include <pragmas/utility_pragmas.h>
#include <pragmas/dos_pragmas.h>
#include <pragmas/asl_pragmas.h>
#include <pragmas/icon_pragmas.h>
#define GetString( g )      ((( struct StringInfo * )g->SpecialInfo )->Buffer  )
#define GetNumber( g )      ((( struct StringInfo * )g->SpecialInfo )->LongInt )
#define REMEM (MEMF_PUBLIC | MEMF_CLEAR)
#define LIST_PT 1
#define LIST_CL 2
#define LIST_WD 3
#define MODE_CHECKEXIST 1
#define MODE_CHECKFILE  2
#define MODE_LOADFILE   3
#define MODE_APPENDFILE 4
#define MODE_LOADCONFIG 5
#define MODE_SAVEFILE   3
#define MODE_SAVECONFIG 5
#define LYR_NAME "Lyr-O-Mat V1.1"

#define OUTFILENAME "con:0/50/320/150/Lyr-O-Mat/INACTIVE"
#define GD_templatestring                      4
#define GD_wordlist                            1
#define GD_class_string                        2
#define GD_classlist                           3
#define GD_wordstring                          0
#define GD_templatelist                        5
#define GD_numpat                              6
#define GD_generate                            7
#define GD_addtemplate                         8
#define GD_deltemplate                         9
#define GD_addclass                            10
#define GD_delclass                            11
#define GD_addword                             12
#define GD_delword                             13
#define GD_mode                                14

#define GDX_templatestring                     4
#define GDX_wordlist                           1
#define GDX_class_string                       2
#define GDX_classlist                          3
#define GDX_wordstring                         0
#define GDX_templatelist                       5
#define GDX_numpat                             6
#define GDX_generate                           7
#define GDX_addtemplate                        8
#define GDX_deltemplate                        9
#define GDX_addclass                           10
#define GDX_delclass                           11
#define GDX_addword                            12
#define GDX_delword                            13
#define GDX_mode                               14

#define Project0_CNT 15

extern struct Screen        *Scr;
extern UBYTE                 *PubScreenName;
extern APTR                  VisualInfo;
extern struct Window        *Project0Wnd;
extern struct Gadget        *Project0GList;
extern struct Menu          *Project0Menus;
extern struct IntuiMessage   Project0Msg;
extern struct Gadget        *Project0Gadgets[15];
extern UWORD                 Project0Left;
extern UWORD                 Project0Top;
extern UWORD                 Project0Width;
extern UWORD                 Project0Height;
extern UBYTE                *Project0Wdt;
extern struct TextAttr      *Font, Attr;
extern UWORD                 FontX, FontY;
extern UWORD                 OffX, OffY;
extern struct GfxBase       *GfxBase;
extern struct IntuitionBase *IntuitionBase;
extern struct Library       *GadToolsBase;
extern struct UtilityBase   *UtilityBase;
extern struct Library       *AslBase;
extern struct Library       *IconBase;
extern struct NewMenu        Project0NewMenu[];
extern UWORD                 Project0GTypes[];
extern struct NewGadget      Project0NGad[];
extern IPTR                 Project0GTags[];

extern int templatestringClicked( void );
extern int wordlistClicked( void );
extern int class_stringClicked( void );
extern int classlistClicked( void );
extern int wordstringClicked( void );
extern int templatelistClicked( void );
extern int numpatClicked( void );
extern int generateClicked( void );
extern int addtemplateClicked( void );
extern int deltemplateClicked( void );
extern int addclassClicked( void );
extern int delclassClicked( void );
extern int addwordClicked( void );
extern int delwordClicked( void );
extern int modeClicked( void );
extern int Project0Loadall( void );
extern int Project0Loadpattern( void );
extern int Project0Loadclasses( void );
extern int Project0appendall( void );
extern int Project0appendpattern( void );
extern int Project0appendclasses( void );
extern int Project0saveall( void );
extern int Project0savepattern( void );
extern int Project0saveclasses( void );
extern int Project0savedefault( void );
extern int Project0Item0( void );
extern int Project0ShowInfo( void );
extern int Project0ShowPInfo( void );
extern int Project0ShowCInfo( void );
extern int Project0about( void );
extern int Project0quit( void );
extern int Project0ConWd( void );
extern int Project0VanillaKey( void );
extern int Project0WriteIcon( void );



extern int SetupScreen( void );
extern void CloseDownScreen( void );
extern int HandleProject0IDCMP( void );
extern int Project0CloseWindow( void );
extern int OpenProject0Window( void );
extern void CloseProject0Window( void );

struct wordnode
       {
        struct Node       wn_Node;
        struct classnode *wn_Class;
       };

struct classnode
       {
        struct Node     cl_Node;
        struct List     cl_Words;
        ULONG           cl_NumWords;
       };
       
struct patternnode
       {
        struct Node     pt_Node;
       };       
       
struct WordsInfo
       {
        struct List         pattern;
        struct List         class;
        ULONG               numpattern;
        ULONG               numclass;
        struct classnode   *currentclass;
        struct patternnode *currentpattern;
        struct wordnode    *currentword;
        FILE               *outfile;
        FILE               *printfile;
        UBYTE              *nach;
        UBYTE              s_dir[256];
        UBYTE              s_name[256];
        UBYTE              l_dir[256];
        UBYTE              l_name[256];
        UBYTE              d_dir[256];
        UBYTE              d_name[256];
        UBYTE              icon_name[512];
        UBYTE              outfilename[256];
       };

struct ed                       
       {                        
        unsigned pattern   : 1;
        unsigned class     : 1;
        unsigned word      : 1;
        unsigned s_p       : 1;
        unsigned s_c       : 1;
        unsigned l_p       : 1;
        unsigned l_c       : 1;
        unsigned writeicon : 1; 
        unsigned pmode     : 1;                
       };

union wbstart
      {
       char **args;
       struct WBStartup *msg;
      };

extern struct WordsInfo winfo;
extern ULONG            NumGen;
extern struct ed        edit;
extern __far ULONG            RangeSeed;

      
ULONG __stdargs FastRand(ULONG seed );
/*UWORD __stdargs RangeRand( unsigned long maxValue );*/
void newpattern(UBYTE *name);
void newclass(UBYTE *name);
void newword(UBYTE *name);
void delpattern(void);
void delclass(void);
void delword(void);
void delAll(void); 
struct Node *NumToNode(struct List *list,UWORD Num);
void delwordlist(struct classnode *cl);
void list_off(ULONG which);
void list_on(ULONG which);
ULONG NodeToNum(struct List *list,struct Node *node);
void SaveAll(void);
void RequestFName(void);
int  checkfile(UBYTE *file,ULONG mode);
void saveAll(ULONG mode,UBYTE *title);
void loadAll(ULONG mode,UBYTE *title);
void ReplaceLine(void);
void ReplaceLineA(UBYTE *von);
struct classnode *LookForClass(UBYTE *cls);
void line_out(UBYTE *line);
void LoadApp(int argc,char **argv);

