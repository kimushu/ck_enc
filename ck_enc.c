/* CKコーデックエンコーダ（即席版） */

/*
	<入力ファイル名>_[n].bmp
		n : 0または1で始まる10進数の通し番号。左詰のゼロは自動判別します 

	で示される連番bmpをCKコーデックで圧縮します。
	該当のファイルが存在しない場合、そこで処理を終了します。

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#include "bmplib.h"
#include "wavlib.h"


#define mcu_rgb32					bmp_RGB32  		// RGB:888を32bitでパックした１ピクセル分のデータ 
#define mcu_rgb32_red				bmp_RGB32_red
#define mcu_rgb32_green				bmp_RGB32_green
#define mcu_rgb32_blue				bmp_RGB32_blue
#define mcu_rgb32_pack				bmp_RGB32_pack


unsigned long acm_compless_frame(	// フレームのデータサイズを返す 
		bmp_BITMAP_HANDLE *pBH,		// フレームの画像データ(BMP画像) 
		bmp_BITMAP_HANDLE *pREF,	// 参照フレームバッファのポインタ 
		int qual,					// クオリティ引数（0～6）
		unsigned short *pWork,		// 圧縮データが格納されるワークエリア 
		int rbflush					// 参照フレームバッファをフラッシュ 
	);

#pragma pack (1)
typedef struct {
	unsigned char id[2];			// ヘッダ 
	unsigned char ver[2];			// コーデックバージョン 
	unsigned short x_size;			// 画像の横サイズ 
	unsigned short y_size;			// 画像の縦サイズ 
	unsigned short mcu_n;			// フレームを構成するMCUの個数 
	unsigned short fps;				// フレームレート 
	unsigned long fnum;				// 総フレーム数 
	unsigned char a_codec;			// 音声のコーデック
	unsigned char a_channel;		// 音声のチャネル数
	unsigned short a_rate;			// 音声のサンプリングレート
	char dummy[12];
} ck_header;
#pragma pack ()

#define STRIPBUFFER_SIZE		(512)

#define AUDIO_PCM8				(0x0001)
#define AUDIO_ADPCM4			(0x0020)


const char *scan_inputfile(const char *filename, int frame_num)
{
	FILE *fp;
	char num_str[16],*p_num;
	static char s[1024];

	if (strlen(filename) > 1024-14) {	// ファイル名が長すぎる 
		return NULL;
	}

	sprintf(num_str,"%09d.bmp", frame_num);
	p_num = &num_str[1];

	while( *(p_num-1) == '0' ) {		// 左詰のゼロを削っていく 
		sprintf(s,"%s_%s", filename, p_num);

		fp = fopen(s, "rb");
		if (fp != NULL) break;

		p_num++;
	}

	if (fp != NULL) {
		fclose(fp);
		return s;
	} else {
		return NULL;
	}
}



int main(int argc, char *argv[])
{
	int i,j,fn,gopf;
	int mcu_n,qual,fps,gop,pld;
	char ch;

	ck_header ckh;
	char *inputfile;
	const char *audiofile;
	int file_number;
	unsigned long frame_num,frame_dsize,file_dsize;
	unsigned short *pWork;

	bmp_BITMAP_HANDLE *bmp,*ref;
	int bmp_err;

	FILE *fckh,*fbmp;
	const char *fin_name;
	char fout_name[1024];

	FILE *faud;
	WAVEfmt wave_fmt;
	unsigned long a_drate, a_total, a_lead;
	char payload_buff[512];


	// 使い方 
	if (argc < 2) {
		fprintf(stderr,"usage : ck_enc <file-prefix> [-q<0-6>] [-f<fps>] [-a<audio-file>]\n\n");
		fprintf(stderr,"  <file-prefix>_<n>.bmp の連番BMPをCKコーデックで圧縮します。\n");
		fprintf(stderr,"             ※ <n>は0または1で始まる10進数の通し番号\n");
		fprintf(stderr,"  -q<qual> : 圧縮品質（-q0:最低 - q6:最高）デフォルトは-q4\n");
		fprintf(stderr,"  -f<fps>  : フレームレート デフォルトは-f10.0(10.0fps)\n");
		fprintf(stderr,"  -a<file> : 音声情報を付加する(-pと併用不可)\n");
		fprintf(stderr,"  -l<frame>: 音声情報の先行フレーム数を指定する(デフォルト0)\n");
		fprintf(stderr,"  -g<gop>  : GOPフレーム数 デフォルトは-g15(15フレーム単位)\n");
		fprintf(stderr,"  -p<byte> : フレームペイロードデータを付ける(テスト用)\n\n");
		exit(0);
	}

	qual = 4;
	fps  = 10*256;
	gop  = 15;
	pld  = 0;
	audiofile = NULL;
	a_lead = 0;
	while((ch = getopt(argc, argv, "q:Q:f:F:g:G:a:A:l:L:p:P:")) != -1) {
		switch(ch)
		{
		case 'q':
		case 'Q':
			qual = atoi( optarg );
			break;
		case 'f':
		case 'F':
			fps  = (int)(atof( optarg )*256.0);
			break;
		case 'g':
		case 'G':
			gop  = atoi( optarg );
			break;
		case 'a':
		case 'A':
			audiofile = optarg;
			break;
		case 'l':
		case 'L':
			a_lead = atoi( optarg );
			break;
		case 'p':
		case 'P':
			pld  = atoi( optarg );
			break;
		default:
			fprintf(stderr, "無効なオプション - %s\n", optarg);
			break;
		}
	}


	// ファイル入力 

	file_number = 0;
	inputfile = argv[optind];

	fin_name = scan_inputfile(inputfile, file_number);		// 連番が0からスタートする 
	if (fin_name == NULL) {
		file_number++;
		fin_name = scan_inputfile(inputfile, file_number);	// 連番が1からスタートする 
		if (fin_name == NULL) {
			fprintf(stderr, "[!] 有効な連番ファイルが見つかりません.\n\n");
			exit(-1);
		}
	}

	sprintf(fout_name,"%s.ck\0", inputfile);

	fprintf(stderr,"Input file  : %s_*.bmp\n", inputfile);
	fprintf(stderr,"Output file : %s\n", fout_name);

	bmp = bmp_loadbmpfile(fin_name, &bmp_err);
	if (bmp == NULL) exit(-1);

	fprintf(stderr,"Image size : %d x %d pixel(%dbit color)\n",
						 bmp->h.biWidth, bmp->h.biHeight, bmp->h.biBitCount);

	fprintf(stderr,"Encode Quality : %d\n",qual);
	fprintf(stderr,"Frame rate : %2.2ffps\n",(float)fps/256.0);
	fprintf(stderr,"GOP size : %dframe\n",gop);
	fprintf(stderr,"Payload data : %dbyte/frame\n",pld);

	if (audiofile) {
		RIFFHEADER header;

		fprintf(stderr,"Audio file  : %s\n", audiofile);
		faud = fopen(audiofile, "rb");
		if (faud == NULL) {
			printf("[ ! ] 音声入力ファイル %s が開けません.\n\n", audiofile);
			exit(-1);
		}

		fread(&header, 1, sizeof(header), faud);
		if ((header.chunk.ckID != RIFF_FOURCC_RIFF) ||
			(header.form != RIFF_FOURCC_WAVE)) {
			printf("[ ! ] 音声入力ファイルはWAVEファイルではありません.\n\n");
			exit(-1);
		}

		fread(&header.chunk, 1, sizeof(header.chunk), faud);
		if (header.chunk.ckID != RIFF_FOURCC_fmt) {
			printf("[ ! ] 音声入力ファイルのフォーマットが無効です.\n\n");
			exit(-1);
		}

		fread(&wave_fmt, 1, sizeof(wave_fmt), faud);
		if ((wave_fmt.formatID != 1) ||
			(wave_fmt.channels < 1) ||
			(wave_fmt.channels > 2) ||
			(wave_fmt.bitsPerSample != 8)) {
			printf("[ ! ] この音声入力ファイルのフォーマットには対応していません.\n\n");
			exit(-1);
		}
		fseek(faud, header.chunk.ckSize - sizeof(wave_fmt), SEEK_CUR);

		fprintf(stderr,"Sample rate : %d kHz\n", wave_fmt.samplingRate);
		fprintf(stderr,"              %lf / fps\n", 256.0 * wave_fmt.samplingRate / fps);
		a_total = 0;
		a_drate = wave_fmt.dataRate / fps / 256;
		fprintf(stderr,"Data rate   : %d bytes approx.\n", a_drate);

		while (!feof(faud)) {
			fread(&header.chunk, 1, sizeof(header.chunk), faud);
			if (header.chunk.ckID == RIFF_FOURCC_data) {
				break;
			}
			fseek(faud, header.chunk.ckSize, SEEK_CUR);

			if (feof(faud)) {
				printf("[ ! ] 音声入力ファイルのdataチャンクが無効です.\n\n");
				exit(-1);
			}
		}
	} else {
		faud = NULL;
	}

	mcu_n = ((bmp->h.biWidth + 7) / 8) * ((bmp->h.biHeight + 7) / 8);
	pWork = (unsigned short *)malloc(mcu_n * (48 + 4));
	if (pWork == NULL) exit(-1);

	fckh = fopen(fout_name, "wb");
	if (fckh == NULL) {
		printf("[！] 出力ファイル %s が開けません.\n\n",fout_name);
		exit(-1);
	}


	// ヘッダ作成 
	memset(&ckh, 0, sizeof(ckh));
	ckh.id[0]  = 'C';
	ckh.id[1]  = 'K';
	ckh.ver[0] = '7';
	ckh.ver[1] = (faud != NULL ? '3' : '2');
	ckh.x_size = bmp->h.biWidth;
	ckh.y_size = bmp->h.biHeight;
	ckh.mcu_n  = mcu_n;
	ckh.fps    = fps;
	ckh.fnum   = 0;

	if (faud != NULL) {
		ckh.a_codec = AUDIO_PCM8;
		ckh.a_channel = wave_fmt.channels;
		ckh.a_rate = wave_fmt.samplingRate;
	}

	fseek(fckh, STRIPBUFFER_SIZE, SEEK_SET);

	memset(payload_buff, 0, sizeof(payload_buff));


	// エンコード開始 
	ref = bmp_makebuffer(bmp->h.biWidth, bmp->h.biHeight, &bmp_err);
	if (ref == NULL) exit(-1);

	file_dsize = 0;
	frame_num  = 0;

	fprintf(stderr, "Starting CK-codec encode.\n");

	fn = 1;
	while(1) {
		if (fn >= gop) {
			gopf = 1;
			fn   = 1;
		} else {
			gopf = 0;
			fn++;
		}

		fprintf(stderr,"\rframe %d processing...",frame_num);

		/*
		 * <--512--->
		 * +---------+---------+---------+--------- mrb_value
		 * * |00 aaaaaa|00 aaaaaa|NZ vvvvvv|NZ vvvvvv|(
		 * +---------+---------+---------+---------+
		 * <--------------1frame------------------>
		 *
		 */

		// ペイロードフレーム(音声) 
		//   セクタ先頭2バイトは管理コードなのでペイロードは2バイト減る 
		if (faud) {
			// データレートを調整する
			// (サンプリングレートがFPSで割り切れない場合のため)
			a_drate = ((frame_num + 1 + a_lead) * wave_fmt.samplingRate / (fps / 256)) - a_total;
			i = 0;
			while (i < a_drate) {
				j = (a_drate - i);
				if (j > 508) j = 508;
				payload_buff[2] = j & 0xff;
				payload_buff[3] = j >> 8;
				fread(payload_buff + 4, 1, j, faud);
				fwrite(payload_buff, 1, 512, fckh);
				file_dsize += 512;
				i += j;
				if (feof(faud)) {
					fclose(faud);
					faud = NULL;
					break;
				}
			}
			a_total += a_drate;
			printf("Debug A Frame: 0x%08x, ", ((a_drate + 507) / 508) * 512);
		} else {
			for(i=0 ; i<pld ; i+=510) {
				fwrite(payload_buff, 1, 512, fckh);
				file_dsize += 512;
			}
		}

		// ACMフレーム(画像) 
		frame_dsize = acm_compless_frame(bmp, ref, qual, pWork, gopf);
		fwrite(pWork, 1, frame_dsize, fckh);
		file_dsize += frame_dsize;
		printf("Debug V Frame: 0x%08x\n", frame_dsize);

		// フレームBMPファイルクローズ 
		bmp_removehandle(bmp);
		frame_num++;
		file_number++;

		// 次フレームBMPファイルの検索と読み込み 
		fin_name = scan_inputfile(inputfile, file_number);
		if (fin_name == NULL) break;

		bmp = bmp_loadbmpfile(fin_name, &bmp_err);
		if (bmp == NULL) exit(-1);
	}
	fprintf(stderr,"done.\n");

	fprintf(stderr,"Total time : %.2fs (%d frame)\n", (float)frame_num*256.0/fps, frame_num);
	fprintf(stderr,"Data size : %dkbyte\n",file_dsize/1024);

	bmp_removehandle(ref);

	if (faud) {
		fclose(faud);
	}

	// ヘッダの書き込み 
	ckh.fnum = frame_num;
	fseek(fckh, 0, SEEK_SET);
	fwrite(&ckh, 1, sizeof(ck_header), fckh);

	fclose(fckh);
	free(pWork);
	fprintf(stderr,"\n",file_dsize);

}



