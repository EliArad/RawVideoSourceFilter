//------------------------------------------------------------------------------
// File: fRawInputSource.cpp
//------------------------------------------------------------------------------

#include <streams.h>
#include <olectl.h>
#include <initguid.h>
#include "fRawInputSource.h"
#include "myInterface.h"
#include <io.h>
#include <memory>
#include <thread>


#pragma warning(disable:4710)  // 'function': function not inlined (optimzation)
using namespace std;

FILE *debugh = NULL;

// Setup data

const AMOVIESETUP_MEDIATYPE sudOpPinTypes =
{
    &MEDIATYPE_Video,       // Major type
    &MEDIASUBTYPE_NULL      // Minor type
};

const AMOVIESETUP_PIN sudOpPin =
{
    L"Output",              // Pin string name
    FALSE,                  // Is it rendered
    TRUE,                   // Is it an output
    FALSE,                  // Can we have none
    FALSE,                  // Can we have many
    &CLSID_NULL,            // Connects to filter
    NULL,                   // Connects to pin
    1,                      // Number of types
    &sudOpPinTypes };       // Pin details

const AMOVIESETUP_FILTER sudBallax =
{
	&CLSID_RawInputSource,    // Filter CLSID
    L"Raw Input Source",       // String name
    MERIT_DO_NOT_USE,       // Filter merit
    1,                      // Number pins
    &sudOpPin               // Pin details
};


// COM global table of objects in this dll

CFactoryTemplate g_Templates[] = {
  { L"Raw Input Source"
  , &CLSID_RawInputSource
  , CRawInputSource::CreateInstance
  , NULL
  , &sudBallax }
};
int g_cTemplates = sizeof(g_Templates) / sizeof(g_Templates[0]);


////////////////////////////////////////////////////////////////////////
//
// Exported entry points for registration and unregistration 
// (in this case they only call through to default implementations).
//
////////////////////////////////////////////////////////////////////////

//
// DllRegisterServer
//
// Exported entry points for registration and unregistration
//
STDAPI DllRegisterServer()
{
    return AMovieDllRegisterServer2(TRUE);

} // DllRegisterServer


//
// DllUnregisterServer
//
STDAPI DllUnregisterServer()
{
    return AMovieDllRegisterServer2(FALSE);

} // DllUnregisterServer


//
// DllEntryPoint
//
extern "C" BOOL WINAPI DllEntryPoint(HINSTANCE, ULONG, LPVOID);

BOOL APIENTRY DllMain(HANDLE hModule, 
                      DWORD  dwReason, 
                      LPVOID lpReserved)
{
	return DllEntryPoint((HINSTANCE)(hModule), dwReason, lpReserved);
}

//
// CreateInstance
//
// The only allowed way to create Raw Input Sources!
//
CUnknown * WINAPI CRawInputSourceFilter::CreateInstance(LPUNKNOWN lpunk, HRESULT *phr)
{
    ASSERT(phr);

	CUnknown *punk = new CRawInputSourceFilter(lpunk, phr);
    if(punk == NULL)
    {
        if(phr)
            *phr = E_OUTOFMEMORY;
    }
    return punk;

} // CreateInstance


CRawInputSourceFilter::~CRawInputSourceFilter()
{
	// Release resources used by the critical section object.
	DeleteCriticalSection(&CriticalSection);

	if (m_paStreams != NULL)
	{
		delete m_paStreams[0];
		delete m_paStreams;
		m_paStreams = NULL;
		if (m_videoFifo != NULL)
		{
			delete m_videoFifo;
			m_videoFifo = NULL;
		}
		 
	}
}

uint64_t CRawInputSourceFilter::GetFifoFreeSize()
{

	EnterCriticalSection(&CriticalSection);
	uint64_t size;
	if (m_fifo_write == m_fifo_read)
	{
		size = m_fifo_size;
		LeaveCriticalSection(&CriticalSection);
		return size;
	}

	if (m_fifo_write > m_fifo_read)
	{
		size = (m_fifo_size - m_fifo_write) + m_fifo_read;
		// Request ownership of the critical section.
		LeaveCriticalSection(&CriticalSection);
		return size;
	}


	size = m_fifo_read - m_fifo_write;
	// Request ownership of the critical section.
	LeaveCriticalSection(&CriticalSection);
	return size;

}

