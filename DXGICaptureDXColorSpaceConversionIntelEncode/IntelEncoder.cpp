#include "stdafx.h"
#include "IntelEnoder.h"
#include <time.h>

IntelEncoder::IntelEncoder()
{
	mfxENC = NULL;
	mfxVPP = NULL;
	pMfxAllocator = NULL;
	pSession = NULL;
	nEncSurfNum = 0;
	//CaptureFrameAvailable = false;
}

IntelEncoder::~IntelEncoder()
{
	mfxENC = NULL;
	mfxVPP = NULL;
	pMfxAllocator = NULL;
	pSession = NULL;
	nEncSurfNum = 0;
	Close = false;
}

/**
*	Desktop Duplication API Setup for Capture RGB32 Image
*	Intel Media SDK VPP and encode pipeline setup.
*	RGB32 color conversion to NV12 via VPP then encode
*	Encoding an AVC (H.264) stream
*	Video memory surfaces are used
*/
mfxStatus IntelEncoder::InitializeX()
{
	DUPL_RETURN Ret;
	UINT Output = 0;

	mfxStatus sts = MFX_ERR_NONE;

	// Make duplication manager
	Ret = DuplMgr.InitDupl(fLog, Output);
	if (Ret != DUPL_RETURN_SUCCESS)
	{
		fprintf_s(fLog, "Duplication Manager couldn't be initialized.");
		return MFX_ERR_UNKNOWN;
	}
	if (SetEncodeOptions() == MFX_ERR_NULL_PTR)
	{
		fprintf_s(fLog, "Sink file couldn't be created.");
		return MFX_ERR_NULL_PTR;
	}
	mfxIMPL impl = options.impl;
	//Version 1.3 is selected for Video Conference Mode compatibility.
	mfxVersion ver = { { 3, 1 } };
	pSession = new MFXVideoSession();
	
	pMfxAllocator = (mfxFrameAllocator*)malloc(sizeof(mfxFrameAllocator));
	memset(pMfxAllocator, 0, sizeof(mfxFrameAllocator));
    sts = Initialize(impl, ver, pSession, pMfxAllocator);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	sts = pSession->QueryIMPL(&impl_type);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	if (impl_type == MFX_IMPL_SOFTWARE)
	{
		fprintf_s(fLog, "Implementation type %d is : SOFTWARE\n", impl_type);
	}
	else
	{
		fprintf_s(fLog, "Implementation type %d is : HARDWARE\n", impl_type);
	}
	
	sts = SetEncParameters();
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	// Create Media SDK encoder
	mfxENC = new MFXVideoENCODE(*pSession);

	if (impl_type == MFX_IMPL_SOFTWARE)
	{
		sts = QueryAndAllocRequiredSurfacesSW();
		MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	}
	else
	{
		sts = QueryAndAllocRequiredSurfacesHW();
		MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
	}

	// Initialize the Media SDK encoder
	sts = mfxENC->Init(&mfxEncParams);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	

	// Retrieve video parameters selected by encoder.
	// - BufferSizeInKB parameter is required to set bit stream buffer size
	mfxVideoParam par;
	memset(&par, 0, sizeof(par));
	sts = mfxENC->GetVideoParam(&par);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	// Prepare Media SDK bit stream buffer
	memset(&mfxBS, 0, sizeof(mfxBS));
	mfxBS.MaxLength = par.mfx.BufferSizeInKB * 1000;
	mfxBS.Data = new mfxU8[mfxBS.MaxLength];
	MSDK_CHECK_POINTER(mfxBS.Data, MFX_ERR_MEMORY_ALLOC);

	return MFX_ERR_NONE;
}

int IntelEncoder::SetEncodeOptions()
{
	options.impl = MFX_IMPL_AUTO_ANY;
	options.Width = DuplMgr.GetImageWidth();
	options.Height = DuplMgr.GetImageHeight();
	options.Bitrate = 4000;
	options.FrameRateN = 60;
	options.FrameRateD = 1;
	options.MeasureLatency = true;
	strcpy_s(options.SinkName, "output.h264");
	fopen_s(&fSink, options.SinkName, "wb");
	MSDK_CHECK_POINTER(fSink, MFX_ERR_NULL_PTR);
}

