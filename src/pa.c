/* Audio device for R using PortAudio library
   Copyright(c) 2008 Simon Urbanek

   Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
   * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 
   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND ON INFRINGEMENT. 
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 
   The text above constitutes the entire license; however, the PortAudio community also makes the following non-binding requests:
   * Any person wishing to distribute modifications to the Software is requested to send the modifications to the original developer so that they can be incorporated into the canonical version. It is also requested that these non-binding requests be included along with the license above.

 */

#define USE_RINTERNALS  /* for efficiency */
#define R_NO_REMAP      /* to not pollute the namespace */
#include <R.h>
#include <Rinternals.h>
#include "portaudio.h"

#define kNumberOutputBuffers 2
#define USEFLOAT 1

#define BOOL int
#ifndef YES
#define YES 1
#define NO  0
#endif

typedef signed short int SInt16;

typedef struct play_info {
/*
	AudioQueueRef       queue;
	AudioQueueBufferRef buffers[kNumberOutputBuffers];
	AudioStreamBasicDescription format;
	BOOL done;
	NSError *lastError;
 */
	PaStream *stream;
	float sample_rate;
	SEXP source;
	BOOL stereo, loop, done;
	unsigned int position, length;
} play_info_t;
	
static int paPlayCallback(const void *inputBuffer, void *outputBuffer,
						  unsigned long framesPerBuffer,
						  const PaStreamCallbackTimeInfo* timeInfo,
						  PaStreamCallbackFlags statusFlags,
						  void *userData )
{
    play_info_t *ap = (play_info_t*)userData; 
	//Rprintf("paPlayCallback(in=%p, out=%p, fpb=%d, usr=%p)\n", inputBuffer, outputBuffer, (int) framesPerBuffer, userData);
	//if (ap->done) { Rprintf("done, skipping\n"); return paAbort; }
	//Rprintf(" - (sample_rate=%f, stereo=%d, loop=%d, done=%d, pos=%d, len=%d)\n", ap->sample_rate, ap->stereo, ap->loop, ap->done, ap->position, ap->length);
	if (ap->position == ap->length && ap->loop)
		ap->position = 0;
	unsigned int index = ap->position;
	unsigned int rem = ap->length - index;
	unsigned int spf = ap->stereo ? 2 : 1;
	if (rem > framesPerBuffer) rem = framesPerBuffer;
	//printf("position=%d, length=%d, (LEN=%d), rem=%d, cap=%d, spf=%d\n", ap->position, ap->length, LENGTH(ap->source), rem, framesPerBuffer, spf);
	index *= spf;
	// there is a small caveat - if a zero-size buffer comes along it will stop the playback since rem will be forced to 0 - but then that should not happen ...
	if (rem > 0) {
		unsigned int samples = rem * spf; // samples (i.e. SInt16s)
#ifdef USEFLOAT
		float *iBuf = (float*) outputBuffer;
		float *sentinel = iBuf + samples;
		//printf(" iBuf = %p, sentinel = %p, diff = %d bytes\n", iBuf, sentinel, ((char*)sentinel) - ((char*)iBuf));
		if (TYPEOF(ap->source) == INTSXP) {
			int *iSrc = INTEGER(ap->source) + index;
			while (iBuf < sentinel)
				*(iBuf++) = ((float) *(iSrc++)) / 32768.0;
		} else if (TYPEOF(ap->source) == REALSXP) {
			double *iSrc = REAL(ap->source) + index;
			while (iBuf < sentinel)
				*(iBuf++) = (float) *(iSrc++);
			//{ int i = 0; while (i < framesPerBuffer) printf("%.2f ", ((float*) outputBuffer)[i++]); Rprintf("\n"); }				
		} // FIXME: support functions as sources...
#else
		SInt16 *iBuf = (SInt16*) outputBuffer;
		SInt16 *sentinel = iBuf + samples;
		if (TYPEOF(ap->source) == INTSXP) {
			int *iSrc = INTEGER(ap->source) + index;
			while (iBuf < sentinel)
				*(iBuf++) = (SInt16) *(iSrc++);
		} else if (TYPEOF(ap->source) == REALSXP) {
			double *iSrc = REAL(ap->source) + index;
			while (iBuf < sentinel)
				*(iBuf++) = (SInt16) (32767.0 * (*(iSrc++)));
		} // FIXME: support functions as sources...
#endif
		ap->position += rem;
	} else {
		// printf(" rem ==0 -> stop queue\n");
		ap->done = YES;
		return paComplete;
	}
	return 0;
}