uint64_t CRawInputSourceFilter::GetFifoSize()
{
	// Request ownership of the critical section.
	EnterCriticalSection(&CriticalSection);
	uint64_t size;
	if (m_fifo_write == m_fifo_read)
	{
		size = 0;
		LeaveCriticalSection(&CriticalSection);
		return size;
	}
	if (m_fifo_write > m_fifo_read)
	{
		size = m_fifo_write - m_fifo_read;
		// Request ownership of the critical section.
		LeaveCriticalSection(&CriticalSection);
		return size;			
	}

	size = (m_fifo_size - m_fifo_read) + m_fifo_write;
	// Request ownership of the critical section.
	LeaveCriticalSection(&CriticalSection);
	return size;

}

//
// Constructor
//
// Initialise a CBallStream object so that we have a pin.
//
CRawInputSourceFilter::CRawInputSourceFilter(LPUNKNOWN lpunk, HRESULT *phr) :
CSource(NAME("Raw Input Source"), lpunk, CLSID_RawInputSource)
{
	if (!InitializeCriticalSectionAndSpinCount(&CriticalSection , 0x00000400))
		return;

	m_parseSeqMetaData = false;
	m_frameCount = 0;
	m_running = false;
	m_videoFifo = NULL;
 
	m_callback = NULL;
	m_pauseVideo = false;
	m_iImageWidth = 1920,
	m_iImageHeight = 1080;
	HD_24_bit = m_iImageWidth * m_iImageHeight * 3;
	HD_32_bit = m_iImageWidth * m_iImageHeight * 4;

	m_inputMode = INPUT_MODE::FILE_MODE;
	m_startFrame = 0;
	m_jumpToFrame = -1;
	m_frameDelay = 33;
	m_loop = false;
	m_inFormatRes = RAW_FORMAT_RES::FORMAT_RGB_24;
	m_outFormatRes = RAW_FORMAT_RES::FORMAT_RGB_32;
	m_inputFormat = INPUT_RAW_SOURCE_FORMAT::RAW24;
 
    ASSERT(phr);
    CAutoLock cAutoLock(&m_cStateLock);
	m_created = false;

	strcpy(m_sFileName, "c:\\ball.raw");
	CreateSource();
}

void CRawInputSourceFilter::CreateSource()
{
	HRESULT phr;

	m_paStreams = (CSourceStream **) new CBallStream*[1];
	if (m_paStreams == NULL)
	{		
		return;
	}

	m_paStreams[0] = new CBallStream(&phr, this, L"Out");
	if (m_paStreams[0] == NULL)
	{
		   
		return;
	}
}


CBallStream::CBallStream(HRESULT *phr,
	CRawInputSourceFilter *pParent,
                         LPCWSTR pPinName) :
    CSourceStream(NAME("Raw Input Source"),phr, pParent, pPinName),   
    m_iDefaultRepeatTime(30)
{

	  
    ASSERT(phr);
    CAutoLock cAutoLock(&m_cSharedState);
	m_pFilter = pParent;

	m_iImageHeight = m_pFilter->m_iImageHeight;
	m_iImageWidth = m_pFilter->m_iImageWidth;
	
	pData24Buffer = new uint8_t[m_iImageWidth * m_iImageHeight * 3];
	 
}  
 
CBallStream::~CBallStream()
{
    CAutoLock cAutoLock(&m_cSharedState);
	if (pData24Buffer != NULL)
	{
		delete pData24Buffer;
		pData24Buffer = NULL;
	}
	 
}  
  