mfxStatus IntelEncoder::SetEncParameters()
{

	mfxStatus sts = MFX_ERR_NONE;
	// Initialize encoder parameters
	memset(&mfxEncParams, 0, sizeof(mfxEncParams));
	mfxEncParams.mfx.CodecId = MFX_CODEC_AVC;
	mfxEncParams.mfx.TargetUsage = MFX_TARGETUSAGE_BALANCED;
	mfxEncParams.mfx.TargetKbps = options.Bitrate;
	mfxEncParams.mfx.RateControlMethod = MFX_RATECONTROL_VBR /*MFX_RATECONTROL_VCM*/;
	mfxEncParams.mfx.FrameInfo.FrameRateExtN = options.FrameRateN;
	mfxEncParams.mfx.FrameInfo.FrameRateExtD = options.FrameRateD;
	mfxEncParams.mfx.FrameInfo.FourCC = MFX_FOURCC_NV12;
	mfxEncParams.mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
	mfxEncParams.mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
	mfxEncParams.mfx.FrameInfo.CropX = 0;
	mfxEncParams.mfx.FrameInfo.CropY = 0;
	mfxEncParams.mfx.FrameInfo.CropW = options.Width;
	mfxEncParams.mfx.FrameInfo.CropH = options.Height;
	mfxEncParams.AsyncDepth = 1;
	mfxEncParams.mfx.GopRefDist = 1;
	mfxEncParams.mfx.NumRefFrame = 1;
	// Width must be a multiple of 16
	// Height must be a multiple of 16 in case of frame picture and a multiple of 32 in case of field picture
	mfxEncParams.mfx.FrameInfo.Width = MSDK_ALIGN16(options.Width);
	mfxEncParams.mfx.FrameInfo.Height =
		(MFX_PICSTRUCT_PROGRESSIVE == mfxEncParams.mfx.FrameInfo.PicStruct) ?
		MSDK_ALIGN16(options.Height) :
		MSDK_ALIGN32(options.Height);

	if (impl_type == MFX_IMPL_SOFTWARE)
	{
		mfxEncParams.IOPattern = MFX_IOPATTERN_IN_SYSTEM_MEMORY;
	}
	else
	{
		mfxEncParams.IOPattern = MFX_IOPATTERN_IN_VIDEO_MEMORY;
	}

	return sts;
}
mfxStatus IntelEncoder::QueryAndAllocRequiredSurfacesSW()
{
	mfxStatus sts = MFX_ERR_NONE;
	// Query number of required surfaces for encoder
	mfxFrameAllocRequest EncRequest;
	memset(&EncRequest, 0, sizeof(EncRequest));
	sts = mfxENC->QueryIOSurf(&mfxEncParams, &EncRequest);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	EncRequest.NumFrameSuggested = EncRequest.NumFrameSuggested + mfxEncParams.AsyncDepth;

	nEncSurfNum = EncRequest.NumFrameSuggested;
	
	// Allocate surfaces for Enc
	// - Width and height of buffer must be aligned, a multiple of 32
	// - Frame surface array keeps pointers all surface planes and general frame info
	mfxU16 width = (mfxU16)MSDK_ALIGN16(mfxEncParams.mfx.FrameInfo.Width);
	mfxU16 height = (mfxU16)MSDK_ALIGN16(mfxEncParams.mfx.FrameInfo.Height);
	mfxU8 bitsPerPixel = 12;        // NV12 format is a 12 bits per pixel format
	mfxU32 surfaceSize = width * height * bitsPerPixel / 8;
	mfxU8* surfaceBuffers = (mfxU8*) new mfxU8[surfaceSize * nEncSurfNum];

	pmfxSurfaces = new mfxFrameSurface1 *[nEncSurfNum];
	MSDK_CHECK_POINTER(pmfxSurfaces, MFX_ERR_MEMORY_ALLOC);
	for (int i = 0; i < nEncSurfNum; i++) {
		pmfxSurfaces[i] = new mfxFrameSurface1;
		memset(pmfxSurfaces[i], 0, sizeof(mfxFrameSurface1));
		memcpy(&(pmfxSurfaces[i]->Info), &(mfxEncParams.mfx.FrameInfo), sizeof(mfxFrameInfo));
		pmfxSurfaces[i]->Data.Y = &surfaceBuffers[surfaceSize * i];
		pmfxSurfaces[i]->Data.U = pmfxSurfaces[i]->Data.Y + width * height;
		pmfxSurfaces[i]->Data.V = pmfxSurfaces[i]->Data.U + 1;
		pmfxSurfaces[i]->Data.Pitch = width;
	}

	return sts;
}
mfxStatus IntelEncoder::QueryAndAllocRequiredSurfacesHW()
{
	mfxStatus sts = MFX_ERR_NONE;
	// Query number of required surfaces for encoder
	mfxFrameAllocRequest EncRequest;
	memset(&EncRequest, 0, sizeof(EncRequest));
	sts = mfxENC->QueryIOSurf(&mfxEncParams, &EncRequest);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	EncRequest.NumFrameSuggested = EncRequest.NumFrameSuggested + mfxEncParams.AsyncDepth;

	EncRequest.Type |= WILL_WRITE; // This line is only required for Windows DirectX11 to ensure that surfaces can be written to by the application

	// Allocate required surfaces
	sts = pMfxAllocator->Alloc(pMfxAllocator->pthis, &EncRequest, &mfxResponse);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	nEncSurfNum = mfxResponse.NumFrameActual;

	// Allocate surface headers (mfxFrameSurface1) for decoder
	pmfxSurfaces = new mfxFrameSurface1 *[nEncSurfNum];
	MSDK_CHECK_POINTER(pmfxSurfaces, MFX_ERR_MEMORY_ALLOC);
	for (int i = 0; i < nEncSurfNum; i++) {
		pmfxSurfaces[i] = new mfxFrameSurface1;
		memset(pmfxSurfaces[i], 0, sizeof(mfxFrameSurface1));
		memcpy(&(pmfxSurfaces[i]->Info), &(mfxEncParams.mfx.FrameInfo), sizeof(mfxFrameInfo));
		pmfxSurfaces[i]->Data.MemId = mfxResponse.mids[i];      // MID (memory id) represent one video NV12 surface
	}
	return sts;
}


