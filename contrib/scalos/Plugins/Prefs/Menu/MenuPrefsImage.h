static const ULONG MenuPrefs_colors[48] =
{
	0x66666666,0x66666666,0x66666666,
	0xaaaaaaaa,0xaaaaaaaa,0xaaaaaaaa,
	0x11111111,0x55555555,0xdddddddd,
	0x55555555,0xaaaaaaaa,0x55555555,
	0x66666666,0xcccccccc,0x44444444,
	0x77777777,0xaaaaaaaa,0xffffffff,
	0x44444444,0xcccccccc,0xffffffff,
	0xffffffff,0x00000000,0x34343434,
	0xffffffff,0xeeeeeeee,0x00000000,
	0xcacacaca,0xd9d9d9d9,0xffffffff,
	0x99999999,0xbbbbbbbb,0xeeeeeeee,
	0x88888888,0xdddddddd,0xffffffff,
	0xeeeeeeee,0xeeeeeeee,0x99999999,
	0xcccccccc,0xcccccccc,0xcccccccc,
	0xeeeeeeee,0xdddddddd,0xdddddddd,
	0xffffffff,0xffffffff,0xffffffff,
};

#define MENUPREFS_WIDTH        20
#define MENUPREFS_HEIGHT       24
#define MENUPREFS_DEPTH         4
#define MENUPREFS_COMPRESSION   0
#define MENUPREFS_MASKING       2

//static const struct BitMapHeader MenuPrefs_header =
//{ 20,24,0,0,4,2,0,0,0,44,44,320,256 };

static const UBYTE MenuPrefs_body[384] = {
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x3f,0xff,0x00,0x00,0x3f,0xff,0x00,0x00,0x3f,0xff,0x00,0x00,0x3f,
0xff,0x00,0x00,0x3f,0xfc,0x00,0x00,0x37,0xff,0x00,0x00,0x3f,0xff,0x00,0x00,
0x37,0xff,0x00,0x00,0x36,0x0a,0x00,0x00,0x3f,0xfb,0x00,0x00,0x37,0xff,0x00,
0x00,0x3f,0xfb,0x00,0x00,0x3f,0xfe,0x00,0x00,0x3f,0xff,0x00,0x00,0x3f,0xff,
0x00,0x00,0x3f,0xff,0x00,0x00,0x33,0xae,0x00,0x00,0x37,0xff,0x00,0x00,0x3f,
0xff,0x00,0x00,0x3f,0xff,0x00,0x00,0x3f,0xcb,0x00,0x00,0x3e,0x3a,0x00,0x00,
0x37,0xff,0x00,0x00,0x37,0xfb,0x00,0x00,0x3f,0xff,0x00,0x00,0x3f,0xfe,0x00,
0x00,0x3f,0xff,0x00,0x00,0x3f,0xff,0x00,0x00,0x37,0x0f,0x00,0x00,0x3f,0xfe,
0x00,0x00,0x37,0xff,0x00,0x00,0x37,0xff,0x00,0x00,0x37,0xdb,0x00,0x00,0x3e,
0x2a,0x00,0x00,0x3f,0x7f,0x00,0x00,0x3f,0x7b,0x00,0x00,0x3f,0xff,0x00,0x00,
0x3f,0xff,0x00,0x00,0x3f,0xff,0x00,0x00,0x3f,0xf0,0x00,0x00,0x32,0x0f,0x00,
0x00,0x33,0xfe,0x00,0x00,0x33,0xff,0x00,0x00,0x3f,0xf1,0x00,0x00,0x37,0xff,
0x00,0x00,0x36,0x0e,0x00,0x00,0x37,0xff,0x00,0x00,0x37,0xf1,0x00,0x00,0x20,
0x09,0x00,0x00,0x3f,0xff,0x00,0x00,0x3f,0xff,0x00,0x00,0x3f,0xf6,0x00,0x00,
0x3f,0xff,0x80,0x00,0x3f,0xfe,0x80,0x00,0x3f,0xff,0x80,0x00,0x3f,0xff,0x00,
0x00,0x3e,0x01,0x40,0x00,0x37,0xfe,0x40,0x00,0x37,0xff,0x40,0x00,0x3f,0xff,
0x00,0x00,0x31,0xf1,0x00,0x00,0x3e,0x0e,0x00,0x00,0x37,0xff,0x00,0x00,0x3b,
0xff,0x00,0x00,0x3c,0x01,0x00,0x00,0x3f,0xfe,0x00,0x00,0x3f,0xff,0x00,0x00,
0x3f,0xff,0x00,0x00,0x38,0x41,0x00,0x00,0x3f,0xbe,0x00,0x00,0x3f,0xff,0x00,
0x00,0x3f,0xff,0x00,0x00,0x25,0xf1,0x00,0x00,0x32,0x0e,0x00,0x00,0x36,0x1f,
0x00,0x00,0x36,0x1f,0x00,0x00,0x3f,0xff,0x00,0x00,0x20,0x00,0x00,0x00,0x3f,
0xff,0x00,0x00,0x3f,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, };