HRESULT CBallStream::FillBuffer(IMediaSample *pms)
{

	clock_t t;
	t = clock();
	 
	if (m_pFilter->m_pauseVideo == true)
	{
		m_pFilter->m_event.WaitOne();
	}

	 
	if (HandleRawData(pms) == S_FALSE)
		return S_FALSE;
	  
	long lDataLen;
	// The current time is the sample's start
	//CRefTime rtStart = m_rtSampleTime;
	lDataLen = pms->GetSize();
	// Increment to find the finish time
	//m_rtSampleTime += (LONG)m_iRepeatTime;
	pms->SetActualDataLength(lDataLen);
	//pms->SetTime((REFERENCE_TIME *)&rtStart, (REFERENCE_TIME *)&m_rtSampleTime);
	if (m_pFilter->m_callback != NULL)
		m_pFilter->m_callback(m_pFilter->m_frameNumber , m_pFilter->m_frameCount);
	m_pFilter->m_frameNumber++;

	
	//pms->SetSyncPoint(TRUE);
	t = clock() - t;
	double time_taken = ((double)t) / CLOCKS_PER_SEC; // calculate the elapsed time

	  
	if (time_taken < m_pFilter->m_frameDelay)
		Sleep(m_pFilter->m_frameDelay - time_taken);
	

    return NOERROR;

} // FillBuffer



HRESULT CBallStream::HandleRawData(IMediaSample *pms)
{
	BYTE *pData;
	long lDataLen;

	pms->GetPointer(&pData);
	lDataLen = pms->GetSize();

	 
	{
		CAutoLock cAutoLockShared(&m_cSharedState);

		if (m_pFilter->m_jumpToFrame != -1)
		{
			__int64 fs = m_iImageWidth * m_iImageHeight * 4;
			__int64 r = (__int64)m_pFilter->m_jumpToFrame * fs;
			_fseeki64(m_pFilter->fileHandle, r, SEEK_SET);
			m_pFilter->m_frameNumber = m_pFilter->m_jumpToFrame;
			m_pFilter->m_jumpToFrame = -1;
		}

		if (m_pFilter->m_inFormatRes == m_pFilter->m_outFormatRes)
		{
			if (fread(pData, 1, lDataLen, m_pFilter->fileHandle) != lDataLen)
			{
				if (m_pFilter->m_loop == true)
				{
					__int64 r = m_pFilter->m_startFrame * m_iImageWidth * m_iImageHeight * 4;
					_fseeki64(m_pFilter->fileHandle, r, SEEK_SET);
					if (fread(pData, 1, lDataLen, m_pFilter->fileHandle) != lDataLen)
						return S_FALSE;
				}
				else
				{
					return S_FALSE;
				}
			}
		}
		else
			if (m_pFilter->m_inFormatRes == FORMAT_RGB_24 && m_pFilter->m_outFormatRes == FORMAT_RGB_32)
			{

				if (fread(pData24Buffer, 1, m_pFilter->HD_24_bit, m_pFilter->fileHandle) != m_pFilter->HD_24_bit)
				{
					if (m_pFilter->m_loop == true)
					{
						__int64 r = m_pFilter->m_startFrame * m_iImageWidth * m_iImageHeight * 4;
						_fseeki64(m_pFilter->fileHandle, r, SEEK_SET);
						if (fread(pData, 1, lDataLen, m_pFilter->fileHandle) != lDataLen)
							return S_FALSE;
					}
					else
					{
						return S_FALSE;
					}
				}
				int j = 0;


				for (int i = 0; i < m_pFilter->HD_24_bit; i += 3)
				{
					pData[j] = 0;
					pData[j + 1] = pData24Buffer[i + 1];       // GREEN
					pData[j + 2] = pData24Buffer[i + 1];           // RED
					pData[j + 3] = pData24Buffer[i + 0];
					j += 4;
				}
			}
		 
	}

	return S_OK;
}

//
// Notify
//
// Alter the repeat rate according to quality management messages sent from
// the downstream filter (often the renderer).  Wind it up or down according
// to the flooding level - also skip forward if we are notified of Late-ness
//
STDMETHODIMP CBallStream::Notify(IBaseFilter * pSender, Quality q)
{
    // Adjust the repeat rate.
    if(q.Proportion<=0)
    {
        m_iRepeatTime = 1000;        // We don't go slower than 1 per second
    }
    else
    {
        m_iRepeatTime = m_iRepeatTime*1000 / q.Proportion;
        if(m_iRepeatTime>1000)
        {
            m_iRepeatTime = 1000;    // We don't go slower than 1 per second
        }
        else if(m_iRepeatTime<10)
        {
            m_iRepeatTime = 10;      // We don't go faster than 100/sec
        }
    }

    // skip forwards
    if(q.Late > 0)
        m_rtSampleTime += q.Late;

    return NOERROR;

} // Notify


