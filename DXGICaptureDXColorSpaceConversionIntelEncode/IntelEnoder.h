#ifndef _INTELENCODER_H_
#define _INTELENCODER_H_

#include "DuplicationManager.h"
#include "common_utils.h"

#define MSDK_MAX_PATH 280


extern FILE* fLog;

struct EncodeOptions {
	mfxIMPL impl; // OPTION_IMPL

	
	char SinkName[MSDK_MAX_PATH];   // OPTION_FSINK

	mfxU16 Width; // OPTION_GEOMETRY
	mfxU16 Height;

	mfxU16 Bitrate; // OPTION_BITRATE

	mfxU16 FrameRateN; // OPTION_FRAMERATE
	mfxU16 FrameRateD;

	bool MeasureLatency; // OPTION_MEASURE_LATENCY
};

mfxStatus LoadRawRGBFrameDDA(mfxFrameSurface1* pSurface, DUPLICATIONMANAGER* DuplMgr);

class IntelEncoder
{
public:
	//Methods
	IntelEncoder();
	~IntelEncoder();
	mfxStatus InitializeX();
	mfxStatus RunEncode();
	mfxStatus FlushEncoder();
	void CloseResources();

	//Vars
	MFXVideoENCODE *mfxENC; 
	MFXVideoVPP *mfxVPP;
	mfxIMPL impl_type;
	DUPLICATIONMANAGER DuplMgr;
	bool Close;	

private:
	//Vars
	FILE* fSink;
	EncodeOptions options;
	
	
	MFXVideoSession *pSession;
	mfxFrameAllocator *pMfxAllocator;
	mfxVideoParam mfxEncParams;
	mfxExtBuffer* extBuffers[1];
	mfxFrameSurface1** pmfxSurfaces;
	mfxU16 nEncSurfNum;
	mfxBitstream mfxBS; 
	mfxExtVPPDoNotUse extDoNotUse;
	mfxFrameAllocResponse mfxResponse;
	int nEncSurfIdx;
	mfxSyncPoint syncpEnc;
	mfxU32 nFrame;
	mfxTime tStart, tEnd;
	double elapsed;
	
	//Methods
	int SetEncodeOptions();
	mfxStatus SetEncParameters();
	mfxStatus QueryAndAllocRequiredSurfacesHW();
	mfxStatus QueryAndAllocRequiredSurfacesSW();	

};

#endif