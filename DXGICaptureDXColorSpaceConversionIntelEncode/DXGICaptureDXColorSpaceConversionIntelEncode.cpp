// DXGICaptureDXColorSpaceConversionIntelEncode.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "IntelEnoder.h"
#include <time.h>

FILE *fLog;
char file_name[MAX_PATH];
IntelEncoder intelEncoder;


int main()
{
	fopen_s(&fLog, "logY.txt", "w");
	MSDK_CHECK_POINTER(fLog, MFX_ERR_NULL_PTR);


	if (intelEncoder.InitializeX() == MFX_ERR_NONE)
	{
		intelEncoder.RunEncode();
		intelEncoder.FlushEncoder();
	}

	intelEncoder.CloseResources();

	fclose(fLog);
	printf("Enter a character to close the console : ");
	getchar();
	return 0;
}