//
// GetMediaType
//
// I _prefer_ 5 formats - 8, 16 (*2), 24 or 32 bits per pixel and
// I will suggest these with an image size of 320x240. However
// I can accept any image size which gives me some space to bounce.
//
// A bit of fun:
//      8 bit displays get red balls
//      16 bit displays get blue
//      24 bit see green
//      And 32 bit see yellow
//
// Prefered types should be ordered by quality, zero as highest quality
// Therefore iPosition =
// 0    return a 32bit mediatype
// 1    return a 24bit mediatype
// 2    return 16bit RGB565
// 3    return a 16bit mediatype (rgb555)
// 4    return 8 bit palettised format
// (iPosition > 4 is invalid)
//
HRESULT CBallStream::GetMediaType(int iPosition, CMediaType *pmt)
{
    CheckPointer(pmt,E_POINTER);

    CAutoLock cAutoLock(m_pFilter->pStateLock());
    if(iPosition < 0)
    {
        return E_INVALIDARG;
    }

    // Have we run off the end of types?


	// Have we run off the end of types?

	if (iPosition > 1)
	{
		return VFW_S_NO_MORE_ITEMS;
	}

	VIDEOINFO *pvi = (VIDEOINFO *)pmt->AllocFormatBuffer(sizeof(VIDEOINFO));
	if (NULL == pvi)
		return(E_OUTOFMEMORY);

	ZeroMemory(pvi, sizeof(VIDEOINFO));


	if (m_pFilter->m_inFormatRes == FORMAT_RGB_32)
	{
		// Return our highest quality 32bit format

		// since we use RGB888 (the default for 32 bit), there is
		// no reason to use BI_BITFIELDS to specify the RGB
		// masks. Also, not everything supports BI_BITFIELDS

		SetPaletteEntries(Yellow);
		pvi->bmiHeader.biCompression = BI_RGB;
		pvi->bmiHeader.biBitCount = 32;
	} else
	if (m_pFilter->m_inFormatRes == FORMAT_RGB_24 && m_pFilter->m_outFormatRes == FORMAT_RGB_32)
	{
		SetPaletteEntries(Green);
		pvi->bmiHeader.biCompression = BI_RGB;
		pvi->bmiHeader.biBitCount = 32;

	} else 
	if (m_pFilter->m_inFormatRes == FORMAT_RGB_24 && m_pFilter->m_outFormatRes == FORMAT_RGB_24)
	{   // Return our 24bit format

		SetPaletteEntries(Green);
		pvi->bmiHeader.biCompression = BI_RGB;
		pvi->bmiHeader.biBitCount = 24;

	} else 
	if (m_pFilter->m_inFormatRes == FORMAT_RGB565)
	{
		// 16 bit per pixel RGB565

		// Place the RGB masks as the first 3 doublewords in the palette area
		for (int i = 0; i < 3; i++)
			pvi->TrueColorInfo.dwBitMasks[i] = bits565[i];

		SetPaletteEntries(Blue);
		pvi->bmiHeader.biCompression = BI_BITFIELDS;
		pvi->bmiHeader.biBitCount = 16;

	} else 
	if (m_pFilter->m_inFormatRes == FORMAT_RGB555)
	{   // 16 bits per pixel RGB555

		// Place the RGB masks as the first 3 doublewords in the palette area
		for (int i = 0; i < 3; i++)
			pvi->TrueColorInfo.dwBitMasks[i] = bits555[i];

		SetPaletteEntries(Blue);
		pvi->bmiHeader.biCompression = BI_BITFIELDS;
		pvi->bmiHeader.biBitCount = 16;

	} else 
	if (m_pFilter->m_inFormatRes == FORMAT_RGB_8)
	{   // 8 bit palettised

		SetPaletteEntries(Red);
		pvi->bmiHeader.biCompression = BI_RGB;
		pvi->bmiHeader.biBitCount = 8;
		pvi->bmiHeader.biClrUsed = iPALETTE_COLORS;

	}

    // (Adjust the parameters common to all formats...)

    // put the optimal palette in place
    for(int i = 0; i < iPALETTE_COLORS; i++)
    {
        pvi->TrueColorInfo.bmiColors[i].rgbRed      = m_Palette[i].peRed;
        pvi->TrueColorInfo.bmiColors[i].rgbBlue     = m_Palette[i].peBlue;
        pvi->TrueColorInfo.bmiColors[i].rgbGreen    = m_Palette[i].peGreen;
        pvi->TrueColorInfo.bmiColors[i].rgbReserved = 0;
    }

    pvi->bmiHeader.biSize       = sizeof(BITMAPINFOHEADER);
    pvi->bmiHeader.biWidth      = m_iImageWidth;
    pvi->bmiHeader.biHeight     = m_iImageHeight;
    pvi->bmiHeader.biPlanes     = 1;
    pvi->bmiHeader.biSizeImage  = GetBitmapSize(&pvi->bmiHeader);
    pvi->bmiHeader.biClrImportant = 0;

    SetRectEmpty(&(pvi->rcSource)); // we want the whole image area rendered.
    SetRectEmpty(&(pvi->rcTarget)); // no particular destination rectangle

    pmt->SetType(&MEDIATYPE_Video);
    pmt->SetFormatType(&FORMAT_VideoInfo);
    pmt->SetTemporalCompression(FALSE);

    // Work out the GUID for the subtype from the header info.
    const GUID SubTypeGUID = GetBitmapSubtype(&pvi->bmiHeader);
    pmt->SetSubtype(&SubTypeGUID);
    pmt->SetSampleSize(pvi->bmiHeader.biSizeImage);

    return NOERROR;

} // GetMediaType


