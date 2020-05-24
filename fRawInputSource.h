#include "myInterface.h"
#include "AutoResetEvent.h"
#include <iterator> 
#include <map> 
#include <list>

class CBallStream;
class CRawInputSourceFilter;

using namespace std;

// {F8F74093-87CA-4ACB-9146-D1F65B672A8B}
DEFINE_GUID(CLSID_RawInputSource,
	0x28f74093, 0x87ca, 0x4acb, 0x91, 0x56, 0xd1, 0xf6, 0x1b, 0x67, 0x23, 0x82);

typedef struct SeekValue_t
{ 
	int fileNumber;
	uint64_t positioninFile;
} SeekValue;

typedef enum INPUT_MODE_t
{
	FILE_MODE,
	LIST_OF_FILES

}INPUT_MODE;


typedef enum RAW_FORMAT_RES_
{
	FORMAT_RGB_8 = 1,
	FORMAT_RGB_32 = 4,
	FORMAT_RGB_24 = 3,
	FORMAT_RGB565,
	FORMAT_RGB555,


} RAW_FORMAT_RES;

#define MAX_FIFO_CHUNKS 10

 
class CRawInputSource : public CUnknown
{
	friend class CDumpFilter;
	friend class CDumpInputPin;

public:
	CRawInputSourceFilter   *m_pFilter;       // Methods for filter interfaces
	CBallStream *m_pPin;          // A simple rendered input pin
	 

public:

	DECLARE_IUNKNOWN

	CRawInputSource(LPUNKNOWN pUnk, HRESULT *phr);
	~CRawInputSource();

	static CUnknown * WINAPI CreateInstance(LPUNKNOWN punk, HRESULT *phr);
	 

private:

	// Overriden to say what interfaces we support where
	STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void ** ppv);

};



class CRawInputSourceFilter : public CSource, public IBoutechRawInputSource
{
public:

	DECLARE_IUNKNOWN;

	bool m_pauseVideo;
	AutoResetEvent m_event;

    // The only allowed way to create Bouncing balls!
    static CUnknown * WINAPI CreateInstance(LPUNKNOWN lpunk, HRESULT *phr);

	FILE *fileHandle;
	int OpenRawFile(char *fileName);
	uint64_t GetFifoSize();
	uint64_t GetFifoFreeSize();
	bool m_parseSeqMetaData;
	bool m_running;
	uint32_t m_fifo_size;
	int m_iImageHeight;                
	int m_iImageWidth;                 
	__int64 m_startFrame;
	long m_jumpToFrame;
	int m_frameDelay;
	bool m_loop;
	uint32_t m_frameCount;
	long m_fileSize;
	long m_frameNumber;
	RAW_FORMAT_RES m_inFormatRes;
	RAW_FORMAT_RES m_outFormatRes;
	INPUT_RAW_SOURCE_FORMAT m_inputFormat;
	uint8_t *m_videoFifo;
	uint8_t *m_rgb24VideoBuffer;
	uint64_t m_fifo_write;
	uint64_t m_fifo_read;
	long HD_24_bit;
	long HD_32_bit;
	list<string> m_inputSeqList;
	INPUT_MODE m_inputMode;


	STDMETHODIMP SetResolution(int x, int y, int informatRes, int outformatRes, INPUT_RAW_SOURCE_FORMAT inputFormat);
	STDMETHODIMP SetFileName(WCHAR *flileName);
	STDMETHODIMP SetStartFrame(long startFrame);
	STDMETHODIMP JumpToFrame(long frameNumber);
	STDMETHODIMP Loop(bool l);

	STDMETHODIMP PauseVideo();
	STDMETHODIMP ResumeVideo();
	STDMETHODIMP StepForward();
	STDMETHODIMP StepBackward();
	STDMETHODIMP RegisterRawSourceCallback(RawSourceDelegate callback);
	STDMETHODIMP SetRawSourceFrameDelay(int frameDelay);
	STDMETHODIMP ClearSequenceList();
	STDMETHODIMP AddSequenceFile(const WCHAR *fileName);

	bool m_created;
	RawSourceDelegate m_callback;
	
	CRITICAL_SECTION CriticalSection;

	void CreateSource();
	CCritSec m_Lock;                // Main renderer critical section
    // It is only allowed to to create these objects with CreateInstance
	CRawInputSourceFilter(LPUNKNOWN lpunk, HRESULT *phr);
	~CRawInputSourceFilter();

	STDMETHODIMP Run(REFERENCE_TIME tStart);
	STDMETHODIMP Pause();
	STDMETHODIMP Stop();

	char m_sFileName[500];

	STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void **ppv);

}; // CRawInputSourceFilter

 
//------------------------------------------------------------------------------
// Class CBallStream
//
// This class implements the stream which is used to output the bouncing ball
// data from the source filter. It inherits from DirectShows's base
// CSourceStream class.
//------------------------------------------------------------------------------
class CBallStream : public CSourceStream  
{

public:

	CBallStream(HRESULT *phr, CRawInputSourceFilter *pParent, LPCWSTR pPinName);
    ~CBallStream();

    // plots a ball into the supplied video frame
    HRESULT FillBuffer(IMediaSample *pms);
	HRESULT HandleRawData(IMediaSample *pms);
	HRESULT HandleBayerData(IMediaSample *pms);
	void    ProcessInput();
	map<int, SeekValue> m_seqFramePos;
    // Ask for buffers of the size appropriate to the agreed media type
    HRESULT DecideBufferSize(IMemAllocator *pIMemAlloc,
                             ALLOCATOR_PROPERTIES *pProperties);

    // Set the agreed media type, and set up the necessary ball parameters
    HRESULT SetMediaType(const CMediaType *pMediaType);

    // Because we calculate the ball there is no reason why we
    // can't calculate it in any one of a set of formats...
    HRESULT CheckMediaType(const CMediaType *pMediaType);
    HRESULT GetMediaType(int iPosition, CMediaType *pmt);

    // Resets the stream time to zero
    HRESULT OnThreadCreate(void);

    // Quality control notifications sent to us
    STDMETHODIMP Notify(IBaseFilter * pSender, Quality q);
	
	CRawInputSourceFilter *m_pFilter;
	void ScanSeqFile();
	void ScanSeqList();
	//int m_inMbSeqRgbVideoFrames;

public:
	CCritSec m_Lock;                // Main renderer critical section
    int m_iImageHeight;                 // The current image height
    int m_iImageWidth;                  // And current image width
    int m_iRepeatTime;                  // Time in msec between frames
    const int m_iDefaultRepeatTime;     // Initial m_iRepeatTime

	uint8_t *pData24Buffer;
    BYTE m_BallPixel[4];                // Represents one coloured ball
    int m_iPixelSize;                   // The pixel size in bytes
    PALETTEENTRY m_Palette[256];        // The optimal palette for the image

    CCritSec m_cSharedState;            // Lock on m_rtSampleTime and m_Ball
    CRefTime m_rtSampleTime;            // The time stamp for each sample
    //CBall *m_Ball;                      // The current ball object

    // set up the palette appropriately
    enum Colour {Red, Blue, Green, Yellow};
    HRESULT SetPaletteEntries(Colour colour);

}; // CBallStream
    