/*****************************************
	BMPファイルをメモリに読み込む
 *****************************************/

bmp_BITMAP_HANDLE *bmp_loadbmpfile(
		const char *bmpfilename,
		int *err
	)
{
	int i,j,c,r,g,b;
	int x_size,y_size,pixbyte,lnbyte,bmppos;
	unsigned char *p;
	bmp_BITMAP_HANDLE *pBH;
	bmp_RGB32 *pPix;
	FILE *fbmp;

	// ファイルのオープンとチェック 
	fbmp = fopen(bmpfilename,"rb");
	if (fbmp == NULL) {
		printf("[！] BMPファイル %s が開けません.\n\n",bmpfilename);
		if (err != NULL) *err = bmp_ERR_FILEOPEN_FAIL;
		return (NULL);
	}

	pBH = (bmp_BITMAP_HANDLE *)malloc( sizeof(bmp_BITMAP_HANDLE) );
	if (pBH == NULL) {
		printf("[！] メモリ割り当てに失敗しました.\n\n");
		fclose(fbmp);
		if (err != NULL) *err = bmp_ERR_MALLOC_FAIL;
		return (NULL);
	}

	// ファイルヘッダの読み込みとチェック 
	p = (unsigned char *) &(pBH->h);
	for(i=0 ; i<sizeof(BMPFILEHEAD) ; i++) *p++ = (unsigned char)fgetc(fbmp);

	if (pBH->h.bfType != bmp_BMPFILETYPE_ID) {
		printf("[！] BMPファイルではありません (%02X).\n\n",pBH->h.bfType);
		if (err != NULL) *err = bmp_ERR_NOTBMPFILE;
		free(pBH);
		fclose(fbmp);
		return (NULL);
	}
	if (pBH->h.biBitCount < 16) {
		printf("[！] パレット付きBMPファイルには対応していません (%d).\n\n",pBH->h.biBitCount);
		if (err != NULL) *err = bmp_ERR_NOSUPPORTTYPE;
		free(pBH);
		fclose(fbmp);
		return (NULL);
	}

	// 画像データの読み込み 
	x_size = pBH->h.biWidth;
	y_size = pBH->h.biHeight;
	pixbyte= pBH->h.biBitCount / 8;

	pBH->pRGB_begin = (bmp_RGB32 *)malloc(x_size * y_size * sizeof(bmp_RGB32));
	if (pBH->pRGB_begin == NULL) {
		printf("[！] メモリ割り当てに失敗しました.\n\n");
		free(pBH);
		fclose(fbmp);
		if (err != NULL) *err = bmp_ERR_MALLOC_FAIL;
		return (NULL);
	}

	lnbyte = x_size * pixbyte;
	if((lnbyte % 4) != 0) lnbyte = ((lnbyte / 4) + 1) * 4;

	pPix = pBH->pRGB_begin;
	for(i=0 ; i<y_size ; i++) {
		bmppos = sizeof(BMPFILEHEAD) + lnbyte * ((y_size - 1)- i);
		fseek(fbmp, bmppos, SEEK_SET);

		for(j=0 ; j<x_size ; j++) {
			if (pixbyte == 3 || pixbyte==4) {	// 24bitBMP / 32bitBMP
				b = fgetc(fbmp);
				g = fgetc(fbmp);
				r = fgetc(fbmp);
				if(pixbyte == 4) fgetc(fbmp);	/* ポインタを進めるダミー */

			} else {							// 16ビットBMP(※規格外のためVixの15bppBMPのみ対応) 
				c  = fgetc(fbmp);
				c += fgetc(fbmp) << 8;
				r  = ((c >> 10) & 31) << 3;
				g  = ((c >>  5) & 31) << 3;
				b  = ((c >>  0) & 31) << 3;
			}

			*pPix++ = bmp_RGB32_pack(r,g,b);
		}
	}
	pBH->pRGB_end = pPix - 1;
	pBH->pRGB     = pBH->pRGB_begin;

	fclose(fbmp);
	if (err != NULL) *err = bmp_OK;
	return (pBH);
}