//
// CheckMediaType
//
// We will accept 8, 16, 24 or 32 bit video formats, in any
// image size that gives room to bounce.
// Returns E_INVALIDARG if the mediatype is not acceptable
//
HRESULT CBallStream::CheckMediaType(const CMediaType *pMediaType)
{
    CheckPointer(pMediaType,E_POINTER);

    if((*(pMediaType->Type()) != MEDIATYPE_Video) ||   // we only output video
       !(pMediaType->IsFixedSize()))                   // in fixed size samples
    {                                                  
        return E_INVALIDARG;
    }

    // Check for the subtypes we support
    const GUID *SubType = pMediaType->Subtype();
    if (SubType == NULL)
        return E_INVALIDARG;

    if((*SubType != MEDIASUBTYPE_RGB8)
        && (*SubType != MEDIASUBTYPE_RGB565)
        && (*SubType != MEDIASUBTYPE_RGB555)
        && (*SubType != MEDIASUBTYPE_RGB24)
        && (*SubType != MEDIASUBTYPE_RGB32))
    {
        return E_INVALIDARG;
    }

    // Get the format area of the media type
    VIDEOINFO *pvi = (VIDEOINFO *) pMediaType->Format();

    if(pvi == NULL)
        return E_INVALIDARG;

    // Check the image size. As my default ball is 10 pixels big
    // look for at least a 20x20 image. This is an arbitary size constraint,
    // but it avoids balls that are bigger than the picture...

    if((pvi->bmiHeader.biWidth < 20) || ( abs(pvi->bmiHeader.biHeight) < 20))
    {
        return E_INVALIDARG;
    }

	/*
    // Check if the image width & height have changed
    if(pvi->bmiHeader.biWidth != m_Ball->GetImageWidth() || 
       abs(pvi->bmiHeader.biHeight) != m_Ball->GetImageHeight())
    {
        // If the image width/height is changed, fail CheckMediaType() to force
        // the renderer to resize the image.
        return E_INVALIDARG;
    }
	*/

    return S_OK;  // This format is acceptable.

} // CheckMediaType


