#include "CircleSound.h"
#include <circle/synchronize.h>
#include <string.h>

static CVCHIQSoundBaseDevice *s_pSound = nullptr;

#define SOUND_QUEUE_MS 200

static void PrefillSoundQueue(CVCHIQSoundBaseDevice *pSound)
{
    if (!pSound) return;
    int16_t silence[256];
    memset(silence, 0, sizeof(silence));
    unsigned nFrames = pSound->GetQueueSizeFrames();
    unsigned nAvail = pSound->GetQueueFramesAvail();
    if (nAvail >= nFrames) return;
    unsigned nTarget = nFrames - nAvail;
    unsigned nWritten = 0;
    while (nWritten < nTarget) {
        unsigned n = nTarget - nWritten;
        if (n > 128) n = 128;
        int nBytes = pSound->Write(silence, n * 2 * sizeof(int16_t));
        if (nBytes <= 0) break;
        nWritten += (unsigned)nBytes / (2 * sizeof(int16_t));
    }
}

extern "C" {

void CircleSoundInit(CVCHIQSoundBaseDevice *pSoundDevice)
{
    s_pSound = pSoundDevice;
    if (s_pSound) {
        s_pSound->AllocateQueue(SOUND_QUEUE_MS);
        s_pSound->SetWriteFormat(SoundFormatSigned16, 2);
        PrefillSoundQueue(s_pSound);
        s_pSound->Start();
    }
}

size_t CircleSoundWrite(const int16_t *data, size_t frames)
{
    if (!s_pSound || !data || frames == 0) return 0;

    int nBytes = s_pSound->Write(data, frames * 2 * sizeof(int16_t));
    if (nBytes <= 0) return 0;
    return (size_t)(nBytes / (2 * sizeof(int16_t)));
}

unsigned CircleSoundGetQueueFramesAvail(void)
{
    if (!s_pSound) return 0;
    return s_pSound->GetQueueFramesAvail();
}

} // extern "C"