bmp_BITMAP_HANDLE *bmp_makebuffer(
		int x_size, int y_size,
		int *err
	)
{
	int i;
	bmp_BITMAP_HANDLE *pBH;
	bmp_RGB32 *pPix;

	// ハンドラの取得 
	pBH = (bmp_BITMAP_HANDLE *)malloc( sizeof(bmp_BITMAP_HANDLE) );
	if (pBH == NULL) {
		printf("[！] メモリ割り当てに失敗しました.\n\n");
		if (err != NULL) *err = bmp_ERR_MALLOC_FAIL;
		return (NULL);
	}

	// 画像バッファの作成 
	pBH->h.bfType         = bmp_BMPFILETYPE_ID;
	pBH->h.bfSize         = sizeof(bmp_BITMAP_HANDLE);
	pBH->h.biWidth        = x_size;
	pBH->h.biHeight       = y_size;
	pBH->h.biPlanes       = 1;
	pBH->h.biBitCount     = 32;
	pBH->h.biCompresson   = 0;
	pBH->h.biSizeImage    = x_size * y_size * sizeof(bmp_RGB32);
	pBH->h.biXPlesPerMeter= 0;
	pBH->h.biYPlesPerMeter= 0;
	pBH->h.biClrUsed      = 0;
	pBH->h.biClrImportant = 0;

	pBH->pRGB_begin = (bmp_RGB32 *)malloc(x_size * y_size * sizeof(bmp_RGB32));
	if (pBH->pRGB_begin == NULL) {
		printf("[！] メモリ割り当てに失敗しました.\n\n");
		free(pBH);
		if (err != NULL) *err = bmp_ERR_MALLOC_FAIL;
		return (NULL);
	}

	// バッファの初期化 
	pPix = pBH->pRGB_begin;
	for(i=0 ; i<x_size*y_size ; i++) *pPix++ = 0;
	pBH->pRGB_end = pPix - 1;
	pBH->pRGB     = pBH->pRGB_begin;

	if (err != NULL) *err = bmp_OK;
	return (pBH);
}


void bmp_removehandle(
		bmp_BITMAP_HANDLE *pBH
	)
{
	if (pBH != NULL) {
		free(pBH->pRGB);
		free(pBH);
	}
}