//
// DecideBufferSize
//
// This will always be called after the format has been sucessfully
// negotiated. So we have a look at m_mt to see what size image we agreed.
// Then we can ask for buffers of the correct size to contain them.
//
HRESULT CBallStream::DecideBufferSize(IMemAllocator *pAlloc,
                                      ALLOCATOR_PROPERTIES *pProperties)
{
    CheckPointer(pAlloc,E_POINTER);
    CheckPointer(pProperties,E_POINTER);

    CAutoLock cAutoLock(m_pFilter->pStateLock());
    HRESULT hr = NOERROR;

    VIDEOINFO *pvi = (VIDEOINFO *) m_mt.Format();
    pProperties->cBuffers = 1;
    pProperties->cbBuffer = pvi->bmiHeader.biSizeImage;

    ASSERT(pProperties->cbBuffer);

    // Ask the allocator to reserve us some sample memory, NOTE the function
    // can succeed (that is return NOERROR) but still not have allocated the
    // memory that we requested, so we must check we got whatever we wanted

    ALLOCATOR_PROPERTIES Actual;
    hr = pAlloc->SetProperties(pProperties,&Actual);
    if(FAILED(hr))
    {
        return hr;
    }

    // Is this allocator unsuitable

    if(Actual.cbBuffer < pProperties->cbBuffer)
    {
        return E_FAIL;
    }

    // Make sure that we have only 1 buffer (we erase the ball in the
    // old buffer to save having to zero a 200k+ buffer every time
    // we draw a frame)

    ASSERT(Actual.cBuffers == 1);
    return NOERROR;

} // DecideBufferSize


//
// SetMediaType
//
// Called when a media type is agreed between filters
//
HRESULT CBallStream::SetMediaType(const CMediaType *pMediaType)
{	
    CAutoLock cAutoLock(m_pFilter->pStateLock());

    // Pass the call up to my base class

    HRESULT hr = CSourceStream::SetMediaType(pMediaType);

    if(SUCCEEDED(hr))
    {
        VIDEOINFO * pvi = (VIDEOINFO *) m_mt.Format();
        if (pvi == NULL)
            return E_UNEXPECTED;

        switch(pvi->bmiHeader.biBitCount)
        {
            case 8:     // Make a red pixel

                m_BallPixel[0] = 10;    // 0 is palette index of red
                m_iPixelSize   = 1;
                SetPaletteEntries(Red);
                break;

            case 16:    // Make a blue pixel

                m_BallPixel[0] = 0xf8;  // 00000000 00011111 is blue in rgb555 or rgb565
                m_BallPixel[1] = 0x0;   // don't forget the byte ordering within the mask word.
                m_iPixelSize   = 2;
                SetPaletteEntries(Blue);
                break;

            case 24:    // Make a green pixel

                m_BallPixel[0] = 0x0;
                m_BallPixel[1] = 0xff;
                m_BallPixel[2] = 0x0;
                m_iPixelSize   = 3;
                SetPaletteEntries(Green);
                break;

            case 32:    // Make a yellow pixel

                m_BallPixel[0] = 0x0;
                m_BallPixel[1] = 0xff;
                m_BallPixel[2] = 0xff;
                m_BallPixel[3] = 0x00;
                m_iPixelSize   = 4;
                SetPaletteEntries(Yellow);
                break;

            default:
                // We should never agree any other pixel sizes
                ASSERT(FALSE);
                break;
        }

       
        return NOERROR;
    } 

    return hr;

} // SetMediaType


