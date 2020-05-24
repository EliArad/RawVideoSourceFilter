#pragma once 

#include <streams.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

  
typedef enum INPUT_RAW_SOURCE_FORMAT
{
	RAW24,
	RAW32,
} INPUT_RAW_SOURCE_FORMAT;

 
static const GUID IID_IRawInputSource =
{ 0x653520da, 0x11b7, 0x46c8, { 0x96, 0x4a, 0xa7, 0xd4, 0xd4, 0x43, 0x26, 0xe1 } };


typedef void(__stdcall * RawSourceDelegate)(int frameNumber, int frameCount);

DECLARE_INTERFACE_(IBoutechRawInputSource, IUnknown)
{	
	 	
	STDMETHOD(SetResolution)(int x, int y, int inFormatRes, int outFormatRes, INPUT_RAW_SOURCE_FORMAT inputFormat) PURE;
	STDMETHOD(SetFileName)(WCHAR *flileName) PURE;
	STDMETHOD(SetStartFrame)(long startFrame) PURE;
	STDMETHOD(JumpToFrame)(long frameNumber) PURE;
	STDMETHOD(Loop)(bool l) PURE;

	STDMETHOD(PauseVideo)()PURE;
	STDMETHOD(ResumeVideo)()PURE;
	STDMETHOD(StepForward)()PURE;
	STDMETHOD(StepBackward)()PURE;
	STDMETHOD(RegisterRawSourceCallback)(RawSourceDelegate callback) PURE;
	STDMETHOD(SetRawSourceFrameDelay)(int frameDelay) PURE;
	STDMETHOD(ClearSequenceList)() PURE;
	STDMETHOD(AddSequenceFile)(const WCHAR *fileName) PURE;

	
};