/*****************************************
	BMPをCKコーデックの１フレームに圧縮
 *****************************************/

#define ck_flamemark_p0		(0xe790)	// 暫定Pフレームマーカー 
#define ck_flamemark_p1		(0x8191)
#define ck_flamemark_i0		(0x4094)	// 暫定Iフレームマーカー 
#define ck_flamemark_i1		(0x8e8c)

int mcu_skip_check(					// MCUスキップするかどうかのチェック (0 / 1を返す)
		mcu_rgb32 p[8][8],			// 該当MCUブロック 
		mcu_rgb32 r[8][8],			// リファレンスMCUブロック 
		int qual					// クオリティ引数（0～6）
	);

int mcu_encode(						// MCUのワードサイズを返す 
		mcu_rgb32 p[8][8],			// R:G:B = 8:8:8を32bitにパックした8x8ピクセル 
		int qual,					// クオリティ引数（0～6）
		unsigned short *pMCU		// MCUデータ書き込みポインタ 
	);


unsigned long acm_compless_frame(	// フレームのバイトサイズを返す 
		bmp_BITMAP_HANDLE *pBH,
		bmp_BITMAP_HANDLE *pREF,
		int qual,
		unsigned short *pACM_top,
		int rbflush
	)
{
	int i,n,x,y,xx,yy;
	int s_no,s_size,s_mcu;
	unsigned short mcu_code[24];
	unsigned short *pACM,*pSbtop;
	mcu_rgb32 p[8][8],r[8][8];
	int skipmcu_num = 0;

	pACM   = pACM_top;

	pSbtop = pACM++;
	s_size = 1;
	s_mcu  = 0;
	s_no   = 0;


	// フレームデータを圧縮 
	for(y=0 ; y< pBH->h.biHeight ; y+=8) {
		for(x=0 ; x< pBH->h.biWidth ; x+=8) {

			// 8x8ピクセルを切り出す 
			for(yy=0 ; yy<8 ; yy++) {
				for(xx=0 ; xx<8 ; xx++) {
					if((x+xx)< pBH->h.biWidth && (y+yy)< pBH->h.biHeight) {
						i = (y + yy)* pBH->h.biWidth + (x + xx);
						p[yy][xx] = *(pBH->pRGB_begin + i);
						r[yy][xx] = *(pREF->pRGB_begin + i);
					} else {
						p[yy][xx] = 0;
						r[yy][xx] = 0;
					}
				}
			}

			// MCUエンコード
			if ( mcu_skip_check(p, r, qual) && !rbflush ) {		// MCUスキップ 
				mcu_code[0] = 0xffff;
				n = 1;
				skipmcu_num++;
			} else {
				n = mcu_encode(p, qual, mcu_code);

				for(yy=0 ; yy<8 ; yy++) {						// 参照バッファを更新 
					for(xx=0 ; xx<8 ; xx++) {
						if((x+xx)< pBH->h.biWidth && (y+yy)< pBH->h.biHeight) {
							i = (y + yy)* pBH->h.biWidth + (x + xx);
							*(pREF->pRGB_begin + i) = *(pBH->pRGB_begin + i);
						}
					}
				}
			}

			// ストライプバッファに格納 
			if ((s_size+n) > (STRIPBUFFER_SIZE/2)) {

				for( ; s_size < (STRIPBUFFER_SIZE/2) ; s_size++,pACM++) {
					if (s_size & 1) *pACM = 0x8e8c; else *pACM = 0x4094;
				}
				*pSbtop = s_mcu;

				pSbtop = pACM++;
				s_size = 1;
				s_mcu  = 0;
				s_no++;
			}

			for(i=0 ; i<n ; i++) *pACM++ = mcu_code[i];
			s_size += n;
			s_mcu++;
		}
	}

	// フレームの最終ストライプの処理 
	for( ; s_size < (STRIPBUFFER_SIZE/2) ; s_size++,pACM++) {
		if (s_size & 1) *pACM = 0x8191; else *pACM = 0xe790;
	}
	*pSbtop = s_mcu;
/*
	printf("\n");
	if (rbflush) {
		printf("GOP\n");
	} else {
		printf("skip MCU num %d\n",skipmcu_num);
	}
*/
	return ((unsigned long)pACM - (unsigned long)pACM_top);
}



/*****************************************
	８×８個のデータ列からＭＣＵを生成
 *****************************************/

void mcu_conv_yuv420(				// YUV420変換 
		mcu_rgb32 p[8][8],
		float y[8][8],
		float u[4][4],
		float v[4][4]
	);

int dcb_encode(
		int psrc[],					// 元データ配列 (引数)
		int qual,					// 圧縮クオリティ値 (引数)
		unsigned short *pDCB		// DCB書き込みポインタ (引数)
	);


int mcu_encode(						// MCUのワードサイズを返す 
		mcu_rgb32 p[8][8],			// R:G:B = 8:8:8を32bitにパックした8x8ピクセル 
		int qual,					// クオリティ引数（0～6）
		unsigned short *pMCU		// MCUデータ書き込みポインタ 
	)
{
	int i,j,x,y,n,mcu_n;
	float fr,fg,fb,fy,fu,fv,fye;
	float ty[8][8],tu[4][4],tv[4][4];
	int c,psrc[16];

	// 縮小Ｙ成分許容誤差テーブル 
	float fye_sl[10] ={ 2048.0, 2048.0, 1536.0, 1024.0, 576.0, 320.0, 192.0, 0.0, 0.0, 0.0};

	mcu_n = 0;

	// RGB→YUV変換 
	mcu_conv_yuv420(p, ty, tu, tv);

	/* 輝度補正や量子化を行う場合はここに処理を入れる */


	// U成分のDCBを作成 
	for(i=0 ; i<4 ; i++) {
		for(j=0 ; j<4 ; j++) {
			psrc[i*4 + j] = (int)tu[i][j];
		}
	}
	c = dcb_encode(psrc, qual, pMCU);
	mcu_n += c;
	pMCU  += c;

	// V成分のDCBを作成 
	for(i=0 ; i<4 ; i++) {
		for(j=0 ; j<4 ; j++) {
			psrc[i*4 + j] = (int)tv[i][j];
		}
	}
	c = dcb_encode(psrc, qual, pMCU);
	mcu_n += c;
	pMCU  += c;


	// Y成分のDCBを作成 
	fye = 0.0;
	for(i=0 ; i<4 ; i++) {
		for(j=0 ; j<4 ; j++) {
			fy = (ty[i*2][j*2] + ty[i*2][j*2+1] + ty[i*2+1][j*2] + ty[i*2+1][j*2+1]) / 4;
			if (fy < 0.0) fy = 0.0;
			if (fy > 255.0) fy = 255.0;
			psrc[i*4 + j] = (int)fy;

			fye += (ty[i*2  ][j*2  ] - fy)*(ty[i*2  ][j*2  ] - fy) +
				   (ty[i*2  ][j*2+1] - fy)*(ty[i*2  ][j*2+1] - fy) +
				   (ty[i*2+1][j*2  ] - fy)*(ty[i*2+1][j*2  ] - fy) +
				   (ty[i*2+1][j*2+1] - fy)*(ty[i*2+1][j*2+1] - fy);
		}
	}

	if (fye < fye_sl[qual]) {
		c = dcb_encode(psrc, 99, pMCU);
		mcu_n += c;
		pMCU  += c;

	} else {
		for(n=0 ; n<4 ; n++) {
			for(i=0 ; i<4 ; i++) {
				for(j=0 ; j<4 ; j++) {
					x = (n & 1) * 4 + j;
					y = (n >>1) * 4 + i;
					fy = ty[y][x];
					if (fy < 0.0) fy = 0.0;
					if (fy > 255.0) fy = 255.0;
					psrc[i*4 + j] = (int)fy;
				}
			}
			c = dcb_encode(psrc, qual, pMCU);
			mcu_n += c;
			pMCU  += c;
		}

	}

	return (mcu_n);
}


