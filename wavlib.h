
#ifndef __wavlib_h_
#define __wavlib_h_

#include <stdint.h>
typedef uint32_t riff_FOURCC;
typedef riff_FOURCC riff_CKID;
typedef uint32_t riff_CKSIZE;

#pragma pack(1)
typedef struct {
	riff_CKID ckID;
	riff_CKSIZE ckSize;
} RIFFCHUNK;

typedef struct {
	RIFFCHUNK chunk;
	riff_FOURCC form;
} RIFFHEADER;

typedef struct {
	uint16_t formatID;
	uint16_t channels;
	uint32_t samplingRate;
	uint32_t dataRate;
	uint16_t blockSize;
	uint16_t bitsPerSample;
} WAVEfmt;

#pragma pack()

#define RIFF_FOURCC_RIFF	0x46464952 /* RIFF */
#define RIFF_FOURCC_WAVE	0x45564157 /* WAVE */
#define RIFF_FOURCC_fmt		0x20746d66 /* fmt  */
#define RIFF_FOURCC_data	0x61746164 /* data */

#endif