mfxStatus IntelEncoder::RunEncode()
{
	mfxStatus sts = MFX_ERR_NONE;
	// ===================================
	// Start processing frames
	//
	clock_t start1 = 0, duration1 = 0;
	clock_t duration4 = 0;

	mfxGetTime(&tStart);

	nEncSurfIdx = 0;
	nFrame = 0;

	//
	// Stage 1: Main VPP/encoding loop
	//
	while (MFX_ERR_NONE <= sts || MFX_ERR_MORE_DATA == sts)
	{
		nEncSurfIdx = GetFreeSurfaceIndex(pmfxSurfaces, nEncSurfNum);    // Find free input frame surface
		MSDK_CHECK_ERROR(MFX_ERR_NOT_FOUND, nEncSurfIdx, MFX_ERR_MEMORY_ALLOC);

		if (impl_type == MFX_IMPL_SOFTWARE)
		{
			//sts = LoadRawRGBFrame(pmfxSurfacesVPPIn[nVPPSurfIdx], fSource);  // Load frame from file into surface
			sts = LoadRawRGBFrameDDA(pmfxSurfaces[nEncSurfIdx], &DuplMgr);  // Load frame from file into surface
			MSDK_BREAK_ON_ERROR(sts);
		}
		else
		{
			// Surface locking required when read/write video surfaces
			sts = pMfxAllocator->Lock(pMfxAllocator->pthis, pmfxSurfaces[nEncSurfIdx]->Data.MemId, &(pmfxSurfaces[nEncSurfIdx]->Data));
			MSDK_BREAK_ON_ERROR(sts);

			//sts = LoadRawRGBFrame(pmfxSurfacesVPPIn[nVPPSurfIdx], fSource);  // Load frame from file into surface
			sts = LoadRawRGBFrameDDA(pmfxSurfaces[nEncSurfIdx], &DuplMgr);  // Load frame from file into surface
			/*if(sts == MFX_ERR_NONE)
			{
				CaptureFrameAvailable = false;
			}*/
			MSDK_BREAK_ON_ERROR(sts);

			sts = pMfxAllocator->Unlock(pMfxAllocator->pthis, pmfxSurfaces[nEncSurfIdx]->Data.MemId, &(pmfxSurfaces[nEncSurfIdx]->Data));
			MSDK_BREAK_ON_ERROR(sts);

		}
		start1 = clock();
		for (;;) {
			// Encode a frame asychronously (returns immediately)
			sts = mfxENC->EncodeFrameAsync(NULL, pmfxSurfaces[nEncSurfIdx], &mfxBS, &syncpEnc);

			if (MFX_ERR_NONE < sts && !syncpEnc) {  // Repeat the call if warning and no output
				if (MFX_WRN_DEVICE_BUSY == sts)
					MSDK_SLEEP(1);  // Wait if device is busy, then repeat the same call
			}
			else if (MFX_ERR_NONE < sts && syncpEnc) {
				sts = MFX_ERR_NONE;     // Ignore warnings if output is available
				break;
			}
			else if (MFX_ERR_NOT_ENOUGH_BUFFER == sts) {
				// Allocate more bitstream buffer memory here if needed...
				break;
			}
			else
				break;
		}

		if (MFX_ERR_NONE == sts) {
			sts = pSession->SyncOperation(syncpEnc, 60000);   // Synchronize. Wait until encoded frame is ready
			MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

			duration1 += clock() - start1;

			sts = WriteBitStreamFrame(&mfxBS, fSink);
			MSDK_BREAK_ON_ERROR(sts);
			duration4 += clock() - start1;
			
			++nFrame;
			//break;
			//fprintf_s(fLog, "Frame number: %d\n", nFrame);
		}
	}

	// MFX_ERR_MORE_DATA means that the input file has ended, need to go to buffering loop, exit in case of other errors
	MSDK_IGNORE_MFX_STS(sts, MFX_ERR_MORE_DATA);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	/*mfxGetTime(&tEnd);
	elapsed = TimeDiffMsec(tEnd, tStart) / 1000;
	double fps = ((double)nFrame / elapsed);
	fprintf_s(fLog, "\nExecution time: %3.2f s (%3.2f fps)\n", elapsed, fps);
	double elapsed1 = duration1 / 1000;
	fps = (double)nFrame / elapsed1;
	fprintf_s(fLog, "\nExecution time for encode exclusive : %3.2f s (%3.2f fps)\n", elapsed1, fps);
	double elapsed4 = duration4 / 1000;
	fps = (double)nFrame / elapsed4;
	fprintf_s(fLog, "\nExecution time for encode and writing to file : %3.2f s (%3.2f fps)\n", elapsed4, fps);*/
	return sts;
}