void mcu_conv_yuv420(				// YUV420変換 
		mcu_rgb32 p[8][8],
		float y[8][8],
		float u[4][4],
		float v[4][4]
	)
{
	int i,j;
	float fr,fg,fb,fu,fv;
	float tu[8][8],tv[8][8];

	// RGB→YUV変換 
	for(i=0 ; i<8 ; i++) {
		for(j=0 ; j<8 ; j++) {
			fr = (float)mcu_rgb32_red  ( p[i][j] );
			fg = (float)mcu_rgb32_green( p[i][j] );
			fb = (float)mcu_rgb32_blue ( p[i][j] );
			y[i][j]  =  0.299 * fr + 0.587 * fg + 0.114 * fb;
			tu[i][j] = -0.169 * fr - 0.331 * fg + 0.500 * fb + 128.0;
			tv[i][j] =  0.500 * fr - 0.419 * fg - 0.081 * fb + 128.0;
		}
	}

	// YUV444→YUV420変換 
	for(i=0 ; i<4 ; i++) {
		for(j=0 ; j<4 ; j++) {
			fu = (tu[i*2][j*2] + tu[i*2][j*2+1] + tu[i*2+1][j*2] + tu[i*2+1][j*2+1]) / 4;
			if (fu < 0.0) fu = 0.0;
			if (fu > 255.0) fu = 255.0;
			u[i][j] = fu;
		}
	}

	for(i=0 ; i<4 ; i++) {
		for(j=0 ; j<4 ; j++) {
			fv = (tv[i*2][j*2] + tv[i*2][j*2+1] + tv[i*2+1][j*2] + tv[i*2+1][j*2+1]) / 4;
			if (fv < 0.0) fv = 0.0;
			if (fv > 255.0) fv = 255.0;
			v[i][j] = fv;
		}
	}
}



/***************************************
	４×４個のデータ列をＤＣＢに圧縮
 ***************************************/

/* メディアンフィルタ偏差から重要度を算出 */
/*
	256 +          ----------
	    |         /:        :\
	 e0 -+       / :        : \
	    ||______/  :        :  \
	    |   el  :  :        :   \____________
	    |       :  :        :   :     eh
	  0 +-------:--:--------:---:-----------+
	    0      p0 p1       p2  p3          255 : m
*/
int calc_imp_encoding(int m)		// ピクセル偏差 0～255
{
	int e0 = 128;			// 偏差無しの場合の重要度 
	int el = 64;			// 小偏差側の飽和重要度 
	int eh = 24;			// 大偏差側の飽和重要度 
	int p0 = 8;				// 重要度勾配の開始偏差値 
	int p1 = 48;			// 最大重要度エリアの開始 
	int p2 = 96;			// 最大重要度エリアの終了 
	int p3 = 192;			// 重要度勾配の開始偏差値 

	if (m == 0) {
		return (e0);
	} else if (m < p0) {
		return (e0);
	} else if (m < p1) {
		return ( ((m - p0)*(256 - el))/(p1 - p0) + el );
	} else if (m < p2) {
		return (256);
	} else if (m < p3) {
		return ( ((m - p2)*(eh - 256))/(p3 - p2) + 256 );
	} else {
		return (eh);
	}

	return (0);
}

/* テスト圧縮を行う関数 */
float test_dcb_encode(int an,int a[],int imp[],int psrc[],int ptmp[])
{
	int i,j,m;
	int ep,ep0,ep1;
	unsigned long esum;
	float e=0.0;

	/* a[]は要素 0～an-1 の順で昇順に並んでいる  */
	/* psrc[]は要素 0～15 の順で昇順に並んでいる */
	/* imp[]はpsrc[]の同じ要素の重要度を表す     */

	m = 0;
	esum = 0;
	for(i=0 ; i<16 ; i++) {
		while(m+1 < an) {
			ep0 = psrc[i] - a[m];		if (ep0 < 0) ep0 = -ep0;
			ep1 = psrc[i] - a[m+1];		if (ep1 < 0) ep1 = -ep1;

			if (ep0 <= ep1) break;
			m++;
		}
		ptmp[i] = m;
		ep = psrc[i] - a[m];
		esum += (ep * ep * imp[i]);					// 最小自乗法で誤差を累積 
	}

	e = (float)esum / (255.0 * 255.0);

	return e;
}

/* raw encodingを試行する関数 */
float test_raw_encoding(int imp[],int psrc[],int pdat[])
{
	int i;

	pdat[0] = 0xfeff;
	for(i=0 ; i<16 ; i++) pdat[i+1] = psrc[i];

	return (0.0);
}