//
// OnThreadCreate
//
// As we go active reset the stream time to zero
//
HRESULT CBallStream::OnThreadCreate()
{
	 
	m_pFilter->m_frameCount = 0;
	m_pFilter->m_running = true;
    CAutoLock cAutoLockShared(&m_cSharedState);
    m_rtSampleTime = 0;
	m_pFilter->m_frameNumber = 0;

	if (m_pFilter->m_inputMode == INPUT_MODE::FILE_MODE)
	{
		if (m_pFilter->OpenRawFile(m_pFilter->m_sFileName) == 0)
		{
			return S_FALSE;
		}
	}
 
	 
	/*
	_fseeki64(m_pFilter->fileHandle, 0, SEEK_END);
	__int64 size = _ftelli64(m_pFilter->fileHandle);
	_fseeki64(m_pFilter->fileHandle, 0, SEEK_SET);
	*/
	
	
    // we need to also reset the repeat time in case the system
    // clock is turned off after m_iRepeatTime gets very big
    m_iRepeatTime = m_iDefaultRepeatTime;

    return NOERROR;

} // OnThreadCreate
 

 
//
// SetPaletteEntries
//
// If we set our palette to the current system palette + the colours we want
// the system has the least amount of work to do whilst plotting our images,
// if this stream is rendered to the current display. The first non reserved
// palette slot is at m_Palette[10], so put our first colour there. Also
// guarantees that black is always represented by zero in the frame buffer
//
HRESULT CBallStream::SetPaletteEntries(Colour color)
{
    CAutoLock cAutoLock(m_pFilter->pStateLock());

    HDC hdc = GetDC(NULL);  // hdc for the current display.
    UINT res = GetSystemPaletteEntries(hdc, 0, iPALETTE_COLORS, (LPPALETTEENTRY) &m_Palette);
    ReleaseDC(NULL, hdc);

    if(res == 0)
        return E_FAIL;

    switch(color)
    {
        case Red:
            m_Palette[10].peBlue  = 0;
            m_Palette[10].peGreen = 0;
            m_Palette[10].peRed   = 0xff;
            break;

        case Yellow:
            m_Palette[10].peBlue  = 0;
            m_Palette[10].peGreen = 0xff;
            m_Palette[10].peRed   = 0xff;
            break;

        case Blue:
            m_Palette[10].peBlue  = 0xff;
            m_Palette[10].peGreen = 0;
            m_Palette[10].peRed   = 0;
            break;

        case Green:
            m_Palette[10].peBlue  = 0;
            m_Palette[10].peGreen = 0xff;
            m_Palette[10].peRed   = 0;
            break;
    }

    m_Palette[10].peFlags = 0;
    return NOERROR;

} // SetPaletteEntries
 
  
int CRawInputSourceFilter::OpenRawFile(char *fileName)
{
	fileHandle = fopen(fileName, "r+b");
	if (fileHandle == NULL)
		return 0;

	 
	if (m_startFrame > 0)
	{
		__int64 r = m_startFrame * m_iImageWidth * m_iImageHeight * 4;
		_fseeki64(fileHandle, r, SEEK_END);
		m_frameNumber = m_startFrame;
	}

	return 1;
}
STDMETHODIMP CRawInputSourceFilter::Run(REFERENCE_TIME tStart)
{ 

 
	
	return CBaseFilter::Run(tStart);
}
STDMETHODIMP CRawInputSourceFilter::Pause()
{
	return CBaseFilter::Pause();
}
STDMETHODIMP CRawInputSourceFilter::Stop()
{
	m_pauseVideo = false;
	m_event.Set();

	if (fileHandle != NULL)
	{
		fclose(fileHandle);
		fileHandle = NULL;
	}
	m_running = false;
	return CBaseFilter::Stop();
}

STDMETHODIMP CRawInputSourceFilter::SetFileName(WCHAR *fileName)
{	 
	m_inputMode = INPUT_MODE::FILE_MODE;
	wcstombs(m_sFileName, fileName, 500);	
	return S_OK;
}

STDMETHODIMP CRawInputSourceFilter::ClearSequenceList()
{
	
	m_inputSeqList.clear();
	return S_OK;
}

STDMETHODIMP CRawInputSourceFilter::AddSequenceFile(const WCHAR *fileName)
{
	char f[500];
	wcstombs(f, fileName, 500);
	std::string str(f);
	m_inputSeqList.push_back(str);
	m_inputMode = INPUT_MODE::LIST_OF_FILES;
	return S_OK;
}
 

STDMETHODIMP CRawInputSourceFilter::SetResolution(int x, int y, int inFormatRes, int outFormatRes, INPUT_RAW_SOURCE_FORMAT inputFormat)
{

	m_iImageWidth = x;
	m_iImageHeight = y;
	m_inFormatRes = (RAW_FORMAT_RES)inFormatRes;
	m_outFormatRes = (RAW_FORMAT_RES)outFormatRes;
	m_inputFormat = inputFormat;
	 
	m_fifo_size = m_iImageWidth * m_iImageHeight * m_inFormatRes * MAX_FIFO_CHUNKS;

	CreateSource();

	return S_OK;
}