static void *portaudio_create_player(SEXP source) {
	PaError err = Pa_Initialize();
	if( err != paNoError ) Rf_error("cannot initialize audio system: %s\n", Pa_GetErrorText( err ) );
	play_info_t *ap = (play_info_t*) calloc(sizeof(play_info_t), 1);
	ap->source = source;
	R_PreserveObject(ap->source);
	ap->sample_rate = 44100.0;
	ap->done = NO;
	ap->position = 0;
	ap->length = LENGTH(source);
	ap->stereo = NO; // FIXME: support dim[2] = 2
	ap->loop = NO;
	if (ap->stereo) ap->length /= 2;
	return ap;
}

static int portaudio_start(void *usr) {
	play_info_t *p = (play_info_t*) usr;
    PaError err;
	p->done = NO;
	
#if SIMPLEAPI
    err = Pa_OpenDefaultStream(&p->stream,
							   0, /* in ch. */
							   p->stereo ? 1 : 2, /* out ch */
#ifdef USEFLOAT
							   paFloat32,
#else
							   paInt16,
#endif
							   p->sample_rate,
							   // paFramesPerBufferUnspecified,
							   1024,
							   paPlayCallback,
							   p );
#else
	PaStreamParameters outputParameters;
	outputParameters.device = Pa_GetDefaultOutputDevice();
    outputParameters.channelCount = p->stereo ? 1 : 2;       /* MONO output */
    outputParameters.sampleFormat = 
#ifdef USEFLOAT
	paFloat32;
#else
	paInt16;
#endif
    outputParameters.suggestedLatency = Pa_GetDeviceInfo( outputParameters.device )->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = NULL;
	
    err = Pa_OpenStream(
						&p->stream,
						NULL, /* no input */
						&outputParameters,
						p->sample_rate,
						1000,
						paClipOff,      /* we won't output out of range samples so don't bother clipping them */
						paPlayCallback,
						p );
#endif
	
	if( err != paNoError ) Rf_error("cannot open audio for playback: %s\n", Pa_GetErrorText( err ) );
	err = Pa_StartStream( p->stream );
	if( err != paNoError ) Rf_error("cannot start audio playback: %s\n", Pa_GetErrorText( err ) );
	return YES;
}

static int portaudio_pause(void *usr) {
	play_info_t *p = (play_info_t*) usr;
	PaError err = Pa_StopStream( p->stream );
	return (err == paNoError);
}

static int portaudio_resume(void *usr) {
	play_info_t *p = (play_info_t*) usr;
	PaError err = Pa_StartStream( p->stream );
	return (err == paNoError);
}

static int portaudio_close(void *usr) {
	play_info_t *p = (play_info_t*) usr;
    PaError err = Pa_CloseStream( p->stream );
	return (err == paNoError);
}

static void portaudio_dispose(void *usr) {
	Pa_Terminate();
	free(usr);
}

typedef struct portaudio_audio {
	void *(*create)(SEXP);
	int (*start)(void *);
	int (*pause)(void *);
	int (*resume)(void *);
	int (*close)(void *);
	void (*dispose)(void *);
} portaudio_audio_t;

portaudio_audio_t portaudio_audio_driver = {
portaudio_create_player,
portaudio_start,
portaudio_pause,
portaudio_resume,
portaudio_close,
portaudio_dispose
};

portaudio_audio_t *current_driver = &portaudio_audio_driver;

// FIXME: we need to wrap the driver in the SEXP as well ...

SEXP audio_player(SEXP source) {
	void *p = current_driver->create(source);
	if (!p) Rf_error("cannot start audio driver");
	SEXP ptr = R_MakeExternalPtr(p, R_NilValue, R_NilValue);
	Rf_protect(ptr);
	Rf_setAttrib(ptr, Rf_install("class"), Rf_mkString("audioDriver"));
	Rf_unprotect(1);
	return ptr;	
}

SEXP audio_start(SEXP driver) {
	if (TYPEOF(driver) != EXTPTRSXP)
		Rf_error("invalid audio driver");
	void *p = EXTPTR_PTR(driver);
	return Rf_ScalarLogical(current_driver->start(p));
}

SEXP audio_pause(SEXP driver) {
	if (TYPEOF(driver) != EXTPTRSXP)
		Rf_error("invalid audio driver");
	void *p = EXTPTR_PTR(driver);
	return Rf_ScalarLogical(current_driver->pause(p));
}

SEXP audio_resume(SEXP driver) {
	if (TYPEOF(driver) != EXTPTRSXP)
		Rf_error("invalid audio driver");
	void *p = EXTPTR_PTR(driver);
	return Rf_ScalarLogical(current_driver->resume(p));
}

SEXP audio_close(SEXP driver) {
	if (TYPEOF(driver) != EXTPTRSXP)
		Rf_error("invalid audio driver");
	void *p = EXTPTR_PTR(driver);
	return Rf_ScalarLogical(current_driver->close(p));
}