/* 0/1bit encodingを試行する関数 */
float test_01bit_encoding(int imp[],int psrc[],int pdat[],float er_qual)
{
	int i,n,s,ep0,ep1,pt,p0,p1;
	int impsum,psum,esum,impsum0,impsum1;
	float e0,e1;							// 最大誤差値は4096 (256×16)

	// ヒストグラム重心を算出 
	impsum = 0;
	psum = 0;
	for(i=0 ; i<16 ; i++) {
		impsum += imp[i];
		psum += psrc[i] * imp[i];
	}
	pt = psum / impsum;
	if ((psum % impsum) > (impsum >> 1)) pt++;

	// 0bit encodingの誤差計算とマッピング 
	esum = 0;
	for(i=0 ; i<16 ; i++) {
		ep0 = psrc[i] - pt;
		esum += (ep0 * ep0 * imp[i]);
	}

	e0 = (float)esum / (255.0 * 255.0);

	pdat[0] = (pt << 8) | 0x00;
	if (e0 < er_qual) return e0;


	// ヒストグラムの２点重心を算出 
	impsum0 = 1;						// 重心位置で2つに分割して個別に重心を計算 
	psum = 0;
	for(i=0 ; i<16 ; i++) {
		if (psrc[i] >= pt) break;
		impsum0 += imp[i];
		psum += psrc[i] * imp[i];
	}
	p0 = psum / impsum0;
	if ((psum % impsum0) > (impsum0 >> 1)) p0++;

	impsum1 = 1;
	psum = 0;
	for(; i<16 ; i++) {
		impsum1 += imp[i];
		psum += psrc[i] * imp[i];
	}
	p1 = psum / impsum1;
	if ((psum % impsum1) > (impsum1 >> 1)) p1++;

	// 1bit encodingの誤差計算とマッピング 
	i = p1 - p0;
	if (i >= 131) {				// n = 0x60 ～ 0x7f : s = +131 ～ +255 (n*4+131)
		n = (i - 131) >> 2;
		if (n > 31) n = 31;
		s = (n << 2) + 131;
		n += 0x60;
	} else if (i >= 66) {		// n = 0x40 ～ 0x5f : s = +66 ～ +128 (n*2+66)
		n = (i - 66) >> 1;
		if (n > 31) n = 31;
		s = (n << 1) + 66;
		n += 0x40;
	} else {					// n = 0x01 ～ 0x3f : s =  +2 ～  +64 (n+1)
		n = i - 1;
		if (n < 1) n = 1; else if (n > 63) n = 63;
		s = n + 1;
	}

	if ( (p1 != (p0 + s))&&(impsum1 > impsum0) ) p0 += (p1 - (p0 + s));

	esum = 0;
	for(i=0 ; i<16 ; i++) {
		ep0 = psrc[i] - p0;
		if (ep0 < 0) ep0 = -ep0;
		ep1 = psrc[i] - (p0 + s);
		if (ep1 < 0) ep1 = -ep1;

		if (ep0 <= ep1) {
			pdat[i+1] = 0;
			esum += (ep0 * ep0 * imp[i]);
		} else {
			pdat[i+1] = 1;
			esum += (ep1 * ep1 * imp[i]);
		}
	}

	e1 = (float)esum / (255.0 * 255.0);

	if (e0 <= e1) return e0;

	pdat[0] = (p0 << 8) | (n << 1) | 0x00;
	return e1;
}

/* 2bit encodingを試行する関数 */
// 開始値と終了値からmode値を逆算する 
int test_2bit_encoding_span(int db,int de,int st)
{
	int p0,p1,span;
	int n,s,code;

	p0 = db;

	if (st) {		// ノンリニアステッピング 
		span = (de - db) / 5;
		code = 0x03;
	} else {		// リニアステッピング 
		span = (de - db) / 3;
		code = 0x01;
	}

	if (span <= 16) {
		s = span - 1;
		if (s < 0) s = 0;
		n = 0x00 | s;
	} else if (span <= 32) {
		s = span - 18;
		if (s < 0) s = 0;
		s >>= 1;
		n = 0x10 | s;
	} else {
		s = span - 35;
		if (s < 0) s = 0;
		s >>= 2;
		if (s > 7) s = 7;
		n = 0x18 | s;
	}

	return ((p0 << 8) | (n << 3) | code);
}

float test_2bit_encoding(int imp[],int psrc[],int pdat[],int reduce,float er_qual)
{
	int pt,i,n,mode,p,s,a[8],ptmp[16];
	int rloop,pb,pe,dlb,dle,dhb,dhe;
	float e,eq = 5000.0;						// 最大誤差値は4096 (256×16)

//	// 縮小Y成分コントロール 
//	if (reduce) {
//		reduce = 2;		// リニアステッピングのみ検索 
//	} else {
//		reduce = 1;		// リニア・ノンリニア両方検索 
//	}

	// 分布データの上限と下限を検索 
	dlb = psrc[0];
	dhb = psrc[15];

	dle = dlb + 12;
	if (dle > dhb-4) dle = dhb-4;
	dlb -= 12;
	if (dlb < 0) dlb = 0;

	dhe = dhb + 12;
	if (dhe > 255) dhe = 255;
	dhb -= 12;
	if (dhb < dle+4) dhb = dle+4;

	if (dlb > dle) dle = dlb;
	if (dhe < dhb) dhb = dhe;

	// データ分布範囲でマッピングを試行 
	for(rloop=0 ; rloop<1 ; rloop++) {
		for(pb=dlb ; pb<=dle ; pb+=2) {				// 開始値をずらしてスキャン 
			for(pe=dhe ; pe>=dhb ; pe-=2) {			// 終了値をずらしてスキャン 

				mode = test_2bit_encoding_span(pb, pe, rloop);

				p = (mode >> 8);
			/* dcb_decodeからコピー */
				if (mode & 0x80) {				// 参照テーブルの作成 
					if (mode & 0x40) {
						s = ((mode & 0x38) >> 1) + 35;
					} else {
						s = ((mode & 0x38) >> 2) + 18;
					}
				} else {
					s = ((mode & 0x78) >> 3) + 1;
				}

				for(i=0 ; i<4 ; i++) {			// リニアステッピング 
					if (p > 255) p = 255;
					a[i] = p;
					p += s;
					if ((mode & 0x02) && i==1) p += (s << 1); // ノンリニアステッピング 
				}
			/* ここまで */

				e = test_dcb_encode(4, a, imp, psrc, ptmp);
				if (e < eq) {						// 誤差が小さいものを選択 
					eq = e;
					if (rloop==0 && reduce) {
						pdat[0] = mode | 0x04;
					} else {
						pdat[0] = mode;
					}
					for(i=0 ; i<16 ; i++) pdat[i+1] = ptmp[i];

					if (eq <= er_qual) return eq;
				}
			}
		}

		if (reduce) break;		// Yrはリニアマッピングのみ 
	}


#if 0
	for(n=0x00 ; n<=0x3f ; n+=reduce) {			// ステッピングをスキャン(type1,type2) 
		for(pt=dlb ; pt<=dle ; pt++) {			// データが存在する範囲を少しずつずらしてスキャン 

			mode = ((n & 0x3e) << 2) | ((n & 1) << 1);
			p    = pt;

			/* dcb_decodeからコピー */
				if (mode & 0x80) {				// 参照テーブルの作成 
					if (mode & 0x40) {
						s = ((mode & 0x38) >> 1) + 35;
					} else {
						s = ((mode & 0x38) >> 2) + 18;
					}
				} else {
					s = ((mode & 0x78) >> 3) + 1;
				}

				for(i=0 ; i<4 ; i++) {			// リニアステッピング 
					if (p > 255) p = 255;
					a[i] = p;
					p += s;
					if ((mode & 0x02) && i==1) p += (s << 1); // ノンリニアステッピング 
				}
			/* ここまで */

			if (dhb <= a[3] && a[3] <= dhe) {	// マッピング予定範囲で試行する 
				e = test_dcb_encode(4, a, imp, psrc, ptmp);
				if (e < eq) {						// 誤差が小さいものを選択 
					eq = e;
					if (reduce == 2) {
						pdat[0] = (pt << 8) | mode | 0x05;
					} else {
						pdat[0] = (pt << 8) | mode | 0x01;
					}
					for(i=0 ; i<16 ; i++) pdat[i+1] = ptmp[i];

					if (eq <= er_qual) return eq;
				}
			}
		}
	}
#endif

	return eq;
}