STDMETHODIMP CRawInputSourceFilter::PauseVideo()
{
	m_pauseVideo = true;
	return S_OK;
}
STDMETHODIMP CRawInputSourceFilter::ResumeVideo()
{
	m_pauseVideo = false;
	m_event.Set();
	return S_OK;
}

STDMETHODIMP CRawInputSourceFilter::StepForward()
{
	m_event.Set();
	return S_OK;
}

STDMETHODIMP CRawInputSourceFilter::StepBackward()
{
	if (m_frameNumber > 1)
	{
		m_jumpToFrame = m_frameNumber - 2;
		m_event.Set();
	}
	return S_OK;
}

STDMETHODIMP CRawInputSourceFilter::Loop(bool l)
{
	m_loop = l;

	return S_OK;
}

STDMETHODIMP CRawInputSourceFilter::JumpToFrame(long frameNumber)
{
	m_jumpToFrame = frameNumber;
	m_event.Set();

	return S_OK;
}

STDMETHODIMP CRawInputSourceFilter::SetRawSourceFrameDelay(int frameDelay)
{
	m_frameDelay = frameDelay;
	return S_OK;
}

 
STDMETHODIMP CRawInputSourceFilter::SetStartFrame(long startFrame)
{
	m_startFrame = startFrame;

	return S_OK;
}

 
STDMETHODIMP CRawInputSourceFilter::RegisterRawSourceCallback(RawSourceDelegate callback)
{
	m_callback = callback;
	return S_OK;
}
 
STDMETHODIMP CRawInputSource::NonDelegatingQueryInterface(REFIID riid, void **ppv)
{
	CheckPointer(ppv, E_POINTER);
	 

	 
	if (riid == IID_IFileSourceFilter)
	{
		return GetInterface((IFileSourceFilter *)this, ppv);
	}
	else
	if (riid == IID_IRawInputSource)
	{
		return GetInterface((IBoutechRawInputSource*)this, ppv);
	}
	else if (riid == IID_IAMFilterMiscFlags)
	{
		return GetInterface((IAMFilterMiscFlags*) this, ppv);
	}
	else
	{
		return m_pFilter->NonDelegatingQueryInterface(riid, ppv);
	}
}


STDMETHODIMP CRawInputSourceFilter::NonDelegatingQueryInterface(REFIID riid, void **ppv)
{
	CheckPointer(ppv, E_POINTER);
	if (riid == IID_IRawInputSource)
	{		
		return GetInterface((IBoutechRawInputSource*)this, ppv);
	}	 
	else
	{
		return CSource::NonDelegatingQueryInterface(riid, ppv);
	}
}

/*
STDMETHODIMP CRawInputSourceFilter::Load(LPCOLESTR lpwszFileName, const AM_MEDIA_TYPE *pmt)
{
	// do nothing for now...Just for test
	return S_OK;
}
STDMETHODIMP CRawInputSourceFilter::GetCurFile(LPOLESTR * ppszFileName, AM_MEDIA_TYPE *pmt)
{
	// do nothing for now...Just for test
	return S_OK;
}
*/


CRawInputSource::CRawInputSource(LPUNKNOWN pUnk, HRESULT *phr) :
CUnknown(NAME("CRawInputSource"), pUnk),
m_pFilter(NULL),
m_pPin(NULL)
{
	ASSERT(phr);
  
	m_pFilter = new CRawInputSourceFilter(pUnk, phr);
	if (m_pFilter == NULL)
	{

	}

}

//
// CreateInstance
//
// Provide the way for COM to create a dump filter
//

CUnknown * WINAPI CRawInputSource::CreateInstance(LPUNKNOWN punk, HRESULT *phr)
{
	ASSERT(phr);
	CRawInputSource *pNewObject = NULL;
	pNewObject = new CRawInputSource(punk, phr);
	if (pNewObject == NULL) {
		if (phr)
			*phr = E_OUTOFMEMORY;
	}

	return pNewObject;

}

CRawInputSource::~CRawInputSource()
{

}



FILE *handle = NULL;
 