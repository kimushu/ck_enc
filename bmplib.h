// 簡易BMPファイルハンドルライブラリ

#ifndef __bmplib_h_
#define __bmplib_h_

typedef unsigned char	bmp_Byte;
typedef unsigned short	bmp_Word;
typedef unsigned long	bmp_DWord;
typedef bmp_DWord		bmp_RGB32;

#pragma pack(1)						/* パディングを１バイト単位にする */
typedef struct {					/* BMPファイルヘッダ構造体 */
	bmp_Word	bfType;
	bmp_DWord	bfSize;
	bmp_Word	bfReserve1;
	bmp_Word	bfReserve2;
	bmp_DWord	bfOffbits;
	bmp_DWord	biSize;
	bmp_DWord	biWidth;
	bmp_DWord	biHeight;
	bmp_Word	biPlanes;
	bmp_Word	biBitCount;
	bmp_DWord	biCompresson;
	bmp_DWord	biSizeImage;
	bmp_DWord	biXPlesPerMeter;
	bmp_DWord	biYPlesPerMeter;
	bmp_DWord	biClrUsed;
	bmp_DWord	biClrImportant;
} BMPFILEHEAD;
#pragma pack()

typedef struct {
	BMPFILEHEAD	h;				// BMPファイルヘッダ 
	bmp_RGB32 *pRGB;			// ビットマップデータのカレントポインタ 
	bmp_RGB32 *pRGB_begin;		// ビットマップデータの先頭ポインタ 
	bmp_RGB32 *pRGB_end;		// ビットマップデータの最終ポインタ 
} bmp_BITMAP_HANDLE;


#define bmp_RGB32_red(_x)			(((_x)>>16) & 0xff)
#define bmp_RGB32_green(_x)			(((_x)>> 8) & 0xff)
#define bmp_RGB32_blue(_x)			(((_x)>> 0) & 0xff)
#define bmp_RGB32_pack(_r,_g,_b)	( (bmp_RGB32)( (((_r)&0xff)<<16) | (((_g)&0xff)<<8) | ((_b)&0xff) ) )

#define bmp_BMPFILETYPE_ID		(0x4d42)

#define bmp_OK					(0)
#define bmp_ERR_FILEOPEN_FAIL	(-1)
#define bmp_ERR_MALLOC_FAIL		(-2)
#define bmp_ERR_NOTBMPFILE		(-3)
#define bmp_ERR_NOSUPPORTTYPE	(-10)


bmp_BITMAP_HANDLE *bmp_loadbmpfile(const char *,int *);
bmp_BITMAP_HANDLE *bmp_makebuffer(int, int, int *);
void bmp_removehandle(bmp_BITMAP_HANDLE *);
void bmp_wrireppmfile(bmp_BITMAP_HANDLE *,char *,int *);


#endif