/* 3bit encodingを試行する関数 */
// 開始値と終了値からmode値を逆算する 
int test_3bit_encoding_span(int db,int de)
{
	int p0,p1,span;
	int n,s,code;

	p0   = db;
	span = (de - db) / 7;

	s = span - 1;
	if (s < 0) s = 0;
	if (s > 31) s = 31;
	n = 0x00 | s;

	return ((p0 << 8) | (n << 3) | 0x07);
}

float test_3bit_encoding(int imp[],int psrc[],int pdat[],float er_qual)
{
	int pt,i,n,mode,p,s,a[8],ptmp[16];
	int pb,pe,dlb,dle,dhb,dhe;
	float e,eq = 5000.0;						// 最大誤差値は4096 (256×16)

	// 分布データの上限と下限を検索 
	dlb = psrc[0];
	dhb = psrc[15];

	dle = dlb + 8;
	if (dle > dhb-8) dle = dhb-8;
	dlb -= 8;
	if (dlb < 0) dlb = 0;

	dhe = dhb + 8;
	if (dhe > 255) dhe = 255;
	dhb -= 8;
	if (dhb < dle+8) dhb = dle+8;

	if (dlb > dle) dle = dlb;
	if (dhe < dhb) dhb = dhe;

	// データ分布範囲でマッピングを試行 
	for(pb=dlb ; pb<=dle ; pb++) {				// 開始値をずらしてスキャン 
		for(pe=dhe ; pe>=dhb ; pe--) {			// 終了値をずらしてスキャン 

			mode = test_3bit_encoding_span(pb, pe);

			p = (mode >> 8);
			/* dcb_decodeからコピー */
				s = ((mode & 0xf8) >> 3) + 1;	// 参照テーブルの作成 

				for(i=0 ; i<8 ; i++) {
					if (p > 255) p = 255;
					a[i] = p;
					p += s;
				}
			/* ここまで */

			e = test_dcb_encode(8, a, imp, psrc, ptmp);
			if (e < eq) {						// 誤差が小さいものを選択 
				eq = e;
				pdat[0] = mode;
				for(i=0 ; i<16 ; i++) pdat[i+1] = ptmp[i];

				if (eq <= er_qual) return eq;
			}
		}
	}


#if 0
	dle = dlb + 16;
	if (dle > (256-8)) dle = (256-8);
	dlb = dlb - 8;
	if (dlb < 0) dlb = 0;

	dhe = dhb + 8;
	if (dhe > 255) dhe = 255;
	dhb = dhb - 16;
	if (dhb < (0+8)) dhb = (0+8);

	// データ分布範囲でマッピングを試行 
	for(n=0x00 ; n<=0x1f ; n++) {				// ステッピングをスキャン
		for(pt=dlb ; pt<=dle ; pt++) {			// データが存在する範囲を少しずつずらしてスキャン 
			mode = (n << 3);
			p    = pt;

			/* dcb_decodeからコピー */
				s = ((mode & 0xf8) >> 3) + 1;	// 参照テーブルの作成 

				for(i=0 ; i<8 ; i++) {
					if (p > 255) p = 255;
					a[i] = p;
					p += s;
				}
			/* ここまで */

			if (dhb <= a[7] && a[7] <= dhe) {	// マッピング予定範囲で試行する 
				e = test_dcb_encode(8, a, imp, psrc, ptmp);
				if (e < eq) {						// 誤差が小さいものを選択 
					eq = e;
					pdat[0] = (pt << 8) | mode | 0x07;
					for(i=0 ; i<16 ; i++) pdat[i+1] = ptmp[i];

					if (eq <= er_qual) return eq;
				}
			}
		}
	}
#endif

	return eq;
}

/* ＤＣＢを生成 */
void generate_dcb(int pdat[],unsigned short *pDCB)
{
	int i,mode,c0,c1,c2;
	unsigned short *pDCBtest = pDCB;
	int ptest[16];

	mode = pdat[0];
	c0 = c1 = c2 = 0;

	if (mode == 0xfeff) {
		*pDCB++ = (unsigned short)mode;
		for(i=0 ; i<8 ; i++) *pDCB++ = ((pdat[i+2] & 0xff)<<8)|(pdat[i+1] & 0xff);

	} else {
		for(i=16 ; i>=1 ; i--) {
			c0 <<= 1;
			c1 <<= 1;
			c2 <<= 1;
			if(pdat[i] & 0x01) c0 |= 1;
			if(pdat[i] & 0x02) c1 |= 1;
			if(pdat[i] & 0x04) c2 |= 1;
		}

		*pDCB++ = (unsigned short)mode;

		if ((mode & 0xff)!= 0) {
			*pDCB++ = (unsigned short)c0;

			if ((mode & 0x01)!= 0) {
				*pDCB++ = (unsigned short)c1;

				if ((mode & 0x06)== 0x06) {
					*pDCB = (unsigned short)c2;
				}
			}
		}
	}

}


/***************************************
	４×４個のデータ列をＤＣＢに圧縮
 ***************************************/