mfxStatus IntelEncoder::FlushEncoder()
{
	mfxGetTime(&tStart);
	
	mfxStatus sts = MFX_ERR_NONE;
	//
	// Stage 3: Retrieve the buffered encoder frames
	//
	while (MFX_ERR_NONE <= sts) {
		for (;;) {
			// Encode a frame asychronously (returns immediately)
			sts = mfxENC->EncodeFrameAsync(NULL, NULL, &mfxBS, &syncpEnc);

			if (MFX_ERR_NONE < sts && !syncpEnc) {  // Repeat the call if warning and no output
				if (MFX_WRN_DEVICE_BUSY == sts)
					MSDK_SLEEP(1);  // Wait if device is busy, then repeat the same call
			}
			else if (MFX_ERR_NONE < sts && syncpEnc) {
				sts = MFX_ERR_NONE;     // Ignore warnings if output is available
				break;
			}
			else
				break;
		}

		if (MFX_ERR_NONE == sts) {
			sts = pSession->SyncOperation(syncpEnc, 60000);   // Synchronize. Wait until encoded frame is ready
			MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

			//sts = WriteBitStreamFrame(&mfxBS, fSink);
			MSDK_BREAK_ON_ERROR(sts);

			++nFrame;
			fprintf_s(fLog, "Frame number: %d\r", nFrame);
		}
	}

	// MFX_ERR_MORE_DATA indicates that there are no more buffered frames, exit in case of other errors
	MSDK_IGNORE_MFX_STS(sts, MFX_ERR_MORE_DATA);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	/*mfxGetTime(&tEnd);
	elapsed += TimeDiffMsec(tEnd, tStart) / 1000;
	double fps = ((double)nFrame / elapsed);
	fprintf_s(fLog, "\nExecution time: %3.2f s (%3.2f fps)\n", elapsed, fps);*/
	return sts;
}

mfxStatus LoadRawRGBFrameDDA(mfxFrameSurface1* pSurface, DUPLICATIONMANAGER* DuplMgr)
{

	DUPL_RETURN Ret;
	static int frameCount = 0;
	if (1500 > frameCount++)
	{
		Ret = DuplMgr->GetFrame();
		if (Ret != DUPL_RETURN_SUCCESS)
		{
			fprintf_s(fLog, "Could not get the frame.");
			return MFX_ERR_MORE_DATA;
		}
		Ret = DuplMgr->TransformFrame(pSurface);
		if (Ret != DUPL_RETURN_SUCCESS)
		{
			fprintf_s(fLog, "Could not transform the frame.");
			return MFX_ERR_MORE_DATA;
		}
		return MFX_ERR_NONE;
	}
	else
	{
		return MFX_ERR_MORE_DATA;
	}
}

void IntelEncoder::CloseResources()
{
	Close = true;
	//DuplMgr.PrintTimings();
	// ===================================================================
	// Clean up resources
	//  - It is recommended to close Media SDK components first, before releasing allocated surfaces, since
	//    some surfaces may still be locked by internal Media SDK resources.
	if(mfxENC)
		mfxENC->Close();
	

	for (int i = 0; i < nEncSurfNum; i++)
		delete pmfxSurfaces[i];
	MSDK_SAFE_DELETE_ARRAY(pmfxSurfaces);
	MSDK_SAFE_DELETE_ARRAY(mfxBS.Data);

	if (pMfxAllocator)
	{
		if (pMfxAllocator->Free)
		{
			pMfxAllocator->Free(pMfxAllocator->pthis, &mfxResponse);
			//pMfxAllocator->Free(pMfxAllocator->pthis, &mfxResponseVPPOutEnc);
		}
		free(pMfxAllocator);
	}
	if(pSession)
		delete pSession;

	if (fSink) 
		fclose(fSink);

	Release();
}