int dcb_encode(
		int psrc[],					// 元データ配列 (引数)
		int qual,					// 圧縮クオリティ値 (引数)
		unsigned short *pDCB		// DCB書き込みポインタ (引数)
	)
{
	int i,j,c,y,yy,x,xx,tmp;
	int m,med[9],imp[16],idx[16];		// 重要度の重み付けとソート 
	float e01,e2,e3,er_qual;
	int pass,offs,pdat[17],pdat01[17],pdat2[17],pdat3[17];

	// psss    : 適用するコーディングの制限 (0=0/1bit, 1=0/1/2bit, 2=0/1/2/3bit, 99=縮小Y)
	// offs    : 重要度テーブルのオフセット値 
	// er_qual : クオリティスレッショルド 
	/* 値はチューニングの余地があります */
	switch(qual) {
	case 0 :
		pass = 1;	offs = 0;	er_qual = 16.0;		break;
	case 1 :
		pass = 2;	offs = 0;	er_qual = 12.0;		break;
	case 2 :
		pass = 2;	offs = 0;	er_qual = 10.0;		break;
	case 3 :
		pass = 2;	offs = 0;	er_qual =  8.0;		break;
	case 4 :
		pass = 2;	offs = 0;	er_qual =  4.4;		break;
	case 5 :
		pass = 2;	offs = 0;	er_qual =  2.2;		break;
	case 6 :
		pass = 2;	offs = 0;	er_qual =  0.9;		break;

	/* 縮小Ｙ成分ＭＣＵ計算用 */
	case 99 :
		pass = 99;	offs = 0;	er_qual =  0.0;		break;

	default :
		pass = 2;	offs = 256;	er_qual =  1.0;		break;
	}

	// メディアンフィルタとの偏差から各データの重要度を算出 
	for(y=0 ; y<4 ; y++) {
		for(x=0 ; x<4 ; x++) {

			for(i=0 ; i<3 ; i++) {			// メディアンフィルタを適用 
				yy = y + (i-1);
				if (yy < 0) yy = 1; else if (yy > 4) yy = 2;
				for(j=0 ; j<3 ; j++) {
					xx = x + (j-1);
					if (xx < 0) xx = 1; else if (xx > 4) xx = 2;
					med[i*3 + j] = psrc[yy*4 + xx];
				}
			}
			for(i=0 ; i<(9-1) ; i++) {
				for(j=i+1 ; j<9 ; j++) {
					if(med[i] > med[j]) {
						c      = med[j];
						med[j] = med[i];
						med[i] = c;
					}
				}
			}

			m = med[4] - psrc[y*4 + x];
			if(m < 0) m = -m;							// 偏差範囲 0～255

			c = calc_imp_encoding( m ) + offs;			// 重要度 0～256
			if (c > 256) c = 256;
			imp[y*4 + x] = c;
		}
	}
	m = 0;												// 正規化 
	for(i=0 ; i<16 ; i++) if(m < imp[i]) m = imp[i];
	for(i=0 ; i<16 ; i++) imp[i] = (imp[i] * 256) / m;


	// データをソート 
	for(i=0 ; i<16 ; i++) idx[i] = i;

	for(i=0 ; i<16-1 ; i++) {
		for(j=i+1 ; j<16 ; j++) {
			if (psrc[i] > psrc[j]) {
				tmp = psrc[i];	psrc[i] = psrc[j];	psrc[j] = tmp;
				tmp = imp[i];	imp[i]  = imp[j];	imp[j]  = tmp;
				tmp = idx[i];	idx[i]  = idx[j];	idx[j]  = tmp;
			}
		}
	}


	// エンコード試行 
	if (pass == 99) {
		e2 = test_2bit_encoding(imp, psrc, pdat2, 1, 0.0);

		pdat[0] = pdat2[0];
		for(i=0 ; i<16 ; i++) pdat[idx[i]+1] = pdat2[i+1];
		generate_dcb(pdat, pDCB);

		return (3);
	}

	e01 = test_01bit_encoding(imp, psrc, pdat01, er_qual);
	if (e01 <= er_qual || pass == 0) {
		pdat[0] = pdat01[0];
		for(i=0 ; i<16 ; i++) pdat[idx[i]+1] = pdat01[i+1];
		generate_dcb(pdat, pDCB);

		if (pdat01[0] & 0xff) {
			return (2);
		} else {
			return (1);
		}
	}

	e2 = test_2bit_encoding(imp, psrc, pdat2, 0, er_qual);
	if (e2 <= er_qual || pass == 1) {
		pdat[0] = pdat2[0];
		for(i=0 ; i<16 ; i++) pdat[idx[i]+1] = pdat2[i+1];
		generate_dcb(pdat, pDCB);

		return (3);
	}

	e3 = test_3bit_encoding(imp, psrc, pdat3, er_qual);

	pdat[0] = pdat3[0];
	for(i=0 ; i<16 ; i++) pdat[idx[i]+1] = pdat3[i+1];
	generate_dcb(pdat, pDCB);

	return (4);
}


/*****************************************
	ＭＣＵをスキップするかどうかの判定
 *****************************************/

int mcu_skip_check(					// MCUスキップするかどうかのチェック (0 / 1を返す)
		mcu_rgb32 p[8][8],			// 該当MCUブロック 
		mcu_rgb32 r[8][8],			// リファレンスMCUブロック 
		int qual					// クオリティ引数（0～6）
	)
{
	int i,j;
	float py[8][8],pu[4][4],pv[4][4];
	float ry[8][8],ru[4][4],rv[4][4];
	float y_er,uv_er;

	// 許容誤差テーブル 
	float fe_sl[10] ={ 1536.0, 1536.0, 1024.0, 768.0, 512.0, 384.0, 256.0, 0.0, 0.0, 0.0};


	mcu_conv_yuv420(p, py, pu, pv);
	mcu_conv_yuv420(r, ry, ru, rv);

	// U,V成分の誤差を自乗和で求める 
	uv_er = 0.0;

	for(i=0 ; i<4 ; i++) {
		for(j=0 ; j<4 ; j++) {
			uv_er += (pu[i][j] - ru[i][j]) * (pu[i][j] - ru[i][j])
				   + (pv[i][j] - rv[i][j]) * (pv[i][j] - rv[i][j]);
		}
	}

	if (uv_er > fe_sl[qual]) return 0;


	// Y成分の誤差を自乗和で求める 
	y_er = 0.0;

	for(i=0 ; i<8 ; i++) {
		for(j=0 ; j<8 ; j++) {
			y_er += (py[i][j] - ry[i][j]) * (py[i][j] - ry[i][j])
				  + (py[i][j] - ry[i][j]) * (py[i][j] - ry[i][j]);
		}
	}

	if (y_er > fe_sl[qual]) return 0;


	return 1;
}




